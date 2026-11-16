#include "panel.h"

#include <d2d1helper.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace {
constexpr float kWidth = 560.0f;
constexpr float kHeight = 820.0f;
constexpr float kMargin = 28.0f;
constexpr float kContentWidth = 504.0f;
constexpr int kListTop = 212;
constexpr int kRowHeight = 54;
constexpr int kRowGap = 6;
constexpr int kVisibleRows = 8;

D2D1_RECT_F R(float x, float y, float w, float h) {
    return D2D1::RectF(x, y, x + w, y + h);
}

void FillRound(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const gfx::Color& color,
               D2D1_RECT_F rect, float radius) {
    if (!target || !brush) return;
    brush->SetColor(color.D2D());
    target->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush);
}

void StrokeRound(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const gfx::Color& color,
                 D2D1_RECT_F rect, float radius, float width) {
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

void Line(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush, const gfx::Color& color,
          float x1, float y1, float x2, float y2, float width) {
    if (!target || !brush) return;
    brush->SetColor(color.D2D());
    target->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush, width);
}

void Text(ID2D1RenderTarget* target, IDWriteFactory* factory, ID2D1SolidColorBrush* brush,
          const std::wstring& text, float size, DWRITE_FONT_WEIGHT weight, const gfx::Color& color,
          D2D1_RECT_F rect,
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

void DrawChevronLeft(ID2D1RenderTarget* target, ID2D1SolidColorBrush* brush,
                     const gfx::Color& color, float cx, float cy, float scale) {
    brush->SetColor(color.D2D());
    float size = 4.0f * scale;
    target->DrawLine(D2D1::Point2F(cx + size * 0.35f, cy - size),
                     D2D1::Point2F(cx - size * 0.55f, cy), brush, 1.6f * scale);
    target->DrawLine(D2D1::Point2F(cx - size * 0.55f, cy),
                     D2D1::Point2F(cx + size * 0.35f, cy + size), brush, 1.6f * scale);
}

}  // namespace

void ControlPanel::RenderBackground(const Theme& theme) {
    FillRound(renderTarget_.Get(), brush_.Get(), theme.background, R(0, 0, kWidth, kHeight),
              26.0f);
}

void ControlPanel::RenderHeader(const Theme& theme) {
    AddHit(HitKind::Header, -1, -1, R(0, 0, kWidth, 88));

    D2D1_RECT_F logo = R(28, 26, 40, 40);
    FillRound(renderTarget_.Get(), brush_.Get(), theme.accentSoft, logo, 12);
    StrokeRound(renderTarget_.Get(), brush_.Get(), theme.accent.WithAlpha(65), logo, 12, 1.0f);
    float lx = 48, ly = 46;
    StrokeCircle(renderTarget_.Get(), brush_.Get(), theme.accent, lx, ly, 10.0f, 1.25f);
    StrokeCircle(renderTarget_.Get(), brush_.Get(), theme.accent.WithAlpha(190), lx, ly, 5.2f,
                 1.0f);
    FillCircle(renderTarget_.Get(), brush_.Get(), theme.accent, lx, ly - 10.0f, 1.25f);

    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"AURA", 21.0f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(82, 20, 220, 30));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         L"声场可视化  ·  DESKTOP AUDIO", 8.8f, DWRITE_FONT_WEIGHT_NORMAL, theme.muted,
         R(82, 49, 300, 16));

    DrawWindowButtons(theme);
}

void ControlPanel::DrawWindowButtons(const Theme& theme) {
    const D2D1_RECT_F minimize = R(466, 26, 30, 30);
    const D2D1_RECT_F close = R(502, 26, 30, 30);
    bool minHover = IsHovered(HitKind::Minimize, -1, -1);
    bool closeHover = IsHovered(HitKind::Close, -1, -1);
    if (minHover)
        FillCircle(renderTarget_.Get(), brush_.Get(), theme.surfaceHover, 481, 41, 15);
    if (closeHover)
        FillCircle(renderTarget_.Get(), brush_.Get(), theme.danger.WithAlpha(40), 517, 41, 15);
    gfx::Color icon = minHover ? theme.text : theme.muted;
    Line(renderTarget_.Get(), brush_.Get(), icon, 476, 41, 486, 41, 1.4f);
    gfx::Color closeIcon = closeHover ? theme.danger : theme.muted;
    Line(renderTarget_.Get(), brush_.Get(), closeIcon, 513, 37, 521, 45, 1.4f);
    Line(renderTarget_.Get(), brush_.Get(), closeIcon, 521, 37, 513, 45, 1.4f);
    AddHit(HitKind::Minimize, -1, -1, minimize);
    AddHit(HitKind::Close, -1, -1, close);
}

