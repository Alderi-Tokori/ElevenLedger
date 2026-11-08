#ifndef PITCHRATE_UI_THEME_H
#define PITCHRATE_UI_THEME_H

#include <windows.h>
#include <commctrl.h>
#include <stdbool.h>

#define UI_COLOR_BG RGB(244, 247, 250)
#define UI_COLOR_CARD RGB(255, 255, 255)
#define UI_COLOR_SIDEBAR RGB(17, 35, 51)
#define UI_COLOR_SIDEBAR_HOVER RGB(28, 52, 72)
#define UI_COLOR_SIDEBAR_ACTIVE RGB(34, 62, 83)
#define UI_COLOR_SIDEBAR_TEXT RGB(234, 241, 246)
#define UI_COLOR_SIDEBAR_MUTED RGB(145, 166, 181)
#define UI_COLOR_TEXT RGB(27, 40, 54)
#define UI_COLOR_MUTED RGB(102, 118, 134)
#define UI_COLOR_FAINT RGB(147, 160, 172)
#define UI_COLOR_BORDER RGB(225, 231, 237)
#define UI_COLOR_BORDER_STRONG RGB(207, 216, 225)
#define UI_COLOR_ACCENT RGB(31, 138, 86)
#define UI_COLOR_ACCENT_HOVER RGB(26, 120, 74)
#define UI_COLOR_ACCENT_PRESSED RGB(21, 102, 63)
#define UI_COLOR_ACCENT_SOFT RGB(232, 247, 239)
#define UI_COLOR_DANGER RGB(188, 58, 58)
#define UI_COLOR_DANGER_HOVER RGB(165, 46, 46)
#define UI_COLOR_DANGER_SOFT RGB(253, 238, 238)
#define UI_COLOR_AMBER RGB(166, 120, 26)
#define UI_COLOR_AMBER_SOFT RGB(252, 246, 227)
#define UI_COLOR_BLUE RGB(49, 106, 156)
#define UI_COLOR_BLUE_SOFT RGB(235, 243, 250)
#define UI_COLOR_FIELD_BG RGB(255, 255, 255)
#define UI_COLOR_ROW_ALT RGB(249, 251, 252)
#define UI_COLOR_SELECTION RGB(232, 246, 239)

typedef struct {
    int dpi;
    HFONT font_h1;
    HFONT font_h2;
    HFONT font_body;
    HFONT font_body_bold;
    HFONT font_small;
    HFONT font_small_bold;
    HFONT font_stat;
    HFONT font_button;
    HBRUSH brush_bg;
    HBRUSH brush_card;
    HBRUSH brush_sidebar;
    HBRUSH brush_field;
    HBRUSH brush_white;
    HIMAGELIST row_images;
} UiTheme;

typedef struct {
    HWND hwnd;
    HDC target;
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ old_bitmap;
    RECT rect;
    bool active;
} UiCanvas;

int ui_scale(const UiTheme *theme, int value);
bool ui_theme_init(UiTheme *theme, int dpi);
void ui_theme_destroy(UiTheme *theme);

bool ui_canvas_begin(UiCanvas *canvas, HWND hwnd, HDC target,
                     const UiTheme *theme, COLORREF background);
void ui_canvas_end(UiCanvas *canvas);

void ui_fill_round_rect(HDC dc, RECT rect, int radius, COLORREF fill);
void ui_stroke_round_rect(HDC dc, RECT rect, int radius,
                          COLORREF fill, COLORREF border);
void ui_draw_line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color);
void ui_draw_text(HDC dc, HFONT font, COLORREF color, RECT rect,
                  const wchar_t *text, UINT flags);
void ui_draw_text_utf8(HDC dc, HFONT font, COLORREF color, RECT rect,
                       const char *text, UINT flags);
void ui_draw_rating_badge(HDC dc, const UiTheme *theme, RECT rect,
                          double rating);
COLORREF ui_rating_color(double rating);
COLORREF ui_rating_soft_color(double rating);

bool ui_utf8_to_wide(const char *input, wchar_t *output, size_t output_count);
void ui_wide_to_utf8(const wchar_t *input, char *output, size_t output_count);
void ui_format_wide(wchar_t *buffer, size_t count, const wchar_t *format, ...);

#endif
