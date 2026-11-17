#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <string>
#include <vector>

#include "audio.h"
#include "gfx.h"
#include "settings.h"

class ControlPanel {
public:
    ControlPanel();
    ~ControlPanel();

    ControlPanel(const ControlPanel&) = delete;
    ControlPanel& operator=(const ControlPanel&) = delete;

    bool Create(HINSTANCE instance, VisualSettings* settings, AudioEngine* audio);
    void Destroy();

    HWND Hwnd() const { return hwnd_; }
    bool Valid() const { return valid_; }
    const std::wstring& LastError() const { return lastError_; }

    void Show();
    void Tick();
    void Invalidate();
    void ReassertTopmost();

    void SetOnChanged(std::function<void()> callback) { onChanged_ = std::move(callback); }
    void SetOnExit(std::function<void()> callback) { onExit_ = std::move(callback); }
    void SetOnRecenter(std::function<void()> callback) { onRecenter_ = std::move(callback); }

private:
    enum class Page {
        Outer = 0,
        Dots = 1,
        Wave = 2,
        Shards = 3,
        Glow = 4,
        Ambient = 5,
        Color = 6,
        Appearance = 7,
        Source = 8,
    };

    enum class HitKind {
        None,
        Header,
        Close,
        Minimize,
        Source,
        Pin,
        Tab,
        Toggle,
        ColorField,
        Slider,
        Value,
        PickerBack,
        PickerClose,
        PickerRefresh,
        PickerSystem,
        PickerApp,
        Preset,
        ShardTemplate,
        Recenter,
        Reset,
        ColorEnable,
        ColorPreset,
        ColorBehavior,
        OuterMode,
        AmbientEnable,
        AmbientFeature,
    };

    struct FieldRef {
        int group = 0;  // 0..2 effect, 3 panel, 4 shards, 5 glow, 6 color roles, 7 dynamics, 8 ambient
        int field = 0;  // 0..2 color; 3..7 effect/global values
        bool operator==(const FieldRef& other) const {
            return group == other.group && field == other.field;
        }
        bool operator!=(const FieldRef& other) const { return !(*this == other); }
    };

    struct Hit {
        HitKind kind = HitKind::None;
        int index = -1;
        int field = -1;
        D2D1_RECT_F rect{};
    };