void ControlPanel::RenderSourceCard(const Theme& theme) {
    AddHit(HitKind::Source, -1, -1, R(436, 116, 78, 36));
    D2D1_RECT_F card = R(kMargin, 94, kContentWidth, 82);
    FillRound(renderTarget_.Get(), brush_.Get(), theme.surface, card, 18);
    StrokeRound(renderTarget_.Get(), brush_.Get(), theme.border, card, 18, 1.0f);

    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"音频来源", 8.8f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(48, 103, 200, 14));
    std::wstring title = TruncateToWidth(SourceTitle(), 350.0f, 14.8f,
                                         DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), title, 14.8f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(48, 117, 368, 26));

    CaptureState state = audio_ ? audio_->State() : CaptureState::Idle;
    gfx::Color dotColor = theme.muted;
    if (settings_ && settings_->sourceKind == SourceKind::None) dotColor = theme.muted;
    else if (state == CaptureState::Running) dotColor = theme.success;
    else if (state == CaptureState::Connecting) dotColor = theme.accent;
    else if (state == CaptureState::Error) dotColor = theme.danger;
    FillCircle(renderTarget_.Get(), brush_.Get(), dotColor.WithAlpha(55), 50.0f, 152.0f, 5.5f);
    FillCircle(renderTarget_.Get(), brush_.Get(), dotColor, 50.0f, 152.0f, 2.4f);

    gfx::Color statusColor = theme.muted;
    if (state == CaptureState::Error) statusColor = theme.danger;
    else if (state == CaptureState::Running) statusColor = theme.muted;
    std::wstring status = TruncateToWidth(StatusSubText(), 364.0f, 9.0f,
                                          DWRITE_FONT_WEIGHT_NORMAL);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), status, 9.0f,
         DWRITE_FONT_WEIGHT_NORMAL, statusColor, R(59, 143, 362, 18));

    const D2D1_RECT_F button = R(436, 116, 78, 36);
    bool hovered = IsHovered(HitKind::Source, -1, -1);
    FillRound(renderTarget_.Get(), brush_.Get(),
              hovered ? theme.accent.WithAlpha(48) : theme.accentSoft, button, 11);
    StrokeRound(renderTarget_.Get(), brush_.Get(), theme.accent.WithAlpha(hovered ? 120 : 62),
                button, 11, 1.0f);
    std::wstring label = (settings_ && settings_->sourceKind != SourceKind::None) ? L"更换  ›" : L"选择  ›";
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), label, 9.8f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.accent, R(444, 116, 62, 36),
         DWRITE_TEXT_ALIGNMENT_CENTER);
}

void ControlPanel::RenderPinCard(const Theme& theme) {
    D2D1_RECT_F card = R(kMargin, 188, kContentWidth, 64);
    FillRound(renderTarget_.Get(), brush_.Get(), theme.surface, card, 16);
    StrokeRound(renderTarget_.Get(), brush_.Get(), theme.border, card, 16, 1.0f);

    FillRound(renderTarget_.Get(), brush_.Get(), theme.accentSoft, R(44, 201, 34, 34), 10);
    bool locked = settings_ && settings_->overlayFixed;
    gfx::Color iconColor = locked ? theme.accent : theme.muted;
    if (locked) {
        StrokeCircle(renderTarget_.Get(), brush_.Get(), iconColor, 61, 214.5f, 5.4f, 1.3f);
        FillRound(renderTarget_.Get(), brush_.Get(), iconColor, R(53, 215, 16, 10.5f), 2.5f);
    } else {
        // Open shackle plus body.
        Line(renderTarget_.Get(), brush_.Get(), iconColor, 56.5f, 216.0f, 56.5f, 211.5f, 1.25f);
        Line(renderTarget_.Get(), brush_.Get(), iconColor, 56.5f, 211.5f, 60.5f, 209.5f, 1.25f);
        Line(renderTarget_.Get(), brush_.Get(), iconColor, 60.5f, 209.5f, 64.5f, 211.5f, 1.25f);
        Line(renderTarget_.Get(), brush_.Get(), iconColor, 64.5f, 211.5f, 64.5f, 213.0f, 1.25f);
        FillRound(renderTarget_.Get(), brush_.Get(), iconColor, R(53, 215, 16, 10.5f), 2.5f);
    }
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"固定浮窗", 12.2f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(86, 197, 300, 21));
    std::wstring hint = locked ? L"已固定 · 鼠标与输入设备将穿透浮窗"
                               : L"未固定 · 拖动声环可调整位置";
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), hint, 8.8f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(86, 217, 360, 18));

    D2D1_RECT_F toggle = R(462, 206, 46, 26);
    AddHit(HitKind::Pin, -1, -1, R(454, 198, 62, 42));
    DrawToggle(theme, toggle, locked, IsHovered(HitKind::Pin, -1, -1));
}

