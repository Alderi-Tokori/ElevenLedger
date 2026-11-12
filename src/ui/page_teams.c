#include "ui/page.h"

#include "ui/controls.h"
#include "ui/dialogs.h"
#include "ui/pages.h"
#include "ui/theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum {
    TEAM_LIST_ID = 1100,
    TEAM_ADD_ID = 1101,
    TEAM_EDIT_ID = 1102,
    TEAM_DELETE_ID = 1103,
};

typedef enum {
    TEAM_COLUMN_NAME = 0,
    TEAM_COLUMN_SHORT,
    TEAM_COLUMN_CITY,
    TEAM_COLUMN_SQUAD,
    TEAM_COLUMN_RATING,
    TEAM_COLUMN_RECORD,
    TEAM_COLUMN_COUNT
} TeamColumn;

typedef struct {
    const Database *db;
    int column;
    bool ascending;
} TeamSortContext;

typedef struct {
    uint64_t id;
    uint64_t hash;
} TeamRowCache;

typedef struct {
    char squad[64];
    char rating[32];
    char record[64];
    const char *columns[6];
} TeamRowText;

typedef struct {
    Page base;
    HWND list;
    HWND add_button;
    HWND edit_button;
    HWND delete_button;
    bool populating;
    int sort_column;
    bool sort_ascending;
    TeamRowCache *cache;
    size_t cache_count;
    bool cache_valid;
} TeamsPage;

static uint64_t selected_team(TeamsPage *page)
{
    LPARAM data = 0;
    if (!ui_list_selected_data(page->list, &data)) {
        return 0;
    }
    return (uint64_t)data;
}

static int teams_compare_values(uint64_t left, uint64_t right)
{
    if (left < right) {
        return -1;
    }
    if (left > right) {
        return 1;
    }
    return 0;
}

static void teams_optional_text(const char *value, wchar_t *buffer,
                                size_t count)
{
    if (value == NULL || value[0] == '\0' ||
        !ui_utf8_to_wide(value, buffer, count)) {
        ui_format_wide(buffer, count, L"-");
    }
}

static void teams_build_row(const Database *db, const Team *team,
                            TeamRowText *row)
{
    TeamStats stats;
    db_team_stats(db, team->id, &stats);
    snprintf(row->squad, sizeof(row->squad), "%zu / %zu",
             stats.active_players, stats.roster_size);
    if (stats.active_players > 0) {
        snprintf(row->rating, sizeof(row->rating), "%.1f",
                 stats.average_rating);
    } else {
        snprintf(row->rating, sizeof(row->rating), "-");
    }
    snprintf(row->record, sizeof(row->record), "%d-%d-%d", stats.wins,
             stats.draws, stats.losses);
    row->columns[0] = team->name;
    row->columns[1] = team->short_name[0] != '\0' ? team->short_name : "-";
    row->columns[2] = team->city[0] != '\0' ? team->city : "-";
    row->columns[3] = row->squad;
    row->columns[4] = row->rating;
    row->columns[5] = row->record;
}

static const TeamRowCache *teams_cache_find(const TeamsPage *teams,
                                            uint64_t team_id)
{
    for (size_t i = 0; i < teams->cache_count; ++i) {
        if (teams->cache[i].id == team_id) {
            return &teams->cache[i];
        }
    }
    return NULL;
}

static int teams_compare_names(const Team *left, const Team *right)
{
    wchar_t left_name[128];
    wchar_t right_name[128];
    ui_utf8_to_wide(left->name, left_name,
                    sizeof(left_name) / sizeof(left_name[0]));
    ui_utf8_to_wide(right->name, right_name,
                    sizeof(right_name) / sizeof(right_name[0]));
    return ui_text_compare(left_name, right_name);
}

