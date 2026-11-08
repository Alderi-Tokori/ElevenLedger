#include "ui/controls.h"

#include "ui/theme.h"

#include <stdio.h>
#include <string.h>
#include <uxtheme.h>
#include <wchar.h>
#include <windowsx.h>

static HINSTANCE g_control_instance = NULL;

static int control_scale(HWND hwnd, int value)
{
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) {
        dpi = 96;
    }
    return MulDiv(value, (int)dpi, 96);
}

#define BUTTON_STYLE_MASK 0x0F
#define BUTTON_HOVER_FLAG 0x0100
#define BUTTON_PRESSED_FLAG 0x0200
#define BUTTON_TRACK_FLAG 0x0400
#define BUTTON_CLASS_NAME L"PitchRateButton"

#define CHECKBOX_CHECKED_FLAG 0x01
#define CHECKBOX_HOVER_FLAG 0x02
#define CHECKBOX_TRACK_FLAG 0x04
#define CHECKBOX_CLASS_NAME L"PitchRateCheckbox"

static LRESULT CALLBACK button_window_proc(HWND hwnd, UINT message,
                                           WPARAM wparam, LPARAM lparam);
static LRESULT CALLBACK checkbox_window_proc(HWND hwnd, UINT message,
                                             WPARAM wparam, LPARAM lparam);

void ui_register_control_classes(HINSTANCE instance)
{
    g_control_instance = instance;
    WNDCLASSEXW window_class;
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = button_window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hbrBackground = NULL;
    window_class.lpszClassName = BUTTON_CLASS_NAME;
    RegisterClassExW(&window_class);

    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = checkbox_window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_HAND);
    window_class.hbrBackground = NULL;
    window_class.lpszClassName = CHECKBOX_CLASS_NAME;
    RegisterClassExW(&window_class);
}

static UiButtonStyle button_style(HWND hwnd)
{
    LONG_PTR value = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    int style = (int)(value & BUTTON_STYLE_MASK);
    if (style < UI_BUTTON_PRIMARY || style > UI_BUTTON_SOFT) {
        style = UI_BUTTON_SECONDARY;
    }
    return (UiButtonStyle)style;
}

static bool button_flag(HWND hwnd, LONG_PTR flag)
{
    return (GetWindowLongPtrW(hwnd, GWLP_USERDATA) & flag) != 0;
}

static void button_set_flag(HWND hwnd, LONG_PTR flag, bool enabled)
{
    LONG_PTR value = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (enabled) {
        value |= flag;
    } else {
        value &= ~flag;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, value);
}

