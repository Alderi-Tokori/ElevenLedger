#include "ui/team_combo.h"

#include <commctrl.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#define TEAM_COMBO_LIST_SUBCLASS_ID 1

static size_t team_combo_roster_size(const Database *db, uint64_t team_id)
{
    if (db == NULL) {
        return 0;
    }
    if (team_id == 0) {
        return db->player_count;
    }
    size_t count = 0;
    for (size_t i = 0; i < db->player_count; ++i) {
        if (db->players[i].team_id == team_id) {
            count++;
        }
    }
    return count;
}

static void team_combo_monogram(const Team *team, wchar_t *buffer,
                                size_t count)
{
    if (buffer == NULL || count == 0) {
        return;
    }
    buffer[0] = L'\0';
    wchar_t name[128];
    if (team == NULL ||
        !ui_utf8_to_wide(team->name, name,
                         sizeof(name) / sizeof(name[0]))) {
        ui_format_wide(buffer, count, L"?");
        return;
    }
    const wchar_t *word = name;
    while (*word != L'\0' && iswspace(*word)) {
        word++;
    }
    if (*word == L'\0') {
        ui_format_wide(buffer, count, L"?");
        return;
    }
    wchar_t letters[3];
    int length = 0;
    letters[length++] = (wchar_t)towupper(*word);
    const wchar_t *rest = word;
    while (*rest != L'\0' && !iswspace(*rest)) {
        rest++;
    }
    while (*rest != L'\0' && iswspace(*rest)) {
        rest++;
    }
    if (*rest != L'\0') {
        letters[length++] = (wchar_t)towupper(*rest);
    } else if (word[1] != L'\0') {
        letters[length++] = (wchar_t)towupper(word[1]);
    }
    letters[length] = L'\0';
    ui_format_wide(buffer, count, L"%ls", letters);
}

typedef struct {
    COLORREF fill;
    COLORREF text;
} TeamComboBadgeColors;

static TeamComboBadgeColors team_combo_badge_colors(uint64_t team_id)
{
    static const COLORREF fills[4] = {
        UI_COLOR_ACCENT_SOFT, UI_COLOR_BLUE_SOFT,
        UI_COLOR_AMBER_SOFT, UI_COLOR_NEUTRAL_SOFT,
    };
    static const COLORREF texts[4] = {
        UI_COLOR_ACCENT, UI_COLOR_BLUE,
        UI_COLOR_AMBER, UI_COLOR_NEUTRAL,
    };
    size_t index = team_id == 0 ? 0 : (size_t)((team_id - 1) % 4);
    TeamComboBadgeColors colors = { fills[index], texts[index] };
    return colors;
}

static void team_combo_draw_grid(HDC dc, const UiTheme *theme, RECT badge,
                                 COLORREF color)
{
    int cell = ui_scale(theme, 3);
    int gap = ui_scale(theme, 2);
    if (cell < 1) {
        cell = 1;
    }
    if (gap < 1) {
        gap = 1;
    }
    int size = cell * 2 + gap;
    int left = badge.left + (badge.right - badge.left - size) / 2;
    int top = badge.top + (badge.bottom - badge.top - size) / 2;
    HBRUSH brush = CreateSolidBrush(color);
    if (brush == NULL) {
        return;
    }
    RECT top_left = { left, top, left + cell, top + cell };
    RECT top_right = { left + cell + gap, top,
                       left + cell + gap + cell, top + cell };
    RECT bottom_left = { left, top + cell + gap,
                         left + cell, top + cell + gap + cell };
    RECT bottom_right = { left + cell + gap, top + cell + gap,
                          left + cell + gap + cell,
                          top + cell + gap + cell };
    FillRect(dc, &top_left, brush);
    FillRect(dc, &top_right, brush);
    FillRect(dc, &bottom_left, brush);
    FillRect(dc, &bottom_right, brush);
    DeleteObject(brush);
}

