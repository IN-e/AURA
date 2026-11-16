#include "panel.h"

#include <d2d1helper.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kPanelClass[] = L"AuraSoundRingPanel";
constexpr int kDesignWidth = 560;
constexpr int kDesignHeight = 850;
constexpr int kAppIconId = 101;
constexpr int kListTop = 212;
constexpr int kRowHeight = 54;
constexpr int kRowGap = 6;
constexpr int kVisibleRows = 8;

int ClampIntValue(int value, int low, int high) {
    return std::max(low, std::min(high, value));
}

D2D1_RECT_F RectF(float x, float y, float w, float h) {
    return D2D1::RectF(x, y, x + w, y + h);
}

bool Contains(const D2D1_RECT_F& rect, POINT point) {
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top &&
           point.y < rect.bottom;
}

std::wstring Flatten(const std::wstring& text) {
    std::wstring result;
    result.reserve(text.size());
    bool lastSpace = false;
    for (wchar_t ch : text) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') {
            if (!lastSpace) {
                result.push_back(L' ');
                lastSpace = true;
            }
        } else {
            result.push_back(ch);
            lastSpace = ch == L' ';
        }
    }
    while (!result.empty() && result.back() == L' ') result.pop_back();
    return result;
}

std::wstring BaseName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring SampleRateText(int sampleRate) {
    if (sampleRate <= 0) return L"";
    wchar_t buffer[32];
    if (sampleRate % 1000 == 0) swprintf_s(buffer, L"%d kHz", sampleRate / 1000);
    else swprintf_s(buffer, L"%.1f kHz", sampleRate / 1000.0f);
    return buffer;
}

void FillRound(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const gfx::Color& color,
               const D2D1_RECT_F& rect, float radius) {
    if (!target || !brush) return;
    brush->SetColor(color.D2D());
    target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush);
}

void StrokeRound(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const gfx::Color& color,
                 const D2D1_RECT_F& rect, float radius, float width) {
    if (!target || !brush) return;
    brush->SetColor(color.D2D());
    target->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush, width);
}

void FillCircle(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const gfx::Color& color,
                float cx, float cy, float radius) {
    if (!target || !brush) return;
    brush->SetColor(color.D2D());
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush);
}

void StrokeCircle(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const gfx::Color& color,
                  float cx, float cy, float radius, float width) {
    if (!target || !brush) return;
    brush->SetColor(color.D2D());
    target->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush, width);
}

void DrawLine(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const gfx::Color& color,
              float x1, float y1, float x2, float y2, float width) {
    if (!target || !brush) return;
    brush->SetColor(color.D2D());
    target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, width);
}

void DrawLabel(ID2D1RenderTarget* target, IDWriteFactory* factory, ID2D1SolidColorBrush* brush,
               const std::wstring& text, float size, DWRITE_FONT_WEIGHT weight,
               const gfx::Color& color, const D2D1_RECT_F& rect,
               DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
               DWRITE_PARAGRAPH_ALIGNMENT paragraph = DWRITE_PARAGRAPH_ALIGNMENT_CENTER) {
    if (!target || !factory || !brush || text.empty()) return;
    ComPtr<IDWriteTextFormat> format;
    if (!gfx::CreateTextFormat(factory, L"Microsoft YaHei UI", size, weight, align, paragraph,
                               format))
        return;
    brush->SetColor(color.D2D());
    target->DrawText(text.c_str(), static_cast<UINT32>(text.size()), format.Get(), rect, brush,
                     D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

std::wstring Ellipsize(IDWriteFactory* factory, const std::wstring& text, float size,
                       DWRITE_FONT_WEIGHT weight, float maxWidth) {
    if (text.empty()) return text;
    D2D1_SIZE_F measured = gfx::MeasureText(factory, text, size, weight);
    if (measured.width <= maxWidth) return text;
    std::wstring result = text;
    const std::wstring suffix = L"…";
    while (!result.empty()) {
        result.pop_back();
        measured = gfx::MeasureText(factory, result + suffix, size, weight);
        if (measured.width <= maxWidth) return result + suffix;
    }
    return suffix;
}

float ProgressForValue(int value, int low, int high) {
    if (high <= low) return 0.0f;
    return std::clamp((value - low) / static_cast<float>(high - low), 0.0f, 1.0f);
}
}  // namespace

ControlPanel::ControlPanel() {
    QueryPerformanceCounter(&lastPanelTick_);
}

ControlPanel::~ControlPanel() {
    Destroy();
}

bool ControlPanel::Create(HINSTANCE instance, VisualSettings* settings, AudioEngine* audio) {
    instance_ = instance;
    settings_ = settings;
    audio_ = audio;
    if (!CreateWindowInternal(instance)) return false;
    if (!InitGraphics()) return false;
    valid_ = true;
    RefreshStatus();
    Render();
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(hwnd_);
    dirty_ = false;
    return true;
}

void ControlPanel::Destroy() {
    if (windowDragging_ || sliderDragging_) {
        windowDragging_ = false;
        sliderDragging_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
    }
    ReleaseGraphics();
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    valid_ = false;
}

bool ControlPanel::CreateWindowInternal(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = instance;
    wc.hIcon = reinterpret_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(kAppIconId),
                                                  IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPanelClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    POINT cursor{};
    GetCursorPos(&cursor);
    RECT work = gfx::WorkAreaForPoint(cursor);
    dpi_ = GetDpiForSystem();
    if (!dpi_) dpi_ = 96;
    float dpiScale = dpi_ / 96.0f;
    float fit = std::min((work.bottom - work.top - 48.0f) / kDesignHeight,
                         (work.right - work.left - 48.0f) / kDesignWidth);
    scale_ = std::clamp(std::min(dpiScale, fit), 0.60f, 2.0f);
    width_ = gfx::IRound(kDesignWidth * scale_);
    height_ = gfx::IRound(kDesignHeight * scale_);

    int x = work.left + std::max(24, static_cast<int>((work.right - work.left - width_) / 10));
    int y = work.top + ((work.bottom - work.top) - height_) / 2;
    if (settings_->panelX > -100000 && settings_->panelY > -100000 &&
        settings_->panelX != -1 && settings_->panelY != -1) {
        x = settings_->panelX;
        y = settings_->panelY;
        RECT target{x, y, x + width_, y + height_};
        RECT clampWork = gfx::WorkAreaForPoint(
            POINT{target.left + width_ / 2, target.top + height_ / 2});
        if (target.right > clampWork.right) x -= target.right - clampWork.right;
        if (target.bottom > clampWork.bottom) y -= target.bottom - clampWork.bottom;
        if (x < clampWork.left) x = clampWork.left;
        if (y < clampWork.top) y = clampWork.top;
    }

    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_APPWINDOW | WS_EX_TOPMOST, kPanelClass,
                            L"AURA · 声场", WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU, x, y,
                            width_, height_, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    UINT actualDpi = GetDpiForWindow(hwnd_);
    if (!actualDpi) actualDpi = dpi_;
    if (actualDpi != dpi_) {
        dpi_ = actualDpi;
        dpiScale = dpi_ / 96.0f;
        RECT actualWork = gfx::WorkAreaForPoint(POINT{x + width_ / 2, y + height_ / 2});
        fit = std::min((actualWork.bottom - actualWork.top - 48.0f) / kDesignHeight,
                       (actualWork.right - actualWork.left - 48.0f) / kDesignWidth);
        scale_ = std::clamp(std::min(dpiScale, fit), 0.60f, 2.0f);
        int newWidth = gfx::IRound(kDesignWidth * scale_);
        int newHeight = gfx::IRound(kDesignHeight * scale_);
        int centerX = x + width_ / 2;
        int centerY = y + height_ / 2;
        width_ = newWidth;
        height_ = newHeight;
        x = centerX - width_ / 2;
        y = centerY - height_ / 2;
        actualWork = gfx::WorkAreaForPoint(POINT{x + width_ / 2, y + height_ / 2});
        x = ClampIntValue(x, actualWork.left, std::max(actualWork.left, actualWork.right - width_));
        y = ClampIntValue(y, actualWork.top, std::max(actualWork.top, actualWork.bottom - height_));
        SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width_, height_,
                     SWP_NOACTIVATE);
    }

    settings_->panelX = x;
    settings_->panelY = y;
    return true;
}

