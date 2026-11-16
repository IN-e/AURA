#include "audio.h"

#include <initguid.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <dwmapi.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <tlhelp32.h>
#include <versionhelpers.h>

#include <wrl/client.h>
#include <wrl/implements.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

using Microsoft::WRL::ComPtr;

namespace {

constexpr size_t kRingSize = 65536;

std::wstring HResultText(HRESULT hr) {
    wchar_t* buffer = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                  FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD chars = FormatMessageW(flags, nullptr, static_cast<DWORD>(hr),
                                 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                 reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring text;
    if (chars && buffer) {
        text.assign(buffer, chars);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
            text.pop_back();
    }
    if (buffer) LocalFree(buffer);
    wchar_t hex[32];
    swprintf_s(hex, L"0x%08X", static_cast<unsigned>(hr));
    if (text.empty()) text = hex;
    else {
        text += L" (";
        text += hex;
        text += L")";
    }
    return text;
}

std::wstring ProcessImageName(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    wchar_t path[32768]{};
    DWORD size = static_cast<DWORD>(std::size(path));
    std::wstring result;
    if (QueryFullProcessImageNameW(process, 0, path, &size)) result.assign(path, size);
    CloseHandle(process);
    return result;
}

std::wstring FileNamePart(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring FileStem(const std::wstring& path) {
    std::wstring name = FileNamePart(path);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name.resize(dot);
    return name;
}

std::wstring FileDescription(const std::wstring& path) {
    if (path.empty()) return {};
    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (!size) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};

    struct Translation {
        WORD language;
        WORD codePage;
    };
    Translation* translations = nullptr;
    UINT translationBytes = 0;
    std::vector<Translation> candidates;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                       reinterpret_cast<void**>(&translations), &translationBytes) &&
        translationBytes >= sizeof(Translation)) {
        const size_t count = translationBytes / sizeof(Translation);
        for (size_t i = 0; i < count && i < 12; ++i) candidates.push_back(translations[i]);
    }
    if (candidates.empty()) candidates.push_back({0x0409, 0x04B0});

    for (const wchar_t* field : {L"FileDescription", L"ProductName"}) {
        for (const auto& tr : candidates) {
            wchar_t key[128];
            swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\%s", tr.language, tr.codePage, field);
            wchar_t* value = nullptr;
            UINT valueChars = 0;
            if (VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&value), &valueChars) &&
                value && valueChars > 1) {
                std::wstring text(value, valueChars);
                while (!text.empty() && text.back() == L'\0') text.pop_back();
                if (!text.empty()) return text;
            }
        }
    }
    return {};
}

bool IsCloaked(HWND hwnd) {
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))))
        return cloaked != 0;
    return false;
}

struct WindowHit {
    DWORD pid = 0;
    std::wstring title;
    std::wstring className;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM param) {
    auto* windows = reinterpret_cast<std::map<DWORD, WindowHit>*>(param);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
    if (GetWindowTextLengthW(hwnd) == 0) return TRUE;
    LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return TRUE;
    if (IsCloaked(hwnd)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) return TRUE;

    wchar_t title[512]{};
    GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
    wchar_t cls[128]{};
    GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
    if (!title[0]) return TRUE;
    if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 ||
        wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Windows.UI.Core.CoreWindow") == 0)
        return TRUE;

    auto found = windows->find(pid);
    if (found == windows->end() || found->second.title.size() < wcslen(title)) {
        (*windows)[pid] = WindowHit{pid, title, cls};
    }
    return TRUE;
}

struct SessionHit {
    DWORD pid = 0;
    bool active = false;
    std::wstring displayName;
};

bool IsActiveSessionState(AudioSessionState state) {
    return state == AudioSessionStateActive;
}

