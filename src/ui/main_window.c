#include "ui/main_window.h"

#include "app.h"
#include "resource.h"
#include "ui/controls.h"
#include "ui/page.h"
#include "ui/theme.h"

#include <windowsx.h>
#include <string.h>

#define MAIN_WINDOW_CLASS L"ElevenLedgerMainWindow"
#define SIDEBAR_BASE_WIDTH 232
#define NAV_TOP 152
#define NAV_HEIGHT 44
#define NAV_GAP 5
#define NAV_COUNT 5

#ifndef ELEVENLEDGER_VERSION
#define ELEVENLEDGER_VERSION "2.0.10"
#endif

static const wchar_t *g_navigation_labels[NAV_COUNT] = {
    L"Dashboard",
    L"Players",
    L"Teams",
    L"Matches",
    L"Rating guide",
};

static int sidebar_width(const App *app)
{
    return ui_scale(&app->ui, SIDEBAR_BASE_WIDTH);
}

int main_window_sidebar_width(const struct App *app)
{
    return sidebar_width((const App *)app);
}

static RECT navigation_rect(const App *app, int index)
{
    int width = sidebar_width(app);
    RECT rect;
    rect.left = ui_scale(&app->ui, 16);
    rect.top = ui_scale(&app->ui, NAV_TOP) +
               index * ui_scale(&app->ui, NAV_HEIGHT + NAV_GAP);
    rect.right = width - ui_scale(&app->ui, 16);
    rect.bottom = rect.top + ui_scale(&app->ui, NAV_HEIGHT);
    return rect;
}

static int navigation_at_point(App *app, POINT point)
{
    for (int i = 0; i < NAV_COUNT; ++i) {
        RECT rect = navigation_rect(app, i);
        if (PtInRect(&rect, point)) {
            return i;
        }
    }
    return -1;
}

static void draw_dashboard_icon(HDC dc, int x, int y, int size, COLORREF color)
{
    HBRUSH brush = CreateSolidBrush(color);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    int gap = size / 8;
    int block = (size - gap) / 2;
    Rectangle(dc, x, y, x + block, y + block);
    Rectangle(dc, x + block + gap, y,
              x + block + gap + block, y + block);
    Rectangle(dc, x, y + block + gap,
              x + block, y + block + gap + block);
    Rectangle(dc, x + block + gap, y + block + gap,
              x + block + gap + block, y + block + gap + block);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(brush);
}