static void button_paint(HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC target = BeginPaint(hwnd, &paint);
    RECT client;
    GetClientRect(hwnd, &client);

    UiTheme theme;
    memset(&theme, 0, sizeof(theme));
    theme.dpi = 96;
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    if (dc != NULL && bitmap != NULL) {
        HGDIOBJ old = SelectObject(dc, bitmap);
        HBRUSH background = CreateSolidBrush(UI_COLOR_BG);
        FillRect(dc, &client, background);
        DeleteObject(background);

        UiButtonStyle style = button_style(hwnd);
        bool enabled = IsWindowEnabled(hwnd) != FALSE;
        bool hovered = button_flag(hwnd, BUTTON_HOVER_FLAG);
        bool pressed = button_flag(hwnd, BUTTON_PRESSED_FLAG);
        COLORREF fill;
        COLORREF border;
        COLORREF text;
        if (!enabled) {
            fill = RGB(236, 239, 242);
            border = RGB(223, 228, 233);
            text = UI_COLOR_FAINT;
        } else {
            switch (style) {
            case UI_BUTTON_PRIMARY:
                fill = pressed ? UI_COLOR_ACCENT_PRESSED
                               : (hovered ? UI_COLOR_ACCENT_HOVER
                                          : UI_COLOR_ACCENT);
                border = fill;
                text = RGB(255, 255, 255);
                break;
            case UI_BUTTON_DANGER:
                fill = pressed ? UI_COLOR_DANGER_HOVER
                               : (hovered ? RGB(178, 50, 50)
                                          : UI_COLOR_DANGER);
                border = fill;
                text = RGB(255, 255, 255);
                break;
            case UI_BUTTON_GHOST:
                fill = hovered ? RGB(235, 240, 244) : UI_COLOR_BG;
                border = fill;
                text = UI_COLOR_MUTED;
                break;
            case UI_BUTTON_SOFT:
                fill = hovered ? RGB(219, 241, 229) : UI_COLOR_ACCENT_SOFT;
                border = fill;
                text = UI_COLOR_ACCENT;
                break;
            case UI_BUTTON_SECONDARY:
            default:
                fill = pressed ? RGB(235, 240, 244)
                               : (hovered ? RGB(249, 251, 252)
                                          : RGB(255, 255, 255));
                border = hovered ? UI_COLOR_BORDER_STRONG : UI_COLOR_BORDER;
                text = UI_COLOR_TEXT;
                break;
            }
        }

        RECT shape = client;
        ui_stroke_round_rect(dc, shape, control_scale(hwnd, 8), fill, border);

        wchar_t text_buffer[256];
        GetWindowTextW(hwnd, text_buffer,
                       (int)(sizeof(text_buffer) / sizeof(text_buffer[0])));
        RECT text_rect = client;
        if (pressed) {
            OffsetRect(&text_rect, 0, 1);
        }
        HFONT font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        ui_draw_text(dc, font, text, text_rect, text_buffer,
                     DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        if (GetFocus() == hwnd && enabled) {
            RECT focus_rect = client;
            InflateRect(&focus_rect, -4, -4);
            DrawFocusRect(dc, &focus_rect);
        }

        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
    } else {
        if (dc != NULL) {
            DeleteDC(dc);
        }
        if (bitmap != NULL) {
            DeleteObject(bitmap);
        }
        FillRect(target, &client, (HBRUSH)(COLOR_BTNFACE + 1));
    }
    EndPaint(hwnd, &paint);
}

static void button_track_mouse(HWND hwnd)
{
    if (button_flag(hwnd, BUTTON_TRACK_FLAG)) {
        return;
    }
    TRACKMOUSEEVENT event = {
        .cbSize = sizeof(event),
        .dwFlags = TME_LEAVE,
        .hwndTrack = hwnd,
        .dwHoverTime = HOVER_DEFAULT,
    };
    if (TrackMouseEvent(&event)) {
        button_set_flag(hwnd, BUTTON_TRACK_FLAG, true);
    }
}

static void button_notify_click(HWND hwnd)
{
    HWND parent = GetParent(hwnd);
    if (parent != NULL) {
        SendMessageW(parent, WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED),
                     (LPARAM)hwnd);
    }
}

