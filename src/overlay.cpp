#include "overlay.h"

#include <d2d1effects.h>
#include <d2d1helper.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kOverlayClass[] = L"AuraSoundRingOverlay";
constexpr float kMaxRingDeform = 1.25f;

float BloomSigmaLogical(const VisualSettings& settings) {
    return 2.5f + 0.075f * std::clamp(settings.bloomRange, 10, 200);
}

float OuterReachLogical(const EffectConfig& cfg) {
    const float base = 62.0f + 1.38f * cfg.size;
    const float halfGap = 0.5f * (7.0f + 12.0f + 2.8f + 3.5f + 1.2f);
    const float amp = 5.0f + 0.22f * cfg.amplitude;
    const float halfLine = 0.5f * 1.55f * cfg.thickness / 100.0f;
    return base + halfGap + amp * kMaxRingDeform + 3.0f + halfLine + 3.0f;
}

float DotsReachLogical(const EffectConfig& cfg) {
    const float base = 48.0f + 1.03f * cfg.size;
    const float amp = 4.0f + 0.15f * cfg.amplitude;
    return base + amp * 1.15f + 6.0f;
}

float WaveReachLogical(const EffectConfig& cfg) {
    const float halfWidth = 55.0f + 1.50f * cfg.size;
    const float halfLine = 0.5f * 1.25f * cfg.thickness / 100.0f;
    return halfWidth + halfLine + 6.0f;
}

float ShardStartLogical(const VisualSettings& settings) {
    return OuterReachLogical(settings.outer) * 0.86f;
}

float ShardSizeLogical(int size) {
    return 7.0f + 0.13f * size;
}

float OverlayUserScale(const VisualSettings& settings) {
    return std::clamp(settings.overlayScale, 40, 200) / 100.0f;
}

float CenterGlowRadiusLogical(const VisualSettings& settings) {
    if (!settings.glow.enabled || settings.glow.opacity <= 0) return 0.0f;
    return OuterReachLogical(settings.outer) * 0.95f *
           (std::clamp(settings.glow.size, 20, 200) / 100.0f);
}

std::wstring HResultText(HRESULT hr) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, static_cast<DWORD>(hr), 0, reinterpret_cast<LPWSTR>(&buffer), 0,
                   nullptr);
    std::wstring text;
    if (buffer) {
        text = buffer;
        LocalFree(buffer);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
            text.pop_back();
    }
    wchar_t code[32];
    swprintf_s(code, L" (0x%08X)", static_cast<unsigned>(hr));
    return text + code;
}

bool AnyEffectVisible(const VisualSettings& settings) {
    return (settings.outer.enabled && settings.outer.opacity > 0) ||
           (settings.dots.enabled && settings.dots.opacity > 0) ||
           (settings.wave.enabled && settings.wave.opacity > 0) ||
           (settings.shards.enabled && settings.shards.opacity > 0) ||
           (settings.glow.enabled && settings.glow.opacity > 0);
}
}  // namespace

Overlay::Overlay() {
    QueryPerformanceFrequency(&frequency_);
    QueryPerformanceCounter(&lastTick_);
    audioSnapshot_.resize(AudioAnalyzer::kFftSize, 0.0f);
    shardRngState_ = static_cast<uint32_t>(GetTickCount64()) | 1u;
    shardAngleCursor_ = NextShardRandom() * 6.2831853f;
}

Overlay::~Overlay() {
    Destroy();
}

bool Overlay::Create(HINSTANCE instance, VisualSettings* settings, AudioEngine* audio) {
    instance_ = instance;
    settings_ = settings;
    audio_ = audio;
    if (!CreateWindowInternal(instance)) return false;
    if (!InitGraphics()) {
        if (lastError_.empty()) lastError_ = L"无法初始化浮窗渲染器。";
        return false;
    }
    graphicsReady_ = true;
    RenderOnce();
    RefreshVisibility();
    return true;
}

void Overlay::Destroy() {
    if (dragging_) EndDrag();
    ReleaseGraphics();
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    graphicsReady_ = false;
    visible_ = false;
}

bool Overlay::CreateWindowInternal(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = StaticWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = instance;
    wc.hIcon = nullptr;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = nullptr;
    wc.lpszMenuName = nullptr;
    wc.lpszClassName = kOverlayClass;
    wc.hIconSm = nullptr;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    POINT anchor{0, 0};
    if (settings_ && settings_->overlayX >= 0 && settings_->overlayY >= 0) {
        anchor.x = settings_->overlayX;
        anchor.y = settings_->overlayY;
    } else {
        GetCursorPos(&anchor);
    }
    RECT work = gfx::WorkAreaForPoint(anchor);
    int initialWidth = gfx::IRound(320.0f * GetDpiForSystem() / 96.0f);
    int x = work.left + (work.right - work.left - initialWidth) / 2;
    int y = work.top + (work.bottom - work.top - initialWidth) / 2;

    hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                                WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED,
                            kOverlayClass, L"Aura Sound Ring", WS_POPUP, x, y, initialWidth,
                            initialWidth, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    // Keeping a layered style alongside DirectComposition gives Windows a
    // consistently refreshable hit-test region. It is also what lets
    // WS_EX_TRANSPARENT reliably forward input to the desktop when locked.
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);

    dpi_ = gfx::MonitorDpi(hwnd_);
    int desired = DesiredContentSize(dpi_);
    if (desired <= 0) desired = gfx::IRound(240.0f * dpi_ / 96.0f);
    width_ = desired;
    height_ = desired;
    if (settings_ && settings_->overlayX >= 0 && settings_->overlayY >= 0) {
        x = settings_->overlayX;
        y = settings_->overlayY;
    } else {
        x = work.right - width_ - 48;
        y = work.top + ((work.bottom - work.top) - height_) / 2;
    }
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, width_, height_,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    ClampWindowPosition(false);
    RECT finalRect{};
    GetWindowRect(hwnd_, &finalRect);
    if (settings_) {
        settings_->overlayX = finalRect.left;
        settings_->overlayY = finalRect.top;
    }
    return true;
}

int Overlay::DesiredContentSize(UINT dpi) const {
    if (!settings_) return 0;
    const float scale =
        (dpi ? dpi : 96) / 96.0f * OverlayUserScale(*settings_);
    float content = 0.0f;
    if (settings_->outer.enabled && settings_->outer.opacity > 0)
        content = std::max(content, OuterReachLogical(settings_->outer));
    if (settings_->dots.enabled && settings_->dots.opacity > 0)
        content = std::max(content, DotsReachLogical(settings_->dots));
    if (settings_->wave.enabled && settings_->wave.opacity > 0)
        content = std::max(content, WaveReachLogical(settings_->wave));
    if (settings_->shards.enabled && settings_->shards.opacity > 0) {
        // Anchor the window around the emission ring even when the main ring is
        // hidden; the far, already-faded part of each shard may clip.
        content = std::max(content, ShardStartLogical(*settings_) +
                                        ShardSizeLogical(settings_->shards.size) * 0.8f);
    }
    content = std::max(content, CenterGlowRadiusLogical(*settings_));
    if (content <= 0.0f) return 0;

    // Keep the floating window tight around the actual visual core. Shard
    // travel fades out within this modest bloom padding instead of inflating
    // the window and stealing desktop movement range.
    const float fullSigma = BloomSigmaLogical(*settings_) * scale;
    const float farSigma = fullSigma * 1.30f;
    const float bloomMargin = farSigma * 2.0f + 8.0f * scale;
    const float half = content * scale + bloomMargin;
    int size = gfx::IRound(half * 2.0f);
    size = std::max(size, gfx::IRound(200.0f * scale));
    size = std::min(size, 4096);
    return size;
}

