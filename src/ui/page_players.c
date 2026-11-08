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

enum {
    PLAYER_LIST_ID = 1200,
    PLAYER_SEARCH_ID = 1201,
    PLAYER_TEAM_FILTER_ID = 1202,
    PLAYER_ADD_ID = 1203,
    PLAYER_EDIT_ID = 1204,
    PLAYER_ARCHIVE_ID = 1205,
    PLAYER_DELETE_ID = 1206,
};

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

    ListView_DeleteAllItems(players->list);
    players->visible_rows = 0;
    for (size_t i = 0; i < db->player_count; ++i) {
        const Player *player = &db->players[i];
        if (filter_team != 0 && player->team_id != filter_team) {
            continue;
        }
        const Team *team = db_find_team(db, player->team_id);
        wchar_t name[160];
        players_player_name(player, name, sizeof(name) / sizeof(name[0]));
        wchar_t team_name[128] = L"-";
        if (team != NULL) {
            ui_utf8_to_wide(team->name, team_name,
                            sizeof(team_name) / sizeof(team_name[0]));
        }
        wchar_t haystack[512];
        ui_format_wide(haystack, sizeof(haystack) / sizeof(haystack[0]),
                       L"%ls %ls", name, team_name);
        if (!contains_case_insensitive(haystack, search_text)) {
            continue;
        }

        char name_utf8[192];
        char team_utf8[128];
        char rating[32];
        char matches[32];
        char minutes[32];
        char status[32];
        ui_wide_to_utf8(name, name_utf8, sizeof(name_utf8));
        ui_wide_to_utf8(team_name, team_utf8, sizeof(team_utf8));
        snprintf(rating, sizeof(rating), "%.1f", player->current_rating);
        snprintf(matches, sizeof(matches), "%u", player->matches_played);
        snprintf(minutes, sizeof(minutes), "%u", player->minutes_played);
        snprintf(status, sizeof(status), "%s",
                 player->active ? "Active" : "Archived");
        const char *columns[6] = {
            name_utf8, team_utf8, rating, matches, minutes, status,
        };
        if (ui_list_add_row(players->list, columns, 6,
                            (LPARAM)player->id) >= 0) {
            players->visible_rows++;
        }
    }
    players->populating = false;

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

    int field_height = ui_scale(theme, 30);
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
    if (message == WM_COMMAND) {
        int command = LOWORD(wparam);
        int notification = HIWORD(wparam);
        if (command == PLAYER_SEARCH_ID && notification == EN_CHANGE &&
            !players->populating) {
            players_refresh(page);
            return 0;
        }
        if (command == PLAYER_TEAM_FILTER_ID &&
            notification == CBN_SELCHANGE && !players->populating) {
            players_refresh(page);
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
            return ui_list_custom_draw((NMLVCUSTOMDRAW *)lparam,
                                       page->app->ui.row_images);
        case LVN_ITEMCHANGED:
            if (!players->populating) {
                players_update_buttons(players);
            }
            return 0;
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
    (void)page;
}

Page *players_page_create(App *app)
{
    PlayersPage *players = calloc(1, sizeof(*players));
    if (players == NULL) {
        return NULL;
    }
    if (!page_create(&players->base, app, PAGE_PLAYERS,
                     L"Players",
                     L"Build squads, track playing time and manage availability.",
                     NULL, players_layout, players_paint, players_refresh,
                     players_destroy, players_message)) {
        free(players);
        return NULL;
    }
    players->base.create_command = PLAYER_ADD_ID;

    UiTheme *theme = &app->ui;
    players->list = ui_list_create(players->base.hwnd, PLAYER_LIST_ID,
                                   0, 0, 10, 10, theme->font_body,
                                   theme->row_images);
    players->search = ui_edit_create(players->base.hwnd, PLAYER_SEARCH_ID,
                                     L"", 0, 0, 10, 10, theme->font_body, 80);
    players->team_filter = ui_combo_create(players->base.hwnd,
                                           PLAYER_TEAM_FILTER_ID,
                                           0, 0, 10, 10, theme->font_body);
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
