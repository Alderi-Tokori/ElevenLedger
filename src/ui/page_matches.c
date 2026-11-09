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
    MATCH_EDIT_ID = 1303,
    MATCH_IMPORT_ID = 1304,
};

typedef struct {
    Page base;
    HWND list;
    HWND record_button;
    HWND import_button;
    HWND view_button;
    HWND edit_button;
    bool populating;
    size_t visible_rows;
    uint64_t *row_ids;
    uint64_t *row_hashes;
    size_t row_count;
    size_t row_capacity;
    uint64_t *scratch_ids;
    uint64_t *scratch_hashes;
    size_t scratch_capacity;
    bool snapshot_valid;
} MatchesPage;

typedef struct {
    char date[MATCH_DATE_MAX + 1];
    char home[128];
    char score[32];
    char away[128];
    char venue[16];
    char home_change[32];
    char away_change[32];
    const char *columns[7];
} MatchRowText;

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
    bool selected = selected_match(page) != 0;
    EnableWindow(page->view_button, selected);
    EnableWindow(page->edit_button, selected);
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
    double average = 0.0;
    if (db_match_team_average_delta(db, match->id, team_id, &average)) {
        snprintf(buffer, buffer_size, "%+.2f", average);
    } else {
        snprintf(buffer, buffer_size, "-");
    }
}

static void matches_row_text(const Database *db, const Match *match,
                             MatchRowText *row)
{
    const Team *home = db_find_team(db, match->home_team_id);
    const Team *away = db_find_team(db, match->away_team_id);
    snprintf(row->date, sizeof(row->date), "%s", match->date);
    snprintf(row->home, sizeof(row->home), "%s",
             home != NULL ? home->name : "Unknown");
    snprintf(row->score, sizeof(row->score), "%d - %d",
             match->home_goals, match->away_goals);
    snprintf(row->away, sizeof(row->away), "%s",
             away != NULL ? away->name : "Unknown");
    snprintf(row->venue, sizeof(row->venue), "%s",
             match->neutral_venue ? "Neutral" : "Home");
    match_team_summary(db, match, match->home_team_id, row->home_change,
                       sizeof(row->home_change));
    match_team_summary(db, match, match->away_team_id, row->away_change,
                       sizeof(row->away_change));
    row->columns[0] = row->date;
    row->columns[1] = row->home;
    row->columns[2] = row->score;
    row->columns[3] = row->away;
    row->columns[4] = row->venue;
    row->columns[5] = row->home_change;
    row->columns[6] = row->away_change;
}

static const Match *matches_match_at(const Database *db, size_t display_index)
{
    return &db->matches[db->match_count - 1 - display_index];
}

static bool matches_reserve(uint64_t **ids, uint64_t **hashes,
                            size_t *capacity, size_t needed)
{
    if (needed <= *capacity) {
        return true;
    }
    if (needed > SIZE_MAX / sizeof(uint64_t)) {
        return false;
    }
    size_t new_capacity = *capacity == 0 ? 64 : *capacity;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2;
    }
    uint64_t *new_ids = realloc(*ids, new_capacity * sizeof(*new_ids));
    if (new_ids == NULL) {
        return false;
    }
    *ids = new_ids;
    uint64_t *new_hashes =
        realloc(*hashes, new_capacity * sizeof(*new_hashes));
    if (new_hashes == NULL) {
        return false;
    }
    *hashes = new_hashes;
    *capacity = new_capacity;
    return true;
}

static void matches_rebuild(MatchesPage *matches, const Database *db)
{
    ListView_DeleteAllItems(matches->list);
    for (size_t i = 0; i < db->match_count; ++i) {
        const Match *match = matches_match_at(db, i);
        MatchRowText row;
        matches_row_text(db, match, &row);
        ui_list_add_row(matches->list, row.columns, 7, (LPARAM)match->id);
    }
}

static void matches_update_row(MatchesPage *matches, const Database *db,
                               size_t display_index)
{
    const Match *match = matches_match_at(db, display_index);
    MatchRowText row;
    matches_row_text(db, match, &row);
    ui_list_set_row(matches->list, (int)display_index, row.columns, 7,
                    (LPARAM)match->id);
}