void Overlay::ApplyContentSize(bool preserveCenter) {
    if (!hwnd_ || !settings_) return;
    const int desired = DesiredContentSize(dpi_);
    if (desired <= 0) return;
    if (desired == width_ && desired == height_) {
        contentSizeDirty_ = false;
        return;
    }

    RECT oldRect{};
    GetWindowRect(hwnd_, &oldRect);
    int x = settings_->overlayX;
    int y = settings_->overlayY;
    if (preserveCenter) {
        int centerX = oldRect.left + (oldRect.right - oldRect.left) / 2;
        int centerY = oldRect.top + (oldRect.bottom - oldRect.top) / 2;
        x = centerX - desired / 2;
        y = centerY - desired / 2;
    }
    RECT desiredRect{x, y, x + desired, y + desired};
    RECT clamped = ClampedRect(desiredRect);
    SetWindowPos(hwnd_, HWND_TOPMOST, clamped.left, clamped.top, desired, desired,
                 SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    width_ = desired;
    height_ = desired;
    settings_->overlayX = clamped.left;
    settings_->overlayY = clamped.top;
    contentSizeDirty_ = false;
}

bool Overlay::InitGraphics() {
    lastError_.clear();
    if (!d2dFactory_) {
        D2D1_FACTORY_OPTIONS options{};
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                       __uuidof(ID2D1Factory1), &options,
                                       reinterpret_cast<void**>(d2dFactory_.GetAddressOf()));
        if (FAILED(hr)) {
            lastError_ = L"Direct2D 初始化失败" + HResultText(hr);
            return false;
        }
    }

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                         D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                   featureLevels, ARRAYSIZE(featureLevels),
                                   D3D11_SDK_VERSION, &d3dDevice_, &featureLevel, &d3dContext_);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, featureLevels,
                               ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &d3dDevice_,
                               &featureLevel, &d3dContext_);
    }
    if (FAILED(hr) || !d3dDevice_) {
        lastError_ = L"Direct3D 初始化失败" + HResultText(hr);
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3dDevice_.As(&dxgiDevice);
    if (FAILED(hr)) {
        lastError_ = L"无法取得图形设备接口" + HResultText(hr);
        return false;
    }
    hr = d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_);
    if (FAILED(hr)) {
        lastError_ = L"Direct2D 设备创建失败" + HResultText(hr);
        return false;
    }
    hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_);
    if (FAILED(hr)) {
        lastError_ = L"Direct2D 上下文创建失败" + HResultText(hr);
        return false;
    }
    d2dContext_->SetDpi(96.0f, 96.0f);
    d2dContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush_);

    D2D1_STROKE_STYLE_PROPERTIES strokeProps = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND,
        10.0f, D2D1_DASH_STYLE_SOLID, 0.0f);
    d2dFactory_->CreateStrokeStyle(strokeProps, nullptr, 0, &strokeStyle_);

    RECT client{};
    GetClientRect(hwnd_, &client);
    width_ = static_cast<int>(client.right - client.left);
    height_ = static_cast<int>(client.bottom - client.top);
    if (!CreateSwapChain(static_cast<UINT>(width_), static_cast<UINT>(height_))) return false;

    hr = DCompositionCreateDevice(dxgiDevice.Get(), __uuidof(IDCompositionDevice),
                                  reinterpret_cast<void**>(compositionDevice_.GetAddressOf()));
    if (FAILED(hr)) {
        lastError_ = L"DirectComposition 初始化失败" + HResultText(hr);
        return false;
    }
    hr = compositionDevice_->CreateTargetForHwnd(hwnd_, TRUE, &compositionTarget_);
    if (FAILED(hr)) {
        lastError_ = L"浮窗合成目标创建失败" + HResultText(hr);
        return false;
    }
    hr = compositionDevice_->CreateVisual(&compositionVisual_);
    if (FAILED(hr)) {
        lastError_ = L"浮窗合成视觉创建失败" + HResultText(hr);
        return false;
    }
    compositionVisual_->SetContent(swapChain_.Get());
    compositionTarget_->SetRoot(compositionVisual_.Get());
    compositionDevice_->Commit();
    CreateSceneTarget();
    return true;
}

bool Overlay::CreateSwapChain(UINT width, UINT height) {
    if (width == 0 || height == 0) return false;
    if (!swapChain_) {
        ComPtr<IDXGIDevice> dxgiDevice;
        HRESULT hr = d3dDevice_.As(&dxgiDevice);
        if (FAILED(hr)) return false;
        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetAdapter(&adapter);
        if (FAILED(hr)) return false;
        ComPtr<IDXGIFactory2> factory;
        hr = adapter->GetParent(IID_PPV_ARGS(&factory));
        if (FAILED(hr)) return false;

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Stereo = FALSE;
        desc.SampleDesc.Count = 1;
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        desc.Flags = 0;
        hr = factory->CreateSwapChainForComposition(d3dDevice_.Get(), &desc, nullptr, &swapChain_);
        if (FAILED(hr)) {
            lastError_ = L"浮窗交换链创建失败" + HResultText(hr);
            return false;
        }
    } else {
        targetBitmap_.Reset();
        HRESULT hr = swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(hr)) {
            lastError_ = L"浮窗交换链调整失败" + HResultText(hr);
            return false;
        }
    }
    width_ = static_cast<int>(width);
    height_ = static_cast<int>(height);
    return true;
}

void Overlay::ReleaseGlowTarget() {
    if (glowContext_) glowContext_->SetTarget(nullptr);
    glowDirty_ = true;
    glowReady_ = false;
    glowBrush_.Reset();
    glowBlur_.Reset();
    glowBitmap_.Reset();
    glowResultBitmap_.Reset();
    glowContext_.Reset();
}

void Overlay::ReleaseSceneTarget() {
    if (sceneContext_) sceneContext_->SetTarget(nullptr);
    bloomReady_ = false;
    ReleaseGlowTarget();
    sceneBrush_.Reset();
    bloomColorNear_.Reset();
    bloomColorFar_.Reset();
    bloomBlurNear_.Reset();
    bloomBlurFar_.Reset();
    sceneBitmap_.Reset();
    sceneContext_.Reset();
}

void Overlay::ReleaseGraphics() {
    ReleaseSceneTarget();
    shards_.clear();
    for (auto& geometry : shardGeometries_) geometry.Reset();
    brush_.Reset();
    strokeStyle_.Reset();
    targetBitmap_.Reset();
    compositionVisual_.Reset();
    compositionTarget_.Reset();
    compositionDevice_.Reset();
    swapChain_.Reset();
    d2dContext_.Reset();
    d2dDevice_.Reset();
    d3dContext_.Reset();
    d3dDevice_.Reset();
    d2dFactory_.Reset();
}

