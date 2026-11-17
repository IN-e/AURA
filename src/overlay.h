#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d2d1_1.h>
#include <dcomp.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "analyzer.h"
#include "audio.h"
#include "gfx.h"
#include "settings.h"

class Overlay {
public:
    Overlay();
    ~Overlay();

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    bool Create(HINSTANCE instance, VisualSettings* settings, AudioEngine* audio);
    void Destroy();

    HWND Hwnd() const { return hwnd_; }
    bool Valid() const { return graphicsReady_; }
    const std::wstring& LastError() const { return lastError_; }

    void OnSettingsChanged();
    void RefreshVisibility();
    void SetInteractive(bool interactive);
    void ReassertTopmost();
    bool IsVisible() const { return visible_; }

    void Tick();
    void RenderOnce();

    void ResetAnalysis();
    void ApplyEdgeSnap(bool savePosition);
    void RecenterOnPrimary();
    POINT Position() const;
    int Width() const { return width_; }

    void SetOnChanged(std::function<void()> callback) { onChanged_ = std::move(callback); }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateWindowInternal(HINSTANCE instance);
    bool InitGraphics();
    void ReleaseGraphics();
    bool CreateSwapChain(UINT width, UINT height);
    bool CreateSceneTarget();
    void ReleaseSceneTarget();
    void UpdateBloomParameters();
    bool EnsureTargetBitmap();
    void Resize(UINT width, UINT height);
    int DesiredContentSize(UINT dpi) const;
    void ApplyContentSize(bool preserveCenter);
    void RenderFrame(float dt);
    void DrawVisuals(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, float width,
                     float height, float scale, float time);
    void UpdateAnalysis(float dt);
    void UpdateDynamicColors(float dt);
    gfx::Color DynamicOuterColor(float position) const;
    bool PointHitsVisual(POINT client) const;
    void UpdateHitRegion();
    void BeginDrag(POINT screen);
    void DragTo(POINT screen);
    void EndDrag();
    void ClampWindowPosition(bool savePosition);
    RECT ClampedRect(RECT desired);
    void SavePositionToSettings();

    struct VisualPath {
        ComPtr<ID2D1PathGeometry> geometry;
        bool valid = false;
    };

    struct DynamicPalette {
        gfx::Color primary{};
        gfx::Color secondary{};
        gfx::Color accent{};
        gfx::Color glow{};
    };

    VisualPath BuildRingPath(const std::vector<D2D1_POINT_2F>& points,
                             bool closed = true) const;
    VisualPath BuildMeshPath(const std::vector<D2D1_POINT_2F>& outer,
                             const std::vector<D2D1_POINT_2F>& inner) const;
    VisualPath BuildWebMeshPath(const std::vector<D2D1_POINT_2F>& inner,
                                const std::vector<D2D1_POINT_2F>& outer) const;
    std::vector<VisualPath> BuildWebMeshLayers(const std::vector<D2D1_POINT_2F>& inner,
                                               const std::vector<D2D1_POINT_2F>& outer,
                                               int layerCount) const;
    void DrawGlowGeometry(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
                          ID2D1PathGeometry* geometry, const gfx::Color& color, float opacity,
                          float coreWidth);
    void DrawDot(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, float x, float y,
                 float radius, const gfx::Color& color, float opacity);
    bool CreateGlowTarget();
    void ReleaseGlowTarget();
    bool RenderGlowLayer();
    bool EnsureShardGeometries();
    void UpdateShards(float dt);
    void DrawShards(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, float cx, float cy,
                    float scale);
    float NextShardRandom();

    struct ShardParticle {
        float angle = 0.0f;
        float radius = 0.0f;
        float speed = 0.0f;
        float angularDrift = 0.0f;
        float age = 0.0f;
        float lifetime = 1.0f;
        float sizeVariation = 1.0f;
        float rotation = 0.0f;
        float spin = 0.0f;
        int shape = 0;
    };