static int matches_insert_row(MatchesPage *matches, const Database *db,
                              size_t display_index)
{
    const Match *match = matches_match_at(db, display_index);
    MatchRowText row;
    matches_row_text(db, match, &row);
    return ui_list_insert_row(matches->list, (int)display_index, row.columns,
                              7, (LPARAM)match->id);
}

static bool matches_same_ids(const MatchesPage *matches, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (matches->row_ids[i] != matches->scratch_ids[i]) {
            return false;
        }
    }
    return true;
}

static bool matches_single_insertion(const MatchesPage *matches,
                                     size_t old_count, size_t new_count,
                                     size_t *out_index)
{
    if (new_count != old_count + 1) {
        return false;
    }
    size_t index = 0;
    while (index < old_count &&
           matches->row_ids[index] == matches->scratch_ids[index]) {
        index++;
    }
    if (index == old_count) {
        *out_index = index;
        return true;
    }
    for (size_t i = index; i < old_count; ++i) {
        if (matches->row_ids[i] != matches->scratch_ids[i + 1]) {
            return false;
        }
    }
    *out_index = index;
    return true;
}

static bool matches_single_deletion(const MatchesPage *matches,
                                    size_t old_count, size_t new_count,
                                    size_t *out_index)
{
    if (new_count + 1 != old_count) {
        return false;
    }
    size_t index = 0;
    while (index < new_count &&
           matches->row_ids[index] == matches->scratch_ids[index]) {
        index++;
    }
    if (index == new_count) {
        *out_index = index;
        return true;
    }
    for (size_t i = index; i < new_count; ++i) {
        if (matches->row_ids[i + 1] != matches->scratch_ids[i]) {
            return false;
        }
    }
    *out_index = index;
    return true;
}

static void matches_refresh(Page *page)
{
    MatchesPage *matches = (MatchesPage *)page;
    Database *db = &page->app->db;
    uint64_t previous = selected_match(matches);
    size_t new_count = db->match_count;
    matches->populating = true;

    bool have_snapshot =
        matches_reserve(&matches->scratch_ids, &matches->scratch_hashes,
                        &matches->scratch_capacity, new_count);
    if (have_snapshot) {
        for (size_t i = 0; i < new_count; ++i) {
            const Match *match = matches_match_at(db, i);
            MatchRowText row;
            matches_row_text(db, match, &row);
            matches->scratch_ids[i] = match->id;
            matches->scratch_hashes[i] =
                ui_list_hash_columns(row.columns, 7);
        }
    }

    bool rebuild = !have_snapshot || !matches->snapshot_valid;
    if (!rebuild && new_count == matches->row_count) {
        if (!matches_same_ids(matches, new_count)) {
            rebuild = true;
        } else {
            for (size_t i = 0; i < new_count; ++i) {
                if (matches->row_hashes[i] !=
                    matches->scratch_hashes[i]) {
                    matches_update_row(matches, db, i);
                }
            }
        }
    } else if (!rebuild) {
        size_t index = 0;
        if (matches_single_insertion(matches, matches->row_count, new_count,
                                     &index)) {
            if (matches_insert_row(matches, db, index) >= 0) {
                for (size_t i = 0; i < new_count; ++i) {
                    if (i == index) {
                        continue;
                    }
                    size_t old_hash_index = i < index ? i : i - 1;
                    if (matches->row_hashes[old_hash_index] !=
                        matches->scratch_hashes[i]) {
                        matches_update_row(matches, db, i);
                    }
                }
            } else {
                rebuild = true;
            }
        } else if (matches_single_deletion(matches, matches->row_count,
                                           new_count, &index)) {
            ListView_DeleteItem(matches->list, (int)index);
            for (size_t i = 0; i < new_count; ++i) {
                size_t old_hash_index = i < index ? i : i + 1;
                if (matches->row_hashes[old_hash_index] !=
                    matches->scratch_hashes[i]) {
                    matches_update_row(matches, db, i);
                }
            }
        } else {
            rebuild = true;
        }
    }

    if (rebuild) {
        matches_rebuild(matches, db);
    }

    if (have_snapshot) {
        uint64_t *old_ids = matches->row_ids;
        uint64_t *old_hashes = matches->row_hashes;
        size_t old_capacity = matches->row_capacity;
        matches->row_ids = matches->scratch_ids;
        matches->row_hashes = matches->scratch_hashes;
        matches->row_count = new_count;
        matches->row_capacity = matches->scratch_capacity;
        matches->scratch_ids = old_ids;
        matches->scratch_hashes = old_hashes;
        matches->scratch_capacity = old_capacity;
        matches->snapshot_valid = true;
    } else {
        matches->snapshot_valid = false;
        matches->row_count = 0;
    }

    matches->visible_rows = (size_t)ListView_GetItemCount(matches->list);
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
    int import_width = ui_scale(theme, 188);
    int view_width = ui_scale(theme, 108);
    int edit_width = ui_scale(theme, 84);
    int right = page->width - margin;
    MoveWindow(matches->view_button, right - view_width, top,
               view_width, button_height, TRUE);
    MoveWindow(matches->edit_button,
               right - view_width - gap - edit_width, top,
               edit_width, button_height, TRUE);
    MoveWindow(matches->record_button,
               right - view_width - gap - edit_width - gap - record_width,
               top, record_width, button_height, TRUE);
    MoveWindow(matches->import_button,
               right - view_width - gap - edit_width - gap - record_width -
                   gap - import_width,
               top, import_width, button_height, TRUE);

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
            L"Record a result with line-ups, or import a completed match from WhoScored.");
    }
}