bool ui_team_combo_draw_item(UiTeamCombo *view, DRAWITEMSTRUCT *item)
{
    if (view == NULL || item == NULL || view->combo == NULL ||
        view->db == NULL || view->theme == NULL ||
        item->hwndItem != view->combo) {
        return false;
    }
    const UiTheme *theme = view->theme;
    const Database *db = view->db;
    HWND combo = item->hwndItem;
    bool closed = (item->itemState & ODS_COMBOBOXEDIT) != 0;
    bool selected = (item->itemState & ODS_SELECTED) != 0;
    bool disabled = (item->itemState & ODS_DISABLED) != 0 ||
                    !IsWindowEnabled(combo);

    int index = (int)item->itemID;
    if (index < 0) {
        index = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    }
    wchar_t label[128];
    label[0] = L'\0';
    uint64_t team_id = 0;
    if (index >= 0) {
        SendMessageW(combo, CB_GETLBTEXT, (WPARAM)index, (LPARAM)label);
        team_id = (uint64_t)SendMessageW(combo, CB_GETITEMDATA,
                                         (WPARAM)index, 0);
    }
    const Team *team = team_id != 0 ? db_find_team(db, team_id) : NULL;

    RECT row = item->rcItem;
    if (closed) {
        COMBOBOXINFO info;
        memset(&info, 0, sizeof(info));
        info.cbSize = sizeof(info);
        if (GetComboBoxInfo(combo, &info)) {
            RECT field = info.rcItem;
            IntersectRect(&row, &row, &field);
        }
    }
    if (row.right <= row.left || row.bottom <= row.top) {
        return true;
    }

    bool hovered = !closed && !disabled && index >= 0 &&
                   index == view->hover_index;
    COLORREF background;
    if (disabled) {
        background = UI_COLOR_NEUTRAL_SOFT;
    } else if (closed) {
        background = UI_COLOR_FIELD_BG;
    } else if (selected) {
        background = UI_COLOR_SELECTION;
    } else if (hovered) {
        background = UI_COLOR_BG;
    } else {
        background = UI_COLOR_CARD;
    }
    HBRUSH background_brush = CreateSolidBrush(background);
    if (background_brush != NULL) {
        FillRect(item->hDC, &row, background_brush);
        DeleteObject(background_brush);
    }

    if (selected && !closed) {
        int bar_width = ui_scale(theme, 3);
        RECT bar = { row.left, row.top, row.left + bar_width, row.bottom };
        HBRUSH accent_brush = CreateSolidBrush(UI_COLOR_ACCENT);
        if (accent_brush != NULL && bar.right > bar.left) {
            FillRect(item->hDC, &bar, accent_brush);
        }
        if (accent_brush != NULL) {
            DeleteObject(accent_brush);
        }
    }

    int row_height = row.bottom - row.top;
    int badge_size = ui_scale(theme, closed ? 22 : 24);
    int max_badge = row_height - ui_scale(theme, 6);
    if (badge_size > max_badge) {
        badge_size = max_badge;
    }
    int pad = ui_scale(theme, closed ? 8 : 10);
    int gap = ui_scale(theme, 8);
    int badge_left = row.left + pad;
    int badge_top = row.top + (row_height - badge_size) / 2;
    RECT badge = {
        badge_left,
        badge_top,
        badge_left + badge_size,
        badge_top + badge_size,
    };

    if (badge_size > 0) {
        if (team_id == 0) {
            ui_fill_round_rect(item->hDC, badge, badge_size / 2,
                               disabled ? UI_COLOR_NEUTRAL_SOFT
                                        : UI_COLOR_ACCENT_SOFT);
            team_combo_draw_grid(item->hDC, theme, badge,
                                 disabled ? UI_COLOR_FAINT
                                          : UI_COLOR_ACCENT);
        } else {
            TeamComboBadgeColors colors = team_combo_badge_colors(team_id);
            ui_fill_round_rect(item->hDC, badge, badge_size / 2,
                               disabled ? UI_COLOR_NEUTRAL_SOFT
                                        : colors.fill);
            wchar_t monogram[3];
            team_combo_monogram(team, monogram,
                                sizeof(monogram) / sizeof(monogram[0]));
            ui_draw_text(item->hDC, theme->font_small_bold,
                         disabled ? UI_COLOR_FAINT : colors.text, badge,
                         monogram,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    int text_left = badge_size > 0 ? badge.right + gap : row.left + pad;
    int text_right = row.right - pad;
    RECT count_rect = { 0, 0, 0, 0 };
    if (!closed) {
        int count_width = ui_scale(theme, 66);
        int count_left = row.right - pad - count_width;
        if (count_left > text_left + ui_scale(theme, 8)) {
            count_rect.left = count_left;
            count_rect.top = row.top;
            count_rect.right = row.right - pad;
            count_rect.bottom = row.bottom;
            text_right = count_left - ui_scale(theme, 6);
        }
    }
    if (text_left < text_right) {
        RECT text_rect = { text_left, row.top, text_right, row.bottom };
        ui_draw_text(item->hDC,
                     selected && !closed ? theme->font_body_bold
                                         : theme->font_body,
                     disabled ? UI_COLOR_FAINT : UI_COLOR_TEXT, text_rect,
                     label,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    if (count_rect.right > count_rect.left) {
        size_t roster = team_combo_roster_size(db, team_id);
        wchar_t count_text[32];
        ui_format_wide(count_text,
                       sizeof(count_text) / sizeof(count_text[0]),
                       roster == 1 ? L"%zu player" : L"%zu players",
                       roster);
        ui_draw_text(item->hDC, theme->font_small,
                     disabled ? UI_COLOR_FAINT
                              : (selected ? UI_COLOR_ACCENT
                                          : UI_COLOR_MUTED),
                     count_rect, count_text,
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    return true;
}

static LRESULT CALLBACK team_combo_list_proc(HWND hwnd, UINT message,
                                             WPARAM wparam, LPARAM lparam,
                                             UINT_PTR id, DWORD_PTR ref)
{
    UiTeamCombo *view = (UiTeamCombo *)ref;
    switch (message) {
    case WM_MOUSEMOVE:
        if (view != NULL) {
            LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
            int hovered = HIWORD(hit) != 0 ? -1 : (int)LOWORD(hit);
            if (hovered != view->hover_index) {
                view->hover_index = hovered;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            if (!view->tracking) {
                TRACKMOUSEEVENT track = {
                    .cbSize = sizeof(track),
                    .dwFlags = TME_LEAVE,
                    .hwndTrack = hwnd,
                    .dwHoverTime = HOVER_DEFAULT,
                };
                if (TrackMouseEvent(&track)) {
                    view->tracking = true;
                }
            }
        }
        break;
    case WM_MOUSELEAVE:
        if (view != NULL) {
            view->tracking = false;
            if (view->hover_index != -1) {
                view->hover_index = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, team_combo_list_proc, id);
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

void ui_team_combo_init(UiTeamCombo *view, HWND combo, const Database *db,
                        const UiTheme *theme)
{
    if (view == NULL) {
        return;
    }
    memset(view, 0, sizeof(*view));
    view->combo = combo;
    view->db = db;
    view->theme = theme;
    view->hover_index = -1;
    if (combo == NULL) {
        return;
    }
    COMBOBOXINFO info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(combo, &info) && info.hwndList != NULL) {
        SetWindowSubclass(info.hwndList, team_combo_list_proc,
                          TEAM_COMBO_LIST_SUBCLASS_ID, (DWORD_PTR)view);
    }
}

void ui_team_combo_reset_hover(UiTeamCombo *view)
{
    if (view == NULL) {
        return;
    }
    view->hover_index = -1;
    view->tracking = false;
}
