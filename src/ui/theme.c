#include "ui/theme.h"

#include <commctrl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

int ui_scale(const UiTheme *theme, int value)
{
    if (theme == NULL || theme->dpi <= 0) {
        return value;
    }
    return MulDiv(value, theme->dpi, 96);
}

static HFONT make_font(const UiTheme *theme, int pixels, int weight)
{
    return CreateFontW(-ui_scale(theme, pixels), 0, 0, 0, weight, FALSE, FALSE,
                       FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

bool ui_theme_init(UiTheme *theme, int dpi)
{
    if (theme == NULL) {
        return false;
    }
    memset(theme, 0, sizeof(*theme));
    theme->dpi = dpi > 0 ? dpi : 96;

    theme->font_h1 = make_font(theme, 28, FW_BOLD);
    theme->font_h2 = make_font(theme, 18, FW_SEMIBOLD);
    theme->font_body = make_font(theme, 14, FW_NORMAL);
    theme->font_body_bold = make_font(theme, 14, FW_SEMIBOLD);
    theme->font_small = make_font(theme, 12, FW_NORMAL);
    theme->font_small_bold = make_font(theme, 12, FW_SEMIBOLD);
    theme->font_stat = make_font(theme, 32, FW_BOLD);
    theme->font_button = make_font(theme, 14, FW_SEMIBOLD);

    theme->brush_bg = CreateSolidBrush(UI_COLOR_BG);
    theme->brush_card = CreateSolidBrush(UI_COLOR_CARD);
    theme->brush_sidebar = CreateSolidBrush(UI_COLOR_SIDEBAR);
    theme->brush_field = CreateSolidBrush(UI_COLOR_FIELD_BG);
    theme->brush_white = CreateSolidBrush(RGB(255, 255, 255));
    theme->row_images = ImageList_Create(1, ui_scale(theme, 30),
                                         ILC_COLOR32, 1, 1);
    if (theme->font_h1 == NULL || theme->font_h2 == NULL ||
        theme->font_body == NULL || theme->font_body_bold == NULL ||
        theme->font_small == NULL || theme->font_small_bold == NULL ||
        theme->font_stat == NULL || theme->font_button == NULL ||
        theme->brush_bg == NULL || theme->brush_card == NULL ||
        theme->brush_sidebar == NULL || theme->brush_field == NULL ||
        theme->brush_white == NULL || theme->row_images == NULL) {
        ui_theme_destroy(theme);
        return false;
    }
    return true;
}

void ui_theme_destroy(UiTheme *theme)
{
    if (theme == NULL) {
        return;
    }
    if (theme->font_h1 != NULL) DeleteObject(theme->font_h1);
    if (theme->font_h2 != NULL) DeleteObject(theme->font_h2);
    if (theme->font_body != NULL) DeleteObject(theme->font_body);
    if (theme->font_body_bold != NULL) DeleteObject(theme->font_body_bold);
    if (theme->font_small != NULL) DeleteObject(theme->font_small);
    if (theme->font_small_bold != NULL) DeleteObject(theme->font_small_bold);
    if (theme->font_stat != NULL) DeleteObject(theme->font_stat);
    if (theme->font_button != NULL) DeleteObject(theme->font_button);
    if (theme->brush_bg != NULL) DeleteObject(theme->brush_bg);
    if (theme->brush_card != NULL) DeleteObject(theme->brush_card);
    if (theme->brush_sidebar != NULL) DeleteObject(theme->brush_sidebar);
    if (theme->brush_field != NULL) DeleteObject(theme->brush_field);
    if (theme->brush_white != NULL) DeleteObject(theme->brush_white);
    if (theme->row_images != NULL) ImageList_Destroy(theme->row_images);
    memset(theme, 0, sizeof(*theme));
}

bool ui_canvas_begin(UiCanvas *canvas, HWND hwnd, HDC target,
                     const UiTheme *theme, COLORREF background)
{
    if (canvas == NULL) {
        return false;
    }
    memset(canvas, 0, sizeof(*canvas));
    GetClientRect(hwnd, &canvas->rect);
    if (canvas->rect.right <= 0 || canvas->rect.bottom <= 0) {
        return false;
    }
    canvas->hwnd = hwnd;
    canvas->target = target;
    canvas->dc = CreateCompatibleDC(target);
    if (canvas->dc == NULL) {
        return false;
    }
    canvas->bitmap = CreateCompatibleBitmap(target, canvas->rect.right,
                                            canvas->rect.bottom);
    if (canvas->bitmap == NULL) {
        DeleteDC(canvas->dc);
        return false;
    }
    canvas->old_bitmap = SelectObject(canvas->dc, canvas->bitmap);
    SetBkMode(canvas->dc, TRANSPARENT);
    HBRUSH brush = CreateSolidBrush(background);
    if (brush != NULL) {
        FillRect(canvas->dc, &canvas->rect, brush);
        DeleteObject(brush);
    } else if (theme != NULL && theme->brush_bg != NULL) {
        FillRect(canvas->dc, &canvas->rect, theme->brush_bg);
    }
    canvas->active = true;
    return true;
}

void ui_canvas_end(UiCanvas *canvas)
{
    if (canvas == NULL || !canvas->active) {
        return;
    }
    BitBlt(canvas->target, 0, 0, canvas->rect.right, canvas->rect.bottom,
           canvas->dc, 0, 0, SRCCOPY);
    SelectObject(canvas->dc, canvas->old_bitmap);
    DeleteObject(canvas->bitmap);
    DeleteDC(canvas->dc);
    memset(canvas, 0, sizeof(*canvas));
}

void ui_fill_round_rect(HDC dc, RECT rect, int radius, COLORREF fill)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    int ellipse = radius * 2;
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom,
              ellipse, ellipse);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(brush);
}