static LRESULT CALLBACK button_window_proc(HWND hwnd, UINT message,
                                           WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          (LONG_PTR)(INT_PTR)create->lpCreateParams);
        return TRUE;
    }
    case WM_MOUSEMOVE:
        button_track_mouse(hwnd);
        if (!button_flag(hwnd, BUTTON_HOVER_FLAG)) {
            button_set_flag(hwnd, BUTTON_HOVER_FLAG, true);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        button_set_flag(hwnd, BUTTON_TRACK_FLAG, false);
        if (button_flag(hwnd, BUTTON_HOVER_FLAG)) {
            button_set_flag(hwnd, BUTTON_HOVER_FLAG, false);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (IsWindowEnabled(hwnd)) {
            SetFocus(hwnd);
            SetCapture(hwnd);
            button_set_flag(hwnd, BUTTON_PRESSED_FLAG, true);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (button_flag(hwnd, BUTTON_PRESSED_FLAG)) {
            button_set_flag(hwnd, BUTTON_PRESSED_FLAG, false);
            ReleaseCapture();
            POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            RECT client;
            GetClientRect(hwnd, &client);
            if (PtInRect(&client, point) && IsWindowEnabled(hwnd)) {
                button_notify_click(hwnd);
                if (!IsWindow(hwnd)) {
                    return 0;
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (button_flag(hwnd, BUTTON_PRESSED_FLAG)) {
            button_set_flag(hwnd, BUTTON_PRESSED_FLAG, false);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if ((wparam == VK_SPACE || wparam == VK_RETURN) &&
            IsWindowEnabled(hwnd) && !button_flag(hwnd, BUTTON_PRESSED_FLAG)) {
            button_set_flag(hwnd, BUTTON_PRESSED_FLAG, true);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        break;
    case WM_KEYUP:
        if ((wparam == VK_SPACE || wparam == VK_RETURN) &&
            button_flag(hwnd, BUTTON_PRESSED_FLAG)) {
            button_set_flag(hwnd, BUTTON_PRESSED_FLAG, false);
            InvalidateRect(hwnd, NULL, FALSE);
            if (IsWindowEnabled(hwnd)) {
                button_notify_click(hwnd);
            }
            return 0;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        button_paint(hwnd);
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT && IsWindowEnabled(hwnd)) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_GETDLGCODE:
        return DLGC_BUTTON | DLGC_WANTCHARS;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

HWND ui_button_create(HWND parent, int id, const wchar_t *text,
                      int x, int y, int width, int height,
                      UiButtonStyle style, HFONT font)
{
    HWND button = CreateWindowExW(
        0, BUTTON_CLASS_NAME, text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x, y, width, height, parent, (HMENU)(INT_PTR)id,
        g_control_instance, (LPVOID)(INT_PTR)style);
    if (button != NULL) {
        SetWindowTextW(button, text != NULL ? text : L"");
        SendMessageW(button, WM_SETFONT, (WPARAM)font, TRUE);
    }
    return button;
}

void ui_button_set_text(HWND button, const wchar_t *text)
{
    SetWindowTextW(button, text);
    InvalidateRect(button, NULL, FALSE);
}

static void edit_set_margins(HWND edit, int scale)
{
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(scale, scale));
}

HWND ui_edit_create(HWND parent, int id, const wchar_t *text,
                    int x, int y, int width, int height, HFONT font,
                    int limit)
{
    HWND edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        x, y, width, height, parent, (HMENU)(INT_PTR)id,
        g_control_instance, NULL);
    if (edit != NULL) {
        SendMessageW(edit, WM_SETFONT, (WPARAM)font, TRUE);
        if (limit > 0) {
            SendMessageW(edit, EM_SETLIMITTEXT, (WPARAM)limit, 0);
        }
        edit_set_margins(edit, 8);
    }
    return edit;
}

void ui_edit_set_cue(HWND edit, const wchar_t *cue)
{
    SendMessageW(edit, EM_SETCUEBANNER, TRUE, (LPARAM)cue);
}

void ui_edit_get_utf8(HWND edit, char *buffer, size_t buffer_size)
{
    wchar_t wide[512];
    ui_edit_get_wide(edit, wide, sizeof(wide) / sizeof(wide[0]));
    ui_wide_to_utf8(wide, buffer, buffer_size);
}

void ui_edit_set_utf8(HWND edit, const char *text)
{
    wchar_t wide[512];
    if (!ui_utf8_to_wide(text, wide, sizeof(wide) / sizeof(wide[0]))) {
        wide[0] = L'\0';
    }
    SetWindowTextW(edit, wide);
}

void ui_edit_get_wide(HWND edit, wchar_t *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0) {
        return;
    }
    GetWindowTextW(edit, buffer, (int)buffer_size);
}

HWND ui_combo_create(HWND parent, int id, int x, int y, int width, int height,
                     HFONT font)
{
    HWND combo = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
            CBS_DROPDOWNLIST,
        x, y, width, height, parent, (HMENU)(INT_PTR)id,
        g_control_instance, NULL);
    if (combo != NULL) {
        SendMessageW(combo, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(combo, CB_SETMINVISIBLE, 10, 0);
    }
    return combo;
}

int ui_combo_add_utf8(HWND combo, const char *text, uint64_t data)
{
    wchar_t wide[256];
    if (!ui_utf8_to_wide(text, wide, sizeof(wide) / sizeof(wide[0]))) {
        wide[0] = L'\0';
    }
    return ui_combo_add_wide(combo, wide, data);
}

int ui_combo_add_wide(HWND combo, const wchar_t *text, uint64_t data)
{
    LRESULT index = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text);
    if (index != CB_ERR && index != CB_ERRSPACE) {
        SendMessageW(combo, CB_SETITEMDATA, (WPARAM)index, (LPARAM)data);
        return (int)index;
    }
    return -1;
}

void ui_combo_select_data(HWND combo, uint64_t data)
{
    int count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; ++i) {
        if ((uint64_t)SendMessageW(combo, CB_GETITEMDATA, (WPARAM)i, 0) ==
            data) {
            SendMessageW(combo, CB_SETCURSEL, (WPARAM)i, 0);
            return;
        }
    }
    if (count > 0) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
}

uint64_t ui_combo_selected_data(HWND combo)
{
    LRESULT selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selected == CB_ERR) {
        return 0;
    }
    return (uint64_t)SendMessageW(combo, CB_GETITEMDATA,
                                  (WPARAM)selected, 0);
}

HWND ui_checkbox_create(HWND parent, int id, const wchar_t *text,
                        int x, int y, int width, int height, HFONT font)
{
    HWND checkbox = CreateWindowExW(
        0, CHECKBOX_CLASS_NAME, text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x, y, width, height, parent, (HMENU)(INT_PTR)id,
        g_control_instance, NULL);
    if (checkbox != NULL) {
        SetWindowTextW(checkbox, text != NULL ? text : L"");
        SendMessageW(checkbox, WM_SETFONT, (WPARAM)font, TRUE);
    }
    return checkbox;
}

void ui_checkbox_set_checked(HWND checkbox, bool checked)
{
    LONG_PTR value = GetWindowLongPtrW(checkbox, GWLP_USERDATA);
    if (checked) {
        value |= CHECKBOX_CHECKED_FLAG;
    } else {
        value &= ~CHECKBOX_CHECKED_FLAG;
    }
    SetWindowLongPtrW(checkbox, GWLP_USERDATA, value);
    InvalidateRect(checkbox, NULL, FALSE);
}

bool ui_checkbox_get_checked(HWND checkbox)
{
    return (GetWindowLongPtrW(checkbox, GWLP_USERDATA) &
            CHECKBOX_CHECKED_FLAG) != 0;
}

static void checkbox_draw(HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC target = BeginPaint(hwnd, &paint);
    RECT client;
    GetClientRect(hwnd, &client);
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right,
                                            client.bottom);
    if (dc != NULL && bitmap != NULL) {
        HGDIOBJ old = SelectObject(dc, bitmap);
        HBRUSH background = CreateSolidBrush(UI_COLOR_BG);
        FillRect(dc, &client, background);
        DeleteObject(background);

        bool checked =
            (GetWindowLongPtrW(hwnd, GWLP_USERDATA) &
             CHECKBOX_CHECKED_FLAG) != 0;
        bool hovered =
            (GetWindowLongPtrW(hwnd, GWLP_USERDATA) &
             CHECKBOX_HOVER_FLAG) != 0;
        bool enabled = IsWindowEnabled(hwnd) != FALSE;
        int box_size = control_scale(hwnd, 18);
        int edge = control_scale(hwnd, 1);
        RECT box = { edge, (client.bottom - box_size) / 2,
                     edge + box_size,
                     (client.bottom - box_size) / 2 + box_size };
        COLORREF border = checked ? UI_COLOR_ACCENT
                                  : (hovered ? UI_COLOR_ACCENT
                                             : UI_COLOR_BORDER_STRONG);
        if (!enabled) {
            border = UI_COLOR_BORDER;
        }
        ui_stroke_round_rect(dc, box, control_scale(hwnd, 4),
                             checked ? UI_COLOR_ACCENT : UI_COLOR_FIELD_BG,
                             border);
        if (checked) {
            int pen_width = control_scale(hwnd, 2);
            if (pen_width < 1) {
                pen_width = 1;
            }
            HPEN pen = CreatePen(PS_SOLID, pen_width, RGB(255, 255, 255));
            HGDIOBJ old_pen = SelectObject(dc, pen);
            MoveToEx(dc, box.left + control_scale(hwnd, 4),
                     box.top + control_scale(hwnd, 9), NULL);
            LineTo(dc, box.left + control_scale(hwnd, 8),
                   box.top + control_scale(hwnd, 13));
            LineTo(dc, box.left + control_scale(hwnd, 14),
                   box.top + control_scale(hwnd, 5));
            SelectObject(dc, old_pen);
            DeleteObject(pen);
        }

        wchar_t label[512];
        GetWindowTextW(hwnd, label,
                       (int)(sizeof(label) / sizeof(label[0])));
        RECT text_rect = { box.right + control_scale(hwnd, 9), 0,
                           client.right, client.bottom };
        HFONT font = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
        ui_draw_text(dc, font,
                     enabled ? UI_COLOR_TEXT : UI_COLOR_FAINT,
                     text_rect, label,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                         DT_END_ELLIPSIS);

        if (GetFocus() == hwnd && enabled) {
            RECT focus_rect = client;
            InflateRect(&focus_rect, -1, -1);
            DrawFocusRect(dc, &focus_rect);
        }

        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
    } else {
        if (dc != NULL) {
            DeleteDC(dc);
        }
        if (bitmap != NULL) {
            DeleteObject(bitmap);
        }
    }
    EndPaint(hwnd, &paint);
}

static void checkbox_toggle(HWND hwnd)
{
    LONG_PTR value = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    value ^= CHECKBOX_CHECKED_FLAG;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, value);
    InvalidateRect(hwnd, NULL, FALSE);
    HWND parent = GetParent(hwnd);
    if (parent != NULL) {
        SendMessageW(parent, WM_COMMAND,
                     MAKEWPARAM(GetDlgCtrlID(hwnd), BN_CLICKED),
                     (LPARAM)hwnd);
    }
}