void ControlPanel::DrawToggle(const Theme& theme, D2D1_RECT_F rect, bool on, bool hovered) {
    float radius = (rect.bottom - rect.top) * 0.5f;
    gfx::Color track = on ? theme.accent : theme.surfaceHover;
    if (hovered) {
        if (on) track = theme.accent.Lightened(0.06f);
        else track = theme.surfaceHover.Lightened(0.05f);
    }
    FillRound(renderTarget_.Get(), brush_.Get(), track, rect, radius);
    if (!on) {
        StrokeRound(renderTarget_.Get(), brush_.Get(), theme.border, rect, radius, 1.0f);
    }
    float knobRadius = radius - 3.5f;
    float knobX = on ? rect.right - 3.5f - knobRadius : rect.left + 3.5f + knobRadius;
    float knobY = (rect.top + rect.bottom) * 0.5f;
    FillCircle(renderTarget_.Get(), brush_.Get(),
               on ? gfx::Rgb(255, 255, 255, 248) : gfx::Rgb(theme.muted.r, theme.muted.g,
                                                               theme.muted.b, 220),
               knobX, knobY, knobRadius);
}

void ControlPanel::RenderTabs(const Theme& theme) {
    D2D1_RECT_F tray = R(kMargin, 266, kContentWidth, 42);
    FillRound(renderTarget_.Get(), brush_.Get(), theme.surface, tray, 13);
    StrokeRound(renderTarget_.Get(), brush_.Get(), theme.border, tray, 13, 1.0f);
    const wchar_t* labels[] = {L"声环", L"点阵", L"横波", L"碎片", L"底光", L"外观"};
    int active = CurrentEffect();
    if (page_ == Page::Shards) active = 3;
    else if (page_ == Page::Glow) active = 4;
    else if (page_ == Page::Appearance) active = 5;
    const float tabWidth = (kContentWidth - 6.0f) / 6.0f;
    for (int i = 0; i < 6; ++i) {
        D2D1_RECT_F tab = R(kMargin + 3.0f + i * tabWidth, 269, tabWidth, 36);
        bool hovered = IsHovered(HitKind::Tab, i, -1);
        if (i == active) {
            FillRound(renderTarget_.Get(), brush_.Get(), theme.accent, tab, 10);
        } else if (hovered) {
            FillRound(renderTarget_.Get(), brush_.Get(), theme.surfaceHover, tab, 10);
        }
        Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), labels[i], 10.7f,
             i == active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
             i == active ? theme.accentText : theme.muted, tab,
             DWRITE_TEXT_ALIGNMENT_CENTER);
        AddHit(HitKind::Tab, i, -1, tab);
    }
}

void ControlPanel::DrawColorSection(const Theme& theme, int group, const wchar_t* label,
                                    float y) {
    gfx::Color color;
    if (group == 3) color = gfx::Rgb(settings_->panelR, settings_->panelG, settings_->panelB);
    else if (group == 4) color = gfx::Rgb(settings_->shards.r, settings_->shards.g, settings_->shards.b);
    else if (group == 5) color = gfx::Rgb(settings_->glow.r, settings_->glow.g, settings_->glow.b);
    else {
        const EffectConfig* config = ConfigForGroup(group);
        color = gfx::Rgb(config->r, config->g, config->b);
    }
    D2D1_SIZE_F labelSize =
        gfx::MeasureText(writeFactory_.Get(), label, 11.2f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), label, 11.2f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text,
         R(30, y + 1, labelSize.width + 8, 34));
    float swatchX = 30 + labelSize.width + 13;
    FillRound(renderTarget_.Get(), brush_.Get(), color, R(swatchX, y + 8, 18, 18), 6);
    StrokeRound(renderTarget_.Get(), brush_.Get(), theme.border, R(swatchX, y + 8, 18, 18), 6,
                1.0f);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"RGB · 0–255", 8.5f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted,
         R(swatchX + 26, y + 1, 160, 34));

    DrawColorField(theme, group, 0, R(346, y, 58, 36));
    DrawColorField(theme, group, 1, R(410, y, 58, 36));
    DrawColorField(theme, group, 2, R(474, y, 58, 36));
}

void ControlPanel::DrawColorField(const Theme& theme, int group, int channel,
                                  D2D1_RECT_F rect) {
    FieldRef ref{group, channel};
    bool hovered = IsHovered(HitKind::ColorField, group, channel);
    bool editing = IsEditing(ref);
    FillRound(renderTarget_.Get(), brush_.Get(),
              editing ? theme.accentSoft : hovered ? theme.surfaceHover : theme.surface, rect,
              10);
    StrokeRound(renderTarget_.Get(), brush_.Get(),
                (hovered || editing) ? theme.accent.WithAlpha(150) : theme.border, rect, 10,
                editing ? 1.3f : 1.0f);
    const wchar_t* letters[] = {L"R", L"G", L"B"};
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), letters[channel], 7.8f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(rect.left + 9, rect.top + 3, 12, 11));
    std::wstring value =
        editing ? editText_ : gfx::FormatInt(GetFieldValue(ref));
    if (editing && value.empty()) value = L" ";
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), value, 11.6f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text,
         R(rect.left + 9, rect.top + 14, rect.right - rect.left - 16, 20));
    if (editing) {
        D2D1_SIZE_F measured =
            gfx::MeasureText(writeFactory_.Get(), value, 11.6f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        float caretX = std::min(rect.right - 8, rect.left + 10 + measured.width + 1);
        Line(renderTarget_.Get(), brush_.Get(), theme.accent, caretX, rect.top + 15, caretX,
             rect.top + 30, 1.0f);
    }
    AddHit(HitKind::ColorField, group, channel, rect);
}