std::vector<SessionHit> EnumerateAudioSessions() {
    std::vector<SessionHit> result;
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return result;

    ComPtr<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) return result;

    ComPtr<IAudioSessionManager2> manager;
    hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(manager.GetAddressOf()));
    if (FAILED(hr)) return result;

    ComPtr<IAudioSessionEnumerator> sessions;
    if (FAILED(manager->GetSessionEnumerator(&sessions))) return result;
    int count = 0;
    if (FAILED(sessions->GetCount(&count))) return result;

    for (int i = 0; i < count; ++i) {
        ComPtr<IAudioSessionControl> control;
        if (FAILED(sessions->GetSession(i, &control))) continue;
        ComPtr<IAudioSessionControl2> control2;
        if (FAILED(control.As(&control2))) continue;
        DWORD pid = 0;
        if (FAILED(control2->GetProcessId(&pid)) || pid == 0 || pid == GetCurrentProcessId())
            continue;
        AudioSessionState state = AudioSessionStateInactive;
        control->GetState(&state);
        LPWSTR display = nullptr;
        std::wstring displayName;
        if (SUCCEEDED(control->GetDisplayName(&display)) && display) {
            displayName.assign(display);
            CoTaskMemFree(display);
        }
        result.push_back(SessionHit{pid, IsActiveSessionState(state), std::move(displayName)});
    }
    return result;
}

struct ProcessRecord {
    DWORD parent = 0;
    std::wstring exeName;
};

std::unordered_map<DWORD, ProcessRecord> EnumerateProcesses() {
    std::unordered_map<DWORD, ProcessRecord> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            ProcessRecord record;
            record.parent = entry.th32ParentProcessID;
            record.exeName = entry.szExeFile;
            result[entry.th32ProcessID] = std::move(record);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

DWORD FindWindowAncestor(DWORD pid, const std::set<DWORD>& windowPids,
                         const std::unordered_map<DWORD, ProcessRecord>& processes) {
    for (int depth = 0; depth < 16 && pid > 4; ++depth) {
        if (windowPids.count(pid)) return pid;
        auto it = processes.find(pid);
        if (it == processes.end() || it->second.parent == pid) break;
        pid = it->second.parent;
    }
    return 0;
}

bool IsUninterestingHost(const std::wstring& exe) {
    static const wchar_t* names[] = {
        L"ApplicationFrameHost.exe", L"TextInputHost.exe", L"ShellExperienceHost.exe",
        L"StartMenuExperienceHost.exe", L"SearchHost.exe", L"SystemSettings.exe",
        L"LockApp.exe", L"Widgets.exe", L"WidgetService.exe", L"Copilot.exe",
    };
    for (const wchar_t* name : names) {
        if (_wcsicmp(exe.c_str(), name) == 0) return true;
    }
    return false;
}

struct ActivationHandler final
    : Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                   Microsoft::WRL::FtmBase,
                                   IActivateAudioInterfaceCompletionHandler> {
    ActivationHandler() = default;
    void SetCompletionEvent(HANDLE event) { event_ = event; }
    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation*) override {
        if (event_) ::SetEvent(event_);
        return S_OK;
    }

private:
    HANDLE event_ = nullptr;
};

HRESULT ActivateProcessLoopback(DWORD pid, IAudioClient** out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    HANDLE completed = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!completed) return HRESULT_FROM_WIN32(GetLastError());

    auto handler = Microsoft::WRL::Make<ActivationHandler>();
    if (!handler) {
        CloseHandle(completed);
        return E_OUTOFMEMORY;
    }
    handler->SetCompletionEvent(completed);

    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT prop{};
    PropVariantInit(&prop);
    prop.vt = VT_BLOB;
    prop.blob.cbSize = sizeof(params);
    prop.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                             __uuidof(IAudioClient), &prop, handler.Get(),
                                             operation.GetAddressOf());
    // Do not call PropVariantClear here: VT_BLOB owns pBlobData only conceptually,
    // but the async API may retain it until activation completes. The sample data
    // lives on the stack for the duration of this call.
    if (SUCCEEDED(hr)) {
        DWORD wait = WaitForSingleObject(completed, 5000);
        if (wait != WAIT_OBJECT_0) hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    }
    if (SUCCEEDED(hr)) {
        HRESULT activationResult = E_UNEXPECTED;
        ComPtr<IUnknown> unknown;
        hr = operation->GetActivateResult(&activationResult, unknown.GetAddressOf());
        if (SUCCEEDED(hr)) hr = activationResult;
        if (SUCCEEDED(hr) && unknown) {
            hr = unknown->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(out));
        }
    }
    CloseHandle(completed);
    return hr;
}