static void draw_players_icon(HDC dc, int x, int y, int size, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, size / 9 > 0 ? size / 9 : 1, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    int head = size / 3;
    Ellipse(dc, x + size / 2 - head / 2, y,
            x + size / 2 + head / 2, y + head);
    Arc(dc, x + 1, y + size / 2, x + size - 1, y + size + size / 3,
        x + size / 5, y + size / 2, x + size * 4 / 5, y + size / 2);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

static void draw_teams_icon(HDC dc, int x, int y, int size, COLORREF color)
{
    POINT points[5] = {
        { x + 2, y + 1 },
        { x + size - 2, y + 1 },
        { x + size - 2, y + size / 2 },
        { x + size / 2, y + size - 1 },
        { x + 2, y + size / 2 },
    };
    HPEN pen = CreatePen(PS_SOLID, size / 9 > 0 ? size / 9 : 1, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Polygon(dc, points, 5);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

static void draw_matches_icon(HDC dc, int x, int y, int size, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, x, y + 1, x + size, y + size - 1);
    MoveToEx(dc, x + size / 2, y + 1, NULL);
    LineTo(dc, x + size / 2, y + size - 1);
    Ellipse(dc, x + size / 2 - size / 5, y + size / 2 - size / 5,
            x + size / 2 + size / 5, y + size / 2 + size / 5);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

static void draw_guide_icon(HDC dc, int x, int y, int size, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, size / 9 > 0 ? size / 9 : 1, color);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, x, y, x + size, y + size);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
    SetTextColor(dc, color);
    RECT rect = { x, y - 1, x + size, y + size };
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, L"i", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void draw_navigation_icon(HDC dc, int index, int x, int y, int size,
                                 COLORREF color)
{
    switch (index) {
    case PAGE_DASHBOARD:
        draw_dashboard_icon(dc, x, y, size, color);
        break;
    case PAGE_PLAYERS:
        draw_players_icon(dc, x, y, size, color);
        break;
    case PAGE_TEAMS:
        draw_teams_icon(dc, x, y, size, color);
        break;
    case PAGE_MATCHES:
        draw_matches_icon(dc, x, y, size, color);
        break;
    case PAGE_GUIDE:
    default:
        draw_guide_icon(dc, x, y, size, color);
        break;
    }
}

void main_window_draw_navigation(struct App *app, HDC dc)
{
    UiTheme *theme = &app->ui;
    int width = sidebar_width(app);
    RECT client;
    GetClientRect(app->main_hwnd, &client);
    RECT sidebar = { 0, 0, width, client.bottom };
    FillRect(dc, &sidebar, theme->brush_sidebar);

    int logo_size = ui_scale(theme, 42);
    int logo_x = ui_scale(theme, 22);
    int logo_y = ui_scale(theme, 22);
    if (app->icon_large != NULL) {
        DrawIconEx(dc, logo_x, logo_y, app->icon_large, logo_size, logo_size,
                   0, NULL, DI_NORMAL);
    } else {
        RECT logo = { logo_x, logo_y, logo_x + logo_size,
                      logo_y + logo_size };
        ui_fill_round_rect(dc, logo, ui_scale(theme, 12), UI_COLOR_ACCENT);
        ui_draw_text(dc, theme->font_h2, RGB(255, 255, 255), logo, L"P",
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    RECT title = { ui_scale(theme, 76), ui_scale(theme, 23),
                   width - ui_scale(theme, 14), ui_scale(theme, 54) };
    ui_draw_text(dc, theme->font_h2, UI_COLOR_SIDEBAR_TEXT, title, L"ElevenLedger",
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT tagline = { ui_scale(theme, 77), ui_scale(theme, 51),
                     width - ui_scale(theme, 12), ui_scale(theme, 68) };
    ui_draw_text(dc, theme->font_small, UI_COLOR_SIDEBAR_MUTED, tagline,
                 L"FOOTBALL RATINGS",
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT section = { ui_scale(theme, 30), ui_scale(theme, 116),
                     width - ui_scale(theme, 20), ui_scale(theme, 134) };
    ui_draw_text(dc, theme->font_small_bold, UI_COLOR_SIDEBAR_MUTED, section,
                 L"WORKSPACE", DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    for (int i = 0; i < NAV_COUNT; ++i) {
        RECT rect = navigation_rect(app, i);
        bool active = (PageId)i == app->active_page;
        bool hovered = app->hovered_nav == i && !active;
        if (active || hovered) {
            ui_fill_round_rect(dc, rect, ui_scale(theme, 9),
                               active ? UI_COLOR_SIDEBAR_ACTIVE
                                      : UI_COLOR_SIDEBAR_HOVER);
        }
        if (active) {
            RECT marker = {
                rect.left,
                rect.top + ui_scale(theme, 12),
                rect.left + ui_scale(theme, 3),
                rect.bottom - ui_scale(theme, 12),
            };
            ui_fill_round_rect(dc, marker, ui_scale(theme, 2),
                               UI_COLOR_ACCENT);
        }

        COLORREF text_color = active ? UI_COLOR_SIDEBAR_TEXT
                                     : UI_COLOR_SIDEBAR_MUTED;
        int icon_size = ui_scale(theme, 18);
        int icon_x = rect.left + ui_scale(theme, 16);
        int icon_y = rect.top + (rect.bottom - rect.top - icon_size) / 2;
        draw_navigation_icon(dc, i, icon_x, icon_y, icon_size, text_color);

        RECT label = {
            icon_x + icon_size + ui_scale(theme, 13),
            rect.top,
            rect.right - ui_scale(theme, 8),
            rect.bottom,
        };
        ui_draw_text(dc, theme->font_body_bold, text_color, label,
                     g_navigation_labels[i],
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }

    int footer_y = client.bottom - ui_scale(theme, 96);
    ui_draw_line(dc, ui_scale(theme, 22), footer_y,
                 width - ui_scale(theme, 22), footer_y,
                 RGB(42, 66, 84));

    int dot = ui_scale(theme, 7);
    int dot_x = ui_scale(theme, 25);
    int dot_y = footer_y + ui_scale(theme, 23);
    HBRUSH dot_brush = CreateSolidBrush(UI_COLOR_ACCENT);
    HGDIOBJ old_brush = SelectObject(dc, dot_brush);
    HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, dot_x, dot_y, dot_x + dot, dot_y + dot);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(dot_brush);

    RECT status = { ui_scale(theme, 41), footer_y + ui_scale(theme, 14),
                    width - ui_scale(theme, 12),
                    footer_y + ui_scale(theme, 36) };
    const wchar_t *save_status = app->save_pending
                                     ? L"Saving changes..."
                                     : L"Saved automatically";
    ui_draw_text(dc, theme->font_small, UI_COLOR_SIDEBAR_MUTED, status,
                 save_status, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT version = { ui_scale(theme, 25), footer_y + ui_scale(theme, 51),
                     width - ui_scale(theme, 12),
                     footer_y + ui_scale(theme, 74) };
    wchar_t version_text[64];
    ui_format_wide(version_text, sizeof(version_text) / sizeof(version_text[0]),
                   L"ElevenLedger v%hs", ELEVENLEDGER_VERSION);
    ui_draw_text(dc, theme->font_small, RGB(98, 120, 136), version,
                 version_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static LRESULT CALLBACK main_window_proc(HWND hwnd, UINT message,
                                         WPARAM wparam, LPARAM lparam)
{
    App *app = (App *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    case WM_SIZE:
        if (app != NULL) {
            app_layout_pages(app);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (app != NULL) {
            PAINTSTRUCT paint;
            HDC target = BeginPaint(hwnd, &paint);
            UiCanvas canvas;
            if (ui_canvas_begin(&canvas, hwnd, target, &app->ui,
                                UI_COLOR_BG)) {
                RECT client;
                GetClientRect(hwnd, &client);
                RECT content = { sidebar_width(app), 0,
                                 client.right, client.bottom };
                HBRUSH background = app->ui.brush_bg;
                FillRect(canvas.dc, &content, background);
                main_window_draw_navigation(app, canvas.dc);
                ui_canvas_end(&canvas);
            }
            EndPaint(hwnd, &paint);
        }
        return 0;
    case WM_MOUSEMOVE: {
        if (app != NULL) {
            POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            int hovered = navigation_at_point(app, point);
            if (hovered != app->hovered_nav) {
                app->hovered_nav = hovered;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            TRACKMOUSEEVENT track = {
                .cbSize = sizeof(track),
                .dwFlags = TME_LEAVE,
                .hwndTrack = hwnd,
                .dwHoverTime = HOVER_DEFAULT,
            };
            TrackMouseEvent(&track);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (app != NULL && app->hovered_nav != -1) {
            app->hovered_nav = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_SETCURSOR:
        if (app != NULL && LOWORD(lparam) == HTCLIENT) {
            POINT point;
            GetCursorPos(&point);
            ScreenToClient(hwnd, &point);
            if (navigation_at_point(app, point) >= 0) {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;
    case WM_LBUTTONUP:
        if (app != NULL) {
            POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            int index = navigation_at_point(app, point);
            if (index >= 0 && index < PAGE_COUNT) {
                app_navigate(app, (PageId)index);
            }
        }
        return 0;
    case WM_COMMAND:
        if (app != NULL) {
            int command = LOWORD(wparam);
            if (command == CMD_CREATE_ENTRY) {
                Page *page = app->pages[app->active_page];
                if (page != NULL && page->create_command != 0) {
                    SendMessageW(page->hwnd, WM_COMMAND,
                                 MAKEWPARAM(page->create_command, BN_CLICKED),
                                 0);
                }
                return 0;
            }
            if (command >= CMD_NAVIGATE_FIRST &&
                command < CMD_NAVIGATE_FIRST + PAGE_COUNT) {
                app_navigate(app, (PageId)(command - CMD_NAVIGATE_FIRST));
                return 0;
            }
        }
        break;
    case WM_GETMINMAXINFO:
        if (app != NULL) {
            MINMAXINFO *info = (MINMAXINFO *)lparam;
            int minimum_width = ui_scale(&app->ui, 1060);
            int minimum_height = ui_scale(&app->ui, 680);
            MONITORINFO monitor;
            memset(&monitor, 0, sizeof(monitor));
            monitor.cbSize = sizeof(monitor);
            if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST),
                                &monitor)) {
                int available_width = monitor.rcWork.right -
                                      monitor.rcWork.left;
                int available_height = monitor.rcWork.bottom -
                                       monitor.rcWork.top;
                if (minimum_width > available_width - ui_scale(&app->ui, 24)) {
                    minimum_width = available_width -
                                    ui_scale(&app->ui, 24);
                }
                if (minimum_height > available_height - ui_scale(&app->ui, 24)) {
                    minimum_height = available_height -
                                     ui_scale(&app->ui, 24);
                }
            }
            info->ptMinTrackSize.x = minimum_width;
            info->ptMinTrackSize.y = minimum_height;
        }
        return 0;
    case APP_WM_SAVE_COMPLETE:
        if (app != NULL) {
            app_handle_save_complete(app, wparam);
        }
        return 0;
    case WM_CLOSE:
        if (app != NULL && app->save_job != NULL) {
            app->close_when_saved = true;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

bool main_window_register_class(HINSTANCE instance)
{
    WNDCLASSEXW window_class;
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = main_window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = NULL;
    window_class.lpszClassName = MAIN_WINDOW_CLASS;
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    window_class.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    return RegisterClassExW(&window_class) != 0;
}

HWND main_window_create(struct App *app)
{
    int default_width = ui_scale(&app->ui, 1220);
    int default_height = ui_scale(&app->ui, 800);
    RECT work_area;
    if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0)) {
        int max_width = (work_area.right - work_area.left) - ui_scale(&app->ui, 80);
        int max_height = (work_area.bottom - work_area.top) - ui_scale(&app->ui, 80);
        if (default_width > max_width) {
            default_width = max_width;
        }
        if (default_height > max_height) {
            default_height = max_height;
        }
    }
    RECT desired = { 0, 0, default_width, default_height };
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    AdjustWindowRectEx(&desired, style, FALSE, 0);
    HWND hwnd = CreateWindowExW(
        WS_EX_CONTROLPARENT, MAIN_WINDOW_CLASS,
        L"ElevenLedger - Football Rating Calculator",
        style | WS_CLIPSIBLINGS,
        CW_USEDEFAULT, CW_USEDEFAULT,
        desired.right - desired.left, desired.bottom - desired.top,
        NULL, NULL, app->instance, app);
    if (hwnd == NULL) {
        return NULL;
    }
    SetWindowTextW(hwnd, L"ElevenLedger - Football Rating Calculator");
    ui_center_window(hwnd, NULL);
    return hwnd;
}