void ControlPanel::DrawValuePill(const Theme& theme, int group, int field,
                                 D2D1_RECT_F rect) {
    FieldRef ref{group, field};
    bool hovered = IsHovered(HitKind::Value, group, field);
    bool editing = IsEditing(ref);
    FillRound(renderTarget_.Get(), brush_.Get(),
              editing ? theme.accentSoft : hovered ? theme.surfaceHover : theme.surface, rect,
              10);
    StrokeRound(renderTarget_.Get(), brush_.Get(),
                (hovered || editing) ? theme.accent.WithAlpha(150) : theme.border, rect, 10,
                editing ? 1.3f : 1.0f);
    std::wstring value = editing ? editText_ : FormatFieldValue(ref);
    if (editing && value.empty()) value = L" ";
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), value, 10.8f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, rect, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (editing) {
        D2D1_SIZE_F measured =
            gfx::MeasureText(writeFactory_.Get(), value, 10.8f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        float caretX = (rect.left + rect.right) * 0.5f + measured.width * 0.5f + 2.0f;
        Line(renderTarget_.Get(), brush_.Get(), theme.accent, caretX, rect.top + 10, caretX,
             rect.bottom - 10, 1.0f);
    }
    AddHit(HitKind::Value, group, field, rect);
}

void ControlPanel::DrawSliderRow(const Theme& theme, int group, int field, const wchar_t* label,
                                 float y) {
    FieldRef ref{group, field};
    int value = GetFieldValue(ref);
    int low = MinValue(ref);
    int high = MaxValue(ref);
    float progress = high > low ? (value - low) / static_cast<float>(high - low) : 0.0f;
    progress = std::clamp(progress, 0.0f, 1.0f);

    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), label, 11.0f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.text, R(30, y + 8, 110, 24));

    const float trackX = 152.0f;
    const float trackW = 276.0f;
    const float centerY = y + 28.0f;
    D2D1_RECT_F trackBg = R(trackX, centerY - 2.5f, trackW, 5);
    FillRound(renderTarget_.Get(), brush_.Get(), theme.surfaceHover, trackBg, 2.5f);
    if (progress > 0.002f) {
        FillRound(renderTarget_.Get(), brush_.Get(), theme.accent,
                  R(trackX, centerY - 2.5f, trackW * progress, 5), 2.5f);
    }
    float thumbX = trackX + trackW * progress;
    FillCircle(renderTarget_.Get(), brush_.Get(), theme.accent.WithAlpha(45), thumbX, centerY,
               10.0f);
    FillCircle(renderTarget_.Get(), brush_.Get(), theme.accent, thumbX, centerY, 7.0f);
    FillCircle(renderTarget_.Get(), brush_.Get(), theme.accentText.WithAlpha(225), thumbX,
               centerY, 3.2f);

    D2D1_RECT_F trackHit = R(trackX - 4, y + 8, trackW + 8, 38);
    AddHit(HitKind::Slider, group, field, trackHit);
    DrawValuePill(theme, group, field, R(440, y + 10, 92, 34));
}

void ControlPanel::RenderEffectPage(const Theme& theme, int effectIndex) {
    EffectConfig* config = ConfigForGroup(effectIndex);
    if (!config) return;
    static const wchar_t* micros[] = {
        L"01   /   OUTER RING",
        L"02   /   DOT HALO",
        L"03   /   WAVE LINE",
    };
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), micros[effectIndex], 8.4f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(30, 321, 300, 14));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), kEffectSpecs[effectIndex].title,
         20.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(30, 336, 300, 30));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         kEffectSpecs[effectIndex].description, 9.2f, DWRITE_FONT_WEIGHT_NORMAL, theme.muted,
         R(30, 365, 410, 16));
    AddHit(HitKind::Toggle, effectIndex, -1, R(452, 329, 82, 44));
    DrawToggle(theme, R(462, 338, 46, 26), config->enabled,
               IsHovered(HitKind::Toggle, effectIndex, -1));

    DrawColorSection(theme, effectIndex, L"颜色", 390);
    DrawSliderRow(theme, effectIndex, 3, L"大小", 440);
    DrawSliderRow(theme, effectIndex, 4, L"变化幅度", 494);
    DrawSliderRow(theme, effectIndex, 5, L"波形密度", 548);
    DrawSliderRow(theme, effectIndex, 7, L"线宽", 602);
    DrawSliderRow(theme, effectIndex, 6, L"透明度", 656);
}

