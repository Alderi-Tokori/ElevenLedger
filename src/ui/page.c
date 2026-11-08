#include "ui/page.h"

#include "ui/theme.h"

#include <string.h>

#define PAGE_CLASS_NAME L"PitchRatePage"

static LRESULT CALLBACK page_window_proc(HWND hwnd, UINT message,
                                         WPARAM wparam, LPARAM lparam);

bool page_register_class(HINSTANCE instance)
{
    WNDCLASSEXW window_class;
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    window_class.lpfnWndProc = page_window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = NULL;
    window_class.lpszClassName = PAGE_CLASS_NAME;
    return RegisterClassExW(&window_class) != 0;
}

bool page_create(Page *page, App *app, PageId id,
                 const wchar_t *title, const wchar_t *subtitle,
                 void (*on_create)(Page *page),
                 void (*on_layout)(Page *page, int width, int height),
                 void (*on_paint)(Page *page, HDC dc),
                 void (*on_refresh)(Page *page),
                 void (*on_destroy)(Page *page),
                 LRESULT (*on_message)(Page *page, UINT message,
                                       WPARAM wparam, LPARAM lparam))
{
    if (page == NULL || app == NULL) {
        return false;
    }
    page->app = app;
    page->id = id;
    page->title = title;
    page->subtitle = subtitle;
    page->on_create = on_create;
    page->on_layout = on_layout;
    page->on_paint = on_paint;
    page->on_refresh = on_refresh;
    page->on_destroy = on_destroy;
    page->on_message = on_message;

    page->hwnd = CreateWindowExW(
        WS_EX_CONTROLPARENT, PAGE_CLASS_NAME, title != NULL ? title : L"",
        WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 100, 100, app->main_hwnd, NULL, app->instance, page);
    if (page->hwnd == NULL) {
        return false;
    }
    if (title != NULL) {
        SetWindowTextW(page->hwnd, title);
    }
    if (on_create != NULL) {
        on_create(page);
    }
    return true;
}

void page_destroy(Page *page)
{
    if (page == NULL) {
        return;
    }
    if (page->on_destroy != NULL) {
        page->on_destroy(page);
    }
    if (page->hwnd != NULL && IsWindow(page->hwnd)) {
        DestroyWindow(page->hwnd);
    }
    page->hwnd = NULL;
}

RECT page_card_rect(const Page *page)
{
    UiTheme *theme = &page->app->ui;
    RECT card;
    int margin = ui_scale(theme, 28);
    card.left = margin;
    card.top = ui_scale(theme, 104);
    card.right = page->width - margin;
    card.bottom = page->height - margin;
    return card;
}

RECT page_list_rect(const Page *page)
{
    UiTheme *theme = &page->app->ui;
    RECT card = page->card;
    int inset = ui_scale(theme, 12);
    RECT list;
    list.left = card.left + inset;
    list.top = card.top + ui_scale(theme, 54);
    list.right = card.right - inset;
    list.bottom = card.bottom - inset;
    return list;
}

void page_draw_toolbar_label(Page *page, HDC dc, int x, int y,
                             const wchar_t *text)
{
    UiTheme *theme = &page->app->ui;
    RECT rect = { x, y, x + ui_scale(theme, 300), y + ui_scale(theme, 18) };
    HGDIOBJ old = SelectObject(dc, theme->font_small_bold);
    SetTextColor(dc, UI_COLOR_MUTED);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text, -1, &rect, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
}

