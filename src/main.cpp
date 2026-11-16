#include "audio.h"
#include "overlay.h"
#include "panel.h"
#include "settings.h"

#include <shellscalingapi.h>
#include <windows.h>

#include <string>

namespace {
constexpr wchar_t kMutexName[] = L"Local\\AuraSoundRing.SingleInstance";

LARGE_INTEGER Now() {
    LARGE_INTEGER value{};
    QueryPerformanceCounter(&value);
    return value;
}

double SecondsBetween(const LARGE_INTEGER& a, const LARGE_INTEGER& b,
                      const LARGE_INTEGER& frequency) {
    if (frequency.QuadPart == 0) return 0.0;
    return static_cast<double>(b.QuadPart - a.QuadPart) /
           static_cast<double>(frequency.QuadPart);
}

AudioSource FindSavedSource(const VisualSettings& settings) {
    AudioSource source;
    if (settings.sourceKind == SourceKind::System) {
        source.kind = SourceKind::System;
        source.name = settings.sourceName.empty() ? L"系统全部音频" : settings.sourceName;
        return source;
    }
    if (settings.sourceKind != SourceKind::Application) return source;

    std::vector<AudioApp> apps = AudioEngine::EnumerateApps();
    const AudioApp* match = nullptr;
    if (!settings.sourcePath.empty()) {
        for (const auto& app : apps) {
            if (!app.path.empty() && _wcsicmp(app.path.c_str(), settings.sourcePath.c_str()) == 0) {
                match = &app;
                break;
            }
        }
    }
    if (!match && !settings.sourceName.empty()) {
        for (const auto& app : apps) {
            if (_wcsicmp(app.name.c_str(), settings.sourceName.c_str()) == 0) {
                match = &app;
                break;
            }
        }
    }
    if (!match) {
        for (const auto& app : apps) {
            if (!app.playing) continue;
            if (!settings.sourceName.empty() &&
                _wcsicmp(app.name.c_str(), settings.sourceName.c_str()) == 0) {
                match = &app;
                break;
            }
        }
    }
    if (!match) return source;
    source.kind = SourceKind::Application;
    source.pid = match->pid;
    source.path = match->path;
    source.name = match->name;
    return source;
}
}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        MessageBoxW(nullptr, L"无法初始化 Windows COM 组件。", L"AURA", MB_ICONERROR);
        return 1;
    }

    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"AURA 已经在运行。请从任务栏打开控制面板。", L"AURA",
                    MB_ICONINFORMATION);
        CloseHandle(mutex);
        CoUninitialize();
        return 0;
    }

    SettingsStore store(DefaultSettingsPath());
    VisualSettings settings = store.Load();
    AudioEngine audio;
    Overlay overlay;
    ControlPanel panel;

    bool running = true;
    bool savePending = false;
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER lastChange = Now();
    LARGE_INTEGER lastRender = Now();
    LARGE_INTEGER lastTopmostCheck = Now();

    if (!overlay.Create(instance, &settings, &audio)) {
        MessageBoxW(nullptr, overlay.LastError().c_str(),
                    L"AURA · 无法初始化浮窗", MB_ICONERROR);
        if (mutex) CloseHandle(mutex);
        CoUninitialize();
        return 2;
    }

    overlay.SetOnChanged([&] {
        savePending = true;
        lastChange = Now();
    });
    panel.SetOnChanged([&] {
        savePending = true;
        lastChange = Now();
        overlay.OnSettingsChanged();
    });
    panel.SetOnExit([&] {
        running = false;
        PostQuitMessage(0);
    });
    panel.SetOnRecenter([&] { overlay.RecenterOnPrimary(); });

    if (!panel.Create(instance, &settings, &audio)) {
        MessageBoxW(nullptr, panel.LastError().c_str(), L"AURA · 无法初始化控制面板",
                    MB_ICONERROR);
        overlay.Destroy();
        if (mutex) CloseHandle(mutex);
        CoUninitialize();
        return 3;
    }

    overlay.OnSettingsChanged();
    AudioSource restored = FindSavedSource(settings);
    if (restored.kind != SourceKind::None) {
        if (restored.kind == SourceKind::Application) {
            settings.sourcePath = restored.path;
            if (!restored.name.empty()) settings.sourceName = restored.name;
        }
        audio.Start(restored);
    }

    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr,
                                          CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                          TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    if (timer) {
        LARGE_INTEGER due{};
        due.QuadPart = -10000000LL * 16 / 1000;
        SetWaitableTimer(timer, &due, 16, nullptr, nullptr, FALSE);
    }

    MSG message{};
    while (running) {
        DWORD waitResult = MsgWaitForMultipleObjectsEx(
            timer ? 1 : 0, timer ? &timer : nullptr, timer ? INFINITE : 16, QS_ALLINPUT,
            MWMO_INPUTAVAILABLE);
        if (waitResult == WAIT_FAILED) break;

        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!running) break;

        LARGE_INTEGER now = Now();
        if (SecondsBetween(lastRender, now, frequency) >= 1.0 / 70.0) {
            lastRender = now;
            overlay.Tick();
            panel.Tick();
            // Windows can drop a topmost window below a restored normal window
            // after "show desktop" / minimize-all. Re-assert the band periodically
            // and keep the control panel above the overlay.
            if (SecondsBetween(lastTopmostCheck, now, frequency) >= 1.0) {
                lastTopmostCheck = now;
                overlay.ReassertTopmost();
                panel.ReassertTopmost();
            }
            if (savePending && SecondsBetween(lastChange, now, frequency) >= 0.40) {
                store.Save(settings);
                savePending = false;
            }
        }
    }

    store.Save(settings);
    if (timer) CloseHandle(timer);
    audio.Stop();
    overlay.Destroy();
    panel.Destroy();
    if (mutex) CloseHandle(mutex);
    CoUninitialize();
    return 0;
}