HRESULT CreateSystemCaptureClient(IAudioClient** out, WAVEFORMATEX** format) {
    *out = nullptr;
    *format = nullptr;
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) return hr;
    ComPtr<IMMDevice> device;
    hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(hr)) return hr;
    ComPtr<IAudioClient> client;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                          reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(hr)) return hr;
    hr = client->GetMixFormat(format);
    if (FAILED(hr)) return hr;
    *out = client.Detach();
    return S_OK;
}

bool IsFloatFormat(const WAVEFORMATEX* format) {
    if (!format) return false;
    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE;
    }
    return false;
}

int EffectiveChannelCount(const WAVEFORMATEX* format) {
    if (!format) return 0;
    if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        return ext->Format.nChannels ? ext->Format.nChannels : format->nChannels;
    }
    return format->nChannels;
}

float ReadPcmSample(const BYTE* p, int bits, bool isFloat) {
    if (isFloat) {
        if (bits == 32) {
            float value;
            memcpy(&value, p, sizeof(float));
            if (!std::isfinite(value)) return 0.0f;
            return std::clamp(value, -1.0f, 1.0f);
        }
        if (bits == 64) {
            double value;
            memcpy(&value, p, sizeof(double));
            if (!std::isfinite(value)) return 0.0f;
            return static_cast<float>(std::clamp(value, -1.0, 1.0));
        }
        return 0.0f;
    }
    switch (bits) {
        case 8: return (static_cast<int>(*p) - 128) / 128.0f;
        case 16: {
            int16_t value;
            memcpy(&value, p, sizeof(value));
            return value / 32768.0f;
        }
        case 24: {
            int32_t value = static_cast<int32_t>(p[0]) | (static_cast<int32_t>(p[1]) << 8) |
                            (static_cast<int32_t>(p[2]) << 16);
            if (value & 0x800000) value |= ~0xFFFFFF;
            return value / 8388608.0f;
        }
        case 32: {
            int32_t value;
            memcpy(&value, p, sizeof(value));
            return static_cast<float>(value / 2147483648.0);
        }
        default: return 0.0f;
    }
}

}  // namespace

AudioEngine::AudioEngine() {
    ring_.assign(kRingSize, 0.0f);
}

AudioEngine::~AudioEngine() {
    Stop();
}

