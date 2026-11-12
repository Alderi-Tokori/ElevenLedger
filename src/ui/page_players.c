#include "ui/page.h"

#include "ui/controls.h"
#include "ui/dialogs.h"
#include "ui/pages.h"
#include "ui/theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <windowsx.h>

#define PLAYER_FIELD_HEIGHT 34
#define PLAYER_TEAM_FILTER_DROP_WIDTH 260

enum {
    PLAYER_LIST_ID = 1200,
    PLAYER_SEARCH_ID = 1201,
    PLAYER_TEAM_FILTER_ID = 1202,
    PLAYER_ADD_ID = 1203,
    PLAYER_EDIT_ID = 1204,
    PLAYER_ARCHIVE_ID = 1205,
    PLAYER_DELETE_ID = 1206,
};

typedef enum {
    PLAYER_COLUMN_NAME = 0,
    PLAYER_COLUMN_TEAM,
    PLAYER_COLUMN_RATING,
    PLAYER_COLUMN_MATCHES,
    PLAYER_COLUMN_MINUTES,
    PLAYER_COLUMN_STATUS,
    PLAYER_COLUMN_COUNT
} PlayerColumn;

typedef struct {
    const Database *db;
    int column;
    bool ascending;
} PlayerSortContext;

typedef struct {
    uint64_t id;
    uint64_t hash;
} PlayerRowCache;

typedef struct {
    char name_utf8[192];
    char team_utf8[128];
    char rating[32];
    char matches[32];
    char minutes[32];
    char status[32];
    const char *columns[6];
} PlayerRowText;

typedef struct {
    Page base;
    HWND list;
    HWND search;
    HWND team_filter;
    HWND add_button;
    HWND edit_button;
    HWND archive_button;
    HWND delete_button;
    bool populating;
    size_t visible_rows;
    int sort_column;
    bool sort_ascending;
    int team_filter_hover;
    bool team_filter_tracking;
    PlayerRowCache *cache;
    size_t cache_count;
    bool cache_valid;
} PlayersPage;

static uint64_t selected_player(PlayersPage *page)
{
    LPARAM data = 0;
    if (!ui_list_selected_data(page->list, &data)) {
        return 0;
    }
    return (uint64_t)data;
}

static bool contains_case_insensitive(const wchar_t *haystack,
                                      const wchar_t *needle)
{
    if (needle[0] == L'\0') {
        return true;
    }
    size_t hay_len = wcslen(haystack);
    size_t needle_len = wcslen(needle);
    if (needle_len > hay_len) {
        return false;
    }
    wchar_t lowered_hay[512];
    wchar_t lowered_needle[256];
    if (hay_len >= sizeof(lowered_hay) / sizeof(lowered_hay[0]) ||
        needle_len >= sizeof(lowered_needle) / sizeof(lowered_needle[0])) {
        return false;
    }
    for (size_t i = 0; i <= hay_len; ++i) {
        lowered_hay[i] = (wchar_t)towlower(haystack[i]);
    }
    for (size_t i = 0; i <= needle_len; ++i) {
        lowered_needle[i] = (wchar_t)towlower(needle[i]);
    }
    return wcsstr(lowered_hay, lowered_needle) != NULL;
}

static void players_player_name(const Player *player, wchar_t *buffer,
                                size_t count)
{
    wchar_t first[128];
    wchar_t last[128];
    ui_utf8_to_wide(player->first_name, first,
                    sizeof(first) / sizeof(first[0]));
    ui_utf8_to_wide(player->last_name, last,
                    sizeof(last) / sizeof(last[0]));
    if (first[0] != L'\0' && last[0] != L'\0') {
        ui_format_wide(buffer, count, L"%ls %ls", first, last);
    } else if (first[0] != L'\0') {
        ui_format_wide(buffer, count, L"%ls", first);
    } else {
        ui_format_wide(buffer, count, L"%ls", last);
    }
}

static void players_team_name(const Database *db, uint64_t team_id,
                              wchar_t *buffer, size_t count)
{
    const Team *team = db_find_team(db, team_id);
    if (team == NULL) {
        ui_format_wide(buffer, count, L"-");
        return;
    }
    ui_utf8_to_wide(team->name, buffer, count);
}

