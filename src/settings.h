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

struct VisualSettings {
    EffectConfig outer;
    EffectConfig dots;
    EffectConfig wave;
    ShardConfig shards;
    GlowConfig glow;

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