bool Overlay::CreateSceneTarget() {
    if (!d2dDevice_ || !d2dContext_ || width_ <= 0 || height_ <= 0) return false;
    ReleaseSceneTarget();

    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
        96.0f);
    HRESULT hr = d2dContext_->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(width_),
                                                       static_cast<UINT32>(height_)),
                                           nullptr, 0, &props, &sceneBitmap_);
    if (FAILED(hr) || !sceneBitmap_) return false;
    hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &sceneContext_);
    if (FAILED(hr) || !sceneContext_) return false;
    sceneContext_->SetDpi(96.0f, 96.0f);
    sceneContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    sceneContext_->SetTarget(sceneBitmap_.Get());
    sceneContext_->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &sceneBrush_);
    if (!sceneBrush_) return false;

    if (!bloomBlurNear_) {
        hr = d2dContext_->CreateEffect(CLSID_D2D1GaussianBlur, &bloomBlurNear_);
        if (FAILED(hr) || !bloomBlurNear_) return false;
    }
    if (!bloomBlurFar_) {
        hr = d2dContext_->CreateEffect(CLSID_D2D1GaussianBlur, &bloomBlurFar_);
        if (FAILED(hr) || !bloomBlurFar_) return false;
    }
    if (!bloomColorNear_) {
        hr = d2dContext_->CreateEffect(CLSID_D2D1ColorMatrix, &bloomColorNear_);
        if (FAILED(hr) || !bloomColorNear_) return false;
    }
    if (!bloomColorFar_) {
        hr = d2dContext_->CreateEffect(CLSID_D2D1ColorMatrix, &bloomColorFar_);
        if (FAILED(hr) || !bloomColorFar_) return false;
    }
    bloomBlurNear_->SetInput(0, sceneBitmap_.Get());
    bloomBlurFar_->SetInput(0, sceneBitmap_.Get());
    bloomColorNear_->SetInputEffect(0, bloomBlurNear_.Get());
    bloomColorFar_->SetInputEffect(0, bloomBlurFar_.Get());
    bloomBlurNear_->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION,
                             D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);
    bloomBlurFar_->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION,
                            D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);
    bloomBlurNear_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,
                             D2D1_BORDER_MODE_SOFT);
    bloomBlurFar_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
    bloomReady_ = true;
    UpdateBloomParameters();
    CreateGlowTarget();
    return true;
}

bool Overlay::CreateGlowTarget() {
    if (!d2dDevice_ || !d2dContext_ || width_ <= 0 || height_ <= 0) return false;
    ReleaseGlowTarget();
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
        96.0f);
    HRESULT hr = d2dContext_->CreateBitmap(
        D2D1::SizeU(static_cast<UINT32>(width_), static_cast<UINT32>(height_)), nullptr, 0,
        &props, &glowBitmap_);
    if (FAILED(hr) || !glowBitmap_) return false;
    hr = d2dContext_->CreateBitmap(
        D2D1::SizeU(static_cast<UINT32>(width_), static_cast<UINT32>(height_)), nullptr, 0,
        &props, &glowResultBitmap_);
    if (FAILED(hr) || !glowResultBitmap_) return false;
    hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &glowContext_);
    if (FAILED(hr) || !glowContext_) return false;
    glowContext_->SetDpi(96.0f, 96.0f);
    glowContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (!glowBlur_) {
        hr = d2dContext_->CreateEffect(CLSID_D2D1GaussianBlur, &glowBlur_);
        if (FAILED(hr) || !glowBlur_) return false;
    }
    glowBlur_->SetInput(0, glowBitmap_.Get());
    glowBlur_->SetValue(D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION,
                        D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED);
    glowBlur_->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_SOFT);
    glowDirty_ = true;
    glowReady_ = false;
    return true;
}

bool Overlay::RenderGlowLayer() {
    if (!settings_ || !glowBitmap_ || !glowResultBitmap_ || !glowContext_ || !glowBlur_)
        return false;
    const GlowConfig& glow = settings_->glow;
    if (!glow.enabled || glow.opacity <= 0) {
        glowReady_ = false;
        return false;
    }
    if (glowReady_ && !glowDirty_) return true;

    const float scale =
        (dpi_ ? dpi_ : 96) / 96.0f * OverlayUserScale(*settings_);
    const float radius = CenterGlowRadiusLogical(*settings_) * scale;
    if (radius <= 1.0f) return false;

    D2D1_GRADIENT_STOP stops[6]{};
    const float r = glow.r / 255.0f;
    const float g = glow.g / 255.0f;
    const float b = glow.b / 255.0f;
    stops[0] = D2D1::GradientStop(0.00f, D2D1::ColorF(r, g, b, 0.72f));
    stops[1] = D2D1::GradientStop(0.30f, D2D1::ColorF(r, g, b, 0.62f));
    stops[2] = D2D1::GradientStop(0.55f, D2D1::ColorF(r, g, b, 0.44f));
    stops[3] = D2D1::GradientStop(0.76f, D2D1::ColorF(r, g, b, 0.24f));
    stops[4] = D2D1::GradientStop(0.90f, D2D1::ColorF(r, g, b, 0.08f));
    stops[5] = D2D1::GradientStop(1.00f, D2D1::ColorF(r, g, b, 0.0f));
    ComPtr<ID2D1GradientStopCollection> collection;
    HRESULT hr = glowContext_->CreateGradientStopCollection(
        stops, 6, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &collection);
    if (FAILED(hr) || !collection) return false;
    glowBrush_.Reset();
    hr = glowContext_->CreateRadialGradientBrush(
        D2D1::RadialGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(0, 0), 1.0f,
                                            1.0f),
        collection.Get(), &glowBrush_);
    if (FAILED(hr) || !glowBrush_) return false;

    glowContext_->SetTarget(glowBitmap_.Get());
    glowContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    glowContext_->BeginDraw();
    glowContext_->Clear(D2D1::ColorF(0, 0.0f));
    glowBrush_->SetCenter(D2D1::Point2F(width_ * 0.5f, height_ * 0.5f));
    glowBrush_->SetRadiusX(radius);
    glowBrush_->SetRadiusY(radius);
    // The extra Gaussian pass softens the edge but also lowers the peak alpha;
    // compensate slightly so the original opacity still reads as a visible base glow.
    glowBrush_->SetOpacity(
        std::clamp(glow.opacity / 100.0f * 1.25f, 0.0f, 1.0f));
    glowContext_->FillRectangle(
        D2D1::RectF(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)),
        glowBrush_.Get());
    HRESULT drawResult = glowContext_->EndDraw();
    glowContext_->SetTarget(nullptr);
    if (drawResult == D2DERR_RECREATE_TARGET || FAILED(drawResult)) {
        glowDirty_ = true;
        glowReady_ = false;
        return false;
    }

    const float sigma = std::clamp(radius * 0.10f, 6.0f * scale, 52.0f * scale);
    glowBlur_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, sigma);

    // Bake the blurred result once. Drawing ID2D1Effect directly every frame
    // would re-evaluate the Gaussian pass; the result bitmap is static.
    glowContext_->SetTarget(glowResultBitmap_.Get());
    glowContext_->BeginDraw();
    glowContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    glowContext_->Clear(D2D1::ColorF(0, 0.0f));
    glowContext_->DrawImage(glowBlur_.Get(), D2D1::Point2F(0.0f, 0.0f));
    HRESULT resultDraw = glowContext_->EndDraw();
    glowContext_->SetTarget(nullptr);
    if (resultDraw == D2DERR_RECREATE_TARGET || FAILED(resultDraw)) {
        glowDirty_ = true;
        glowReady_ = false;
        return false;
    }

    glowDirty_ = false;
    glowReady_ = true;
    return true;
}

