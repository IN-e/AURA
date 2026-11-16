#include "settings.h"

#include <shlobj.h>

#include <algorithm>
#include <iterator>

int ClampInt(int value, int low, int high) {
    return std::max(low, std::min(high, value));
}

BYTE ToByte(int value) {
    return static_cast<BYTE>(ClampInt(value, 0, 255));
}

EffectConfig DefaultOuter() {
    EffectConfig config;
    config.enabled = true;
    config.r = 88;
    config.g = 235;
    config.b = 214;
    config.size = 88;
    config.amplitude = 100;
    config.density = 104;
    config.thickness = 100;
    config.opacity = 92;
    return config;
}

EffectConfig DefaultDots() {
    EffectConfig config;
    config.enabled = true;
    config.r = 137;
    config.g = 255;
    config.b = 236;
    config.size = 72;
    config.amplitude = 112;
    config.density = 88;
    config.thickness = 100;
    config.opacity = 82;
    return config;
}

EffectConfig DefaultWave() {
    EffectConfig config;
    config.enabled = true;
    config.r = 194;
    config.g = 255;
    config.b = 246;
    config.size = 72;
    config.amplitude = 100;
    config.density = 180;
    config.thickness = 100;
    config.opacity = 86;
    return config;
}

VisualSettings DefaultSettings() {
    VisualSettings settings;
    settings.outer = DefaultOuter();
    settings.dots = DefaultDots();
    settings.wave = DefaultWave();
    return settings;
}

namespace {

const wchar_t* kOuterSection = L"effect.outer";
const wchar_t* kDotsSection = L"effect.dots";
const wchar_t* kWaveSection = L"effect.wave";
const wchar_t* kPanelSection = L"panel";
const wchar_t* kOverlaySection = L"overlay";
const wchar_t* kSourceSection = L"source";
const wchar_t* kShardsSection = L"effect.shards";
const wchar_t* kGlowSection = L"effect.glow";

EffectConfig ReadEffect(const std::wstring& path, const wchar_t* section,
                        const EffectConfig& defaults, int index) {
    EffectConfig result = defaults;
    result.enabled = GetPrivateProfileIntW(section, L"enabled", defaults.enabled ? 1 : 0,
                                            path.c_str()) != 0;
    result.r = GetPrivateProfileIntW(section, L"r", defaults.r, path.c_str());
    result.g = GetPrivateProfileIntW(section, L"g", defaults.g, path.c_str());
    result.b = GetPrivateProfileIntW(section, L"b", defaults.b, path.c_str());
    result.size = GetPrivateProfileIntW(section, L"size", defaults.size, path.c_str());
    result.amplitude =
        GetPrivateProfileIntW(section, L"amplitude", defaults.amplitude, path.c_str());
    result.density = GetPrivateProfileIntW(section, L"density", defaults.density, path.c_str());
    result.thickness =
        GetPrivateProfileIntW(section, L"thickness", defaults.thickness, path.c_str());
    result.opacity = GetPrivateProfileIntW(section, L"opacity", defaults.opacity, path.c_str());

    result.r = ClampInt(result.r, 0, 255);
    result.g = ClampInt(result.g, 0, 255);
    result.b = ClampInt(result.b, 0, 255);
    result.size = ClampInt(result.size, kEffectSpecs[index].sizeMin, kEffectSpecs[index].sizeMax);
    result.amplitude =
        ClampInt(result.amplitude, kEffectSpecs[index].ampMin, kEffectSpecs[index].ampMax);
    result.density =
        ClampInt(result.density, kEffectSpecs[index].densityMin, kEffectSpecs[index].densityMax);
    result.thickness = ClampInt(result.thickness, 40, 300);
    result.opacity = ClampInt(result.opacity, 1, 100);
    return result;
}

void WriteEffect(const std::wstring& path, const wchar_t* section, const EffectConfig& value) {
    auto write = [&](const wchar_t* key, int number) {
        WritePrivateProfileStringW(section, key, std::to_wstring(number).c_str(), path.c_str());
    };
    write(L"enabled", value.enabled ? 1 : 0);
    write(L"r", value.r);
    write(L"g", value.g);
    write(L"b", value.b);
    write(L"size", value.size);
    write(L"amplitude", value.amplitude);
    write(L"density", value.density);
    write(L"thickness", value.thickness);
    write(L"opacity", value.opacity);
}

std::wstring ReadString(const wchar_t* section, const wchar_t* key, const std::wstring& defaultVal,
                        const std::wstring& path) {
    wchar_t buffer[4096]{};
    GetPrivateProfileStringW(section, key, defaultVal.c_str(), buffer,
                             static_cast<DWORD>(std::size(buffer)), path.c_str());
    return buffer;
}

}  // namespace