std::vector<AudioApp> AudioEngine::EnumerateApps() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    std::map<DWORD, WindowHit> windows;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windows));
    const auto processes = EnumerateProcesses();
    std::set<DWORD> windowPids;
    for (const auto& [pid, hit] : windows) windowPids.insert(pid);

    std::unordered_map<DWORD, std::wstring> pathCache;
    std::unordered_map<DWORD, std::wstring> friendlyCache;
    auto infoForPid = [&](DWORD pid, std::wstring* exe, std::wstring* path,
                          std::wstring* friendly) {
        auto pit = processes.find(pid);
        if (exe) {
            if (pit != processes.end()) *exe = pit->second.exeName;
            else {
                auto pathIt = pathCache.find(pid);
                if (pathIt != pathCache.end()) *exe = FileNamePart(pathIt->second);
            }
        }
        if (path) {
            auto it = pathCache.find(pid);
            if (it == pathCache.end()) {
                it = pathCache.emplace(pid, ProcessImageName(pid)).first;
            }
            *path = it->second;
        }
        if (friendly) {
            auto it = friendlyCache.find(pid);
            if (it == friendlyCache.end()) {
                std::wstring processPath = pathCache.count(pid) ? pathCache[pid] : ProcessImageName(pid);
                std::wstring desc = FileDescription(processPath);
                if (desc.empty() && pit != processes.end()) desc = FileStem(pit->second.exeName);
                if (desc.empty()) desc = L"应用程序";
                it = friendlyCache.emplace(pid, desc).first;
            }
            *friendly = it->second;
        }
    };

    const auto sessions = EnumerateAudioSessions();
    std::map<DWORD, bool> windowSessionActive;
    std::vector<SessionHit> unmappedSessions;
    for (const auto& session : sessions) {
        DWORD root = FindWindowAncestor(session.pid, windowPids, processes);
        if (root) {
            windowSessionActive[root] = windowSessionActive[root] || session.active;
        } else {
            unmappedSessions.push_back(session);
        }
    }

    std::vector<AudioApp> apps;
    std::set<DWORD> included;
    for (const auto& [pid, hit] : windows) {
        AudioApp app;
        app.pid = pid;
        app.title = hit.title;
        std::wstring friendly;
        infoForPid(pid, &app.exe, &app.path, &friendly);
        if (app.exe.empty() && !app.path.empty()) app.exe = FileNamePart(app.path);
        if (IsUninterestingHost(app.exe)) continue;
        app.name = friendly;
        if (app.name.empty() || app.name == L"应用程序") {
            if (!app.exe.empty()) app.name = FileStem(app.exe);
        }
        app.playing = windowSessionActive.count(pid) && windowSessionActive[pid];
        apps.push_back(std::move(app));
        included.insert(pid);
    }

    for (const auto& session : unmappedSessions) {
        if (session.pid <= 4 || included.count(session.pid)) continue;
        if (IsUninterestingHost(processes.count(session.pid) ? processes.at(session.pid).exeName : L""))
            continue;
        AudioApp app;
        app.pid = session.pid;
        std::wstring friendly;
        infoForPid(session.pid, &app.exe, &app.path, &friendly);
        if (app.exe.empty() && !app.path.empty()) app.exe = FileNamePart(app.path);
        app.name = friendly;
        if (!session.displayName.empty() && session.displayName[0] != L'@') {
            // Prefer a human friendly session name when the file has no useful metadata.
            if (app.name.empty() || app.name == L"应用程序") app.name = session.displayName;
        }
        if ((app.name.empty() || app.name == L"应用程序") && !app.exe.empty())
            app.name = FileStem(app.exe);
        app.playing = session.active;
        apps.push_back(std::move(app));
        included.insert(session.pid);
    }

    std::sort(apps.begin(), apps.end(), [](const AudioApp& a, const AudioApp& b) {
        if (a.playing != b.playing) return a.playing > b.playing;
        int cmp = _wcsicmp(a.name.c_str(), b.name.c_str());
        if (cmp != 0) return cmp < 0;
        return a.pid < b.pid;
    });
    // Remove duplicate rows with the same process (defensive).
    apps.erase(std::unique(apps.begin(), apps.end(),
                           [](const AudioApp& a, const AudioApp& b) { return a.pid == b.pid; }),
               apps.end());
    return apps;
}

bool AudioEngine::Start(const AudioSource& source) {
    Stop();
    if (source.kind == SourceKind::None) return true;

    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent_) {
            state_.store(CaptureState::Error);
            SetError(L"无法创建音频线程同步事件。");
            return false;
        }
    }
    {
        std::lock_guard<std::mutex> lock(sourceMutex_);
        current_ = source;
    }
    ClearAudio();
    SetError({});
    level_.store(0.0f);
    sampleRate_.store(44100);
    state_.store(CaptureState::Connecting);
    thread_ = std::thread(&AudioEngine::ThreadProc, this, source);
    return true;
}

void AudioEngine::Stop() {
    HANDLE event = nullptr;
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        event = stopEvent_;
    }
    if (event) SetEvent(event);
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lock(controlMutex_);
        if (stopEvent_) CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    if (state_.load() != CaptureState::Error) state_.store(CaptureState::Idle);
    level_.store(0.0f);
}