void Overlay::UpdateBloomParameters() {
    if (!settings_ || !bloomBlurNear_ || !bloomBlurFar_ || !bloomColorNear_ ||
        !bloomColorFar_) {
        return;
    }
    const float dpiScale =
        (dpi_ ? dpi_ : 96) / 96.0f * OverlayUserScale(*settings_);
    const float fullSigma = BloomSigmaLogical(*settings_) * dpiScale;
    const float nearSigma = std::max(0.8f, fullSigma * 0.34f);
    const float farSigma = std::max(1.2f, fullSigma * 1.30f);
    bloomBlurNear_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, nearSigma);
    bloomBlurFar_->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, farSigma);

    const float brightness = std::clamp(settings_->bloomBrightness, 0, 200) / 100.0f;
    auto setScale = [](ID2D1Effect* effect, float scale) {
        D2D1_MATRIX_5X4_F matrix{};
        matrix._11 = scale;
        matrix._22 = scale;
        matrix._33 = scale;
        matrix._44 = scale;
        effect->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, matrix);
        effect->SetValue(D2D1_COLORMATRIX_PROP_ALPHA_MODE,
                         D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED);
    };
    setScale(bloomColorNear_.Get(), 1.9f * brightness);
    setScale(bloomColorFar_.Get(), 0.95f * brightness);
}

bool Overlay::EnsureTargetBitmap() {
    if (!swapChain_ || !d2dContext_) return false;
    targetBitmap_.Reset();
    ComPtr<IDXGISurface> surface;
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr)) return false;
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
        96.0f);
    hr = d2dContext_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &targetBitmap_);
    if (FAILED(hr)) {
        lastError_ = L"浮窗渲染目标创建失败";
        return false;
    }
    d2dContext_->SetTarget(targetBitmap_.Get());
    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    return true;
}

void Overlay::Resize(UINT width, UINT height) {
    if (!graphicsReady_ || width == 0 || height == 0) return;
    if (static_cast<int>(width) == width_ && static_cast<int>(height) == height_) return;
    d2dContext_->SetTarget(nullptr);
    targetBitmap_.Reset();
    if (!CreateSwapChain(width, height)) {
        lastError_ = L"无法调整浮窗尺寸。";
        return;
    }
    width_ = static_cast<int>(width);
    height_ = static_cast<int>(height);
    CreateSceneTarget();
}

void Overlay::OnSettingsChanged() {
    bool wantedInteractive = !settings_->overlayFixed;
    if (interactive_ != wantedInteractive) SetInteractive(wantedInteractive);
    if (!settings_->shards.enabled || settings_->shards.opacity <= 0) {
        shards_.clear();
        shardAccumulator_ = 0.0f;
    }
    glowDirty_ = true;
    contentSizeDirty_ = true;
    QueryPerformanceCounter(&contentSizeChanged_);
    RefreshVisibility();
}

void Overlay::ReassertTopmost() {
    if (!hwnd_ || !visible_) return;
    if (!IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void Overlay::RefreshVisibility() {
    if (!hwnd_ || !graphicsReady_) return;
    bool shouldShow = AnyEffectVisible(*settings_);
    if (shouldShow && !visible_) {
        if (contentSizeDirty_) ApplyContentSize(true);
        SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        visible_ = true;
        RenderOnce();
    } else if (!shouldShow && visible_) {
        ShowWindow(hwnd_, SW_HIDE);
        visible_ = false;
    }
}

void Overlay::SetInteractive(bool interactive) {
    if (!hwnd_) return;
    interactive_ = interactive;
    LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    style |= WS_EX_LAYERED;
    if (interactive) {
        style &= ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    } else {
        style |= WS_EX_TRANSPARENT;
        if (dragging_) EndDrag();
    }
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, style);
    // SetLayeredWindowAttributes refreshes the layered hit-test region after an
    // extended-style change. Without this, WS_EX_TRANSPARENT on a
    // DirectComposition window can keep swallowing clicks after being locked.
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void Overlay::Tick() {
    if (!visible_ || !graphicsReady_ || !hwnd_) return;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (contentSizeDirty_ && frequency_.QuadPart > 0 &&
        static_cast<double>(now.QuadPart - contentSizeChanged_.QuadPart) /
                static_cast<double>(frequency_.QuadPart) >
            0.13) {
        ApplyContentSize(true);
    }
    float dt = 1.0f / 60.0f;
    if (frequency_.QuadPart > 0) {
        dt = static_cast<float>(now.QuadPart - lastTick_.QuadPart) /
             static_cast<float>(frequency_.QuadPart);
    }
    lastTick_ = now;
    dt = std::clamp(dt, 1.0f / 240.0f, 0.10f);
    elapsed_ += dt;
    RenderFrame(dt);
}

void Overlay::RenderOnce() {
    if (!graphicsReady_ || !hwnd_) return;
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    lastTick_ = now;
    RenderFrame(1.0f / 60.0f);
}

void Overlay::UpdateAnalysis(float dt) {
    if (!audio_) return;
    int sampleRate = audio_->SampleRate();
    if (sampleRate != lastSampleRate_) {
        analyzer_.SetSampleRate(sampleRate);
        lastSampleRate_ = sampleRate;
        lastAudioTotal_ = UINT64_MAX;
    }
    uint64_t total = audio_->TotalWritten();
    uint64_t previous = lastAudioTotal_;
    if (total < previous) analyzer_.Reset();
    lastAudioTotal_ = total;
    size_t count = audio_->Snapshot(audioSnapshot_.data(), audioSnapshot_.size());
    bool newer = total != previous;
    analyzer_.Update(audioSnapshot_.data(), count, total, newer, dt);
}

void Overlay::RenderFrame(float dt) {
    if (!graphicsReady_ || !hwnd_) return;
    UpdateAnalysis(dt);
    UpdateShards(dt);
    UpdateBloomParameters();
    const float scale =
        (dpi_ ? dpi_ : 96) / 96.0f * OverlayUserScale(*settings_);

    bool sceneRendered = false;
    if (bloomReady_ && sceneContext_ && sceneBitmap_ && bloomColorNear_ &&
        bloomColorFar_ && sceneBrush_) {
        sceneContext_->SetTarget(sceneBitmap_.Get());
        sceneContext_->BeginDraw();
        sceneContext_->SetTransform(D2D1::Matrix3x2F::Identity());
        sceneContext_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        sceneContext_->Clear(D2D1::ColorF(0, 0.0f));
        DrawVisuals(sceneContext_.Get(), sceneBrush_.Get(), static_cast<float>(width_),
                    static_cast<float>(height_), scale, elapsed_);
        HRESULT sceneResult = sceneContext_->EndDraw();
        sceneContext_->SetTarget(nullptr);
        if (sceneResult == D2DERR_RECREATE_TARGET) {
            graphicsReady_ = false;
            ReleaseGraphics();
            if (InitGraphics()) graphicsReady_ = true;
            return;
        }
        if (SUCCEEDED(sceneResult)) {
            sceneRendered = true;
        } else {
            bloomReady_ = false;
        }
    }

    const bool glowRendered = RenderGlowLayer();

    if (!EnsureTargetBitmap()) return;

    d2dContext_->BeginDraw();
    d2dContext_->SetTarget(targetBitmap_.Get());
    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    d2dContext_->Clear(D2D1::ColorF(0, 0.0f));
    if (glowRendered && glowResultBitmap_) {
        d2dContext_->DrawBitmap(glowResultBitmap_.Get(),
                                D2D1::RectF(0.0f, 0.0f, static_cast<float>(width_),
                                            static_cast<float>(height_)),
                                1.0f, D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
    }
    if (sceneRendered) {
        // Both layers are Gaussian falloffs, never constant-alpha outlines.
        // Source-over preserves the soft alpha ramp towards the outside.
        if (settings_->bloomBrightness > 0) {
            d2dContext_->DrawImage(bloomColorFar_.Get(), D2D1::Point2F(0.0f, 0.0f));
            d2dContext_->DrawImage(bloomColorNear_.Get(), D2D1::Point2F(0.0f, 0.0f));
        }
        d2dContext_->DrawBitmap(sceneBitmap_.Get(),
                                D2D1::RectF(0.0f, 0.0f, static_cast<float>(width_),
                                            static_cast<float>(height_)),
                                1.0f, D2D1_INTERPOLATION_MODE_LINEAR, nullptr, nullptr);
        // Shards deliberately bypass Bloom: they are crisp, semi-transparent
        // geometric facets, not soft light blobs.
        DrawShards(d2dContext_.Get(), brush_.Get(), static_cast<float>(width_) * 0.5f,
                   static_cast<float>(height_) * 0.5f, scale);
    } else {
        DrawVisuals(d2dContext_.Get(), brush_.Get(), static_cast<float>(width_),
                    static_cast<float>(height_), scale, elapsed_);
        DrawShards(d2dContext_.Get(), brush_.Get(), static_cast<float>(width_) * 0.5f,
                   static_cast<float>(height_) * 0.5f, scale);
    }
    HRESULT hr = d2dContext_->EndDraw();
    d2dContext_->SetTarget(nullptr);
    targetBitmap_.Reset();
    if (hr == D2DERR_RECREATE_TARGET) {
        graphicsReady_ = false;
        ReleaseGraphics();
        if (InitGraphics()) {
            graphicsReady_ = true;
        }
        return;
    }
    swapChain_->Present(0, 0);
}

Overlay::VisualPath Overlay::BuildRingPath(
    const std::vector<D2D1_POINT_2F>& points, bool closed) const {
    VisualPath result;
    if (!d2dFactory_ || points.empty()) return result;
    if (FAILED(d2dFactory_->CreatePathGeometry(&result.geometry)) || !result.geometry)
        return result;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(result.geometry->Open(&sink)) || !sink) return result;
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    sink->BeginFigure(points.front(), D2D1_FIGURE_BEGIN_HOLLOW);
    for (size_t i = 1; i < points.size(); ++i) sink->AddLine(points[i]);
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return result;
    result.valid = true;
    return result;
}

Overlay::VisualPath Overlay::BuildMeshPath(const std::vector<D2D1_POINT_2F>& outer,
                                            const std::vector<D2D1_POINT_2F>& inner) const {
    VisualPath result;
    if (!d2dFactory_ || outer.empty() || outer.size() != inner.size()) return result;
    if (FAILED(d2dFactory_->CreatePathGeometry(&result.geometry)) || !result.geometry)
        return result;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(result.geometry->Open(&sink)) || !sink) return result;
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    const size_t count = outer.size();
    for (size_t i = 0; i < count; ++i) {
        // Radial spokes at every sample, plus the interleaved diagonals. The
        // reference mesh has both groups; omitting the spokes made it sparse.
        sink->BeginFigure(outer[i], D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(inner[i]);
        sink->EndFigure(D2D1_FIGURE_END_OPEN);

        sink->BeginFigure(outer[i], D2D1_FIGURE_BEGIN_HOLLOW);
        sink->AddLine(inner[(i + 1) % count]);
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
    }
    if (FAILED(sink->Close())) return result;
    result.valid = true;
    return result;
}

void Overlay::DrawGlowGeometry(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
                               ID2D1PathGeometry* geometry, const gfx::Color& color,
                               float opacity, float coreWidth) {
    if (!target || !brush || !geometry || !strokeStyle_ || opacity <= 0.001f) return;
    gfx::Color core = color;
    core.a = static_cast<int>(
        std::lround(color.a * std::clamp(opacity, 0.0f, 1.0f)));
    if (core.a <= 0) return;
    brush->SetColor(core.D2D());
    target->DrawGeometry(geometry, brush, coreWidth, strokeStyle_.Get());
}

void Overlay::DrawDot(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, float x, float y,
                      float radius, const gfx::Color& color, float opacity) {
    if (!target || !brush || radius <= 0.1f) return;
    gfx::Color core = color;
    core.a = static_cast<int>(
        std::lround(color.a * std::clamp(opacity, 0.0f, 1.0f)));
    if (core.a <= 0) return;
    brush->SetColor(core.D2D());
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), brush);
}