    struct Theme {
        bool light = false;
        gfx::Color background;
        gfx::Color text;
        gfx::Color muted;
        gfx::Color faint;
        gfx::Color surface;
        gfx::Color surfaceHover;
        gfx::Color border;
        gfx::Color accent;
        gfx::Color accentSoft;
        gfx::Color accentText;
        gfx::Color danger;
        gfx::Color success;
    };

    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam);

    bool CreateWindowInternal(HINSTANCE instance);
    bool InitGraphics();
    void ReleaseGraphics();
    void RecreateSurface();
    void ComputeWindowScale();
    void ApplyWindowPosition();

    void Render();
    void RenderSourcePage(const Theme& theme);
    void RenderBackground(const Theme& theme);
    void RenderHeader(const Theme& theme);
    void RenderSourceCard(const Theme& theme);
    void RenderPinCard(const Theme& theme);
    void RenderTabs(const Theme& theme);
    void RenderEffectPage(const Theme& theme, int effectIndex);
    void RenderShardsPage(const Theme& theme);
    void RenderGlowPage(const Theme& theme);
    void RenderAmbientPage(const Theme& theme);
    void RenderColorPage(const Theme& theme);
    void RenderAppearancePage(const Theme& theme);
    void RenderFooter(const Theme& theme, int effectIndex);
    void DrawToggle(const Theme& theme, D2D1_RECT_F rect, bool on, bool hovered);
    void DrawColorSection(const Theme& theme, int group, const wchar_t* label, float y);
    void DrawColorField(const Theme& theme, int group, int channel, D2D1_RECT_F rect);
    void DrawColorRoleRow(const Theme& theme, int baseField, const wchar_t* label, float y);
    void DrawSliderRow(const Theme& theme, int group, int field, const wchar_t* label, float y);
    void DrawValuePill(const Theme& theme, int group, int field, D2D1_RECT_F rect);
    void DrawWindowButtons(const Theme& theme);
    void DrawSourceRow(const Theme& theme, const AudioApp& app, int index, D2D1_RECT_F rect);
    void DrawScrollbar(const Theme& theme, float x, float y, float height);

    void AddHit(HitKind kind, int index, int field, D2D1_RECT_F rect);
    const Hit* HitTest(POINT client) const;
    bool IsHovered(HitKind kind, int index, int field) const;
    void SetHover(const Hit& hit);
    void MarkDirty();

    void HandleMouseDown(POINT client, bool doubleClick);
    void HandleMouseUp();
    void HandleMouseMove(POINT client);
    void HandleMouseWheel(POINT client, int delta);
    void HandleKeyDown(WPARAM key);
    void HandleChar(wchar_t character);
    void HandleMouseLeave();

    void StartWindowDrag(POINT screen);
    void DragWindow(POINT screen);
    void EndWindowDrag();
    void UpdateSlider(POINT client);
    void EndSlider();

    void OpenSourcePage();
    void CloseSourcePage();
    void RefreshApps();
    void SelectApp(int index);
    void SelectSystem();
    void SelectTab(int index);
    void ToggleEffect(int index);
    void TogglePin();
    void ToggleDynamicColors();
    void ApplyColorPreset(int index);
    void ApplyPreset(int index);
    void ResetVisuals();
    void RefreshStatus();

    FieldRef EffectField(int effect, int field) const;
    EffectConfig* ConfigForGroup(int group);
    const EffectConfig* ConfigForGroup(int group) const;
    int GetFieldValue(const FieldRef& ref) const;
    void SetFieldValue(const FieldRef& ref, int value);
    int MinValue(const FieldRef& ref) const;
    int MaxValue(const FieldRef& ref) const;
    bool IsPercentField(const FieldRef& ref) const;
    std::wstring FormatFieldValue(const FieldRef& ref) const;
    std::wstring FormatFieldValueForEdit(const FieldRef& ref) const;
    void BeginEdit(const FieldRef& ref);
    void CancelEdit();
    void CommitEdit();
    void ApplyEditText();
    bool IsEditing(const FieldRef& ref) const;

    Theme BuildTheme() const;
    std::wstring SourceTitle() const;
    std::wstring StatusSubText() const;
    std::wstring TruncateToWidth(const std::wstring& text, float maxWidth, float size,
                                 DWRITE_FONT_WEIGHT weight) const;
    bool IsCurrentSource(const AudioApp& app) const;
    int CurrentEffect() const;

    void NotifyChanged(bool saveImmediately = false);
    void SaveWindowPosition();

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    VisualSettings* settings_ = nullptr;
    AudioEngine* audio_ = nullptr;
    bool valid_ = false;
    bool dirty_ = true;
    bool windowDragging_ = false;
    bool sliderDragging_ = false;
    Page page_ = Page::Outer;
    Page lastMainPage_ = Page::Outer;

    int width_ = 0;
    int height_ = 0;
    float scale_ = 1.0f;
    UINT dpi_ = 96;

    POINT dragStartCursor_{};
    POINT dragStartWindow_{};
    FieldRef editField_{};
    FieldRef sliderField_{};
    bool editActive_ = false;
    bool editReplaceOnType_ = false;
    int editInitialValue_ = 0;
    std::wstring editText_;
    Hit hover_{};
    D2D1_RECT_F activeSliderRect_{};

    std::vector<AudioApp> apps_;
    int appScroll_ = 0;
    std::wstring lastStatus_;
    LARGE_INTEGER lastPanelTick_{};

    HDC memoryDc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    void* bitmapBits_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;

    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<ID2D1DCRenderTarget> renderTarget_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteFactory> writeFactory_;
    ComPtr<ID2D1StrokeStyle> strokeStyle_;

    std::vector<Hit> hits_;
    std::function<void()> onChanged_;
    std::function<void()> onExit_;
    std::function<void()> onRecenter_;
    std::wstring lastError_;
};