static bool has_match_teams(const Database *db)
{
    return db->team_count >= 2;
}

static void matches_record(MatchesPage *page)
{
    App *app = page->base.app;
    if (!has_match_teams(&app->db)) {
        MessageBoxW(app->main_hwnd,
                    L"Recording a match needs at least two clubs.\n\nAdd another club first; players can be created from the match form.",
                    L"Not enough clubs", MB_OK | MB_ICONINFORMATION);
        return;
    }
    uint64_t match_id = dialog_record_match(app);
    if (match_id != 0) {
        matches_select_id(page, match_id);
        dialog_match_details(app, match_id);
    }
}

static void matches_import(MatchesPage *page)
{
    App *app = page->base.app;
    uint64_t match_id = dialog_import_match(app);
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
        if (command == MATCH_IMPORT_ID) {
            matches_import(matches);
            return 0;
        }
        if (command == MATCH_VIEW_ID) {
            uint64_t match_id = selected_match(matches);
            if (match_id != 0) {
                dialog_match_details(page->app, match_id);
            }
            return 0;
        }
        if (command == MATCH_EDIT_ID) {
            uint64_t match_id = selected_match(matches);
            if (match_id != 0) {
                dialog_edit_match(page->app, page->app->main_hwnd, match_id);
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
    MatchesPage *matches = (MatchesPage *)page;
    free(matches->row_ids);
    free(matches->row_hashes);
    free(matches->scratch_ids);
    free(matches->scratch_hashes);
    matches->row_ids = NULL;
    matches->row_hashes = NULL;
    matches->scratch_ids = NULL;
    matches->scratch_hashes = NULL;
    matches->row_count = 0;
    matches->row_capacity = 0;
    matches->scratch_capacity = 0;
    matches->snapshot_valid = false;
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
                                   theme->row_images, false);
    matches->record_button = ui_button_create(
        matches->base.hwnd, MATCH_RECORD_ID, L"+  Record match",
        0, 0, 10, 10, UI_BUTTON_PRIMARY, theme->font_button);
    matches->import_button = ui_button_create(
        matches->base.hwnd, MATCH_IMPORT_ID, L"Import from WhoScored",
        0, 0, 10, 10, UI_BUTTON_SOFT, theme->font_button);
    matches->view_button = ui_button_create(
        matches->base.hwnd, MATCH_VIEW_ID, L"View details",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    matches->edit_button = ui_button_create(
        matches->base.hwnd, MATCH_EDIT_ID, L"Edit match",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    if (matches->list == NULL || matches->record_button == NULL ||
        matches->import_button == NULL || matches->view_button == NULL ||
        matches->edit_button == NULL) {
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
    EnableWindow(matches->edit_button, FALSE);
    return &matches->base;
}