bool Overlay::EnsureShardGeometries() {
    if (!d2dFactory_) return false;
    bool ready = true;
    for (const auto& geometry : shardGeometries_) {
        if (!geometry) {
            ready = false;
            break;
        }
    }
    if (ready) return true;

    static const D2D1_POINT_2F kShapes[4][4] = {
        {{0.0f, 1.05f}, {0.43f, -0.55f}, {-0.43f, -0.55f}, {0, 0}},
        {{0.0f, 0.92f}, {0.90f, -0.52f}, {-0.90f, -0.52f}, {0, 0}},
        {{0.0f, 1.00f}, {0.60f, 0.06f}, {0.0f, -0.95f}, {-0.60f, 0.06f}},
        {{0.22f, 1.00f}, {0.82f, 0.20f}, {0.05f, -0.86f}, {-0.68f, -0.06f}},
    };
    static const int kCounts[4] = {3, 3, 4, 4};
    for (int i = 0; i < 4; ++i) {
        shardGeometries_[i].Reset();
        ComPtr<ID2D1PathGeometry> geometry;
        if (FAILED(d2dFactory_->CreatePathGeometry(&geometry)) || !geometry) return false;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(geometry->Open(&sink)) || !sink) return false;
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        sink->BeginFigure(kShapes[i][0], D2D1_FIGURE_BEGIN_FILLED);
        for (int point = 1; point < kCounts[i]; ++point) sink->AddLine(kShapes[i][point]);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (FAILED(sink->Close())) return false;
        shardGeometries_[i] = geometry;
    }
    return true;
}

float Overlay::NextShardRandom() {
    shardRngState_ = shardRngState_ * 1664525u + 1013904223u;
    return static_cast<float>((shardRngState_ >> 8) & 0x00FFFFFFu) / 16777216.0f;
}