namespace {

bool CanWriteSettingsFile(const std::wstring& filePath) {
    HANDLE file = CreateFileW(filePath.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    return true;
}

std::wstring JoinPath(const std::wstring& directory, const wchar_t* name) {
    std::wstring result = directory;
    if (!result.empty() && result.back() != L'\\' && result.back() != L'/') result.push_back(L'\\');
    result += name;
    return result;
}

}  // namespace

std::wstring DefaultSettingsPath() {
    wchar_t overridePath[32768]{};
    DWORD overrideLength =
        GetEnvironmentVariableW(L"AURA_SETTINGS_PATH", overridePath,
                               static_cast<DWORD>(std::size(overridePath)));
    if (overrideLength > 0 && overrideLength < std::size(overridePath)) {
        return overridePath;
    }

    // Primary location: beside AURA.exe, so the preset travels with the app.
    wchar_t exePath[32768]{};
    DWORD exeLength = GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    if (exeLength > 0 && exeLength < std::size(exePath)) {
        std::wstring dir(exePath, exeLength);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir.resize(slash);
        std::wstring presetCandidate = JoinPath(dir, L"presets.ini");

        // One-time migration from the previous Documents location when the
        // portable preset does not exist yet.
        if (GetFileAttributesW(presetCandidate.c_str()) == INVALID_FILE_ATTRIBUTES) {
            PWSTR documents = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr,
                                                &documents)) &&
                documents) {
                std::wstring oldDir = JoinPath(documents, L"AURA");
                CoTaskMemFree(documents);
                std::wstring oldPreset = JoinPath(oldDir, L"presets.ini");
                if (GetFileAttributesW(oldPreset.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    CopyFileW(oldPreset.c_str(), presetCandidate.c_str(), TRUE);
                }
            }
        }

        if (CanWriteSettingsFile(presetCandidate)) return presetCandidate;
    }

    // If the program directory is read-only (Program Files, etc.), fall back
    // to the user's documents so the app remains usable.
    PWSTR documents = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_CREATE, nullptr, &documents)) &&
        documents) {
        std::wstring dir = documents;
        CoTaskMemFree(documents);
        dir = JoinPath(dir, L"AURA");
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring documentCandidate = JoinPath(dir, L"presets.ini");
        if (CanWriteSettingsFile(documentCandidate)) return documentCandidate;
    }

    std::wstring localCandidate;
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local)) &&
        local) {
        std::wstring dir = local;
        CoTaskMemFree(local);
        dir = JoinPath(dir, L"AuraSoundRing");
        CreateDirectoryW(dir.c_str(), nullptr);
        localCandidate = JoinPath(dir, L"settings.ini");
        if (CanWriteSettingsFile(localCandidate)) return localCandidate;
    }

    wchar_t temp[MAX_PATH]{};
    GetTempPathW(static_cast<DWORD>(std::size(temp)), temp);
    std::wstring tempCandidate = JoinPath(temp, L"AURA-settings.ini");
    if (CanWriteSettingsFile(tempCandidate)) return tempCandidate;

    if (!localCandidate.empty()) return localCandidate;
    return tempCandidate;
}

SettingsStore::SettingsStore(std::wstring path) : path_(std::move(path)) {}

