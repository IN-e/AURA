#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

#include "audio.h"

struct EffectConfig {
    bool enabled = true;
    int r = 90;
    int g = 232;
    int b = 214;
    int size = 88;
    int amplitude = 100;
    int density = 96;
    int thickness = 100;
    int opacity = 90;
};

struct ShardConfig {
    bool enabled = true;
    int r = 88;
    int g = 235;
    int b = 214;
    int size = 90;
    int frequency = 100;
    int opacity = 70;
    int shape = 0;
};

struct GlowConfig {
    bool enabled = true;
    int r = 88;
    int g = 235;
    int b = 214;
    int size = 95;
    int opacity = 16;
};

struct DynamicColorSettings {
    bool enabled = false;
    int preset = 1;  // -1 = custom, 0..4 = built-in palette
    int primaryR = 157;
    int primaryG = 0;
    int primaryB = 255;
    int secondaryR = 255;
    int secondaryG = 102;
    int secondaryB = 204;
    int accentR = 255;
    int accentG = 179;
    int accentB = 0;
    int glowR = 70;
    int glowG = 20;
    int glowB = 100;

    bool spectrumGradient = true;
    bool bassFlash = true;
    bool timeFlow = true;

    int intensity = 100;      // 0..200 %
    int flowSpeed = 100;      // 0..200 %
    int transitionMs = 500;   // 0..2000 ms
};

struct ColorPreset {
    const wchar_t* name;
    int primaryR, primaryG, primaryB;
    int secondaryR, secondaryG, secondaryB;
    int accentR, accentG, accentB;
    int glowR, glowG, glowB;
};

inline constexpr ColorPreset kColorPresets[5] = {
    {L"赛博", 0, 255, 255, 255, 0, 255, 255, 255, 0, 0, 70, 100},
    {L"幽紫", 157, 0, 255, 255, 102, 204, 255, 179, 0, 70, 20, 100},
    {L"极光", 0, 255, 136, 0, 191, 255, 255, 255, 255, 0, 80, 90},
    {L"落日", 255, 69, 0, 255, 20, 147, 255, 215, 0, 110, 30, 20},
    {L"黑白", 230, 230, 230, 170, 170, 170, 255, 255, 255, 70, 70, 70},
};

struct AmbientSettings {
    bool enabled = true;
    bool fluid = true;
    bool nebula = true;
    bool sustainedGlow = true;
    int fluidAmplitude = 100;   // 0..200 %
    int fluidSpeed = 100;       // 0..200 %
    int wavelength = 3;         // 2..8 lobes
    int nebulaDensity = 100;    // 0..200 %
    int nebulaOpacity = 100;    // 0..200 %
    int glowGain = 100;         // 0..200 %
    int hueShift = 30;          // 0..120 degrees
    int easing = 2;             // 1..10 -> 0.01..0.10
};

struct VisualSettings {
    EffectConfig outer;
    EffectConfig dots;
    EffectConfig wave;
    ShardConfig shards;
    GlowConfig glow;
    DynamicColorSettings colors;
    AmbientSettings ambient;
    bool outerRhythm = true;

    int panelR = 12;
    int panelG = 20;
    int panelB = 24;
    int panelOpacity = 95;
    int bloomRange = 100;
    int bloomBrightness = 115;
    int overlayScale = 100;

    bool overlayFixed = false;
    int overlayX = -1;
    int overlayY = -1;
    int panelX = -1;
    int panelY = -1;

    SourceKind sourceKind = SourceKind::None;
    std::wstring sourcePath;
    std::wstring sourceName;
};

struct EffectSpec {
    const wchar_t* title;
    const wchar_t* english;
    const wchar_t* description;
    int sizeMin;
    int sizeMax;
    int ampMin;
    int ampMax;
    int densityMin;
    int densityMax;
};

inline const EffectSpec kEffectSpecs[3] = {
    {L"声环", L"OUTER RING", L"双层波形环与交织的声纹连线", 30, 100, 0, 200, 24, 160},
    {L"点阵", L"DOT HALO", L"跟随内层声环起伏的光点脉络", 20, 95, 0, 200, 24, 180},
    {L"横波", L"WAVE LINE", L"穿过中心的横向示波声线", 20, 100, 0, 200, 20, 360},
};

EffectConfig DefaultOuter();
EffectConfig DefaultDots();
EffectConfig DefaultWave();
VisualSettings DefaultSettings();

int ClampInt(int value, int low, int high);
BYTE ToByte(int value);

class SettingsStore {
public:
    explicit SettingsStore(std::wstring path);
    VisualSettings Load() const;
    void Save(const VisualSettings& settings) const;
    const std::wstring& Path() const { return path_; }

private:
    std::wstring path_;
};

std::wstring DefaultSettingsPath();