static int teams_compare_columns(const TeamSortContext *context,
                                 const Team *left, const Team *right)
{
    int result = 0;
    switch (context->column) {
    case TEAM_COLUMN_NAME:
        result = teams_compare_names(left, right);
        break;
    case TEAM_COLUMN_SHORT: {
        wchar_t left_short[32];
        wchar_t right_short[32];
        teams_optional_text(left->short_name, left_short,
                            sizeof(left_short) / sizeof(left_short[0]));
        teams_optional_text(right->short_name, right_short,
                            sizeof(right_short) / sizeof(right_short[0]));
        result = ui_text_compare(left_short, right_short);
        break;
    }
    case TEAM_COLUMN_CITY: {
        wchar_t left_city[128];
        wchar_t right_city[128];
        teams_optional_text(left->city, left_city,
                            sizeof(left_city) / sizeof(left_city[0]));
        teams_optional_text(right->city, right_city,
                            sizeof(right_city) / sizeof(right_city[0]));
        result = ui_text_compare(left_city, right_city);
        break;
    }
    case TEAM_COLUMN_SQUAD: {
        TeamStats left_stats;
        TeamStats right_stats;
        db_team_stats(context->db, left->id, &left_stats);
        db_team_stats(context->db, right->id, &right_stats);
        result = teams_compare_values(left_stats.active_players,
                                      right_stats.active_players);
        if (result == 0) {
            result = teams_compare_values(left_stats.roster_size,
                                          right_stats.roster_size);
        }
        break;
    }
    case TEAM_COLUMN_RATING: {
        TeamStats left_stats;
        TeamStats right_stats;
        db_team_stats(context->db, left->id, &left_stats);
        db_team_stats(context->db, right->id, &right_stats);
        bool left_rated = left_stats.active_players > 0;
        bool right_rated = right_stats.active_players > 0;
        if (left_rated != right_rated) {
            return left_rated ? -1 : 1;
        }
        if (left_rated) {
            if (left_stats.average_rating < right_stats.average_rating) {
                result = -1;
            } else if (left_stats.average_rating >
                       right_stats.average_rating) {
                result = 1;
            }
        }
        break;
    }
    case TEAM_COLUMN_RECORD: {
        TeamStats left_stats;
        TeamStats right_stats;
        db_team_stats(context->db, left->id, &left_stats);
        db_team_stats(context->db, right->id, &right_stats);
        result = teams_compare_values((uint64_t)left_stats.wins,
                                      (uint64_t)right_stats.wins);
        if (result == 0) {
            result = teams_compare_values((uint64_t)left_stats.draws,
                                          (uint64_t)right_stats.draws);
        }
        if (result == 0) {
            result = teams_compare_values((uint64_t)left_stats.losses,
                                          (uint64_t)right_stats.losses);
        }
        break;
    }
    default:
        break;
    }
    if (!context->ascending) {
        result = -result;
    }
    if (result == 0 && context->column != TEAM_COLUMN_NAME) {
        result = teams_compare_names(left, right);
    }
    if (result == 0) {
        result = teams_compare_values(left->id, right->id);
    }
    return result;
}

static int CALLBACK teams_compare_items(LPARAM left_data, LPARAM right_data,
                                        LPARAM context_data)
{
    TeamSortContext *context = (TeamSortContext *)context_data;
    const Team *left = db_find_team(context->db, (uint64_t)left_data);
    const Team *right = db_find_team(context->db, (uint64_t)right_data);
    if (left == NULL || right == NULL) {
        return 0;
    }
    return teams_compare_columns(context, left, right);
}

static void teams_update_buttons(TeamsPage *page)
{
    BOOL enabled = selected_team(page) != 0;
    EnableWindow(page->edit_button, enabled);
    EnableWindow(page->delete_button, enabled);
}