VisualSettings SettingsStore::Load() const {
    VisualSettings settings = DefaultSettings();
    settings.outer = ReadEffect(path_, kOuterSection, settings.outer, 0);
    settings.dots = ReadEffect(path_, kDotsSection, settings.dots, 1);
    settings.wave = ReadEffect(path_, kWaveSection, settings.wave, 2);
    settings.shards.enabled =
        GetPrivateProfileIntW(kShardsSection, L"enabled", settings.shards.enabled ? 1 : 0,
                              path_.c_str()) != 0;
    settings.shards.r = ClampInt(
        GetPrivateProfileIntW(kShardsSection, L"r", settings.shards.r, path_.c_str()), 0, 255);
    settings.shards.g = ClampInt(
        GetPrivateProfileIntW(kShardsSection, L"g", settings.shards.g, path_.c_str()), 0, 255);
    settings.shards.b = ClampInt(
        GetPrivateProfileIntW(kShardsSection, L"b", settings.shards.b, path_.c_str()), 0, 255);
    settings.shards.size = ClampInt(
        GetPrivateProfileIntW(kShardsSection, L"size", settings.shards.size, path_.c_str()),
        20, 200);
    settings.shards.frequency = ClampInt(
        GetPrivateProfileIntW(kShardsSection, L"frequency", settings.shards.frequency,
                              path_.c_str()),
        0, 200);
    settings.shards.opacity = ClampInt(
        GetPrivateProfileIntW(kShardsSection, L"opacity", settings.shards.opacity, path_.c_str()),
        0, 100);
    settings.shards.shape = ClampInt(
        GetPrivateProfileIntW(kShardsSection, L"shape", settings.shards.shape, path_.c_str()),
        0, 4);

    settings.glow.enabled =
        GetPrivateProfileIntW(kGlowSection, L"enabled", settings.glow.enabled ? 1 : 0,
                              path_.c_str()) != 0;
    settings.glow.r = ClampInt(
        GetPrivateProfileIntW(kGlowSection, L"r", settings.glow.r, path_.c_str()), 0, 255);
    settings.glow.g = ClampInt(
        GetPrivateProfileIntW(kGlowSection, L"g", settings.glow.g, path_.c_str()), 0, 255);
    settings.glow.b = ClampInt(
        GetPrivateProfileIntW(kGlowSection, L"b", settings.glow.b, path_.c_str()), 0, 255);
    settings.glow.size = ClampInt(
        GetPrivateProfileIntW(kGlowSection, L"size", settings.glow.size, path_.c_str()), 20,
        200);
    settings.glow.opacity = ClampInt(
        GetPrivateProfileIntW(kGlowSection, L"opacity", settings.glow.opacity, path_.c_str()),
        0, 100);

    settings.panelR = ClampInt(GetPrivateProfileIntW(kPanelSection, L"r", settings.panelR,
                                                    path_.c_str()), 0, 255);
    settings.panelG = ClampInt(GetPrivateProfileIntW(kPanelSection, L"g", settings.panelG,
                                                    path_.c_str()), 0, 255);
    settings.panelB = ClampInt(GetPrivateProfileIntW(kPanelSection, L"b", settings.panelB,
                                                    path_.c_str()), 0, 255);
    settings.panelOpacity = ClampInt(GetPrivateProfileIntW(kPanelSection, L"opacity",
                                                          settings.panelOpacity, path_.c_str()),
                                     1, 100);
    settings.bloomRange = ClampInt(GetPrivateProfileIntW(kPanelSection, L"bloomRange",
                                                         settings.bloomRange, path_.c_str()),
                                   10, 200);
    settings.bloomBrightness =
        ClampInt(GetPrivateProfileIntW(kPanelSection, L"bloomBrightness",
                                      settings.bloomBrightness, path_.c_str()),
                 0, 200);
    settings.panelX = GetPrivateProfileIntW(kPanelSection, L"x", -1, path_.c_str());
    settings.panelY = GetPrivateProfileIntW(kPanelSection, L"y", -1, path_.c_str());

    settings.overlayFixed =
        GetPrivateProfileIntW(kOverlaySection, L"fixed", settings.overlayFixed ? 1 : 0,
                              path_.c_str()) != 0;
    settings.overlayScale = ClampInt(
        GetPrivateProfileIntW(kOverlaySection, L"scale", settings.overlayScale, path_.c_str()),
        40, 200);
    settings.overlayX = GetPrivateProfileIntW(kOverlaySection, L"x", -1, path_.c_str());
    settings.overlayY = GetPrivateProfileIntW(kOverlaySection, L"y", -1, path_.c_str());

    int kind = GetPrivateProfileIntW(kSourceSection, L"kind", 0, path_.c_str());
    if (kind < 0 || kind > 2) kind = 0;
    settings.sourceKind = static_cast<SourceKind>(kind);
    settings.sourcePath = ReadString(kSourceSection, L"path", L"", path_);
    settings.sourceName = ReadString(kSourceSection, L"name", L"", path_);
    if (settings.sourceKind == SourceKind::System) {
        if (settings.sourceName.empty()) settings.sourceName = L"系统全部音频";
    }
    if (settings.sourceKind == SourceKind::Application && settings.sourcePath.empty() &&
        settings.sourceName.empty()) {
        settings.sourceKind = SourceKind::None;
    }
    return settings;
}