void ControlPanel::RenderShardsPage(const Theme& theme) {
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"04   /   OUTER SHARDS", 8.4f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(30, 321, 300, 14));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"碎片", 20.0f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(30, 336, 300, 30));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         L"从外圈向外散发并淡出的半透明几何碎片", 9.2f, DWRITE_FONT_WEIGHT_NORMAL, theme.muted,
         R(30, 365, 420, 16));
    AddHit(HitKind::Toggle, 3, -1, R(452, 329, 82, 44));
    DrawToggle(theme, R(462, 338, 46, 26), settings_->shards.enabled,
               IsHovered(HitKind::Toggle, 3, -1));

    DrawColorSection(theme, 4, L"颜色", 390);

    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"形状模板", 9.0f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(30, 438, 200, 16));
    const wchar_t* names[] = {L"锐锋", L"宽刃", L"菱片", L"斜棱", L"随机"};
    static const D2D1_POINT_2F iconShapes[4][4] = {
        {{0.0f, 1.0f}, {0.55f, -0.65f}, {-0.55f, -0.65f}, {0, 0}},
        {{0.0f, 0.85f}, {0.95f, -0.60f}, {-0.95f, -0.60f}, {0, 0}},
        {{0.0f, 1.0f}, {0.66f, 0.05f}, {0.0f, -0.95f}, {-0.66f, 0.05f}},
        {{0.25f, 1.0f}, {0.88f, 0.15f}, {0.05f, -0.88f}, {-0.74f, -0.08f}},
    };
    static const int iconCounts[4] = {3, 3, 4, 4};
    const float chipW = (kContentWidth - 4 * 8.0f) / 5.0f;
    for (int i = 0; i < 5; ++i) {
        D2D1_RECT_F chip = R(kMargin + i * (chipW + 8.0f), 458, chipW, 40);
        bool hovered = IsHovered(HitKind::ShardTemplate, i, -1);
        bool selected = settings_->shards.shape == i;
        FillRound(renderTarget_.Get(), brush_.Get(),
                  hovered ? theme.surfaceHover : theme.surface, chip, 11);
        StrokeRound(renderTarget_.Get(), brush_.Get(),
                    selected ? theme.accent.WithAlpha(180) : theme.border, chip, 11, 1.0f);
        float icx = chip.left + 18.0f;
        float icy = (chip.top + chip.bottom) * 0.5f;
        gfx::Color iconColor = selected ? theme.accent : theme.muted;
        if (i < 4) {
            for (int p = 0; p < iconCounts[i]; ++p) {
                int next = (p + 1) % iconCounts[i];
                Line(renderTarget_.Get(), brush_.Get(), iconColor,
                     icx + iconShapes[i][p].x * 8.0f, icy + iconShapes[i][p].y * 8.0f,
                     icx + iconShapes[i][next].x * 8.0f, icy + iconShapes[i][next].y * 8.0f,
                     1.1f);
            }
        } else {
            Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"?", 13.0f,
                 DWRITE_FONT_WEIGHT_SEMI_BOLD, iconColor, R(icx - 10, icy - 13, 20, 26),
                 DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), names[i], 9.5f,
             DWRITE_FONT_WEIGHT_NORMAL, selected ? theme.text : theme.muted,
             R(chip.left + 31, chip.top, chip.right - chip.left - 34, 40));
        AddHit(HitKind::ShardTemplate, i, -1, chip);
    }

    DrawSliderRow(theme, 4, 3, L"大小", 518);
    DrawSliderRow(theme, 4, 4, L"出现频率", 572);
    DrawSliderRow(theme, 4, 5, L"透明度", 626);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         L"出现频率会随音频强度与频谱变化速度自动增强", 8.2f, DWRITE_FONT_WEIGHT_NORMAL,
         theme.faint, R(30, 682, 500, 16));
}