    struct AmbientParticle {
        float angle = 0.0f;
        float radius = 0.0f;
        float size = 0.0f;
        float phase = 0.0f;
        float alpha = 0.0f;
    };

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    VisualSettings* settings_ = nullptr;
    AudioEngine* audio_ = nullptr;

    bool graphicsReady_ = false;
    bool visible_ = false;
    bool interactive_ = false;
    bool dragging_ = false;
    POINT dragStartCursor_{};
    POINT dragStartWindow_{};
    int width_ = 0;
    int height_ = 0;
    UINT dpi_ = 96;
    bool contentSizeDirty_ = false;
    LARGE_INTEGER contentSizeChanged_{};

    ComPtr<ID3D11Device> d3dDevice_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    ComPtr<IDXGISwapChain1> swapChain_;
    ComPtr<ID2D1Factory1> d2dFactory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<ID2D1DeviceContext> d2dContext_;
    ComPtr<IDCompositionDevice> compositionDevice_;
    ComPtr<IDCompositionTarget> compositionTarget_;
    ComPtr<IDCompositionVisual> compositionVisual_;
    ComPtr<ID2D1Bitmap1> targetBitmap_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<ID2D1StrokeStyle> strokeStyle_;

    ComPtr<ID2D1DeviceContext> sceneContext_;
    ComPtr<ID2D1SolidColorBrush> sceneBrush_;
    ComPtr<ID2D1Bitmap1> sceneBitmap_;
    ComPtr<ID2D1Effect> bloomBlurNear_;
    ComPtr<ID2D1Effect> bloomBlurFar_;
    ComPtr<ID2D1Effect> bloomColorNear_;
    ComPtr<ID2D1Effect> bloomColorFar_;
    ComPtr<ID2D1Bitmap1> glowBitmap_;
    ComPtr<ID2D1Bitmap1> glowResultBitmap_;
    ComPtr<ID2D1DeviceContext> glowContext_;
    ComPtr<ID2D1RadialGradientBrush> glowBrush_;
    ComPtr<ID2D1Effect> glowBlur_;
    bool glowDirty_ = true;
    bool glowReady_ = false;
    std::array<ComPtr<ID2D1PathGeometry>, 4> shardGeometries_;
    std::vector<ShardParticle> shards_;
    float shardAccumulator_ = 0.0f;
    float shardAngleCursor_ = 0.0f;
    uint32_t shardRngState_ = 0x9e3779b9u;
    bool bloomReady_ = false;

    DynamicPalette dynamicStart_{};
    DynamicPalette dynamicTarget_{};
    DynamicPalette dynamicCurrent_{};
    DynamicPalette dynamicLastTarget_{};
    DynamicPalette dynamicRender_{};
    std::vector<float> rhythmEnvelope_;
    std::vector<float> rhythmSlowEnvelope_;
    std::vector<float> rhythmFastEnvelope_;
    float bassSlowEnvelope_ = 0.0f;
    float bassFastEnvelope_ = 0.0f;
    bool rhythmBassOnsetInitialized_ = false;
    std::vector<float> ambientSlowEnvelope_;
    std::vector<AmbientParticle> ambientParticles_;
    gfx::Color ambientNebulaColor_{};
    bool ambientNebulaColorInitialized_ = false;
    float ambientEnergy_ = 0.0f;
    float waveEnvelope_ = 0.0f;
    float ambientRotation_ = 0.0f;
    float ambientRotationSpeed_ = 0.0f;
    float lastAmbientTime_ = -1.0f;
    bool ambientParticlesReady_ = false;
    float visualPulse_ = 1.0f;
    bool dynamicInitialized_ = false;
    float dynamicProgress_ = 1.0f;
    gfx::Color lastGlowCacheColor_{};
    float lastGlowCacheTime_ = -10.0f;

    AudioAnalyzer analyzer_;
    std::vector<float> audioSnapshot_;
    uint64_t lastAudioTotal_ = UINT64_MAX;
    int lastSampleRate_ = 0;
    LARGE_INTEGER frequency_{};
    LARGE_INTEGER lastTick_{};
    float elapsed_ = 0.0f;
    std::wstring lastError_;
    std::function<void()> onChanged_;
};