static LRESULT CALLBACK checkbox_window_proc(HWND hwnd, UINT message,
                                             WPARAM wparam, LPARAM lparam)
{
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        SetWindowTextW(hwnd, create->lpszName);
        return TRUE;
    }
    case WM_MOUSEMOVE: {
        LONG_PTR value = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if ((value & CHECKBOX_TRACK_FLAG) == 0) {
            TRACKMOUSEEVENT track = {
                .cbSize = sizeof(track),
                .dwFlags = TME_LEAVE,
                .hwndTrack = hwnd,
                .dwHoverTime = HOVER_DEFAULT,
            };
            if (TrackMouseEvent(&track)) {
                value |= CHECKBOX_TRACK_FLAG;
            }
        }
        if ((value & CHECKBOX_HOVER_FLAG) == 0) {
            value |= CHECKBOX_HOVER_FLAG;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, value);
        return 0;
    }
    case WM_MOUSELEAVE: {
        LONG_PTR value = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        value &= ~(CHECKBOX_TRACK_FLAG | CHECKBOX_HOVER_FLAG);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, value);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (IsWindowEnabled(hwnd)) {
            SetFocus(hwnd);
            checkbox_toggle(hwnd);
        }
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_SPACE && IsWindowEnabled(hwnd)) {
            checkbox_toggle(hwnd);
            return 0;
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        checkbox_draw(hwnd);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_BUTTON | DLGC_WANTCHARS;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

HWND ui_list_create(HWND parent, int id, int x, int y, int width, int height,
                    HFONT font, HIMAGELIST row_images, bool sortable)
{
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
                  LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS;
    if (!sortable) {
        style |= LVS_NOSORTHEADER;
    }
    HWND list = CreateWindowExW(
        0, WC_LISTVIEWW, L"", style,
        x, y, width, height, parent, (HMENU)(INT_PTR)id,
        g_control_instance, NULL);
    if (list == NULL) {
        return NULL;
    }
    ListView_SetExtendedListViewStyle(
        list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SetWindowTheme(list, L"Explorer", NULL);
    SendMessageW(list, WM_SETFONT, (WPARAM)font, TRUE);
    HWND header = (HWND)SendMessageW(list, LVM_GETHEADER, 0, 0);
    if (header != NULL) {
        SendMessageW(header, WM_SETFONT, (WPARAM)font, TRUE);
    }
    if (row_images != NULL) {
        SendMessageW(list, LVM_SETIMAGELIST, LVSIL_SMALL,
                     (LPARAM)row_images);
    }
    ListView_SetBkColor(list, UI_COLOR_CARD);
    ListView_SetTextBkColor(list, UI_COLOR_CARD);
    ListView_SetTextColor(list, UI_COLOR_TEXT);
    return list;
}

void ui_list_add_column(HWND list, int index, const wchar_t *title, int width)
{
    LVCOLUMNW column;
    memset(&column, 0, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM | LVCF_FMT;
    column.pszText = (LPWSTR)title;
    column.cx = width;
    column.iSubItem = index;
    column.fmt = LVCFMT_LEFT;
    SendMessageW(list, LVM_INSERTCOLUMNW, (WPARAM)index, (LPARAM)&column);
}

void ui_list_set_sort_indicator(HWND list, int column, bool ascending)
{
    HWND header = (HWND)SendMessageW(list, LVM_GETHEADER, 0, 0);
    if (header == NULL) {
        return;
    }
    int count = (int)SendMessageW(header, HDM_GETITEMCOUNT, 0, 0);
    for (int i = 0; i < count; ++i) {
        HDITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = HDI_FORMAT;
        if (!SendMessageW(header, HDM_GETITEMW, (WPARAM)i, (LPARAM)&item)) {
            continue;
        }
        item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == column) {
            item.fmt |= ascending ? HDF_SORTUP : HDF_SORTDOWN;
        }
        SendMessageW(header, HDM_SETITEMW, (WPARAM)i, (LPARAM)&item);
    }
}

int ui_text_compare(const wchar_t *left, const wchar_t *right)
{
    if (left == NULL) {
        left = L"";
    }
    if (right == NULL) {
        right = L"";
    }
    int result = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE,
                                 left, -1, right, -1, NULL, NULL, 0);
    if (result == CSTR_LESS_THAN) {
        return -1;
    }
    if (result == CSTR_GREATER_THAN) {
        return 1;
    }
    return 0;
}

void ui_list_set_percent_widths(HWND list, const int *percents, int count)
{
    if (list == NULL || percents == NULL || count <= 0) {
        return;
    }
    RECT client;
    GetClientRect(list, &client);
    int usable = client.right - GetSystemMetrics(SM_CXVSCROLL) - 4;
    if (usable < 80) {
        usable = 80;
    }
    int used = 0;
    for (int i = 0; i < count; ++i) {
        int width = (i == count - 1) ? usable - used
                                     : MulDiv(usable, percents[i], 100);
        if (width < 30) {
            width = 30;
        }
        ListView_SetColumnWidth(list, i, width);
        used += width;
    }
}

int ui_list_add_row(HWND list, const char **utf8_columns, int column_count,
                    LPARAM item_data)
{
    wchar_t wide[512];
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = ListView_GetItemCount(list);
    item.lParam = item_data;
    if (!ui_utf8_to_wide(column_count > 0 ? utf8_columns[0] : "",
                         wide, sizeof(wide) / sizeof(wide[0]))) {
        wide[0] = L'\0';
    }
    item.pszText = wide;
    int row = (int)SendMessageW(list, LVM_INSERTITEMW, 0, (LPARAM)&item);
    if (row < 0) {
        return -1;
    }
    for (int column = 1; column < column_count; ++column) {
        if (!ui_utf8_to_wide(utf8_columns[column], wide,
                             sizeof(wide) / sizeof(wide[0]))) {
            wide[0] = L'\0';
        }
        LVITEMW subitem;
        memset(&subitem, 0, sizeof(subitem));
        subitem.iSubItem = column;
        subitem.pszText = wide;
        SendMessageW(list, LVM_SETITEMTEXTW, (WPARAM)row, (LPARAM)&subitem);
    }
    return row;
}

bool ui_list_selected_data(HWND list, LPARAM *out_data)
{
    int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (selected < 0) {
        return false;
    }
    LVITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_PARAM;
    item.iItem = selected;
    if (!ListView_GetItem(list, &item)) {
        return false;
    }
    if (out_data != NULL) {
        *out_data = item.lParam;
    }
    return true;
}

LRESULT ui_list_custom_draw(NMLVCUSTOMDRAW *custom_draw, HIMAGELIST row_images)
{
    (void)row_images;
    if (custom_draw == NULL) {
        return CDRF_DODEFAULT;
    }
    switch (custom_draw->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        LRESULT index = (LRESULT)custom_draw->nmcd.dwItemSpec;
        bool selected = (ListView_GetItemState(
                             (HWND)custom_draw->nmcd.hdr.hwndFrom,
                             (int)index, LVIS_SELECTED) &
                         LVIS_SELECTED) != 0;
        custom_draw->clrText = UI_COLOR_TEXT;
        custom_draw->clrTextBk = selected
            ? UI_COLOR_SELECTION
            : ((index % 2) != 0 ? UI_COLOR_ROW_ALT : UI_COLOR_CARD);
        return CDRF_NEWFONT;
    }
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        int index = (int)custom_draw->nmcd.dwItemSpec;
        bool selected = (ListView_GetItemState(
                             (HWND)custom_draw->nmcd.hdr.hwndFrom,
                             index, LVIS_SELECTED) &
                         LVIS_SELECTED) != 0;
        custom_draw->clrText = UI_COLOR_TEXT;
        custom_draw->clrTextBk = selected
            ? UI_COLOR_SELECTION
            : ((index % 2) != 0 ? UI_COLOR_ROW_ALT : UI_COLOR_CARD);
        return CDRF_NEWFONT;
    }
    default:
        return CDRF_DODEFAULT;
    }
}