void ControlPanel::RenderGlowPage(const Theme& theme) {
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"05   /   CENTER GLOW", 8.4f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(30, 321, 300, 14));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"底光", 20.0f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(30, 336, 300, 30));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         L"与外圈尺寸联动的圆形背景光辉", 9.2f, DWRITE_FONT_WEIGHT_NORMAL, theme.muted,
         R(30, 365, 420, 16));
    AddHit(HitKind::Toggle, 4, -1, R(452, 329, 82, 44));
    DrawToggle(theme, R(462, 338, 46, 26), settings_->glow.enabled,
               IsHovered(HitKind::Toggle, 4, -1));

    DrawColorSection(theme, 5, L"底光颜色", 390);
    DrawSliderRow(theme, 5, 3, L"大小", 448);
    DrawSliderRow(theme, 5, 4, L"透明度", 502);

    D2D1_RECT_F info = R(kMargin, 574, kContentWidth, 94);
    FillRound(renderTarget_.Get(), brush_.Get(), theme.surface, info, 14);
    StrokeRound(renderTarget_.Get(), brush_.Get(), theme.border, info, 14, 1.0f);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"外圈联动", 10.0f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(44, 588, 250, 18));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         L"底光半径随声环「大小」自动变化；此处大小是相对外圈的倍率。", 8.4f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(44, 612, 460, 16));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         L"透明度为 0% 时底光隐藏；颜色会同步进入 Bloom。", 8.4f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.faint, R(44, 634, 460, 16));
}

void ControlPanel::RenderAppearancePage(const Theme& theme) {
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"06   /   APPEARANCE", 8.4f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(30, 321, 300, 14));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"外观", 20.0f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(30, 336, 300, 30));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         L"调整总控面板与浮窗的显示方式", 9.2f, DWRITE_FONT_WEIGHT_NORMAL, theme.muted,
         R(30, 365, 410, 16));

    DrawColorSection(theme, 3, L"背景颜色", 388);
    DrawSliderRow(theme, 3, 6, L"浮窗缩放", 436);
    DrawSliderRow(theme, 3, 3, L"透明度", 484);
    DrawSliderRow(theme, 3, 4, L"Bloom 范围", 532);
    DrawSliderRow(theme, 3, 5, L"Bloom 亮度", 580);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         L"浮窗缩放作用于全部图层；Bloom 范围控制扩散、亮度控制强度", 8.2f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.faint, R(30, 618, 490, 16));

    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"快捷主题", 9.0f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(30, 640, 200, 16));
    struct Preset {
        const wchar_t* name;
        int r, g, b;
    };
    static const Preset presets[] = {
        {L"深海", 12, 20, 24},
        {L"夜蓝", 22, 28, 44},
        {L"雾白", 238, 244, 241},
        {L"石墨", 32, 33, 36},
    };
    const float chipW = (kContentWidth - 3 * 8.0f) / 4.0f;
    for (int i = 0; i < 4; ++i) {
        D2D1_RECT_F chip = R(kMargin + i * (chipW + 8.0f), 658, chipW, 38);
        bool hovered = IsHovered(HitKind::Preset, i, -1);
        bool selected = settings_->panelR == presets[i].r && settings_->panelG == presets[i].g &&
                        settings_->panelB == presets[i].b;
        FillRound(renderTarget_.Get(), brush_.Get(),
                  hovered ? theme.surfaceHover : theme.surface, chip, 11);
        StrokeRound(renderTarget_.Get(), brush_.Get(),
                    selected ? theme.accent.WithAlpha(170) : theme.border, chip, 11, 1.0f);
        FillCircle(renderTarget_.Get(), brush_.Get(),
                   gfx::Rgb(presets[i].r, presets[i].g, presets[i].b),
                   chip.left + 18, (chip.top + chip.bottom) * 0.5f, 5.5f);
        Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), presets[i].name, 9.5f,
             DWRITE_FONT_WEIGHT_NORMAL, selected ? theme.text : theme.muted,
             R(chip.left + 30, chip.top, chip.right - chip.left - 34, 38));
        AddHit(HitKind::Preset, i, -1, chip);
    }

    const D2D1_RECT_F recenter = R(kMargin, 706, 246, 54);
    const D2D1_RECT_F reset = R(286, 706, 246, 54);
    bool recenterHover = IsHovered(HitKind::Recenter, -1, -1);
    bool resetHover = IsHovered(HitKind::Reset, -1, -1);
    FillRound(renderTarget_.Get(), brush_.Get(),
              recenterHover ? theme.accentSoft : theme.surface, recenter, 13);
    FillRound(renderTarget_.Get(), brush_.Get(),
              resetHover ? theme.accentSoft : theme.surface, reset, 13);
    StrokeRound(renderTarget_.Get(), brush_.Get(), theme.border, recenter, 13, 1.0f);
    StrokeRound(renderTarget_.Get(), brush_.Get(), theme.border, reset, 13, 1.0f);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"重新居中浮窗", 10.6f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(44, 713, 200, 20));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"让声环回到主屏中央", 8.2f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(44, 732, 200, 15));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"恢复默认外观", 10.6f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(302, 713, 200, 20));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"效果颜色与面板配色", 8.2f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(302, 732, 200, 15));
    AddHit(HitKind::Recenter, -1, -1, recenter);
    AddHit(HitKind::Reset, -1, -1, reset);

    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         L"预设自动保存于程序目录  ·  presets.ini", 8.1f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.faint, R(30, 772, 500, 16));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         L"AURA 1.7   ·   Windows 11", 8.0f, DWRITE_FONT_WEIGHT_NORMAL, theme.faint,
         R(30, 792, 500, 16));
}