void Overlay::UpdateShards(float dt) {
    if (!settings_) return;
    const ShardConfig& cfg = settings_->shards;
    if (!cfg.enabled || cfg.opacity <= 0) {
        shards_.clear();
        shardAccumulator_ = 0.0f;
        return;
    }

    const float frequency = std::clamp(cfg.frequency, 0, 200) / 100.0f;
    const float level = analyzer_.Level();
    const float flux = analyzer_.Flux();
    const float beat = analyzer_.Beat();
    const float rate =
        5.2f * frequency * (0.16f + level * 1.75f + flux * 6.5f + beat * 1.5f);
    shardAccumulator_ += rate * dt;
    shardAccumulator_ = std::min(shardAccumulator_, 4.0f);

    const float tau = 6.2831853f;
    int emitted = 0;
    while (shardAccumulator_ >= 1.0f && emitted < 4 && shards_.size() < 96) {
        shardAccumulator_ -= 1.0f;
        ++emitted;
        ShardParticle particle;
        // Golden-angle stepping spreads successive facets around the ring and
        // avoids the clustered, amorphous blobs random angles produced.
        shardAngleCursor_ = std::fmod(
            shardAngleCursor_ + 2.39996323f + (NextShardRandom() - 0.5f) * 0.55f, tau);
        if (shardAngleCursor_ < 0.0f) shardAngleCursor_ += tau;
        particle.angle = shardAngleCursor_;
        particle.radius = ShardStartLogical(*settings_) * (0.96f + NextShardRandom() * 0.08f) +
                          (NextShardRandom() * 2.0f - 1.0f) * 4.0f;
        particle.speed = (12.0f + 0.12f * cfg.size) * (0.80f + NextShardRandom() * 0.50f);
        particle.angularDrift = (NextShardRandom() * 2.0f - 1.0f) * 0.10f;
        particle.lifetime = 0.95f + NextShardRandom() * 0.70f;
        particle.sizeVariation = (0.72f + NextShardRandom() * 0.56f) *
                                 (0.85f + std::min(flux, 0.35f) * 0.8f);
        particle.rotation = (NextShardRandom() * 2.0f - 1.0f) * 0.9f;
        particle.spin = (NextShardRandom() * 2.0f - 1.0f) * 1.6f;
        int shape = std::clamp(cfg.shape, 0, 4);
        particle.shape = shape >= 4 ? static_cast<int>(NextShardRandom() * 4.0f) % 4 : shape;
        shards_.push_back(particle);
    }

    for (auto& particle : shards_) {
        particle.age += dt;
        particle.radius += particle.speed * dt;
        particle.angle += particle.angularDrift * dt;
    }
    shards_.erase(std::remove_if(shards_.begin(), shards_.end(),
                                 [](const ShardParticle& particle) {
                                     return particle.age >= particle.lifetime;
                                 }),
                  shards_.end());
}

void Overlay::DrawShards(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, float cx,
                         float cy, float scale) {
    if (!target || !brush || !settings_ || shards_.empty()) return;
    const ShardConfig& cfg = settings_->shards;
    if (!cfg.enabled || cfg.opacity <= 0) return;
    if (!EnsureShardGeometries()) return;

    const float baseSize = ShardSizeLogical(cfg.size);
    const float opacity = std::clamp(cfg.opacity / 100.0f, 0.0f, 1.0f);
    for (const auto& particle : shards_) {
        const float t = std::clamp(particle.age / std::max(particle.lifetime, 0.01f), 0.0f, 1.0f);
        const float fadeIn = std::clamp(t / 0.10f, 0.0f, 1.0f);
        const float fadeOut = std::clamp((1.0f - t) / 0.42f, 0.0f, 1.0f);
        const float fade = std::min(fadeIn, fadeOut);
        const float alpha = fade * opacity;
        if (alpha <= 0.005f) continue;
        const float size = baseSize * particle.sizeVariation * (1.0f + t * 0.20f) * scale;
        if (size <= 0.5f) continue;

        const float x = cx + std::cos(particle.angle) * particle.radius * scale;
        const float y = cy + std::sin(particle.angle) * particle.radius * scale;
        const float rotationRadians = particle.angle - 1.5707963f + particle.rotation +
                                      particle.spin * particle.age;
        const float rotationDegrees = rotationRadians * 57.2957795f;
        const D2D1_MATRIX_3X2_F transform =
            D2D1::Matrix3x2F::Scale(size, size) *
            D2D1::Matrix3x2F::Rotation(rotationDegrees) *
            D2D1::Matrix3x2F::Translation(x, y);
        target->SetTransform(transform);

        gfx::Color fill = gfx::Rgb(cfg.r, cfg.g, cfg.b);
        fill.a = static_cast<int>(std::lround(255.0f * alpha));
        brush->SetColor(fill.D2D());
        target->FillGeometry(shardGeometries_[particle.shape].Get(), brush);
    }
    target->SetTransform(D2D1::Matrix3x2F::Identity());
}

