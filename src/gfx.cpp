#include "gfx.h"

#include <cmath>

namespace gfx {

UINT MonitorDpi(HWND hwnd) {
    UINT dpi = 96;
    if (hwnd) dpi = GetDpiForWindow(hwnd);
    if (!dpi) dpi = GetDpiForSystem();
    return dpi ? dpi : 96;
}

float WindowScale(HWND hwnd) {
    return MonitorDpi(hwnd) / 96.0f;
}

RECT WorkAreaForPoint(POINT point) {
    HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor && GetMonitorInfoW(monitor, &info)) return info.rcWork;
    RECT fallback{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    return fallback;
}

bool CreateTextFormat(IDWriteFactory* factory, const wchar_t* family, float size,
                      DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT alignment,
                      DWRITE_PARAGRAPH_ALIGNMENT paragraph, ComPtr<IDWriteTextFormat>& out) {
    if (!factory) return false;
    out.Reset();
    HRESULT hr = factory->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                           DWRITE_FONT_STRETCH_NORMAL, size, L"zh-CN", &out);
    if (FAILED(hr)) return false;
    out->SetTextAlignment(alignment);
    out->SetParagraphAlignment(paragraph);
    out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return true;
}

void DrawTextLine(ID2D1RenderTarget* target, IDWriteFactory* factory, const std::wstring& text,
                  float size, int r, int g, int b, int a, D2D1_RECT_F box,
                  DWRITE_TEXT_ALIGNMENT alignment, DWRITE_PARAGRAPH_ALIGNMENT paragraph,
                  DWRITE_FONT_WEIGHT weight) {
    if (!target || !factory || text.empty()) return;
    ComPtr<IDWriteTextFormat> format;
    if (!CreateTextFormat(factory, L"Microsoft YaHei UI", size, weight, alignment, paragraph,
                          format)) {
        return;
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    target->CreateSolidColorBrush(D2D1::ColorF(r / 255.0f, g / 255.0f, b / 255.0f,
                                              a / 255.0f),
                                  &brush);
    if (!brush) return;
    target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), box,
                     brush.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
}

D2D1_SIZE_F MeasureText(IDWriteFactory* factory, const std::wstring& text, float size,
                        DWRITE_FONT_WEIGHT weight) {
    D2D1_SIZE_F result{0, 0};
    if (!factory || text.empty()) return result;
    ComPtr<IDWriteTextFormat> format;
    if (!CreateTextFormat(factory, L"Microsoft YaHei UI", size, weight, DWRITE_TEXT_ALIGNMENT_LEADING,
                          DWRITE_PARAGRAPH_ALIGNMENT_NEAR, format)) {
        return result;
    }
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                         format.Get(), 4096.0f, 4096.0f, &layout))) {
        return result;
    }
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(layout->GetMetrics(&metrics))) {
        result.width = metrics.widthIncludingTrailingWhitespace;
        result.height = metrics.height;
    }
    return result;
}

ID2D1SolidColorBrush* GetBrush(ID2D1RenderTarget* target, ComPtr<ID2D1SolidColorBrush>& brush,
                               const D2D1_COLOR_F& color) {
    if (!target) return nullptr;
    if (!brush) {
        target->CreateSolidColorBrush(color, &brush);
    }
    if (brush) brush->SetColor(color);
    return brush.Get();
}

std::wstring FormatInt(int value) {
    wchar_t buffer[32];
    swprintf_s(buffer, L"%d", value);
    return buffer;
}

std::wstring FormatPercent(int value) {
    wchar_t buffer[32];
    swprintf_s(buffer, L"%d%%", value);
    return buffer;
}

}  // namespace gfx