void ui_center_window(HWND window, HWND owner)
{
    RECT window_rect;
    RECT owner_rect;
    if (!GetWindowRect(window, &window_rect)) {
        return;
    }
    if (owner != NULL && IsWindow(owner) && GetWindowRect(owner, &owner_rect)) {
        int width = window_rect.right - window_rect.left;
        int height = window_rect.bottom - window_rect.top;
        int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
        int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
        MONITORINFO monitor;
        monitor.cbSize = sizeof(monitor);
        HMONITOR monitor_handle = MonitorFromWindow(
            owner != NULL && IsWindow(owner) ? owner : window,
            MONITOR_DEFAULTTONEAREST);
        if (GetMonitorInfoW(monitor_handle, &monitor)) {
            RECT work = monitor.rcWork;
            if (x + width > work.right) {
                x = work.right - width;
            }
            if (y + height > work.bottom) {
                y = work.bottom - height;
            }
            if (x < work.left) {
                x = work.left;
            }
            if (y < work.top) {
                y = work.top;
            }
        }
        SetWindowPos(window, NULL, x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info;
        info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info)) {
            int width = window_rect.right - window_rect.left;
            int height = window_rect.bottom - window_rect.top;
            int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
            int y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
            SetWindowPos(window, NULL, x, y, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
}

void ui_run_modal(HWND dialog, HWND owner, int default_command_id)
{
    if (dialog == NULL) {
        return;
    }
    if (owner != NULL) {
        EnableWindow(owner, FALSE);
    }
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    SetForegroundWindow(dialog);
    HWND first = GetNextDlgTabItem(dialog, NULL, FALSE);
    if (first != NULL) {
        SetFocus(first);
    }

    MSG message;
    while (IsWindow(dialog) && GetMessageW(&message, NULL, 0, 0) > 0) {
        if (message.message == WM_QUIT) {
            PostQuitMessage((int)message.wParam);
            break;
        }
        if (message.message == WM_KEYDOWN) {
            bool cell_editor =
                GetPropW(message.hwnd, L"PitchRate.CellEditor") != NULL;
            if (cell_editor &&
                (message.wParam == VK_RETURN || message.wParam == VK_ESCAPE)) {
                DispatchMessageW(&message);
                continue;
            }
            if (message.wParam == VK_ESCAPE && !cell_editor) {
                SendMessageW(dialog, WM_CLOSE, 0, 0);
                continue;
            }
            if (message.wParam == VK_RETURN && !cell_editor &&
                default_command_id != 0) {
                wchar_t class_name[32];
                GetClassNameW(message.hwnd, class_name,
                              (int)(sizeof(class_name) /
                                    sizeof(class_name[0])));
                if (_wcsicmp(class_name, L"Edit") == 0 &&
                    (GetWindowLongPtrW(message.hwnd, GWL_STYLE) & ES_MULTILINE)
                        == 0) {
                    SendMessageW(dialog, WM_COMMAND,
                                 MAKEWPARAM(default_command_id, BN_CLICKED),
                                 0);
                    continue;
                }
            }
        }
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (owner != NULL) {
        EnableWindow(owner, TRUE);
        BringWindowToTop(owner);
        SetForegroundWindow(owner);
        SetActiveWindow(owner);
        /*
         * A disabled owner can have its compositor surface discarded while a
         * large modal dialog covers it. Repaint the complete owner tree before
         * returning to the message loop so the uncovered window never presents
         * a stale or transparent frame.
         */
        RedrawWindow(owner, NULL, NULL,
                     RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN |
                         RDW_UPDATENOW);
    }
}