static void draw_page_header(Page *page, HDC dc)
{
    UiTheme *theme = &page->app->ui;
    int margin = ui_scale(theme, 28);
    RECT title_rect;
    title_rect.left = margin;
    title_rect.top = ui_scale(theme, 19);
    title_rect.right = page->width - margin;
    title_rect.bottom = title_rect.top + ui_scale(theme, 42);
    ui_draw_text(dc, theme->font_h1, UI_COLOR_TEXT, title_rect, page->title,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT subtitle_rect;
    subtitle_rect.left = margin;
    subtitle_rect.top = ui_scale(theme, 61);
    subtitle_rect.right = page->width - margin;
    subtitle_rect.bottom = subtitle_rect.top + ui_scale(theme, 22);
    ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, subtitle_rect,
                 page->subtitle,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void page_draw_shell(Page *page, HDC dc)
{
    page->card = page_card_rect(page);
    draw_page_header(page, dc);
}

void page_draw_card(Page *page, HDC dc)
{
    UiTheme *theme = &page->app->ui;
    RECT shadow = page->card;
    OffsetRect(&shadow, 0, ui_scale(theme, 2));
    ui_fill_round_rect(dc, shadow, ui_scale(theme, 12),
                       RGB(232, 237, 242));
    ui_stroke_round_rect(dc, page->card, ui_scale(theme, 12),
                         UI_COLOR_CARD, RGB(233, 238, 243));
}

void page_draw_empty_state(Page *page, HDC dc, const wchar_t *title,
                           const wchar_t *subtitle)
{
    UiTheme *theme = &page->app->ui;
    RECT list = page_list_rect(page);
    int center_x = (list.left + list.right) / 2;
    int center_y = (list.top + list.bottom) / 2;

    int circle = ui_scale(theme, 58);
    RECT icon_rect;
    icon_rect.left = center_x - circle / 2;
    icon_rect.top = center_y - ui_scale(theme, 76);
    icon_rect.right = icon_rect.left + circle;
    icon_rect.bottom = icon_rect.top + circle;
    ui_fill_round_rect(dc, icon_rect, circle / 2, UI_COLOR_ACCENT_SOFT);
    int inset = ui_scale(theme, 18);
    InflateRect(&icon_rect, -inset, -inset);
    ui_stroke_round_rect(dc, icon_rect, ui_scale(theme, 10),
                         RGB(255, 255, 255), UI_COLOR_ACCENT);
    ui_draw_text(dc, theme->font_h2, UI_COLOR_ACCENT, icon_rect, L"+",
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT title_rect;
    title_rect.left = list.left + ui_scale(theme, 20);
    title_rect.top = center_y - ui_scale(theme, 8);
    title_rect.right = list.right - ui_scale(theme, 20);
    title_rect.bottom = title_rect.top + ui_scale(theme, 28);
    ui_draw_text(dc, theme->font_h2, UI_COLOR_TEXT, title_rect, title,
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT subtitle_rect;
    subtitle_rect.left = title_rect.left;
    subtitle_rect.top = title_rect.bottom + ui_scale(theme, 4);
    subtitle_rect.right = title_rect.right;
    subtitle_rect.bottom = subtitle_rect.top + ui_scale(theme, 46);
    ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, subtitle_rect,
                 subtitle, DT_CENTER | DT_TOP | DT_WORDBREAK);
}

static LRESULT CALLBACK page_window_proc(HWND hwnd, UINT message,
                                         WPARAM wparam, LPARAM lparam)
{
    Page *page = (Page *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    case WM_SIZE:
        if (page != NULL) {
            page->width = LOWORD(lparam);
            page->height = HIWORD(lparam);
            if (page->on_layout != NULL) {
                page->on_layout(page, page->width, page->height);
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (page != NULL) {
            PAINTSTRUCT paint;
            HDC target = BeginPaint(hwnd, &paint);
            UiCanvas canvas;
            if (ui_canvas_begin(&canvas, hwnd, target, &page->app->ui,
                                UI_COLOR_BG)) {
                page->card = page_card_rect(page);
                page_draw_shell(page, canvas.dc);
                if (page->on_paint != NULL) {
                    page->on_paint(page, canvas.dc);
                }
                ui_canvas_end(&canvas);
            }
            EndPaint(hwnd, &paint);
        }
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wparam;
        SetTextColor(dc, UI_COLOR_TEXT);
        SetBkColor(dc, UI_COLOR_FIELD_BG);
        return (LRESULT)page->app->ui.brush_field;
    }
    case WM_COMMAND:
    case WM_NOTIFY:
        if (page != NULL && page->on_message != NULL) {
            LRESULT result = page->on_message(page, message, wparam, lparam);
            if (message == WM_NOTIFY || result != 0) {
                return result;
            }
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
