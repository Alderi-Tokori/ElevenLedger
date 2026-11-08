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
    MATCH_LIST_ID = 1300,
    MATCH_RECORD_ID = 1301,
    MATCH_VIEW_ID = 1302,
};

typedef struct {
    Page base;
    HWND list;
    HWND record_button;
    HWND view_button;
    bool populating;
    size_t visible_rows;
} MatchesPage;

static uint64_t selected_match(MatchesPage *page)
{
    LPARAM data = 0;
    if (!ui_list_selected_data(page->list, &data)) {
        return 0;
    }
    return (uint64_t)data;
}

static void matches_update_buttons(MatchesPage *page)
{
    EnableWindow(page->view_button, selected_match(page) != 0);
}

static void matches_select_id(MatchesPage *page, uint64_t match_id)
{
    if (match_id == 0) {
        return;
    }
    int count = ListView_GetItemCount(page->list);
    for (int i = 0; i < count; ++i) {
        LVITEMW item;
        memset(&item, 0, sizeof(item));
        item.mask = LVIF_PARAM;
        item.iItem = i;
        if (ListView_GetItem(page->list, &item) &&
            (uint64_t)item.lParam == match_id) {
            ListView_SetItemState(page->list, i,
                                  LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(page->list, i, FALSE);
            break;
        }
    }
}

static void match_team_summary(const Database *db, const Match *match,
                               uint64_t team_id, char *buffer,
                               size_t buffer_size)
{
    double sum = 0.0;
    size_t count = 0;
    for (size_t i = 0; i < db->appearance_count; ++i) {
        const Appearance *appearance = &db->appearances[i];
        if (appearance->match_id == match->id &&
            appearance->team_id == team_id) {
            sum += appearance->rating_delta;
            count++;
        }
    }
    if (count == 0) {
        snprintf(buffer, buffer_size, "-");
    } else {
        double average = sum / (double)count;
        snprintf(buffer, buffer_size, "%+.2f", average);
    }
}

static void matches_refresh(Page *page)
{
    MatchesPage *matches = (MatchesPage *)page;
    Database *db = &page->app->db;
    uint64_t previous = selected_match(matches);
    matches->populating = true;
    ListView_DeleteAllItems(matches->list);
    matches->visible_rows = 0;

    for (size_t i = db->match_count; i > 0; --i) {
        const Match *match = &db->matches[i - 1];
        const Team *home = db_find_team(db, match->home_team_id);
        const Team *away = db_find_team(db, match->away_team_id);
        char score[32];
        snprintf(score, sizeof(score), "%d - %d", match->home_goals,
                 match->away_goals);
        char home_change[32];
        char away_change[32];
        match_team_summary(db, match, match->home_team_id, home_change,
                           sizeof(home_change));
        match_team_summary(db, match, match->away_team_id, away_change,
                           sizeof(away_change));
        const char *columns[7] = {
            match->date,
            home != NULL ? home->name : "Unknown",
            score,
            away != NULL ? away->name : "Unknown",
            match->neutral_venue ? "Neutral" : "Home",
            home_change,
            away_change,
        };
        if (ui_list_add_row(matches->list, columns, 7,
                            (LPARAM)match->id) >= 0) {
            matches->visible_rows++;
        }
    }
    matches->populating = false;

    if (previous != 0 && db_find_match(db, previous) != NULL) {
        matches_select_id(matches, previous);
    } else {
        ListView_SetItemState(matches->list, -1, 0, LVIS_SELECTED);
    }
    ShowWindow(matches->list,
               matches->visible_rows > 0 ? SW_SHOW : SW_HIDE);
    matches_update_buttons(matches);
    InvalidateRect(page->hwnd, NULL, FALSE);
}

static void matches_layout(Page *page, int width, int height)
{
    (void)width;
    (void)height;
    MatchesPage *matches = (MatchesPage *)page;
    UiTheme *theme = &page->app->ui;
    int margin = ui_scale(theme, 28);
    page->card = page_card_rect(page);
    RECT list = page_list_rect(page);
    MoveWindow(matches->list, list.left, list.top,
               list.right - list.left, list.bottom - list.top, TRUE);

    int button_height = ui_scale(theme, 36);
    int gap = ui_scale(theme, 8);
    int top = ui_scale(theme, 24);
    int record_width = ui_scale(theme, 142);
    int view_width = ui_scale(theme, 108);
    int right = page->width - margin;
    MoveWindow(matches->view_button, right - view_width, top,
               view_width, button_height, TRUE);
    MoveWindow(matches->record_button,
               right - view_width - gap - record_width, top,
               record_width, button_height, TRUE);

    static const int widths[7] = { 12, 22, 9, 22, 11, 12, 12 };
    ui_list_set_percent_widths(matches->list, widths, 7);
}

static void matches_paint(Page *page, HDC dc)
{
    UiTheme *theme = &page->app->ui;
    MatchesPage *matches = (MatchesPage *)page;
    page_draw_card(page, dc);
    page_draw_toolbar_label(page, dc,
                            page->card.left + ui_scale(theme, 18),
                            page->card.top + ui_scale(theme, 18),
                            L"Match history");

    if (matches->visible_rows > 0) {
        wchar_t count[64];
        ui_format_wide(count, sizeof(count) / sizeof(count[0]),
                       matches->visible_rows == 1 ? L"%zu match"
                                                  : L"%zu matches",
                       matches->visible_rows);
        RECT count_rect = {
            page->card.right - ui_scale(theme, 190),
            page->card.top + ui_scale(theme, 18),
            page->card.right - ui_scale(theme, 18),
            page->card.top + ui_scale(theme, 38),
        };
        ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, count_rect, count,
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }

    if (page->app->db.match_count == 0) {
        page_draw_empty_state(
            page, dc, L"No matches recorded",
            L"Record a result with line-ups and minutes to calculate player ratings.");
    }
}

static bool has_playable_team_pair(const Database *db)
{
    int teams_with_players = 0;
    for (size_t i = 0; i < db->team_count; ++i) {
        if (db_team_player_count(db, db->teams[i].id, true) > 0) {
            teams_with_players++;
        }
    }
    return teams_with_players >= 2;
}

static void matches_record(MatchesPage *page)
{
    App *app = page->base.app;
    if (!has_playable_team_pair(&app->db)) {
        MessageBoxW(app->main_hwnd,
                    L"Recording a match needs at least two teams, each with active players.\n\nAdd teams and players first.",
                    L"Not enough players", MB_OK | MB_ICONINFORMATION);
        return;
    }
    uint64_t match_id = dialog_record_match(app);
    if (match_id != 0) {
        matches_select_id(page, match_id);
        dialog_match_details(app, match_id);
    }
}

static LRESULT matches_message(Page *page, UINT message, WPARAM wparam,
                               LPARAM lparam)
{
    MatchesPage *matches = (MatchesPage *)page;
    if (message == WM_COMMAND) {
        int command = LOWORD(wparam);
        if (command == MATCH_RECORD_ID) {
            matches_record(matches);
            return 0;
        }
        if (command == MATCH_VIEW_ID) {
            uint64_t match_id = selected_match(matches);
            if (match_id != 0) {
                dialog_match_details(page->app, match_id);
            }
            return 0;
        }
        return 0;
    }
    if (message == WM_NOTIFY) {
        NMHDR *header = (NMHDR *)lparam;
        if (header->idFrom != MATCH_LIST_ID) {
            return 0;
        }
        switch (header->code) {
        case NM_CUSTOMDRAW:
            return ui_list_custom_draw((NMLVCUSTOMDRAW *)lparam,
                                       page->app->ui.row_images);
        case LVN_ITEMCHANGED:
            if (!matches->populating) {
                matches_update_buttons(matches);
            }
            return 0;
        case NM_DBLCLK: {
            uint64_t match_id = selected_match(matches);
            if (match_id != 0) {
                dialog_match_details(page->app, match_id);
            }
            return 0;
        }
        case LVN_KEYDOWN: {
            NMLVKEYDOWN *key = (NMLVKEYDOWN *)lparam;
            if (key->wVKey == VK_RETURN) {
                uint64_t match_id = selected_match(matches);
                if (match_id != 0) {
                    dialog_match_details(page->app, match_id);
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

static void matches_destroy(Page *page)
{
    (void)page;
}

Page *matches_page_create(App *app)
{
    MatchesPage *matches = calloc(1, sizeof(*matches));
    if (matches == NULL) {
        return NULL;
    }
    if (!page_create(&matches->base, app, PAGE_MATCHES,
                     L"Matches",
                     L"Record results and review how each line-up changed.",
                     NULL, matches_layout, matches_paint, matches_refresh,
                     matches_destroy, matches_message)) {
        free(matches);
        return NULL;
    }
    matches->base.create_command = MATCH_RECORD_ID;

    UiTheme *theme = &app->ui;
    matches->list = ui_list_create(matches->base.hwnd, MATCH_LIST_ID,
                                   0, 0, 10, 10, theme->font_body,
                                   theme->row_images);
    matches->record_button = ui_button_create(
        matches->base.hwnd, MATCH_RECORD_ID, L"+  Record match",
        0, 0, 10, 10, UI_BUTTON_PRIMARY, theme->font_button);
    matches->view_button = ui_button_create(
        matches->base.hwnd, MATCH_VIEW_ID, L"View details",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    if (matches->list == NULL || matches->record_button == NULL ||
        matches->view_button == NULL) {
        page_destroy(&matches->base);
        free(matches);
        return NULL;
    }
    ui_list_add_column(matches->list, 0, L"Date", 100);
    ui_list_add_column(matches->list, 1, L"Home", 190);
    ui_list_add_column(matches->list, 2, L"Score", 80);
    ui_list_add_column(matches->list, 3, L"Away", 190);
    ui_list_add_column(matches->list, 4, L"Venue", 90);
    ui_list_add_column(matches->list, 5, L"Home change", 100);
    ui_list_add_column(matches->list, 6, L"Away change", 100);
    EnableWindow(matches->view_button, FALSE);
    return &matches->base;
}