void SettingsStore::Save(const VisualSettings& settings) const {
    WriteEffect(path_, kOuterSection, settings.outer);
    WriteEffect(path_, kDotsSection, settings.dots);
    WriteEffect(path_, kWaveSection, settings.wave);

    auto writeShards = [&](const wchar_t* key, int number) {
        WritePrivateProfileStringW(kShardsSection, key, std::to_wstring(number).c_str(),
                                   path_.c_str());
    };
    writeShards(L"enabled", settings.shards.enabled ? 1 : 0);
    writeShards(L"r", settings.shards.r);
    writeShards(L"g", settings.shards.g);
    writeShards(L"b", settings.shards.b);
    writeShards(L"size", settings.shards.size);
    writeShards(L"frequency", settings.shards.frequency);
    writeShards(L"opacity", settings.shards.opacity);
    writeShards(L"shape", settings.shards.shape);

    auto writeGlow = [&](const wchar_t* key, int number) {
        WritePrivateProfileStringW(kGlowSection, key, std::to_wstring(number).c_str(),
                                   path_.c_str());
    };
    writeGlow(L"enabled", settings.glow.enabled ? 1 : 0);
    writeGlow(L"r", settings.glow.r);
    writeGlow(L"g", settings.glow.g);
    writeGlow(L"b", settings.glow.b);
    writeGlow(L"size", settings.glow.size);
    writeGlow(L"opacity", settings.glow.opacity);

    auto writePanel = [&](const wchar_t* key, int number) {
        WritePrivateProfileStringW(kPanelSection, key, std::to_wstring(number).c_str(),
                                   path_.c_str());
    };
    writePanel(L"r", settings.panelR);
    writePanel(L"g", settings.panelG);
    writePanel(L"b", settings.panelB);
    writePanel(L"opacity", settings.panelOpacity);
    writePanel(L"bloomRange", settings.bloomRange);
    writePanel(L"bloomBrightness", settings.bloomBrightness);
    writePanel(L"x", settings.panelX);
    writePanel(L"y", settings.panelY);

    auto writeOverlay = [&](const wchar_t* key, int number) {
        WritePrivateProfileStringW(kOverlaySection, key, std::to_wstring(number).c_str(),
                                   path_.c_str());
    };
    writeOverlay(L"fixed", settings.overlayFixed ? 1 : 0);
    writeOverlay(L"scale", settings.overlayScale);
    writeOverlay(L"x", settings.overlayX);
    writeOverlay(L"y", settings.overlayY);

    WritePrivateProfileStringW(kSourceSection, L"kind",
                               std::to_wstring(static_cast<int>(settings.sourceKind)).c_str(),
                               path_.c_str());
    WritePrivateProfileStringW(kSourceSection, L"path", settings.sourcePath.c_str(),
                               path_.c_str());
    WritePrivateProfileStringW(kSourceSection, L"name", settings.sourceName.c_str(),
                               path_.c_str());
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path_.c_str());
}