bool ControlPanel::InitGraphics() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory),
                                   nullptr,
                                   reinterpret_cast<void**>(d2dFactory_.GetAddressOf()));
    if (FAILED(hr)) {
        lastError_ = L"Direct2D 初始化失败";
        return false;
    }
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                             reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf()));
    if (FAILED(hr)) {
        lastError_ = L"文字引擎初始化失败";
        return false;
    }
    D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f);
    d2dFactory_->CreateStrokeStyle(props, nullptr, 0, &strokeStyle_);
    valid_ = true;
    RecreateSurface();
    return renderTarget_ != nullptr;
}

void ControlPanel::ReleaseGraphics() {
    brush_.Reset();
    renderTarget_.Reset();
    writeFactory_.Reset();
    d2dFactory_.Reset();
    strokeStyle_.Reset();
    if (memoryDc_) {
        if (oldBitmap_) SelectObject(memoryDc_, oldBitmap_);
        memoryDc_ = nullptr;
    }
    if (bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
    }
    bitmapBits_ = nullptr;
    oldBitmap_ = nullptr;
}

void ControlPanel::RecreateSurface() {
    if (!hwnd_ || !d2dFactory_) return;
    brush_.Reset();
    renderTarget_.Reset();
    if (memoryDc_) {
        if (oldBitmap_) SelectObject(memoryDc_, oldBitmap_);
        DeleteDC(memoryDc_);
        memoryDc_ = nullptr;
    }
    oldBitmap_ = nullptr;
    if (bitmap_) {
        DeleteObject(bitmap_);
        bitmap_ = nullptr;
        bitmapBits_ = nullptr;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width_;
    info.bmiHeader.biHeight = -height_;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap_ = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bitmapBits_, nullptr, 0);
    if (!bitmap_) return;
    memoryDc_ = CreateCompatibleDC(nullptr);
    if (!memoryDc_) return;
    oldBitmap_ = SelectObject(memoryDc_, bitmap_);

    D2D1_RENDER_TARGET_PROPERTIES targetProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
        96.0f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
    if (FAILED(d2dFactory_->CreateDCRenderTarget(&targetProps, &renderTarget_))) return;
    RECT rect{0, 0, width_, height_};
    renderTarget_->BindDC(memoryDc_, &rect);
    renderTarget_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush_);
    dirty_ = true;
}

void ControlPanel::Show() {
    if (!hwnd_) return;
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(hwnd_);
    dirty_ = true;
}