void ControlPanel::RenderFooter(const Theme& theme, int) {
    FillCircle(renderTarget_.Get(), brush_.Get(), theme.accent, 32.0f, 744.0f, 1.8f);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         L"拖动滑块调整  ·  点击数值直接输入  ·  滚轮微调", 8.3f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.faint, R(40, 737, 420, 16));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"自动保存", 8.2f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.faint, R(430, 737, 102, 16),
         DWRITE_TEXT_ALIGNMENT_TRAILING);
}

void ControlPanel::RenderSourcePage(const Theme& theme) {
    AddHit(HitKind::Header, -1, -1, R(0, 0, kWidth, 88));
    D2D1_RECT_F back = R(28, 26, 34, 34);
    bool backHovered = IsHovered(HitKind::PickerBack, -1, -1);
    FillRound(renderTarget_.Get(), brush_.Get(),
              backHovered ? theme.surfaceHover : theme.surface, back, 11);
    StrokeRound(renderTarget_.Get(), brush_.Get(), theme.border, back, 11, 1.0f);
    DrawChevronLeft(renderTarget_.Get(), brush_.Get(),
                    backHovered ? theme.text : theme.muted, 45, 43, 1.0f);
    AddHit(HitKind::PickerBack, -1, -1, back);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"选择音频来源", 18.0f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(78, 20, 300, 28));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         L"仅捕获所选应用及其子进程的音频", 8.9f, DWRITE_FONT_WEIGHT_NORMAL, theme.muted,
         R(78, 49, 340, 16));
    DrawWindowButtons(theme);

    bool systemSelected = settings_ && settings_->sourceKind == SourceKind::System;
    bool systemHovered = IsHovered(HitKind::PickerSystem, -1, -1);
    D2D1_RECT_F system = R(kMargin, 92, kContentWidth, 64);
    FillRound(renderTarget_.Get(), brush_.Get(),
              systemSelected ? theme.accentSoft : systemHovered ? theme.surfaceHover : theme.surface,
              system, 16);
    StrokeRound(renderTarget_.Get(), brush_.Get(),
                systemSelected ? theme.accent.WithAlpha(130) : theme.border, system, 16, 1.0f);
    FillRound(renderTarget_.Get(), brush_.Get(), theme.accentSoft, R(44, 106, 34, 34), 10);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"全", 11.5f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.accent, R(44, 106, 34, 34),
         DWRITE_TEXT_ALIGNMENT_CENTER);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"系统全部音频", 12.2f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(88, 100, 330, 21));
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"捕获桌面上所有应用的混合声音",
         8.8f, DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(88, 121, 360, 16));
    if (systemSelected) {
        FillCircle(renderTarget_.Get(), brush_.Get(), theme.accent, 494, 124, 10);
        Line(renderTarget_.Get(), brush_.Get(), theme.accentText, 489, 124, 493, 128, 1.7f);
        Line(renderTarget_.Get(), brush_.Get(), theme.accentText, 493, 128, 500, 119, 1.7f);
    }
    AddHit(HitKind::PickerSystem, -1, -1, system);

    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"应用程序", 9.1f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.muted, R(30, 176, 200, 18));
    wchar_t countLabel[64];
    swprintf_s(countLabel, L"%zu 个", apps_.size());
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), countLabel, 8.5f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.faint, R(100, 176, 100, 18));
    D2D1_RECT_F refresh = R(444, 168, 88, 30);
    bool refreshHovered = IsHovered(HitKind::PickerRefresh, -1, -1);
    FillRound(renderTarget_.Get(), brush_.Get(),
              refreshHovered ? theme.accentSoft : theme.surface, refresh, 9);
    StrokeRound(renderTarget_.Get(), brush_.Get(), theme.border, refresh, 9, 1.0f);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"↻  刷新列表", 8.6f,
         DWRITE_FONT_WEIGHT_NORMAL, refreshHovered ? theme.accent : theme.muted,
         R(444, 168, 88, 30), DWRITE_TEXT_ALIGNMENT_CENTER);
    AddHit(HitKind::PickerRefresh, -1, -1, refresh);

    if (apps_.empty()) {
        Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"未发现可选择的应用程序",
             12.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(30, 330, 504, 24),
             DWRITE_TEXT_ALIGNMENT_CENTER);
        Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
             L"打开应用并播放声音后，点击刷新列表再试", 9.0f, DWRITE_FONT_WEIGHT_NORMAL,
             theme.muted, R(30, 358, 504, 20), DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        int maxScroll = std::max(0, static_cast<int>(apps_.size()) - kVisibleRows);
        appScroll_ = std::clamp(appScroll_, 0, maxScroll);
        const bool hasScrollbar = static_cast<int>(apps_.size()) > kVisibleRows;
        const float rowWidth = hasScrollbar ? 494.0f : 504.0f;
        for (int row = 0; row < kVisibleRows; ++row) {
            int index = appScroll_ + row;
            if (index >= static_cast<int>(apps_.size())) break;
            D2D1_RECT_F rowRect =
                R(kMargin, static_cast<float>(kListTop + row * (kRowHeight + kRowGap)), rowWidth,
                  static_cast<float>(kRowHeight));
            DrawSourceRow(theme, apps_[index], index, rowRect);
        }
        if (hasScrollbar) {
            DrawScrollbar(theme, 530, static_cast<float>(kListTop),
                          static_cast<float>(kVisibleRows * (kRowHeight + kRowGap) - kRowGap));
        }
    }

    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(),
         L"提示：浏览器、音乐播放器等应用会通过子进程输出音频，选择主程序即可一并捕获。",
         8.0f, DWRITE_FONT_WEIGHT_NORMAL, theme.faint, R(30, 712, 504, 16));
}