static size_t players_team_roster_size(const Database *db, uint64_t team_id)
{
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

static void players_team_monogram(const Team *team, wchar_t *buffer,
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
} PlayersTeamBadgeColors;

static PlayersTeamBadgeColors players_team_badge_colors(uint64_t team_id)
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
    PlayersTeamBadgeColors colors = { fills[index], texts[index] };
    return colors;
}

static bool players_row_matches(const Database *db, const Player *player,
                                const wchar_t *search_text,
                                uint64_t filter_team)
{
    if (filter_team != 0 && player->team_id != filter_team) {
        return false;
    }
    wchar_t name[160];
    wchar_t team_name[128];
    players_player_name(player, name, sizeof(name) / sizeof(name[0]));
    players_team_name(db, player->team_id, team_name,
                      sizeof(team_name) / sizeof(team_name[0]));
    wchar_t haystack[512];
    ui_format_wide(haystack, sizeof(haystack) / sizeof(haystack[0]),
                   L"%ls %ls", name, team_name);
    return contains_case_insensitive(haystack, search_text);
}

static void players_build_row(const Database *db, const Player *player,
                              PlayerRowText *row)
{
    wchar_t name[160];
    wchar_t team_name[128];
    players_player_name(player, name, sizeof(name) / sizeof(name[0]));
    players_team_name(db, player->team_id, team_name,
                      sizeof(team_name) / sizeof(team_name[0]));
    ui_wide_to_utf8(name, row->name_utf8, sizeof(row->name_utf8));
    ui_wide_to_utf8(team_name, row->team_utf8, sizeof(row->team_utf8));
    snprintf(row->rating, sizeof(row->rating), "%.1f",
             player->current_rating);
    snprintf(row->matches, sizeof(row->matches), "%u",
             player->matches_played);
    snprintf(row->minutes, sizeof(row->minutes), "%u",
             player->minutes_played);
    snprintf(row->status, sizeof(row->status), "%s",
             player->active ? "Active" : "Archived");
    row->columns[0] = row->name_utf8;
    row->columns[1] = row->team_utf8;
    row->columns[2] = row->rating;
    row->columns[3] = row->matches;
    row->columns[4] = row->minutes;
    row->columns[5] = row->status;
}

static const PlayerRowCache *players_cache_find(const PlayersPage *players,
                                                uint64_t player_id)
{
    for (size_t i = 0; i < players->cache_count; ++i) {
        if (players->cache[i].id == player_id) {
            return &players->cache[i];
        }
    }
    return NULL;
}