void ui_stroke_round_rect(HDC dc, RECT rect, int radius,
                          COLORREF fill, COLORREF border)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    int ellipse = radius * 2;
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom,
              ellipse, ellipse);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void ui_draw_line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    MoveToEx(dc, x1, y1, NULL);
    LineTo(dc, x2, y2);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

void ui_draw_text(HDC dc, HFONT font, COLORREF color, RECT rect,
                  const wchar_t *text, UINT flags)
{
    if (text == NULL) {
        text = L"";
    }
    HGDIOBJ old_font = font != NULL ? SelectObject(dc, font) : NULL;
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &rect, flags | DT_NOPREFIX);
    if (old_font != NULL) {
        SelectObject(dc, old_font);
    }
}

void ui_draw_text_utf8(HDC dc, HFONT font, COLORREF color, RECT rect,
                       const char *text, UINT flags)
{
    wchar_t wide[512];
    if (!ui_utf8_to_wide(text, wide,
                         sizeof(wide) / sizeof(wide[0]))) {
        wide[0] = L'\0';
    }
    ui_draw_text(dc, font, color, rect, wide, flags);
}

bool ui_utf8_to_wide(const char *input, wchar_t *output, size_t output_count)
{
    if (output == NULL || output_count == 0) {
        return false;
    }
    output[0] = L'\0';
    if (input == NULL || *input == '\0') {
        return true;
    }
    int result = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1,
                                     output, (int)output_count);
    if (result == 0) {
        return false;
    }
    output[output_count - 1] = L'\0';
    return true;
}

void ui_wide_to_utf8(const wchar_t *input, char *output, size_t output_count)
{
    if (output == NULL || output_count == 0) {
        return;
    }
    output[0] = '\0';
    if (input == NULL) {
        return;
    }
    int result = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, -1,
                                     output, (int)output_count, NULL, NULL);
    if (result == 0) {
        output[0] = '\0';
    }
}

void ui_format_wide(wchar_t *buffer, size_t count, const wchar_t *format, ...)
{
    if (buffer == NULL || count == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    _vsnwprintf(buffer, count - 1, format, args);
    buffer[count - 1] = L'\0';
    va_end(args);
}

typedef struct {
    COLORREF text;
    COLORREF soft;
} RatingBandColors;

/* Rating bands: 80+ excellent, 65+ good, 50+ average (the default
   starting rating), 40+ below average, below 40 poor. */
static RatingBandColors rating_band_colors(double rating)
{
    RatingBandColors colors;
    if (rating >= 80.0) {
        colors.text = RGB(22, 111, 65);
        colors.soft = UI_COLOR_ACCENT_SOFT;
    } else if (rating >= 65.0) {
        colors.text = UI_COLOR_BLUE;
        colors.soft = UI_COLOR_BLUE_SOFT;
    } else if (rating >= 50.0) {
        colors.text = UI_COLOR_NEUTRAL;
        colors.soft = UI_COLOR_NEUTRAL_SOFT;
    } else if (rating >= 40.0) {
        colors.text = UI_COLOR_AMBER;
        colors.soft = UI_COLOR_AMBER_SOFT;
    } else {
        colors.text = UI_COLOR_DANGER;
        colors.soft = UI_COLOR_DANGER_SOFT;
    }
    return colors;
}

COLORREF ui_rating_color(double rating)
{
    return rating_band_colors(rating).text;
}

COLORREF ui_rating_soft_color(double rating)
{
    return rating_band_colors(rating).soft;
}

void ui_draw_rating_badge(HDC dc, const UiTheme *theme, RECT rect,
                          double rating)
{
    ui_fill_round_rect(dc, rect, ui_scale(theme, 7),
                       ui_rating_soft_color(rating));
    wchar_t text[16];
    ui_format_wide(text, sizeof(text) / sizeof(text[0]), L"%.1f", rating);
    ui_draw_text(dc, theme->font_small_bold, ui_rating_color(rating), rect,
                 text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