static void teams_select_id(TeamsPage *page, uint64_t team_id)
{
    if (team_id == 0) {
        return;
    }
    int count = ListView_GetItemCount(page->list);
    for (int i = 0; i < count; ++i) {
        LVITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_PARAM;
        item.iItem = i;
        if (ListView_GetItem(page->list, &item) &&
            (uint64_t)item.lParam == team_id) {
            ListView_SetItemState(page->list, i,
                                  LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(page->list, i, FALSE);
            break;
        }
    }
}

static void teams_refresh(Page *page)
{
    TeamsPage *teams = (TeamsPage *)page;
    Database *db = &page->app->db;
    uint64_t previous = selected_team(teams);
    teams->populating = true;

    TeamRowCache *new_cache = NULL;
    if (db->team_count > 0 &&
        db->team_count <= SIZE_MAX / sizeof(*new_cache)) {
        new_cache = malloc(db->team_count * sizeof(*new_cache));
    }
    size_t new_count = 0;
    bool rebuild = !teams->cache_valid;

    if (rebuild) {
        ListView_DeleteAllItems(teams->list);
    } else {
        int list_count = ListView_GetItemCount(teams->list);
        for (int i = list_count - 1; i >= 0; --i) {
            LVITEMW item;
            memset(&item, 0, sizeof(item));
            item.mask = LVIF_PARAM;
            item.iItem = i;
            if (!ListView_GetItem(teams->list, &item)) {
                ListView_DeleteItem(teams->list, i);
                continue;
            }
            uint64_t team_id = (uint64_t)item.lParam;
            const Team *team = db_find_team(db, team_id);
            if (team == NULL) {
                ListView_DeleteItem(teams->list, i);
                continue;
            }
            TeamRowText row;
            teams_build_row(db, team, &row);
            uint64_t hash = ui_list_hash_columns(row.columns,
                                                 TEAM_COLUMN_COUNT);
            const TeamRowCache *old = teams_cache_find(teams, team_id);
            if (old == NULL || old->hash != hash) {
                ui_list_set_row(teams->list, i, row.columns,
                                TEAM_COLUMN_COUNT, (LPARAM)team_id);
            }
        }
    }

    for (size_t i = 0; i < db->team_count; ++i) {
        const Team *team = &db->teams[i];
        TeamRowText row;
        teams_build_row(db, team, &row);
        uint64_t hash = ui_list_hash_columns(row.columns,
                                             TEAM_COLUMN_COUNT);
        const TeamRowCache *old =
            rebuild ? NULL : teams_cache_find(teams, team->id);
        if (old == NULL) {
            ui_list_add_row(teams->list, row.columns, TEAM_COLUMN_COUNT,
                            (LPARAM)team->id);
        }
        if (new_cache != NULL) {
            new_cache[new_count].id = team->id;
            new_cache[new_count].hash = hash;
            new_count++;
        }
    }

    TeamSortContext sort_context = {
        .db = db,
        .column = teams->sort_column,
        .ascending = teams->sort_ascending,
    };
    SendMessageW(teams->list, LVM_SORTITEMS, (WPARAM)&sort_context,
                 (LPARAM)teams_compare_items);
    ui_list_set_sort_indicator(teams->list, teams->sort_column,
                               teams->sort_ascending);

    free(teams->cache);
    teams->cache = new_cache;
    teams->cache_count = new_count;
    teams->cache_valid = new_cache != NULL || db->team_count == 0;
    teams->populating = false;

    int list_count = ListView_GetItemCount(teams->list);
    if (previous != 0 && db_find_team(db, previous) != NULL) {
        teams_select_id(teams, previous);
    } else {
        ListView_SetItemState(teams->list, -1, 0, LVIS_SELECTED);
    }

    ShowWindow(teams->list, list_count > 0 ? SW_SHOW : SW_HIDE);
    teams_update_buttons(teams);
    InvalidateRect(page->hwnd, NULL, FALSE);
}

static void teams_layout(Page *page, int width, int height)
{
    (void)width;
    (void)height;
    TeamsPage *teams = (TeamsPage *)page;
    UiTheme *theme = &page->app->ui;
    int margin = ui_scale(theme, 28);
    page->card = page_card_rect(page);
    RECT list = page_list_rect(page);
    MoveWindow(teams->list, list.left, list.top,
               list.right - list.left, list.bottom - list.top, TRUE);

    int button_height = ui_scale(theme, 36);
    int gap = ui_scale(theme, 8);
    int top = ui_scale(theme, 24);
    int add_width = ui_scale(theme, 118);
    int edit_width = ui_scale(theme, 82);
    int delete_width = ui_scale(theme, 92);
    int right = page->width - margin;
    MoveWindow(teams->delete_button, right - delete_width, top,
               delete_width, button_height, TRUE);
    MoveWindow(teams->edit_button, right - delete_width - gap - edit_width, top,
               edit_width, button_height, TRUE);
    MoveWindow(teams->add_button,
               right - delete_width - gap - edit_width - gap - add_width,
               top, add_width, button_height, TRUE);

    static const int widths[6] = { 28, 10, 18, 12, 14, 18 };
    ui_list_set_percent_widths(teams->list, widths, 6);
}

static void teams_paint(Page *page, HDC dc)
{
    UiTheme *theme = &page->app->ui;
    TeamsPage *teams = (TeamsPage *)page;
    page_draw_card(page, dc);
    page_draw_toolbar_label(page, dc,
                            page->card.left + ui_scale(theme, 18),
                            page->card.top + ui_scale(theme, 18),
                            L"Club directory");
    if (page->app->db.team_count == 0) {
        page_draw_empty_state(
            page, dc, L"No teams yet",
            L"Add your first club, then build its squad on the Players page.");
    } else if (ListView_GetItemCount(teams->list) == 0) {
        page_draw_empty_state(
            page, dc, L"No teams",
            L"Add a club to start organising players and matches.");
    }
}

static void teams_confirm_delete(TeamsPage *page)
{
    uint64_t team_id = selected_team(page);
    if (team_id == 0) {
        return;
    }
    App *app = page->base.app;
    const Team *team = db_find_team(&app->db, team_id);
    if (team == NULL) {
        return;
    }
    wchar_t name[128];
    ui_utf8_to_wide(team->name, name, sizeof(name) / sizeof(name[0]));
    wchar_t message[512];
    ui_format_wide(message, sizeof(message) / sizeof(message[0]),
                   L"Delete \"%ls\"?\n\nThis cannot be undone. Teams with players or match history cannot be deleted.",
                   name);
    if (MessageBoxW(app->main_hwnd, message, L"Delete team",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    Database backup;
    if (!app_tx_begin(app, &backup)) {
        return;
    }
    char error[512];
    if (!db_delete_team(&app->db, team_id, error, sizeof(error))) {
        app_tx_abort(app, &backup);
        app_show_error_utf8(app, L"Could not delete team", error);
        return;
    }
    app_tx_commit(app, &backup);
}

static LRESULT teams_message(Page *page, UINT message, WPARAM wparam,
                             LPARAM lparam)
{
    TeamsPage *teams = (TeamsPage *)page;
    if (message == WM_COMMAND) {
        int command = LOWORD(wparam);
        if (command == TEAM_ADD_ID) {
            uint64_t created = dialog_edit_team(page->app, 0);
            if (created != 0) {
                teams_select_id(teams, created);
            }
            return 0;
        }
        if (command == TEAM_EDIT_ID) {
            uint64_t team_id = selected_team(teams);
            if (team_id != 0) {
                dialog_edit_team(page->app, team_id);
            }
            return 0;
        }
        if (command == TEAM_DELETE_ID) {
            teams_confirm_delete(teams);
            return 0;
        }
        return 0;
    }
    if (message == WM_NOTIFY) {
        NMHDR *header = (NMHDR *)lparam;
        if (header->idFrom != TEAM_LIST_ID) {
            return 0;
        }
        switch (header->code) {
        case NM_CUSTOMDRAW:
            return ui_list_custom_draw_rating((NMLVCUSTOMDRAW *)lparam,
                                              &page->app->ui,
                                              TEAM_COLUMN_RATING);
        case LVN_ITEMCHANGED:
            if (!teams->populating) {
                teams_update_buttons(teams);
            }
            return 0;
        case LVN_COLUMNCLICK: {
            NMLISTVIEW *click = (NMLISTVIEW *)lparam;
            if (click->iSubItem >= 0 &&
                click->iSubItem < TEAM_COLUMN_COUNT) {
                if (click->iSubItem == teams->sort_column) {
                    teams->sort_ascending = !teams->sort_ascending;
                } else {
                    teams->sort_column = click->iSubItem;
                    teams->sort_ascending = true;
                }
                teams_refresh(page);
            }
            return 0;
        }
        case NM_DBLCLK:
            if (selected_team(teams) != 0) {
                dialog_edit_team(page->app, selected_team(teams));
            }
            return 0;
        case LVN_KEYDOWN: {
            NMLVKEYDOWN *key = (NMLVKEYDOWN *)lparam;
            if (key->wVKey == VK_DELETE) {
                teams_confirm_delete(teams);
            } else if (key->wVKey == VK_RETURN) {
                uint64_t team_id = selected_team(teams);
                if (team_id != 0) {
                    dialog_edit_team(page->app, team_id);
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

static void teams_destroy(Page *page)
{
    TeamsPage *teams = (TeamsPage *)page;
    free(teams->cache);
    teams->cache = NULL;
    teams->cache_count = 0;
    teams->cache_valid = false;
}

Page *teams_page_create(App *app)
{
    TeamsPage *teams = calloc(1, sizeof(*teams));
    if (teams == NULL) {
        return NULL;
    }
    if (!page_create(&teams->base, app, PAGE_TEAMS,
                     L"Teams",
                     L"Manage clubs, squads and overall club strength.",
                     NULL, teams_layout, teams_paint, teams_refresh,
                     teams_destroy, teams_message)) {
        free(teams);
        return NULL;
    }
    teams->base.create_command = TEAM_ADD_ID;
    teams->sort_column = TEAM_COLUMN_NAME;
    teams->sort_ascending = true;

    UiTheme *theme = &app->ui;
    teams->list = ui_list_create(teams->base.hwnd, TEAM_LIST_ID,
                                 0, 0, 10, 10, theme->font_body,
                                 theme->row_images, true);
    if (teams->list == NULL) {
        page_destroy(&teams->base);
        free(teams);
        return NULL;
    }
    ui_list_add_column(teams->list, 0, L"Team", 220);
    ui_list_add_column(teams->list, 1, L"Short", 80);
    ui_list_add_column(teams->list, 2, L"City", 150);
    ui_list_add_column(teams->list, 3, L"Squad", 100);
    ui_list_add_column(teams->list, 4, L"Avg rating", 100);
    ui_list_add_column(teams->list, 5, L"Record", 100);

    teams->add_button = ui_button_create(
        teams->base.hwnd, TEAM_ADD_ID, L"+  Add team",
        0, 0, 10, 10, UI_BUTTON_PRIMARY, theme->font_button);
    teams->edit_button = ui_button_create(
        teams->base.hwnd, TEAM_EDIT_ID, L"Edit",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    teams->delete_button = ui_button_create(
        teams->base.hwnd, TEAM_DELETE_ID, L"Delete",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    if (teams->add_button == NULL || teams->edit_button == NULL ||
        teams->delete_button == NULL) {
        page_destroy(&teams->base);
        free(teams);
        return NULL;
    }
    EnableWindow(teams->edit_button, FALSE);
    EnableWindow(teams->delete_button, FALSE);
    return &teams->base;
}
