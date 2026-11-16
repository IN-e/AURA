#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

using Microsoft::WRL::ComPtr;

namespace gfx {

constexpr float kPi = 3.14159265358979323846f;

struct Color {
    int r = 255;
    int g = 255;
    int b = 255;
    int a = 255;

    D2D1_COLOR_F D2D() const {
        return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    }
    D2D1_COLOR_F D2D(float alpha) const {
        return D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f,
                            (a / 255.0f) * std::clamp(alpha, 0.0f, 1.0f));
    }
    Color WithAlpha(int alpha) const {
        Color result = *this;
        result.a = std::clamp(alpha, 0, 255);
        return result;
    }
    Color WithAlphaF(float alpha) const {
        return WithAlpha(static_cast<int>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)));
    }
    Color Lightened(float amount) const {
        auto mix = [&](int channel) {
            return static_cast<int>(std::lround(channel + (255 - channel) * amount));
        };
        return Color{mix(r), mix(g), mix(b), a};
    }
    Color Darkened(float amount) const {
        auto mix = [&](int channel) { return static_cast<int>(std::lround(channel * (1 - amount))); };
        return Color{mix(r), mix(g), mix(b), a};
    }
    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b;
    }
};

inline Color Rgb(int r, int g, int b, int a = 255) {
    return Color{std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255),
                 std::clamp(a, 0, 255)};
}

inline Color Mix(const Color& a, const Color& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color{static_cast<int>(std::lround(a.r + (b.r - a.r) * t)),
                 static_cast<int>(std::lround(a.g + (b.g - a.g) * t)),
                 static_cast<int>(std::lround(a.b + (b.b - a.b) * t)),
                 static_cast<int>(std::lround(a.a + (b.a - a.a) * t))};
}

inline float Luminance(const Color& color) {
    auto srgb = [](int channel) {
        float value = channel / 255.0f;
        return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * srgb(color.r) + 0.7152f * srgb(color.g) + 0.0722f * srgb(color.b);
}

inline bool IsLight(const Color& color) {
    return Luminance(color) > 0.42f;
}

inline D2D1_ROUNDED_RECT RoundRect(float x, float y, float w, float h, float radius) {
    return D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), radius, radius);
}

inline D2D1_POINT_2F Point(float x, float y) {
    return D2D1::Point2F(x, y);
}

inline float Clamp(float value, float low, float high) {
    return std::max(low, std::min(high, value));
}

inline float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

inline int IRound(float value) {
    return static_cast<int>(std::lround(value));
}

UINT MonitorDpi(HWND hwnd);
float WindowScale(HWND hwnd);
RECT WorkAreaForPoint(POINT point);

bool CreateTextFormat(IDWriteFactory* factory, const wchar_t* family, float size,
                      DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT alignment,
                      DWRITE_PARAGRAPH_ALIGNMENT paragraph, ComPtr<IDWriteTextFormat>& out);

// Draws a single line of text into the supplied box using Microsoft YaHei UI.
void DrawTextLine(ID2D1RenderTarget* target, IDWriteFactory* factory, const std::wstring& text,
                  float size, int r, int g, int b, int a, D2D1_RECT_F box,
                  DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING,
                  DWRITE_PARAGRAPH_ALIGNMENT paragraph = DWRITE_PARAGRAPH_ALIGNMENT_CENTER,
                  DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);

D2D1_SIZE_F MeasureText(IDWriteFactory* factory, const std::wstring& text, float size,
                        DWRITE_FONT_WEIGHT weight);

ID2D1SolidColorBrush* GetBrush(ID2D1RenderTarget* target, ComPtr<ID2D1SolidColorBrush>& brush,
                               const D2D1_COLOR_F& color);

std::wstring FormatInt(int value);
std::wstring FormatPercent(int value);

}  // namespace gfx