std::wstring AudioEngine::Error() const {
    std::lock_guard<std::mutex> lock(errorMutex_);
    return error_;
}

AudioSource AudioEngine::Current() const {
    std::lock_guard<std::mutex> lock(sourceMutex_);
    return current_;
}

void AudioEngine::SetError(const std::wstring& message) {
    std::lock_guard<std::mutex> lock(errorMutex_);
    error_ = message;
}

void AudioEngine::ClearAudio() {
    std::lock_guard<std::mutex> lock(ringMutex_);
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    writePos_ = 0;
    totalWritten_ = 0;
}

void AudioEngine::PushSamples(const float* samples, size_t count) {
    if (!samples || count == 0) return;
    double sumSquares = 0.0;
    for (size_t i = 0; i < count; ++i) {
        double v = samples[i];
        sumSquares += v * v;
    }
    float rms = static_cast<float>(std::sqrt(sumSquares / count));
    float target = std::clamp(rms * 5.5f, 0.0f, 1.0f);
    float previous = level_.load(std::memory_order_relaxed);
    level_.store(std::max(target, previous * 0.86f), std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(ringMutex_);
    for (size_t i = 0; i < count; ++i) {
        ring_[writePos_] = samples[i];
        writePos_ = (writePos_ + 1) % ring_.size();
        ++totalWritten_;
    }
}

size_t AudioEngine::Snapshot(float* out, size_t count) const {
    if (!out || count == 0) return 0;
    std::lock_guard<std::mutex> lock(ringMutex_);
    size_t available = static_cast<size_t>(std::min<uint64_t>(count, totalWritten_));
    size_t start = (writePos_ + ring_.size() - available) % ring_.size();
    for (size_t i = 0; i < available; ++i) {
        out[i] = ring_[(start + i) % ring_.size()];
    }
    return available;
}

uint64_t AudioEngine::TotalWritten() const {
    std::lock_guard<std::mutex> lock(ringMutex_);
    return totalWritten_;
}

void AudioEngine::ThreadProc(AudioSource source) {
    HRESULT coHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool shouldUninitialize = SUCCEEDED(coHr);
    if (FAILED(coHr) && coHr != RPC_E_CHANGED_MODE) {
        SetError(L"无法初始化音频捕获线程。");
        state_.store(CaptureState::Error);
        return;
    }

    bool ok = RunCapture(source, stopEvent_);
    if (!ok) {
        if (state_.load() != CaptureState::Error) {
            SetError(L"音频捕获已停止。");
            state_.store(CaptureState::Error);
        }
    } else if (state_.load() != CaptureState::Error) {
        state_.store(CaptureState::Idle);
    }

    if (shouldUninitialize) CoUninitialize();
}

bool AudioEngine::RunCapture(const AudioSource& source, HANDLE stopEvent) {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX* systemFormat = nullptr;
    WAVEFORMATEX processFormat{};
    WAVEFORMATEX* format = nullptr;
    bool ownsFormat = false;
    bool useEvent = true;
    HANDLE sampleEvent = nullptr;
    HRESULT hr = S_OK;

    auto fail = [&](HRESULT result, const wchar_t* context) {
        std::wstring message = context;
        message += L"\n";
        message += HResultText(result);
        SetError(message);
        state_.store(CaptureState::Error);
        if (systemFormat) CoTaskMemFree(systemFormat);
        if (sampleEvent) CloseHandle(sampleEvent);
        if (client) client->Stop();
        return false;
    };

    if (source.kind == SourceKind::Application) {
        hr = ActivateProcessLoopback(source.pid, client.GetAddressOf());
        if (FAILED(hr)) {
            return fail(hr, L"无法激活该应用的音频通道。请确认应用正在运行，且系统为 Windows 11。");
        }
        processFormat.wFormatTag = WAVE_FORMAT_PCM;
        processFormat.nChannels = 2;
        processFormat.nSamplesPerSec = 44100;
        processFormat.wBitsPerSample = 16;
        processFormat.nBlockAlign = 4;
        processFormat.nAvgBytesPerSec = 44100 * 4;
        format = &processFormat;
        sampleRate_.store(44100);

        const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, format, nullptr);
        if (FAILED(hr)) {
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_LOOPBACK |
                                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                    0, 0, format, nullptr);
            useEvent = false;
        }
        if (FAILED(hr)) return fail(hr, L"无法初始化该应用的音频捕获流。");
    } else if (source.kind == SourceKind::System) {
        hr = CreateSystemCaptureClient(client.GetAddressOf(), &systemFormat);
        if (FAILED(hr)) {
            if (systemFormat) CoTaskMemFree(systemFormat);
            return fail(hr, L"无法访问系统音频输出设备。");
        }
        format = systemFormat;
        ownsFormat = true;
        sampleRate_.store(static_cast<int>(format->nSamplesPerSec));
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                0, 0, format, nullptr);
        if (FAILED(hr)) {
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                                    0, 0, format, nullptr);
            useEvent = false;
        }
        if (FAILED(hr)) return fail(hr, L"无法初始化系统音频捕获流。");
    } else {
        return true;
    }

    if (useEvent) {
        sampleEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!sampleEvent) return fail(HRESULT_FROM_WIN32(GetLastError()), L"无法创建音频采样事件。");
        hr = client->SetEventHandle(sampleEvent);
        if (FAILED(hr)) {
            CloseHandle(sampleEvent);
            sampleEvent = nullptr;
            useEvent = false;
        }
    }

    hr = client->GetService(IID_PPV_ARGS(capture.GetAddressOf()));
    if (FAILED(hr)) return fail(hr, L"无法打开音频采样客户端。");
    hr = client->Start();
    if (FAILED(hr)) return fail(hr, L"无法启动音频捕获。");
    state_.store(CaptureState::Running);

    std::vector<float> mono(16384);
    const int channels = std::max(1, static_cast<int>(EffectiveChannelCount(format)));
    const int bits = format->wBitsPerSample;
    const bool isFloat = IsFloatFormat(format);
    const int blockAlign = format->nBlockAlign;
    HANDLE waits[2] = {stopEvent, sampleEvent};

    while (true) {
        DWORD waitResult = WaitForMultipleObjects(useEvent ? 2 : 1, waits, FALSE,
                                                 useEvent ? 250 : 8);
        if (waitResult == WAIT_OBJECT_0) break;
        if (waitResult == WAIT_FAILED) {
            return fail(HRESULT_FROM_WIN32(GetLastError()), L"音频捕获等待失败。");
        }

        for (;;) {
            UINT32 packetFrames = 0;
            hr = capture->GetNextPacketSize(&packetFrames);
            if (FAILED(hr)) return fail(hr, L"读取音频数据包失败。");
            if (packetFrames == 0) break;

            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 devicePosition = 0;
            UINT64 qpcPosition = 0;
            hr = capture->GetBuffer(&data, &frames, &flags, &devicePosition, &qpcPosition);
            if (hr == AUDCLNT_E_OUT_OF_ORDER) break;
            if (FAILED(hr)) return fail(hr, L"获取音频数据失败。");
            if (frames == 0) {
                capture->ReleaseBuffer(0);
                continue;
            }
            if (mono.size() < frames) mono.resize(frames);
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                std::fill_n(mono.data(), frames, 0.0f);
            } else {
                const BYTE* p = data;
                for (UINT32 frame = 0; frame < frames; ++frame) {
                    float mixed = 0.0f;
                    for (int ch = 0; ch < channels; ++ch) {
                        mixed += ReadPcmSample(p + ch * (bits / 8), bits, isFloat);
                    }
                    mono[frame] = mixed / channels;
                    p += blockAlign;
                }
            }
            capture->ReleaseBuffer(frames);
            PushSamples(mono.data(), frames);
        }
    }

    client->Stop();
    if (sampleEvent) CloseHandle(sampleEvent);
    if (systemFormat) CoTaskMemFree(systemFormat);
    (void)ownsFormat;
    return true;
}