void Overlay::DrawVisuals(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, float width,
                          float height, float scale, float time) {
    if (!target || !brush || !settings_) return;
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float cx = width * 0.5f;
    const float cy = height * 0.5f;
    const float maxRadius = std::min(cx, cy) - 8.0f * scale;
    const float level = analyzer_.Level();
    const float beat = analyzer_.Beat();

    const EffectConfig& outerCfg = settings_->outer;
    const EffectConfig& dotsCfg = settings_->dots;
    const EffectConfig& waveCfg = settings_->wave;

    const float tau = 6.2831853f;
    auto wrap = [](float value) {
        value -= std::floor(value);
        return value;
    };
    auto spectrumOuter = [&](float p) {
        return analyzer_.BandCyclic(p + 0.035f) * 0.67f +
               analyzer_.BandCyclic(wrap(p * 1.017f + 0.41f)) * 0.33f;
    };
    auto gapSignal = [&](float p) {
        return std::clamp(analyzer_.BandCyclic(wrap(p + 0.68f)), 0.0f, 1.0f);
    };
    auto rawOuter = [&](float p) {
        // Outermost ring is driven only by the frequency-domain spectrum.
        return (spectrumOuter(p) - 0.27f) * 1.08f;
    };

    const int densityOuter = std::clamp(outerCfg.density, 24, 160);
    const float baseOuterR = (62.0f + 1.38f * outerCfg.size) * scale;
    const float outerAmp = (5.0f + 0.22f * outerCfg.amplitude) * scale;
    const float innerAmp = outerAmp * 0.78f;
    const float thickness = outerCfg.thickness / 100.0f;
    const float minSeparation = std::max(2.5f, 4.0f * scale);

    std::vector<float> rawOuterValues(densityOuter, 0.0f);
    float outerMean = 0.0f;
    for (int i = 0; i < densityOuter; ++i) {
        float p = static_cast<float>(i) / densityOuter;
        rawOuterValues[i] = rawOuter(p);
        outerMean += rawOuterValues[i];
    }
    outerMean /= densityOuter;

    // The outer contour is the frequency-driven shape. Every inner contour is
    // its exact negative: an outer peak becomes an inward dent on the inner
    // ring and dot ring.
    auto shapeOuter = [&](float value) {
        return std::clamp((value - outerMean) * 2.4f, -kMaxRingDeform, kMaxRingDeform);
    };
    auto outerShapeAt = [&](float p) { return shapeOuter(rawOuter(p)); };

    std::vector<float> outerRadii(densityOuter, baseOuterR);
    std::vector<float> innerRadii(densityOuter, baseOuterR * 0.8f);
    for (int i = 0; i < densityOuter; ++i) {
        float p = static_cast<float>(i) / densityOuter;
        float gapBreath =
            0.5f + 0.5f * std::sin(p * tau * 2.0f + time * 0.34f + 0.7f);
        float localGap = (7.0f + gapSignal(p) * 12.0f + gapBreath * 2.8f + level * 3.5f +
                          std::max(0.0f, beat) * 1.2f) *
                         scale;
        float breath = level * 1.8f + beat * 1.1f;
        float shape = shapeOuter(rawOuterValues[i]);
        float outerR = baseOuterR + breath * scale + localGap * 0.5f + shape * outerAmp;
        float innerR = baseOuterR + breath * scale - localGap * 0.5f - shape * innerAmp;
        outerR = std::clamp(outerR, 24.0f * scale, maxRadius);
        innerR = std::clamp(innerR, 14.0f * scale, maxRadius - minSeparation);
        if (innerR > outerR - minSeparation) {
            innerR = outerR - minSeparation;
            if (innerR < 14.0f * scale) {
                innerR = 14.0f * scale;
                outerR = std::min(maxRadius, innerR + minSeparation);
            }
        }
        outerRadii[i] = outerR;
        innerRadii[i] = innerR;
    }

    auto smoothRadii = [densityOuter](std::vector<float>& radii) {
        if (densityOuter < 3) return;
        std::vector<float> smoothed(radii.size(), 0.0f);
        for (int i = 0; i < densityOuter; ++i) {
            int prev = (i + densityOuter - 1) % densityOuter;
            int next = (i + 1) % densityOuter;
            smoothed[i] = radii[prev] * 0.12f + radii[i] * 0.76f + radii[next] * 0.12f;
        }
        radii.swap(smoothed);
    };
    smoothRadii(outerRadii);
    smoothRadii(innerRadii);
    for (int i = 0; i < densityOuter; ++i) {
        if (innerRadii[i] > outerRadii[i] - minSeparation) {
            float middle = (outerRadii[i] + innerRadii[i]) * 0.5f;
            outerRadii[i] = middle + minSeparation * 0.5f;
            innerRadii[i] = middle - minSeparation * 0.5f;
        }
        outerRadii[i] = std::clamp(outerRadii[i], 24.0f * scale, maxRadius);
        innerRadii[i] = std::clamp(innerRadii[i], 14.0f * scale, maxRadius);
        if (innerRadii[i] > outerRadii[i] - minSeparation)
            innerRadii[i] = std::max(14.0f * scale, outerRadii[i] - minSeparation);
    }

    std::vector<D2D1_POINT_2F> outerPoints;
    std::vector<D2D1_POINT_2F> innerPoints;
    outerPoints.reserve(densityOuter);
    innerPoints.reserve(densityOuter);
    for (int i = 0; i < densityOuter; ++i) {
        float angle = static_cast<float>(i) / densityOuter * tau - 1.5707963f;
        float ca = std::cos(angle);
        float sa = std::sin(angle);
        outerPoints.push_back(
            D2D1::Point2F(cx + ca * outerRadii[i], cy + sa * outerRadii[i]));
        innerPoints.push_back(
            D2D1::Point2F(cx + ca * innerRadii[i], cy + sa * innerRadii[i]));
    }

    if (outerCfg.enabled && outerCfg.opacity > 0) {
        VisualPath outerPath = BuildRingPath(outerPoints);
        VisualPath innerPath = BuildRingPath(innerPoints);
        VisualPath meshPath = BuildMeshPath(outerPoints, innerPoints);
        gfx::Color color = gfx::Rgb(outerCfg.r, outerCfg.g, outerCfg.b);
        float opacity = outerCfg.opacity / 100.0f;
        if (meshPath.valid) {
            DrawGlowGeometry(target, brush, meshPath.geometry.Get(), color, opacity * 0.42f,
                             0.72f * scale * thickness);
        }
        if (outerPath.valid) {
            DrawGlowGeometry(target, brush, outerPath.geometry.Get(), color, opacity,
                             1.55f * scale * thickness);
        }
        if (innerPath.valid) {
            DrawGlowGeometry(target, brush, innerPath.geometry.Get(), color, opacity * 0.94f,
                             1.25f * scale * thickness);
        }
    }

    if (dotsCfg.enabled && dotsCfg.opacity > 0) {
        const int densityDots = std::clamp(dotsCfg.density, 24, 180);
        const float baseDotR = (48.0f + 1.03f * dotsCfg.size) * scale;
        const float dotAmp = (4.0f + 0.15f * dotsCfg.amplitude) * scale;
        const float dotThickness = 0.55f + 0.45f * (dotsCfg.thickness / 100.0f);
        gfx::Color color = gfx::Rgb(dotsCfg.r, dotsCfg.g, dotsCfg.b);
        const float opacity = dotsCfg.opacity / 100.0f;
        for (int i = 0; i < densityDots; ++i) {
            float p = static_cast<float>(i) / densityDots;
            float spectrum = analyzer_.BandCyclic(wrap(p + 0.28f));
            // Dot ring is anti-phase with the outermost ring.
            float radius = baseDotR - outerShapeAt(p) * dotAmp + level * 1.8f * scale;
            radius = std::clamp(radius, 8.0f * scale, maxRadius);
            float angle = p * tau - 1.5707963f;
            float x = cx + std::cos(angle) * radius;
            float y = cy + std::sin(angle) * radius;
            float dotR =
                (1.60f + 0.55f * level + 0.48f * spectrum) * scale * dotThickness;
            DrawDot(target, brush, x, y, dotR, color, opacity);
        }
    }

    if (waveCfg.enabled && waveCfg.opacity > 0) {
        const int densityWave = std::clamp(waveCfg.density, 20, 360);
        const float halfWidth = (55.0f + 1.50f * waveCfg.size) * scale;
        const float waveAmp = (3.0f + 0.23f * waveCfg.amplitude) * scale;
        const float waveThickness = waveCfg.thickness / 100.0f;
        const float opacity = waveCfg.opacity / 100.0f;
        gfx::Color color = gfx::Rgb(waveCfg.r, waveCfg.g, waveCfg.b);

        gfx::Color baseline = color;
        baseline.a = static_cast<int>(std::lround(color.a * opacity * 0.23f));
        brush->SetColor(baseline.D2D());
        target->DrawLine(D2D1::Point2F(cx - halfWidth, cy),
                         D2D1::Point2F(cx + halfWidth, cy), brush,
                         0.7f * scale * waveThickness, strokeStyle_.Get());

        std::vector<float> data(densityWave, 128.0f);
        std::vector<float> averaged(densityWave, 128.0f);
        for (int i = 0; i < densityWave; ++i) {
            float t = static_cast<float>(i) / std::max(1, densityWave - 1);
            float normalized = analyzer_.TimeDomainAt(0.50f + t * 0.50f);
            data[i] = (normalized * 0.5f + 0.5f) * 255.0f;
        }

        // Two passes of a 3-point moving average: smooth and rounded, without
        // the single-pixel fizz of raw oscilloscope data.
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < densityWave; ++i) {
                int prev = std::max(0, i - 1);
                int next = std::min(densityWave - 1, i + 1);
                averaged[i] = (data[prev] + data[i] + data[next]) / 3.0f;
            }
            data.swap(averaged);
        }

        // Exact mirrored waveform around centerY. The dead zone is in the same
        // 0..255 / center-128 units as the reference math.
        constexpr float kCenterValue = 128.0f;
        constexpr float kDeadZone = 5.0f;
        const float verticalScale = waveAmp / 128.0f * 1.5f;
        std::vector<D2D1_POINT_2F> topPoints;
        std::vector<D2D1_POINT_2F> bottomPoints;
        topPoints.reserve(densityWave);
        bottomPoints.reserve(densityWave);
        for (int i = 0; i < densityWave; ++i) {
            float t = static_cast<float>(i) / std::max(1, densityWave - 1);
            float amplitude = data[i] - kCenterValue;
            if (std::fabs(amplitude) < kDeadZone) amplitude = 0.0f;
            float x = cx - halfWidth + t * halfWidth * 2.0f;
            topPoints.push_back(D2D1::Point2F(x, cy - amplitude * verticalScale));
            bottomPoints.push_back(D2D1::Point2F(x, cy + amplitude * verticalScale));
        }

        VisualPath topPath = BuildRingPath(topPoints, false);
        VisualPath bottomPath = BuildRingPath(bottomPoints, false);
        if (topPath.valid) {
            DrawGlowGeometry(target, brush, topPath.geometry.Get(), color, opacity,
                             1.25f * scale * waveThickness);
        }
        if (bottomPath.valid) {
            DrawGlowGeometry(target, brush, bottomPath.geometry.Get(), color, opacity,
                             1.25f * scale * waveThickness);
        }
    }
}