void ControlPanel::DrawSourceRow(const Theme& theme, const AudioApp& app, int index,
                                 D2D1_RECT_F rect) {
    bool selected = IsCurrentSource(app);
    bool hovered = IsHovered(HitKind::PickerApp, index, -1);
    FillRound(renderTarget_.Get(), brush_.Get(),
              selected ? theme.accentSoft : hovered ? theme.surfaceHover : theme.surface, rect,
              13);
    StrokeRound(renderTarget_.Get(), brush_.Get(),
                selected ? theme.accent.WithAlpha(120) : theme.border, rect, 13, 1.0f);
    FillRound(renderTarget_.Get(), brush_.Get(), theme.accentSoft,
              R(rect.left + 14, rect.top + 11, 32, 32), 10);
    wchar_t initial = app.name.empty() ? L'A' : app.name[0];
    std::wstring initialText(1, initial);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), initialText, 11.0f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.accent,
         R(rect.left + 14, rect.top + 11, 32, 32), DWRITE_TEXT_ALIGNMENT_CENTER);

    std::wstring name = TruncateToWidth(app.name.empty() ? L"应用程序" : app.name, 300.0f,
                                        11.8f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), name, 11.8f,
         DWRITE_FONT_WEIGHT_SEMI_BOLD, theme.text, R(rect.left + 60, rect.top + 8, 330, 20));
    std::wstring subtitle;
    if (!app.exe.empty()) subtitle = app.exe;
    if (app.pid != 0) {
        if (!subtitle.empty()) subtitle += L"  ·  ";
        wchar_t pidText[32];
        swprintf_s(pidText, L"PID %lu", app.pid);
        subtitle += pidText;
    }
    if (subtitle.empty()) subtitle = L"应用程序音源";
    Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), subtitle, 8.3f,
         DWRITE_FONT_WEIGHT_NORMAL, theme.muted, R(rect.left + 60, rect.top + 29, 300, 15));

    if (app.playing && !selected) {
        FillCircle(renderTarget_.Get(), brush_.Get(), theme.success, rect.right - 62, rect.top + 26,
                   2.5f);
        Text(renderTarget_.Get(), writeFactory_.Get(), brush_.Get(), L"播放中", 8.2f,
             DWRITE_FONT_WEIGHT_NORMAL, theme.success,
             R(rect.right - 106, rect.top + 17, 42, 18), DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
    if (selected) {
        FillCircle(renderTarget_.Get(), brush_.Get(), theme.accent, rect.right - 22,
                   rect.top + 26, 10);
        Line(renderTarget_.Get(), brush_.Get(), theme.accentText, rect.right - 27,
             rect.top + 26, rect.right - 23, rect.top + 30, 1.7f);
        Line(renderTarget_.Get(), brush_.Get(), theme.accentText, rect.right - 23,
             rect.top + 30, rect.right - 16, rect.top + 21, 1.7f);
    }
    AddHit(HitKind::PickerApp, index, -1, rect);
}

void ControlPanel::DrawScrollbar(const Theme& theme, float x, float y, float height) {
    int total = static_cast<int>(apps_.size());
    if (total <= kVisibleRows) return;
    FillRound(renderTarget_.Get(), brush_.Get(), theme.surface, R(x - 1, y, 4, height), 2);
    float thumbHeight = std::max(32.0f, height * kVisibleRows / total);
    int maxScroll = total - kVisibleRows;
    float t = maxScroll > 0 ? appScroll_ / static_cast<float>(maxScroll) : 0.0f;
    float thumbY = y + (height - thumbHeight) * t;
    FillRound(renderTarget_.Get(), brush_.Get(), theme.accent.WithAlpha(140),
              R(x - 1, thumbY, 4, thumbHeight), 2);
}