void ControlPanel::ReassertTopmost() {
    if (!hwnd_ || !valid_ || !IsWindowVisible(hwnd_)) return;
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void ControlPanel::Tick() {
    if (!valid_ || !hwnd_) return;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    bool timeForStatus = true;
    if (frequency.QuadPart > 0) {
        double seconds = static_cast<double>(now.QuadPart - lastPanelTick_.QuadPart) /
                         static_cast<double>(frequency.QuadPart);
        timeForStatus = seconds >= 0.18;
    }
    if (timeForStatus) {
        lastPanelTick_ = now;
        RefreshStatus();
    }
    if (!dirty_ || IsIconic(hwnd_)) return;
    Render();
}

void ControlPanel::RefreshStatus() {
    if (!settings_ || !audio_) return;
    std::wstring signature = SourceTitle();
    signature += L"\n";
    signature += StatusSubText();
    signature += L"\n";
    signature += std::to_wstring(static_cast<int>(audio_->State()));
    signature += L"\n";
    signature += std::to_wstring(audio_->SampleRate());
    if (signature != lastStatus_) {
        lastStatus_ = signature;
        dirty_ = true;
    }
}

void ControlPanel::Render() {
    if (!renderTarget_ || !writeFactory_ || !settings_) return;
    if (IsIconic(hwnd_)) return;

    hits_.clear();
    Theme theme = BuildTheme();
    renderTarget_->BeginDraw();
    renderTarget_->SetTransform(D2D1::Matrix3x2F::Scale(scale_, scale_));
    renderTarget_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    renderTarget_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    renderTarget_->Clear(D2D1::ColorF(0, 0.0f));
    RenderBackground(theme);
    if (page_ == Page::Source) {
        RenderSourcePage(theme);
    } else {
        RenderHeader(theme);
        RenderSourceCard(theme);
        RenderPinCard(theme);
        RenderTabs(theme);
        if (page_ == Page::Appearance) {
            RenderAppearancePage(theme);
        } else if (page_ == Page::Color) {
            RenderColorPage(theme);
        } else if (page_ == Page::Shards) {
            RenderShardsPage(theme);
            RenderFooter(theme, 3);
        } else if (page_ == Page::Glow) {
            RenderGlowPage(theme);
            RenderFooter(theme, 4);
        } else {
            int effect = CurrentEffect();
            RenderEffectPage(theme, effect);
            RenderFooter(theme, effect);
        }
    }
    StrokeRound(renderTarget_.Get(), brush_.Get(), theme.border,
                RectF(0.5f, 0.5f, kDesignWidth - 1.0f, kDesignHeight - 1.0f), 26.0f, 1.0f);

    HRESULT hr = renderTarget_->EndDraw();
    if (FAILED(hr)) {
        lastError_ = L"面板绘制失败";
    }
    POINT destination{};
    RECT windowRect{};
    GetWindowRect(hwnd_, &windowRect);
    destination.x = windowRect.left;
    destination.y = windowRect.top;
    SIZE size{width_, height_};
    POINT source{0, 0};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    HDC screen = GetDC(nullptr);
    BOOL ok = UpdateLayeredWindow(hwnd_, screen, &destination, &size, memoryDc_, &source, 0,
                                  &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    if (!ok) lastError_ = L"面板合成失败";
    dirty_ = false;
}

void ControlPanel::Invalidate() {
    dirty_ = true;
}

void ControlPanel::AddHit(HitKind kind, int index, int field, D2D1_RECT_F rect) {
    Hit hit;
    hit.kind = kind;
    hit.index = index;
    hit.field = field;
    hit.rect = rect;
    hits_.push_back(hit);
}

const ControlPanel::Hit* ControlPanel::HitTest(POINT client) const {
    for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {
        if (Contains(it->rect, client)) return &*it;
    }
    return nullptr;
}

bool ControlPanel::IsHovered(HitKind kind, int index, int field) const {
    return hover_.kind == kind && (index < 0 || hover_.index == index) &&
           (field < 0 || hover_.field == field);
}

void ControlPanel::SetHover(const Hit& hit) {
    if (hover_.kind == hit.kind && hover_.index == hit.index && hover_.field == hit.field) return;
    hover_ = hit;
    dirty_ = true;
}

void ControlPanel::MarkDirty() {
    dirty_ = true;
}

void ControlPanel::NotifyChanged(bool) {
    if (onChanged_) onChanged_();
    dirty_ = true;
}

void ControlPanel::SaveWindowPosition() {
    if (!hwnd_ || !settings_) return;
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    settings_->panelX = rect.left;
    settings_->panelY = rect.top;
    if (onChanged_) onChanged_();
}

void ControlPanel::ComputeWindowScale() {
    if (!hwnd_) return;
    dpi_ = GetDpiForWindow(hwnd_);
    if (!dpi_) dpi_ = 96;
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    RECT work = gfx::WorkAreaForPoint(
        POINT{rect.left + (rect.right - rect.left) / 2, rect.top + (rect.bottom - rect.top) / 2});
    float dpiScale = dpi_ / 96.0f;
    float fit = std::min((work.bottom - work.top - 48.0f) / kDesignHeight,
                         (work.right - work.left - 48.0f) / kDesignWidth);
    scale_ = std::clamp(std::min(dpiScale, fit), 0.60f, 2.0f);
    width_ = gfx::IRound(kDesignWidth * scale_);
    height_ = gfx::IRound(kDesignHeight * scale_);
}

void ControlPanel::ApplyWindowPosition() {
    if (!hwnd_) return;
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    RECT work = gfx::WorkAreaForPoint(
        POINT{rect.left + (rect.right - rect.left) / 2, rect.top + (rect.bottom - rect.top) / 2});
    int x = ClampIntValue(rect.left, work.left, std::max(work.left, work.right - width_));
    int y = ClampIntValue(rect.top, work.top, std::max(work.top, work.bottom - height_));
    SetWindowPos(hwnd_, nullptr, x, y, width_, height_, SWP_NOZORDER | SWP_NOACTIVATE);
}

ControlPanel::Theme ControlPanel::BuildTheme() const {
    Theme theme;
    theme.background = gfx::Rgb(settings_->panelR, settings_->panelG, settings_->panelB);
    theme.background.a = std::clamp(
        static_cast<int>(std::lround(settings_->panelOpacity * 255.0f / 100.0f)), 1, 255);
    theme.light = gfx::IsLight(theme.background);
    if (theme.light) {
        theme.text = gfx::Rgb(20, 32, 33);
        theme.muted = gfx::Rgb(92, 111, 108);
        theme.faint = gfx::Rgb(125, 143, 139);
        theme.surface = gfx::Rgb(14, 38, 34, 15);
        theme.surfaceHover = gfx::Rgb(14, 38, 34, 28);
        theme.border = gfx::Rgb(14, 38, 34, 28);
        theme.accent = gfx::Rgb(10, 139, 116);
        theme.accentSoft = gfx::Rgb(10, 139, 116, 30);
        theme.accentText = gfx::Rgb(250, 255, 253);
        theme.danger = gfx::Rgb(198, 48, 48);
        theme.success = gfx::Rgb(0, 145, 103);
    } else {
        theme.text = gfx::Rgb(235, 250, 247);
        theme.muted = gfx::Rgb(153, 177, 171);
        theme.faint = gfx::Rgb(115, 139, 134);
        theme.surface = gfx::Rgb(255, 255, 255, 15);
        theme.surfaceHover = gfx::Rgb(255, 255, 255, 28);
        theme.border = gfx::Rgb(255, 255, 255, 24);
        theme.accent = gfx::Rgb(98, 237, 208);
        theme.accentSoft = gfx::Rgb(98, 237, 208, 28);
        theme.accentText = gfx::Rgb(5, 35, 29);
        theme.danger = gfx::Rgb(255, 124, 114);
        theme.success = gfx::Rgb(101, 236, 177);
    }
    return theme;
}

std::wstring ControlPanel::SourceTitle() const {
    if (!settings_) return L"未选择音源";
    if (settings_->sourceKind == SourceKind::None) return L"未选择音源";
    if (settings_->sourceKind == SourceKind::System) {
        return settings_->sourceName.empty() ? L"系统全部音频" : settings_->sourceName;
    }
    if (!settings_->sourceName.empty()) return settings_->sourceName;
    if (audio_) {
        AudioSource current = audio_->Current();
        if (!current.name.empty()) return current.name;
    }
    return L"应用程序音频";
}

std::wstring ControlPanel::StatusSubText() const {
    if (!settings_ || !audio_) return L"";
    if (settings_->sourceKind == SourceKind::None) return L"点击右侧按钮选择需要可视化的应用";
    CaptureState state = audio_->State();
    if (state == CaptureState::Error) {
        std::wstring error = Flatten(audio_->Error());
        if (error.empty()) error = L"音频捕获失败，请更换音源";
        return L"捕获失败 · " + error;
    }
    if (state == CaptureState::Connecting) return L"正在连接音频通道…";
    if (state == CaptureState::Idle) return L"音源已停止 · 点击更换音频来源";

    std::wstring detail = settings_->sourceKind == SourceKind::System
                              ? L"系统混音"
                              : L"应用音频";
    if (settings_->sourceKind == SourceKind::Application && !settings_->sourcePath.empty()) {
        std::wstring exe = BaseName(settings_->sourcePath);
        if (!exe.empty()) detail = exe;
    }
    std::wstring rate = SampleRateText(audio_->SampleRate());
    if (!rate.empty()) detail += L" · " + rate;
    detail += L" · 实时监听";
    return detail;
}

std::wstring ControlPanel::TruncateToWidth(const std::wstring& text, float maxWidth, float size,
                                           DWRITE_FONT_WEIGHT weight) const {
    return Ellipsize(writeFactory_.Get(), text, size, weight, maxWidth);
}

bool ControlPanel::IsCurrentSource(const AudioApp& app) const {
    if (!settings_ || settings_->sourceKind != SourceKind::Application) return false;
    if (!settings_->sourcePath.empty() && !app.path.empty() &&
        _wcsicmp(settings_->sourcePath.c_str(), app.path.c_str()) == 0)
        return true;
    if (audio_) {
        AudioSource current = audio_->Current();
        if (current.kind == SourceKind::Application && current.pid == app.pid) return true;
    }
    if (!settings_->sourceName.empty() &&
        _wcsicmp(settings_->sourceName.c_str(), app.name.c_str()) == 0)
        return true;
    return false;
}

int ControlPanel::CurrentEffect() const {
    if (page_ == Page::Outer) return 0;
    if (page_ == Page::Dots) return 1;
    if (page_ == Page::Wave) return 2;
    return 0;
}

ControlPanel::FieldRef ControlPanel::EffectField(int effect, int field) const {
    return FieldRef{effect, field};
}

EffectConfig* ControlPanel::ConfigForGroup(int group) {
    if (!settings_) return nullptr;
    if (group == 0) return &settings_->outer;
    if (group == 1) return &settings_->dots;
    if (group == 2) return &settings_->wave;
    return nullptr;
}

const EffectConfig* ControlPanel::ConfigForGroup(int group) const {
    return const_cast<ControlPanel*>(this)->ConfigForGroup(group);
}

int ControlPanel::GetFieldValue(const FieldRef& ref) const {
    if (!settings_) return 0;
    if (ref.group == 3) {
        if (ref.field == 0) return settings_->panelR;
        if (ref.field == 1) return settings_->panelG;
        if (ref.field == 2) return settings_->panelB;
        if (ref.field == 4) return settings_->bloomRange;
        if (ref.field == 5) return settings_->bloomBrightness;
        if (ref.field == 6) return settings_->overlayScale;
        return settings_->panelOpacity;
    }
    if (ref.group == 4) {
        if (ref.field == 0) return settings_->shards.r;
        if (ref.field == 1) return settings_->shards.g;
        if (ref.field == 2) return settings_->shards.b;
        if (ref.field == 3) return settings_->shards.size;
        if (ref.field == 4) return settings_->shards.frequency;
        return settings_->shards.opacity;
    }
    if (ref.group == 5) {
        if (ref.field == 0) return settings_->glow.r;
        if (ref.field == 1) return settings_->glow.g;
        if (ref.field == 2) return settings_->glow.b;
        if (ref.field == 3) return settings_->glow.size;
        return settings_->glow.opacity;
    }
    if (ref.group == 6) {
        const DynamicColorSettings& c = settings_->colors;
        switch (ref.field) {
            case 0: return c.primaryR;
            case 1: return c.primaryG;
            case 2: return c.primaryB;
            case 3: return c.secondaryR;
            case 4: return c.secondaryG;
            case 5: return c.secondaryB;
            case 6: return c.accentR;
            case 7: return c.accentG;
            case 8: return c.accentB;
            case 9: return c.glowR;
            case 10: return c.glowG;
            case 11: return c.glowB;
            default: return 0;
        }
    }
    if (ref.group == 7) {
        const DynamicColorSettings& c = settings_->colors;
        if (ref.field == 0) return c.intensity;
        if (ref.field == 1) return c.flowSpeed;
        if (ref.field == 2) return c.transitionMs;
        return 0;
    }
    const EffectConfig* config = ConfigForGroup(ref.group);
    if (!config) return 0;
    switch (ref.field) {
        case 0: return config->r;
        case 1: return config->g;
        case 2: return config->b;
        case 3: return config->size;
        case 4: return config->amplitude;
        case 5: return config->density;
        case 6: return config->opacity;
        case 7: return config->thickness;
        default: return 0;
    }
}

int ControlPanel::MinValue(const FieldRef& ref) const {
    if (ref.group == 6 || ref.group == 7) return 0;
    if (ref.field <= 2) return 0;
    if (ref.group == 3) {
        if (ref.field == 4) return 10;
        if (ref.field == 5) return 0;
        if (ref.field == 6) return 40;
        return 1;
    }
    if (ref.group == 4) {
        if (ref.field == 3) return 20;
        if (ref.field == 4) return 0;
        return 0;
    }
    if (ref.group == 5) {
        if (ref.field == 3) return 20;
        return 1;
    }
    if (ref.group < 0 || ref.group > 2) return 0;
    if (ref.field == 3) return kEffectSpecs[ref.group].sizeMin;
    if (ref.field == 4) return 0;
    if (ref.field == 5) return kEffectSpecs[ref.group].densityMin;
    if (ref.field == 7) return 40;
    return 1;
}

int ControlPanel::MaxValue(const FieldRef& ref) const {
    if (ref.group == 6) return 255;
    if (ref.group == 7) {
        if (ref.field == 2) return 2000;
        return 200;
    }
    if (ref.field <= 2) return 255;
    if (ref.group == 3) {
        if (ref.field == 4 || ref.field == 5 || ref.field == 6) return 200;
        return 100;
    }
    if (ref.group == 4) {
        if (ref.field == 3) return 200;
        if (ref.field == 4) return 200;
        return 100;
    }
    if (ref.group == 5) {
        if (ref.field == 3) return 200;
        return 100;
    }
    if (ref.group < 0 || ref.group > 2) return 100;
    if (ref.field == 3) return kEffectSpecs[ref.group].sizeMax;
    if (ref.field == 4) return 200;
    if (ref.field == 5) return kEffectSpecs[ref.group].densityMax;
    if (ref.field == 7) return 300;
    return 100;
}

bool ControlPanel::IsPercentField(const FieldRef& ref) const {
    if (ref.group == 3) return ref.field >= 3;
    if (ref.group == 4) return ref.field >= 3;
    if (ref.group == 5) return ref.field >= 3;
    if (ref.group == 6) return false;
    if (ref.group == 7) return ref.field == 0 || ref.field == 1;
    return ref.field == 3 || ref.field == 4 || ref.field == 6 || ref.field == 7;
}

void ControlPanel::SetFieldValue(const FieldRef& ref, int value) {
    if (!settings_) return;
    value = ClampIntValue(value, MinValue(ref), MaxValue(ref));
    if (ref.group == 3) {
        if (ref.field == 0) settings_->panelR = value;
        else if (ref.field == 1) settings_->panelG = value;
        else if (ref.field == 2) settings_->panelB = value;
        else if (ref.field == 3) settings_->panelOpacity = value;
        else if (ref.field == 4) settings_->bloomRange = value;
        else if (ref.field == 5) settings_->bloomBrightness = value;
        else if (ref.field == 6) settings_->overlayScale = value;
        return;
    }
    if (ref.group == 4) {
        if (ref.field == 0) settings_->shards.r = value;
        else if (ref.field == 1) settings_->shards.g = value;
        else if (ref.field == 2) settings_->shards.b = value;
        else if (ref.field == 3) settings_->shards.size = value;
        else if (ref.field == 4) settings_->shards.frequency = value;
        else if (ref.field == 5) settings_->shards.opacity = value;
        return;
    }
    if (ref.group == 5) {
        if (ref.field == 0) settings_->glow.r = value;
        else if (ref.field == 1) settings_->glow.g = value;
        else if (ref.field == 2) settings_->glow.b = value;
        else if (ref.field == 3) settings_->glow.size = value;
        else if (ref.field == 4) settings_->glow.opacity = value;
        return;
    }
    if (ref.group == 6) {
        DynamicColorSettings& c = settings_->colors;
        switch (ref.field) {
            case 0: c.primaryR = value; break;
            case 1: c.primaryG = value; break;
            case 2: c.primaryB = value; break;
            case 3: c.secondaryR = value; break;
            case 4: c.secondaryG = value; break;
            case 5: c.secondaryB = value; break;
            case 6: c.accentR = value; break;
            case 7: c.accentG = value; break;
            case 8: c.accentB = value; break;
            case 9: c.glowR = value; break;
            case 10: c.glowG = value; break;
            case 11: c.glowB = value; break;
            default: break;
        }
        c.preset = -1;
        return;
    }
    if (ref.group == 7) {
        DynamicColorSettings& c = settings_->colors;
        if (ref.field == 0) c.intensity = value;
        else if (ref.field == 1) c.flowSpeed = value;
        else if (ref.field == 2) c.transitionMs = value;
        return;
    }
    EffectConfig* config = ConfigForGroup(ref.group);
    if (!config) return;
    switch (ref.field) {
        case 0: config->r = value; break;
        case 1: config->g = value; break;
        case 2: config->b = value; break;
        case 3: config->size = value; break;
        case 4: config->amplitude = value; break;
        case 5: config->density = value; break;
        case 6: config->opacity = value; break;
        case 7: config->thickness = value; break;
        default: break;
    }
}

std::wstring ControlPanel::FormatFieldValue(const FieldRef& ref) const {
    int value = GetFieldValue(ref);
    if (IsPercentField(ref)) return gfx::FormatPercent(value);
    return gfx::FormatInt(value);
}

std::wstring ControlPanel::FormatFieldValueForEdit(const FieldRef& ref) const {
    return gfx::FormatInt(GetFieldValue(ref));
}

void ControlPanel::BeginEdit(const FieldRef& ref) {
    if (editActive_ && editField_ != ref) CommitEdit();
    SetFocus(hwnd_);
    editActive_ = true;
    editField_ = ref;
    editText_ = FormatFieldValueForEdit(ref);
    editInitialValue_ = GetFieldValue(ref);
    editReplaceOnType_ = true;
    dirty_ = true;
}

bool ControlPanel::IsEditing(const FieldRef& ref) const {
    return editActive_ && editField_ == ref;
}

void ControlPanel::ApplyEditText() {
    if (editText_.empty()) return;
    wchar_t* end = nullptr;
    long parsed = wcstol(editText_.c_str(), &end, 10);
    if (end == editText_.c_str()) return;
    SetFieldValue(editField_, static_cast<int>(parsed));
    NotifyChanged(false);
}

void ControlPanel::CommitEdit() {
    if (!editActive_) return;
    if (editText_.empty()) {
        SetFieldValue(editField_, editInitialValue_);
    } else {
        ApplyEditText();
        editText_ = FormatFieldValueForEdit(editField_);
    }
    editActive_ = false;
    editText_.clear();
    NotifyChanged(true);
}

void ControlPanel::CancelEdit() {
    if (!editActive_) return;
    SetFieldValue(editField_, editInitialValue_);
    editActive_ = false;
    editText_.clear();
    NotifyChanged(true);
}

void ControlPanel::HandleKeyDown(WPARAM key) {
    if (!editActive_) {
        if (key == VK_ESCAPE && page_ == Page::Source) {
            CloseSourcePage();
            dirty_ = true;
        }
        return;
    }
    if (key == VK_RETURN) {
        CommitEdit();
    } else if (key == VK_ESCAPE) {
        CancelEdit();
    } else if (key == VK_UP || key == VK_DOWN) {
        int next = GetFieldValue(editField_) + (key == VK_UP ? 1 : -1);
        SetFieldValue(editField_, next);
        editText_ = FormatFieldValueForEdit(editField_);
        editReplaceOnType_ = false;
        NotifyChanged(false);
    } else if (key == VK_TAB) {
        CommitEdit();
    }
}

void ControlPanel::HandleChar(wchar_t character) {
    if (!editActive_) return;
    if (character == L'\r' || character == L'\n') {
        CommitEdit();
        return;
    }
    if (character == L'\b') {
        if (editReplaceOnType_) {
            editText_.clear();
            editReplaceOnType_ = false;
        } else if (!editText_.empty()) {
            editText_.pop_back();
        }
        if (!editText_.empty()) ApplyEditText();
        dirty_ = true;
        return;
    }
    if (character < L'0' || character > L'9') return;
    if (editReplaceOnType_) {
        editText_.clear();
        editReplaceOnType_ = false;
    }
    if (editText_.size() >= 4) return;
    editText_.push_back(character);
    ApplyEditText();
    dirty_ = true;
}

void ControlPanel::HandleMouseDown(POINT client, bool) {
    if (!valid_) return;
    SetFocus(hwnd_);
    Hit hitCopy;
    const Hit* hit = HitTest(client);
    if (hit) hitCopy = *hit;

    if (editActive_) {
        bool sameField = hit && (hit->kind == HitKind::ColorField || hit->kind == HitKind::Value) &&
                         hit->index == editField_.group && hit->field == editField_.field;
        if (!sameField) CommitEdit();
    }
    if (!hit) {
        dirty_ = true;
        return;
    }

    switch (hitCopy.kind) {
        case HitKind::Header: {
            POINT screen{};
            GetCursorPos(&screen);
            StartWindowDrag(screen);
            break;
        }
        case HitKind::Close:
            if (onExit_) onExit_();
            else SendMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        case HitKind::Minimize:
            ShowWindow(hwnd_, SW_MINIMIZE);
            break;
        case HitKind::Source:
            OpenSourcePage();
            break;
        case HitKind::Pin:
            TogglePin();
            break;
        case HitKind::Tab:
            SelectTab(hitCopy.index);
            break;
        case HitKind::Toggle:
            ToggleEffect(hitCopy.index);
            break;
        case HitKind::ColorField:
            BeginEdit(FieldRef{hitCopy.index, hitCopy.field});
            break;
        case HitKind::Slider: {
            sliderField_ = FieldRef{hitCopy.index, hitCopy.field};
            activeSliderRect_ = hitCopy.rect;
            activeSliderRect_.left += 4;
            activeSliderRect_.right -= 4;
            sliderDragging_ = true;
            SetCapture(hwnd_);
            UpdateSlider(client);
            break;
        }
        case HitKind::Value:
            BeginEdit(FieldRef{hitCopy.index, hitCopy.field});
            break;
        case HitKind::PickerBack:
        case HitKind::PickerClose:
            CloseSourcePage();
            break;
        case HitKind::PickerRefresh:
            RefreshApps();
            break;
        case HitKind::PickerSystem:
            SelectSystem();
            break;
        case HitKind::PickerApp:
            SelectApp(hitCopy.index);
            break;
        case HitKind::Preset:
            ApplyPreset(hitCopy.index);
            break;
        case HitKind::ShardTemplate:
            if (hitCopy.index >= 0 && hitCopy.index < 5) {
                settings_->shards.shape = hitCopy.index;
                NotifyChanged(true);
            }
            break;
        case HitKind::Recenter:
            if (onRecenter_) onRecenter_();
            break;
        case HitKind::Reset:
            ResetVisuals();
            break;
        case HitKind::ColorEnable:
            ToggleDynamicColors();
            break;
        case HitKind::ColorPreset:
            ApplyColorPreset(hitCopy.index);
            break;
        case HitKind::ColorBehavior:
            if (hitCopy.index == 0) {
                settings_->colors.spectrumGradient = !settings_->colors.spectrumGradient;
            } else if (hitCopy.index == 1) {
                settings_->colors.bassFlash = !settings_->colors.bassFlash;
            } else if (hitCopy.index == 2) {
                settings_->colors.timeFlow = !settings_->colors.timeFlow;
            }
            NotifyChanged(true);
            break;
        default:
            break;
    }
    dirty_ = true;
}

void ControlPanel::HandleMouseUp() {
    if (sliderDragging_) EndSlider();
    if (windowDragging_) EndWindowDrag();
}

void ControlPanel::HandleMouseMove(POINT client) {
    if (windowDragging_) {
        POINT screen{};
        GetCursorPos(&screen);
        DragWindow(screen);
        return;
    }
    if (sliderDragging_) {
        UpdateSlider(client);
        return;
    }
    const Hit* hit = HitTest(client);
    Hit value;
    if (hit) value = *hit;
    SetHover(value);
    TRACKMOUSEEVENT track{};
    track.cbSize = sizeof(track);
    track.dwFlags = TME_LEAVE;
    track.hwndTrack = hwnd_;
    TrackMouseEvent(&track);
}

void ControlPanel::HandleMouseLeave() {
    if (!windowDragging_ && !sliderDragging_) {
        Hit none;
        SetHover(none);
    }
}

void ControlPanel::HandleMouseWheel(POINT client, int delta) {
    if (!delta) return;
    if (editActive_) CommitEdit();
    if (page_ == Page::Source) {
        if (client.x >= 20 && client.x <= 540 && client.y >= kListTop &&
            client.y <= kListTop + kVisibleRows * (kRowHeight + kRowGap)) {
            int direction = delta > 0 ? -1 : 1;
            int maxScroll = std::max(0, static_cast<int>(apps_.size()) - kVisibleRows);
            int next = std::clamp(appScroll_ + direction, 0, maxScroll);
            if (next != appScroll_) {
                appScroll_ = next;
                dirty_ = true;
            }
        }
        return;
    }
    const Hit* hit = HitTest(client);
    if (!hit) return;
    FieldRef ref{};
    bool found = false;
    if (hit->kind == HitKind::Slider) {
        ref = FieldRef{hit->index, hit->field};
        found = true;
    } else if (hit->kind == HitKind::Value || hit->kind == HitKind::ColorField) {
        ref = FieldRef{hit->index, hit->field};
        found = true;
    }
    if (!found) return;
    int step = delta > 0 ? 1 : -1;
    SetFieldValue(ref, GetFieldValue(ref) + step);
    editText_ = FormatFieldValueForEdit(ref);
    NotifyChanged(false);
}

void ControlPanel::StartWindowDrag(POINT screen) {
    if (windowDragging_) return;
    windowDragging_ = true;
    dragStartCursor_ = screen;
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    dragStartWindow_ = POINT{rect.left, rect.top};
    SetCapture(hwnd_);
}

void ControlPanel::DragWindow(POINT screen) {
    if (!windowDragging_) return;
    int x = dragStartWindow_.x + (screen.x - dragStartCursor_.x);
    int y = dragStartWindow_.y + (screen.y - dragStartCursor_.y);
    RECT work = gfx::WorkAreaForPoint(screen);
    int maxX = std::max(static_cast<int>(work.left), static_cast<int>(work.right) - width_);
    int maxY = std::max(static_cast<int>(work.top), static_cast<int>(work.bottom) - height_);
    x = ClampIntValue(x, work.left, maxX);
    y = ClampIntValue(y, work.top, maxY);
    SetWindowPos(hwnd_, nullptr, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void ControlPanel::EndWindowDrag() {
    windowDragging_ = false;
    if (GetCapture() == hwnd_) ReleaseCapture();
    SaveWindowPosition();
}

void ControlPanel::UpdateSlider(POINT client) {
    if (!sliderDragging_) return;
    float x = static_cast<float>(client.x);
    float left = activeSliderRect_.left;
    float right = activeSliderRect_.right;
    float progress = right > left ? (x - left) / (right - left) : 0.0f;
    progress = std::clamp(progress, 0.0f, 1.0f);
    int low = MinValue(sliderField_);
    int high = MaxValue(sliderField_);
    int value = low + static_cast<int>(std::lround(progress * (high - low)));
    SetFieldValue(sliderField_, value);
    NotifyChanged(false);
}

void ControlPanel::EndSlider() {
    sliderDragging_ = false;
    if (GetCapture() == hwnd_) ReleaseCapture();
    NotifyChanged(true);
}

void ControlPanel::OpenSourcePage() {
    if (editActive_) CommitEdit();
    lastMainPage_ = (page_ == Page::Source) ? lastMainPage_ : page_;
    page_ = Page::Source;
    hover_ = Hit{};
    appScroll_ = 0;
    RefreshApps();
    dirty_ = true;
}

void ControlPanel::CloseSourcePage() {
    page_ = lastMainPage_;
    hover_ = Hit{};
    dirty_ = true;
}

void ControlPanel::RefreshApps() {
    apps_ = AudioEngine::EnumerateApps();
    int selected = -1;
    for (int i = 0; i < static_cast<int>(apps_.size()); ++i) {
        if (IsCurrentSource(apps_[i])) {
            selected = i;
            break;
        }
    }
    if (selected >= 0) appScroll_ = std::max(0, selected - kVisibleRows + 2);
    int maxScroll = std::max(0, static_cast<int>(apps_.size()) - kVisibleRows);
    appScroll_ = std::clamp(appScroll_, 0, maxScroll);
    dirty_ = true;
}

void ControlPanel::SelectApp(int index) {
    if (index < 0 || index >= static_cast<int>(apps_.size())) return;
    AudioApp app = apps_[index];
    settings_->sourceKind = SourceKind::Application;
    settings_->sourceName = app.name.empty() ? app.exe : app.name;
    settings_->sourcePath = app.path;
    AudioSource source;
    source.kind = SourceKind::Application;
    source.pid = app.pid;
    source.path = app.path;
    source.name = settings_->sourceName;
    if (audio_) audio_->Start(source);
    page_ = lastMainPage_;
    hover_ = Hit{};
    editActive_ = false;
    NotifyChanged(true);
}

void ControlPanel::SelectSystem() {
    settings_->sourceKind = SourceKind::System;
    settings_->sourceName = L"系统全部音频";
    settings_->sourcePath.clear();
    AudioSource source;
    source.kind = SourceKind::System;
    source.name = settings_->sourceName;
    if (audio_) audio_->Start(source);
    page_ = lastMainPage_;
    hover_ = Hit{};
    editActive_ = false;
    NotifyChanged(true);
}

void ControlPanel::SelectTab(int index) {
    if (index < 0 || index > 6) return;
    page_ = static_cast<Page>(index);
    lastMainPage_ = page_;
    hover_ = Hit{};
    dirty_ = true;
}

void ControlPanel::ToggleEffect(int index) {
    if (!settings_ || index < 0 || index > 4) return;
    if (index == 3) {
        settings_->shards.enabled = !settings_->shards.enabled;
        NotifyChanged(true);
        return;
    }
    if (index == 4) {
        settings_->glow.enabled = !settings_->glow.enabled;
        NotifyChanged(true);
        return;
    }
    EffectConfig* config = ConfigForGroup(index);
    if (!config) return;
    config->enabled = !config->enabled;
    NotifyChanged(true);
}

void ControlPanel::TogglePin() {
    if (!settings_) return;
    settings_->overlayFixed = !settings_->overlayFixed;
    NotifyChanged(true);
}

void ControlPanel::ToggleDynamicColors() {
    if (!settings_) return;
    settings_->colors.enabled = !settings_->colors.enabled;
    NotifyChanged(true);
}

void ControlPanel::ApplyColorPreset(int index) {
    if (!settings_ || index < 0 || index >= 5) return;
    const ColorPreset& preset = kColorPresets[index];
    DynamicColorSettings& c = settings_->colors;
    c.primaryR = preset.primaryR;
    c.primaryG = preset.primaryG;
    c.primaryB = preset.primaryB;
    c.secondaryR = preset.secondaryR;
    c.secondaryG = preset.secondaryG;
    c.secondaryB = preset.secondaryB;
    c.accentR = preset.accentR;
    c.accentG = preset.accentG;
    c.accentB = preset.accentB;
    c.glowR = preset.glowR;
    c.glowG = preset.glowG;
    c.glowB = preset.glowB;
    c.preset = index;
    NotifyChanged(true);
}

void ControlPanel::ApplyPreset(int index) {
    struct Preset {
        int r, g, b;
    };
    static const Preset presets[] = {
        {12, 20, 24},
        {22, 28, 44},
        {238, 244, 241},
        {32, 33, 36},
    };
    if (index < 0 || index >= 4) return;
    settings_->panelR = presets[index].r;
    settings_->panelG = presets[index].g;
    settings_->panelB = presets[index].b;
    NotifyChanged(true);
}

void ControlPanel::ResetVisuals() {
    if (!settings_) return;
    VisualSettings defaults = DefaultSettings();
    settings_->outer = defaults.outer;
    settings_->dots = defaults.dots;
    settings_->wave = defaults.wave;
    settings_->panelR = defaults.panelR;
    settings_->panelG = defaults.panelG;
    settings_->panelB = defaults.panelB;
    settings_->panelOpacity = defaults.panelOpacity;
    settings_->bloomRange = defaults.bloomRange;
    settings_->bloomBrightness = defaults.bloomBrightness;
    settings_->overlayScale = defaults.overlayScale;
    settings_->shards = defaults.shards;
    settings_->glow = defaults.glow;
    settings_->colors = defaults.colors;
    NotifyChanged(true);
}

LRESULT CALLBACK ControlPanel::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam,
                                              LPARAM lParam) {
    ControlPanel* self = reinterpret_cast<ControlPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<ControlPanel*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = hwnd;
    }
    if (self) return self->WndProc(message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT ControlPanel::WndProc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd_, &ps);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            POINT client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            client.x = static_cast<LONG>(std::lround(client.x / scale_));
            client.y = static_cast<LONG>(std::lround(client.y / scale_));
            HandleMouseMove(client);
            return 0;
        }
        case WM_MOUSELEAVE:
            HandleMouseLeave();
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            POINT client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            client.x = static_cast<LONG>(std::lround(client.x / scale_));
            client.y = static_cast<LONG>(std::lround(client.y / scale_));
            HandleMouseDown(client, message == WM_LBUTTONDBLCLK);
            return 0;
        }
        case WM_LBUTTONUP:
            HandleMouseUp();
            return 0;
        case WM_MOUSEWHEEL: {
            POINT client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ScreenToClient(hwnd_, &client);
            client.x = static_cast<LONG>(std::lround(client.x / scale_));
            client.y = static_cast<LONG>(std::lround(client.y / scale_));
            HandleMouseWheel(client, GET_WHEEL_DELTA_WPARAM(wParam));
            return 0;
        }
        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT) {
                if (windowDragging_) {
                    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32646)));
                    return TRUE;
                }
                if (sliderDragging_) {
                    SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649)));
                    return TRUE;
                }
                POINT point{};
                GetCursorPos(&point);
                ScreenToClient(hwnd_, &point);
                point.x = static_cast<LONG>(std::lround(point.x / scale_));
                point.y = static_cast<LONG>(std::lround(point.y / scale_));
                const Hit* hit = HitTest(point);
                LPCWSTR cursor = MAKEINTRESOURCEW(32512);
                if (hit) {
                    switch (hit->kind) {
                        case HitKind::Header: cursor = MAKEINTRESOURCEW(32646); break;
                        case HitKind::Slider:
                        case HitKind::Close:
                        case HitKind::Minimize:
                        case HitKind::Source:
                        case HitKind::Tab:
                        case HitKind::Toggle:
                        case HitKind::PickerBack:
                        case HitKind::PickerClose:
                        case HitKind::PickerRefresh:
                        case HitKind::PickerSystem:
                        case HitKind::PickerApp:
                        case HitKind::Preset:
                        case HitKind::Recenter:
                        case HitKind::Reset:
                        case HitKind::Pin:
                        case HitKind::ColorEnable:
                        case HitKind::ColorPreset:
                        case HitKind::ColorBehavior:
                            cursor = MAKEINTRESOURCEW(32649);
                            break;
                        case HitKind::ColorField:
                        case HitKind::Value:
                            cursor = MAKEINTRESOURCEW(32513);
                            break;
                        default: break;
                    }
                }
                SetCursor(LoadCursorW(nullptr, cursor));
                return TRUE;
            }
            break;
        }
        case WM_KEYDOWN:
            HandleKeyDown(wParam);
            if (wParam == VK_ESCAPE && !editActive_ && page_ != Page::Source) return 0;
            return 0;
        case WM_CHAR:
            HandleChar(static_cast<wchar_t>(wParam));
            return 0;
        case WM_KILLFOCUS:
            if (editActive_) CommitEdit();
            return 0;
        case WM_CAPTURECHANGED:
            if (windowDragging_) {
                windowDragging_ = false;
                SaveWindowPosition();
            }
            if (sliderDragging_) {
                sliderDragging_ = false;
                NotifyChanged(true);
            }
            return 0;
        case WM_SIZE:
            if (valid_ && wParam != SIZE_MINIMIZED) {
                MarkDirty();
            }
            return 0;
        case WM_SHOWWINDOW:
            if (wParam) MarkDirty();
            return 0;
        case WM_DPICHANGED: {
            POINT cursor{};
            GetCursorPos(&cursor);
            RECT oldRect{};
            GetWindowRect(hwnd_, &oldRect);
            int oldCenterX = oldRect.left + (oldRect.right - oldRect.left) / 2;
            int oldCenterY = oldRect.top + (oldRect.bottom - oldRect.top) / 2;
            dpi_ = LOWORD(wParam);
            if (!dpi_) dpi_ = 96;
            RECT work = gfx::WorkAreaForPoint(POINT{oldCenterX, oldCenterY});
            float dpiScale = dpi_ / 96.0f;
            float fit = std::min((work.bottom - work.top - 48.0f) / kDesignHeight,
                                 (work.right - work.left - 48.0f) / kDesignWidth);
            scale_ = std::clamp(std::min(dpiScale, fit), 0.60f, 2.0f);
            width_ = gfx::IRound(kDesignWidth * scale_);
            height_ = gfx::IRound(kDesignHeight * scale_);
            int x = ClampIntValue(oldCenterX - width_ / 2, work.left,
                                  std::max(work.left, work.right - width_));
            int y = ClampIntValue(oldCenterY - height_ / 2, work.top,
                                  std::max(work.top, work.bottom - height_));
            SetWindowPos(hwnd_, nullptr, x, y, width_, height_,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            RecreateSurface();
            settings_->panelX = x;
            settings_->panelY = y;
            if (onChanged_) onChanged_();
            dirty_ = true;
            return 0;
        }
        case WM_DISPLAYCHANGE:
            ApplyWindowPosition();
            MarkDirty();
            return 0;
        case WM_CLOSE:
            if (onExit_) onExit_();
            else DestroyWindow(hwnd_);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            valid_ = false;
            hwnd_ = nullptr;
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