bool Overlay::PointHitsVisual(POINT client) const {
    if (!settings_) return false;
    const float scale =
        (dpi_ ? dpi_ : 96) / 96.0f * OverlayUserScale(*settings_);
    const float cx = width_ * 0.5f;
    const float cy = height_ * 0.5f;
    const float x = static_cast<float>(client.x);
    const float y = static_cast<float>(client.y);
    const float dx = x - cx;
    const float dy = y - cy;
    const float distance = std::sqrt(dx * dx + dy * dy);

    if (settings_->outer.enabled && settings_->outer.opacity > 0) {
        float radius = (62.0f + 1.38f * settings_->outer.size) * scale;
        float range = (35.0f + 0.28f * settings_->outer.amplitude) * scale;
        if (std::fabs(distance - radius) <= range) return true;
    }
    if (settings_->dots.enabled && settings_->dots.opacity > 0) {
        float radius = (48.0f + 1.03f * settings_->dots.size) * scale;
        float range = (26.0f + 0.18f * settings_->dots.amplitude) * scale;
        if (std::fabs(distance - radius) <= range) return true;
    }
    if (settings_->wave.enabled && settings_->wave.opacity > 0) {
        float halfWidth = (55.0f + 1.50f * settings_->wave.size) * scale;
        float heightRange =
            (18.0f + 0.23f * settings_->wave.amplitude +
             1.8f * settings_->wave.thickness / 100.0f) *
            scale;
        if (x >= cx - halfWidth - 8 * scale && x <= cx + halfWidth + 8 * scale &&
            std::fabs(dy) <= heightRange)
            return true;
    }
    return false;
}

void Overlay::BeginDrag(POINT screen) {
    if (!hwnd_ || !interactive_ || dragging_) return;
    dragging_ = true;
    dragStartCursor_ = screen;
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    dragStartWindow_ = POINT{rect.left, rect.top};
    SetCapture(hwnd_);
}

void Overlay::DragTo(POINT screen) {
    if (!dragging_) return;
    int nx = dragStartWindow_.x + (screen.x - dragStartCursor_.x);
    int ny = dragStartWindow_.y + (screen.y - dragStartCursor_.y);
    RECT desired{nx, ny, nx + width_, ny + height_};
    RECT clamped = ClampedRect(desired);
    SetWindowPos(hwnd_, HWND_TOPMOST, clamped.left, clamped.top, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void Overlay::EndDrag() {
    if (!dragging_) return;
    dragging_ = false;
    if (GetCapture() == hwnd_) ReleaseCapture();
    SavePositionToSettings();
}

void Overlay::SavePositionToSettings() {
    if (!hwnd_ || !settings_) return;
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    settings_->overlayX = rect.left;
    settings_->overlayY = rect.top;
    if (onChanged_) onChanged_();
}

RECT Overlay::ClampedRect(RECT desired) {
    POINT center{desired.left + (desired.right - desired.left) / 2,
                 desired.top + (desired.bottom - desired.top) / 2};
    RECT work = gfx::WorkAreaForPoint(center);
    int width = desired.right - desired.left;
    int height = desired.bottom - desired.top;
    int x = desired.left;
    int y = desired.top;
    if (x + width > work.right) x = work.right - width;
    if (y + height > work.bottom) y = work.bottom - height;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    return RECT{x, y, x + width, y + height};
}

void Overlay::ApplyEdgeSnap(bool savePosition) {
    if (!hwnd_) return;
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    RECT clamped = ClampedRect(rect);
    if (clamped.left != rect.left || clamped.top != rect.top) {
        SetWindowPos(hwnd_, HWND_TOPMOST, clamped.left, clamped.top, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    }
    if (savePosition) SavePositionToSettings();
}

void Overlay::ClampWindowPosition(bool savePosition) {
    ApplyEdgeSnap(savePosition);
}

void Overlay::RecenterOnPrimary() {
    if (!hwnd_) return;
    POINT point{0, 0};
    RECT work = gfx::WorkAreaForPoint(point);
    int x = work.left + ((work.right - work.left) - width_) / 2;
    int y = work.top + ((work.bottom - work.top) - height_) / 2;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    SavePositionToSettings();
}

POINT Overlay::Position() const {
    if (!hwnd_) return POINT{0, 0};
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    return POINT{rect.left, rect.top};
}

void Overlay::ResetAnalysis() {
    analyzer_.Reset();
    lastAudioTotal_ = UINT64_MAX;
}

LRESULT CALLBACK Overlay::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Overlay* self = reinterpret_cast<Overlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = reinterpret_cast<Overlay*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = hwnd;
    }
    if (self) return self->WndProc(message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Overlay::WndProc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_NCHITTEST: {
            if (!interactive_) return HTTRANSPARENT;
            POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT client = screen;
            ScreenToClient(hwnd_, &client);
            RECT rc{};
            GetClientRect(hwnd_, &rc);
            if (client.x < 0 || client.y < 0 || client.x >= rc.right || client.y >= rc.bottom)
                return HTTRANSPARENT;
            return PointHitsVisual(client) ? HTCLIENT : HTTRANSPARENT;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(hwnd_, &ps);
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!interactive_) return 0;
            POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(hwnd_, &screen);
            POINT client{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (!PointHitsVisual(client)) return 0;
            BeginDrag(screen);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (dragging_) {
                POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(hwnd_, &screen);
                DragTo(screen);
            }
            return 0;
        case WM_LBUTTONUP:
            if (dragging_) EndDrag();
            return 0;
        case WM_CAPTURECHANGED:
            if (dragging_) {
                dragging_ = false;
                SavePositionToSettings();
            }
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && interactive_) {
                SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32646)));
                return TRUE;
            }
            break;
        case WM_DPICHANGED: {
            UINT newDpi = LOWORD(wParam);
            RECT oldRect{};
            GetWindowRect(hwnd_, &oldRect);
            int oldWidth = oldRect.right - oldRect.left;
            int oldHeight = oldRect.bottom - oldRect.top;
            dpi_ = newDpi ? newDpi : 96;
            int newSize = DesiredContentSize(dpi_);
            if (newSize <= 0) newSize = width_;
            int centerX = oldRect.left + oldWidth / 2;
            int centerY = oldRect.top + oldHeight / 2;
            RECT desired{centerX - newSize / 2, centerY - newSize / 2,
                         centerX - newSize / 2 + newSize, centerY - newSize / 2 + newSize};
            RECT clamped = ClampedRect(desired);
            SetWindowPos(hwnd_, HWND_TOPMOST, clamped.left, clamped.top, newSize, newSize,
                         SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            Resize(static_cast<UINT>(newSize), static_cast<UINT>(newSize));
            ApplyEdgeSnap(false);
            SavePositionToSettings();
            contentSizeDirty_ = false;
            return 0;
        }
        case WM_DISPLAYCHANGE:
            ApplyEdgeSnap(true);
            return 0;
        case WM_SIZE: {
            UINT w = LOWORD(lParam);
            UINT h = HIWORD(lParam);
            if (graphicsReady_ && w > 0 && h > 0) Resize(w, h);
            return 0;
        }
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}