static int compare_values(uint64_t left, uint64_t right)
{
    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

static int players_compare_names(const Player *left, const Player *right)
{
    wchar_t left_name[160];
    wchar_t right_name[160];
    players_player_name(left, left_name,
                        sizeof(left_name) / sizeof(left_name[0]));
    players_player_name(right, right_name,
                        sizeof(right_name) / sizeof(right_name[0]));
    return ui_text_compare(left_name, right_name);
}

static int players_compare_columns(const PlayerSortContext *context,
                                   const Player *left, const Player *right)
{
    int result = 0;
    switch (context->column) {
    case PLAYER_COLUMN_NAME:
        result = players_compare_names(left, right);
        break;
    case PLAYER_COLUMN_TEAM: {
        wchar_t left_team[128];
        wchar_t right_team[128];
        players_team_name(context->db, left->team_id, left_team,
                          sizeof(left_team) / sizeof(left_team[0]));
        players_team_name(context->db, right->team_id, right_team,
                          sizeof(right_team) / sizeof(right_team[0]));
        result = ui_text_compare(left_team, right_team);
        break;
    }
    case PLAYER_COLUMN_RATING:
        if (left->current_rating < right->current_rating) {
            result = -1;
        } else if (left->current_rating > right->current_rating) {
            result = 1;
        }
        break;
    case PLAYER_COLUMN_MATCHES:
        result = compare_values(left->matches_played, right->matches_played);
        break;
    case PLAYER_COLUMN_MINUTES:
        result = compare_values(left->minutes_played, right->minutes_played);
        break;
    case PLAYER_COLUMN_STATUS:
        result = compare_values(left->active ? 0u : 1u,
                                right->active ? 0u : 1u);
        break;
    default:
        break;
    }
    if (!context->ascending) {
        result = -result;
    }
    if (result == 0 && context->column != PLAYER_COLUMN_NAME) {
        result = players_compare_names(left, right);
    }
    if (result == 0) {
        result = compare_values(left->id, right->id);
    }
    return result;
}

static int CALLBACK players_compare_items(LPARAM left_data,
                                          LPARAM right_data,
                                          LPARAM context_data)
{
    PlayerSortContext *context = (PlayerSortContext *)context_data;
    const Player *left = db_find_player(context->db, (uint64_t)left_data);
    const Player *right = db_find_player(context->db, (uint64_t)right_data);
    if (left == NULL || right == NULL) {
        return 0;
    }
    return players_compare_columns(context, left, right);
}

static void players_select_id(PlayersPage *page, uint64_t player_id)
{
    if (player_id == 0) {
        return;
    }
    int count = ListView_GetItemCount(page->list);
    for (int i = 0; i < count; ++i) {
        LVITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_PARAM;
        item.iItem = i;
        if (ListView_GetItem(page->list, &item) &&
            (uint64_t)item.lParam == player_id) {
            ListView_SetItemState(page->list, i,
                                  LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(page->list, i, FALSE);
            break;
        }
    }
}

static void players_update_buttons(PlayersPage *page)
{
    uint64_t player_id = selected_player(page);
    const Player *player = db_find_player(&page->base.app->db, player_id);
    BOOL enabled = player != NULL;
    EnableWindow(page->edit_button, enabled);
    EnableWindow(page->archive_button, enabled);
    EnableWindow(page->delete_button, enabled);
    if (player != NULL) {
        ui_button_set_text(page->archive_button,
                           player->active ? L"Archive" : L"Restore");
    } else {
        ui_button_set_text(page->archive_button, L"Archive");
    }
}

static void players_rebuild_team_filter(PlayersPage *page)
{
    uint64_t previous = ui_combo_selected_data(page->team_filter);
    SendMessageW(page->team_filter, CB_RESETCONTENT, 0, 0);
    SendMessageW(page->team_filter, CB_ADDSTRING, 0, (LPARAM)L"All teams");
    SendMessageW(page->team_filter, CB_SETITEMDATA, 0, 0);
    for (size_t i = 0; i < page->base.app->db.team_count; ++i) {
        const Team *team = &page->base.app->db.teams[i];
        wchar_t name[128];
        ui_utf8_to_wide(team->name, name, sizeof(name) / sizeof(name[0]));
        LRESULT index = SendMessageW(page->team_filter, CB_ADDSTRING, 0,
                                     (LPARAM)name);
        if (index != CB_ERR && index != CB_ERRSPACE) {
            SendMessageW(page->team_filter, CB_SETITEMDATA,
                         (WPARAM)index, (LPARAM)team->id);
        }
    }
    if (previous != 0 &&
        db_find_team(&page->base.app->db, previous) != NULL) {
        ui_combo_select_data(page->team_filter, previous);
    } else {
        SendMessageW(page->team_filter, CB_SETCURSEL, 0, 0);
    }
}

static void players_refresh(Page *page)
{
    PlayersPage *players = (PlayersPage *)page;
    Database *db = &page->app->db;
    uint64_t previous = selected_player(players);
    players->populating = true;
    players_rebuild_team_filter(players);

    wchar_t search_text[128];
    ui_edit_get_wide(players->search, search_text,
                     sizeof(search_text) / sizeof(search_text[0]));
    uint64_t filter_team = ui_combo_selected_data(players->team_filter);

    PlayerRowCache *new_cache = NULL;
    if (db->player_count > 0 &&
        db->player_count <= SIZE_MAX / sizeof(*new_cache)) {
        new_cache = malloc(db->player_count * sizeof(*new_cache));
    }
    size_t new_count = 0;
    bool rebuild = !players->cache_valid;

    if (rebuild) {
        ListView_DeleteAllItems(players->list);
    } else {
        int list_count = ListView_GetItemCount(players->list);
        for (int i = list_count - 1; i >= 0; --i) {
            LVITEMW item;
            memset(&item, 0, sizeof(item));
            item.mask = LVIF_PARAM;
            item.iItem = i;
            if (!ListView_GetItem(players->list, &item)) {
                ListView_DeleteItem(players->list, i);
                continue;
            }
            uint64_t player_id = (uint64_t)item.lParam;
            const Player *player = db_find_player(db, player_id);
            if (player == NULL ||
                !players_row_matches(db, player, search_text, filter_team)) {
                ListView_DeleteItem(players->list, i);
                continue;
            }
            PlayerRowText row;
            players_build_row(db, player, &row);
            uint64_t hash = ui_list_hash_columns(row.columns,
                                                 PLAYER_COLUMN_COUNT);
            const PlayerRowCache *old =
                players_cache_find(players, player_id);
            if (old == NULL || old->hash != hash) {
                ui_list_set_row(players->list, i, row.columns,
                                PLAYER_COLUMN_COUNT, (LPARAM)player_id);
            }
        }
    }

    for (size_t i = 0; i < db->player_count; ++i) {
        const Player *player = &db->players[i];
        if (!players_row_matches(db, player, search_text, filter_team)) {
            continue;
        }
        PlayerRowText row;
        players_build_row(db, player, &row);
        uint64_t hash = ui_list_hash_columns(row.columns,
                                             PLAYER_COLUMN_COUNT);
        const PlayerRowCache *old =
            rebuild ? NULL : players_cache_find(players, player->id);
        if (old == NULL) {
            ui_list_add_row(players->list, row.columns, PLAYER_COLUMN_COUNT,
                            (LPARAM)player->id);
        }
        if (new_cache != NULL) {
            new_cache[new_count].id = player->id;
            new_cache[new_count].hash = hash;
            new_count++;
        }
    }

    PlayerSortContext sort_context = {
        .db = db,
        .column = players->sort_column,
        .ascending = players->sort_ascending,
    };
    SendMessageW(players->list, LVM_SORTITEMS, (WPARAM)&sort_context,
                 (LPARAM)players_compare_items);
    ui_list_set_sort_indicator(players->list, players->sort_column,
                               players->sort_ascending);

    free(players->cache);
    players->cache = new_cache;
    players->cache_count = new_count;
    players->cache_valid = new_cache != NULL || db->player_count == 0;
    players->populating = false;

    players->visible_rows = (size_t)ListView_GetItemCount(players->list);
    if (previous != 0 && db_find_player(db, previous) != NULL) {
        players_select_id(players, previous);
    } else {
        ListView_SetItemState(players->list, -1, 0, LVIS_SELECTED);
    }
    ShowWindow(players->list,
               players->visible_rows > 0 ? SW_SHOW : SW_HIDE);
    players_update_buttons(players);
    InvalidateRect(page->hwnd, NULL, FALSE);
}

static void players_draw_team_grid(HDC dc, const UiTheme *theme, RECT badge,
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

static void players_draw_team_filter_item(PlayersPage *players,
                                          DRAWITEMSTRUCT *item)
{
    if (players == NULL || item == NULL) {
        return;
    }
    UiTheme *theme = &players->base.app->ui;
    HWND combo = item->hwndItem;
    const Database *db = &players->base.app->db;
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
        return;
    }

    bool hovered = !closed && !disabled && index >= 0 &&
                   index == players->team_filter_hover;
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
            players_draw_team_grid(item->hDC, theme, badge,
                                   disabled ? UI_COLOR_FAINT
                                            : UI_COLOR_ACCENT);
        } else {
            PlayersTeamBadgeColors colors =
                players_team_badge_colors(team_id);
            ui_fill_round_rect(item->hDC, badge, badge_size / 2,
                               disabled ? UI_COLOR_NEUTRAL_SOFT
                                        : colors.fill);
            wchar_t monogram[3];
            players_team_monogram(team, monogram,
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
        size_t roster = players_team_roster_size(db, team_id);
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
}

static LRESULT CALLBACK players_team_filter_list_proc(
    HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR id,
    DWORD_PTR ref)
{
    PlayersPage *players = (PlayersPage *)ref;
    switch (message) {
    case WM_MOUSEMOVE:
        if (players != NULL) {
            LRESULT hit = SendMessageW(hwnd, LB_ITEMFROMPOINT, 0, lparam);
            int hovered = HIWORD(hit) != 0 ? -1 : (int)LOWORD(hit);
            if (hovered != players->team_filter_hover) {
                players->team_filter_hover = hovered;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            if (!players->team_filter_tracking) {
                TRACKMOUSEEVENT track = {
                    .cbSize = sizeof(track),
                    .dwFlags = TME_LEAVE,
                    .hwndTrack = hwnd,
                    .dwHoverTime = HOVER_DEFAULT,
                };
                if (TrackMouseEvent(&track)) {
                    players->team_filter_tracking = true;
                }
            }
        }
        break;
    case WM_MOUSELEAVE:
        if (players != NULL) {
            players->team_filter_tracking = false;
            if (players->team_filter_hover != -1) {
                players->team_filter_hover = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, players_team_filter_list_proc, id);
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

static void players_install_team_filter_hover(PlayersPage *players)
{
    COMBOBOXINFO info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    if (GetComboBoxInfo(players->team_filter, &info) &&
        info.hwndList != NULL) {
        SetWindowSubclass(info.hwndList, players_team_filter_list_proc, 1,
                          (DWORD_PTR)players);
    }
}

static void players_layout(Page *page, int width, int height)
{
    (void)width;
    (void)height;
    PlayersPage *players = (PlayersPage *)page;
    UiTheme *theme = &page->app->ui;
    int margin = ui_scale(theme, 28);
    page->card = page_card_rect(page);
    RECT list = page_list_rect(page);
    MoveWindow(players->list, list.left, list.top,
               list.right - list.left, list.bottom - list.top, TRUE);

    int field_height = ui_scale(theme, PLAYER_FIELD_HEIGHT);
    int field_gap = ui_scale(theme, 10);
    int field_top = page->card.top + ui_scale(theme, 12);
    int search_width = ui_scale(theme, 250);
    int filter_width = ui_scale(theme, 190);
    MoveWindow(players->search, page->card.left + ui_scale(theme, 18),
               field_top, search_width, field_height, TRUE);
    MoveWindow(players->team_filter,
               page->card.left + ui_scale(theme, 18) + search_width + field_gap,
               field_top, filter_width, field_height, TRUE);

    int button_height = ui_scale(theme, 36);
    int gap = ui_scale(theme, 8);
    int top = ui_scale(theme, 24);
    int add_width = ui_scale(theme, 124);
    int edit_width = ui_scale(theme, 76);
    int archive_width = ui_scale(theme, 88);
    int delete_width = ui_scale(theme, 78);
    int right = page->width - margin;
    int x = right - delete_width;
    MoveWindow(players->delete_button, x, top, delete_width, button_height, TRUE);
    x -= gap + archive_width;
    MoveWindow(players->archive_button, x, top, archive_width, button_height, TRUE);
    x -= gap + edit_width;
    MoveWindow(players->edit_button, x, top, edit_width, button_height, TRUE);
    x -= gap + add_width;
    MoveWindow(players->add_button, x, top, add_width, button_height, TRUE);

    static const int widths[6] = { 28, 27, 10, 11, 11, 13 };
    ui_list_set_percent_widths(players->list, widths, 6);
}

static void players_paint(Page *page, HDC dc)
{
    UiTheme *theme = &page->app->ui;
    PlayersPage *players = (PlayersPage *)page;
    page_draw_card(page, dc);

    if (players->visible_rows > 0) {
        wchar_t count[64];
        ui_format_wide(count, sizeof(count) / sizeof(count[0]),
                       players->visible_rows == 1 ? L"%zu player"
                                                  : L"%zu players",
                       players->visible_rows);
        RECT count_rect = {
            page->card.right - ui_scale(theme, 220),
            page->card.top + ui_scale(theme, 18),
            page->card.right - ui_scale(theme, 18),
            page->card.top + ui_scale(theme, 38),
        };
        ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, count_rect, count,
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    if (page->app->db.player_count == 0) {
        page_draw_empty_state(
            page, dc, L"No players yet",
            L"Add players to your clubs, then record matches to calculate their ratings.");
    } else if (players->visible_rows == 0) {
        page_draw_empty_state(
            page, dc, L"No players found",
            L"Try a different name or choose another team filter.");
    }
}

static void players_archive(PlayersPage *page)
{
    uint64_t player_id = selected_player(page);
    App *app = page->base.app;
    const Player *player = db_find_player(&app->db, player_id);
    if (player == NULL) {
        return;
    }
    bool make_active = !player->active;
    Database backup;
    if (!app_tx_begin(app, &backup)) {
        return;
    }
    char error[512];
    if (!db_set_player_active(&app->db, player_id, make_active,
                              error, sizeof(error))) {
        app_tx_abort(app, &backup);
        app_show_error_utf8(app, L"Could not update player", error);
        return;
    }
    if (app_tx_commit(app, &backup)) {
        players_select_id(page, player_id);
    }
}

static void players_delete(PlayersPage *page)
{
    uint64_t player_id = selected_player(page);
    App *app = page->base.app;
    const Player *player = db_find_player(&app->db, player_id);
    if (player == NULL) {
        return;
    }
    wchar_t name[160];
    players_player_name(player, name, sizeof(name) / sizeof(name[0]));
    wchar_t message[512];
    ui_format_wide(message, sizeof(message) / sizeof(message[0]),
                   L"Delete \"%ls\"?\n\nPlayers with match history cannot be deleted. Archive them instead.",
                   name);
    if (MessageBoxW(app->main_hwnd, message, L"Delete player",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }
    Database backup;
    if (!app_tx_begin(app, &backup)) {
        return;
    }
    char error[512];
    if (!db_delete_player(&app->db, player_id, error, sizeof(error))) {
        app_tx_abort(app, &backup);
        app_show_error_utf8(app, L"Could not delete player", error);
        return;
    }
    app_tx_commit(app, &backup);
}

static void players_open_add(PlayersPage *page)
{
    App *app = page->base.app;
    if (app->db.team_count == 0) {
        if (MessageBoxW(app->main_hwnd,
                        L"Add a team before adding players.\n\nGo to the Teams page now?",
                        L"No teams yet",
                        MB_YESNO | MB_ICONINFORMATION) == IDYES) {
            app_navigate(app, PAGE_TEAMS);
        }
        return;
    }
    uint64_t created = dialog_edit_player(app, 0);
    if (created != 0) {
        page->populating = true;
        SetWindowTextW(page->search, L"");
        SendMessageW(page->team_filter, CB_SETCURSEL, 0, 0);
        page->populating = false;
        players_refresh(&page->base);
        players_select_id(page, created);
    }
}

static LRESULT players_message(Page *page, UINT message, WPARAM wparam,
                               LPARAM lparam)
{
    PlayersPage *players = (PlayersPage *)page;
    if (message == WM_DRAWITEM) {
        DRAWITEMSTRUCT *item = (DRAWITEMSTRUCT *)lparam;
        if (item != NULL && item->CtlID == PLAYER_TEAM_FILTER_ID &&
            item->hwndItem == players->team_filter) {
            players_draw_team_filter_item(players, item);
            return 1;
        }
        return 0;
    }
    if (message == WM_COMMAND) {
        int command = LOWORD(wparam);
        int notification = HIWORD(wparam);
        if (command == PLAYER_SEARCH_ID && notification == EN_CHANGE &&
            !players->populating) {
            players_refresh(page);
            return 0;
        }
        if (command == PLAYER_TEAM_FILTER_ID) {
            if (notification == CBN_SELCHANGE && !players->populating) {
                players_refresh(page);
                return 0;
            }
            if (notification == CBN_DROPDOWN ||
                notification == CBN_CLOSEUP) {
                players->team_filter_hover = -1;
                players->team_filter_tracking = false;
            }
            return 0;
        }
        if (command == PLAYER_ADD_ID) {
            players_open_add(players);
            return 0;
        }
        if (command == PLAYER_EDIT_ID) {
            uint64_t player_id = selected_player(players);
            if (player_id != 0) {
                dialog_edit_player(page->app, player_id);
            }
            return 0;
        }
        if (command == PLAYER_ARCHIVE_ID) {
            players_archive(players);
            return 0;
        }
        if (command == PLAYER_DELETE_ID) {
            players_delete(players);
            return 0;
        }
        return 0;
    }
    if (message == WM_NOTIFY) {
        NMHDR *header = (NMHDR *)lparam;
        if (header->idFrom != PLAYER_LIST_ID) {
            return 0;
        }
        switch (header->code) {
        case NM_CUSTOMDRAW:
            return ui_list_custom_draw_rating((NMLVCUSTOMDRAW *)lparam,
                                              &page->app->ui,
                                              PLAYER_COLUMN_RATING);
        case LVN_ITEMCHANGED:
            if (!players->populating) {
                players_update_buttons(players);
            }
            return 0;
        case LVN_COLUMNCLICK: {
            NMLISTVIEW *click = (NMLISTVIEW *)lparam;
            if (click->iSubItem >= 0 &&
                click->iSubItem < PLAYER_COLUMN_COUNT) {
                if (click->iSubItem == players->sort_column) {
                    players->sort_ascending = !players->sort_ascending;
                } else {
                    players->sort_column = click->iSubItem;
                    players->sort_ascending = true;
                }
                players_refresh(page);
            }
            return 0;
        }
        case NM_DBLCLK: {
            uint64_t player_id = selected_player(players);
            if (player_id != 0) {
                dialog_edit_player(page->app, player_id);
            }
            return 0;
        }
        case LVN_KEYDOWN: {
            NMLVKEYDOWN *key = (NMLVKEYDOWN *)lparam;
            if (key->wVKey == VK_DELETE) {
                players_delete(players);
            } else if (key->wVKey == VK_RETURN) {
                uint64_t player_id = selected_player(players);
                if (player_id != 0) {
                    dialog_edit_player(page->app, player_id);
                }
            }
            return 0;
        }
        default:
            return 0;
        }
    }
    return 0;
}

static void players_destroy(Page *page)
{
    PlayersPage *players = (PlayersPage *)page;
    free(players->cache);
    players->cache = NULL;
    players->cache_count = 0;
    players->cache_valid = false;
}

Page *players_page_create(App *app)
{
    PlayersPage *players = calloc(1, sizeof(*players));
    if (players == NULL) {
        return NULL;
    }
    players->team_filter_hover = -1;
    if (!page_create(&players->base, app, PAGE_PLAYERS,
                     L"Players",
                     L"Build squads, track playing time and manage availability.",
                     NULL, players_layout, players_paint, players_refresh,
                     players_destroy, players_message)) {
        free(players);
        return NULL;
    }
    players->base.create_command = PLAYER_ADD_ID;
    players->sort_column = PLAYER_COLUMN_NAME;
    players->sort_ascending = true;

    UiTheme *theme = &app->ui;
    players->list = ui_list_create(players->base.hwnd, PLAYER_LIST_ID,
                                   0, 0, 10, 10, theme->font_body,
                                   theme->row_images, true);
    players->search = ui_edit_create(players->base.hwnd, PLAYER_SEARCH_ID,
                                     L"", 0, 0, 10, 10, theme->font_body, 80);
    players->team_filter = ui_combo_create_owner_draw(
        players->base.hwnd, PLAYER_TEAM_FILTER_ID,
        0, 0, 10, 10, theme->font_body, PLAYER_FIELD_HEIGHT);
    players->add_button = ui_button_create(
        players->base.hwnd, PLAYER_ADD_ID, L"+  Add player",
        0, 0, 10, 10, UI_BUTTON_PRIMARY, theme->font_button);
    players->edit_button = ui_button_create(
        players->base.hwnd, PLAYER_EDIT_ID, L"Edit",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    players->archive_button = ui_button_create(
        players->base.hwnd, PLAYER_ARCHIVE_ID, L"Archive",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    players->delete_button = ui_button_create(
        players->base.hwnd, PLAYER_DELETE_ID, L"Delete",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    if (players->list == NULL || players->search == NULL ||
        players->team_filter == NULL || players->add_button == NULL ||
        players->edit_button == NULL || players->archive_button == NULL ||
        players->delete_button == NULL) {
        page_destroy(&players->base);
        free(players);
        return NULL;
    }
    ui_edit_set_cue(players->search, L"Search players...");
    SendMessageW(players->team_filter, CB_SETDROPPEDWIDTH,
                 (WPARAM)ui_scale(theme, PLAYER_TEAM_FILTER_DROP_WIDTH), 0);
    players_install_team_filter_hover(players);
    ui_list_add_column(players->list, 0, L"Player", 240);
    ui_list_add_column(players->list, 1, L"Team", 210);
    ui_list_add_column(players->list, 2, L"Rating", 80);
    ui_list_add_column(players->list, 3, L"Matches", 80);
    ui_list_add_column(players->list, 4, L"Minutes", 80);
    ui_list_add_column(players->list, 5, L"Status", 90);
    EnableWindow(players->edit_button, FALSE);
    EnableWindow(players->archive_button, FALSE);
    EnableWindow(players->delete_button, FALSE);
    return &players->base;
}
