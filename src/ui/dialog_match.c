#include "ui/dialogs.h"

#include "core/database.h"
#include "core/rating.h"
#include "core/util.h"
#include "ui/controls.h"
#include "ui/dialog_common.h"
#include "ui/theme.h"

#include <commctrl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define RECORD_DIALOG_CLASS L"PitchRateMatchDialog"
#define DETAILS_DIALOG_CLASS L"PitchRateMatchDetailsDialog"
#define CELL_EDITOR_PROPERTY L"PitchRate.CellEditor"
#define WM_RECORD_COMMIT_CELL (WM_APP + 51)

enum {
    RECORD_DATE_ID = 2200,
    RECORD_HOME_TEAM_ID,
    RECORD_AWAY_TEAM_ID,
    RECORD_HOME_SCORE_ID,
    RECORD_AWAY_SCORE_ID,
    RECORD_LIST_ID,
    RECORD_SELECT_ALL_ID,
    RECORD_CLEAR_ALL_ID,
    RECORD_SAVE_ID,
    RECORD_CANCEL_ID,
    RECORD_NEUTRAL_VENUE_ID,
    RECORD_AWAY_LIST_ID,
    RECORD_HOME_ADD_ID,
    RECORD_AWAY_ADD_ID,
};

enum {
    DETAILS_LIST_ID = 2300,
    DETAILS_DELETE_ID,
    DETAILS_CLOSE_ID,
    DETAILS_EDIT_ID,
};

enum {
    LINEUP_COLUMN_PLAYER = 0,
    LINEUP_COLUMN_MINUTES,
    LINEUP_COLUMN_PERFORMANCE,
    LINEUP_COLUMN_RATING,
    LINEUP_COLUMN_COUNT,
};

typedef struct {
    uint64_t player_id;
    uint64_t team_id;
    char player_name[160];
    bool selected;
    int minutes;
    double performance;
    double current_rating;
} LineupRow;

typedef struct {
    uint64_t player_id;
    bool selected;
    int minutes;
    double performance;
} LineupSnapshot;

typedef struct {
    App *app;
    HWND hwnd;
    HWND date;
    HWND home_team;
    HWND away_team;
    HWND home_score;
    HWND away_score;
    HWND home_list;
    HWND away_list;
    HWND home_add;
    HWND away_add;
    HWND select_all;
    HWND clear_all;
    HWND neutral_venue;
    HWND save;
    HWND cancel;
    LineupRow *rows;
    size_t row_count;
    size_t home_row_count;
    bool building;
    HWND cell_edit;
    HWND cell_list;
    uint64_t cell_edit_generation;
    int cell_item;
    size_t cell_row;
    int cell_subitem;
    wchar_t error[512];
    bool editing;
    uint64_t match_id;
    uint64_t saved_match_id;
    RECT label_date;
    RECT label_home_team;
    RECT label_home_score;
    RECT label_away_score;
    RECT label_away_team;
    RECT neutral_rect;
    RECT info_rect;
    RECT section_rect;
    RECT home_header_rect;
    RECT away_header_rect;
    RECT home_list_frame;
    RECT away_list_frame;
    RECT footer_line;
    RECT error_rect;
} RecordMatchState;

typedef struct {
    App *app;
    HWND hwnd;
    uint64_t match_id;
    HWND list;
    HWND edit_button;
    HWND delete_button;
    HWND close_button;
    RECT scoreboard;
    RECT list_frame;
    RECT footer_line;
} MatchDetailsState;

static void format_player_name(const Player *player, char *buffer,
                               size_t buffer_size)
{
    if (player->first_name[0] != '\0' && player->last_name[0] != '\0') {
        snprintf(buffer, buffer_size, "%s %s", player->first_name,
                 player->last_name);
    } else if (player->first_name[0] != '\0') {
        snprintf(buffer, buffer_size, "%s", player->first_name);
    } else {
        snprintf(buffer, buffer_size, "%s", player->last_name);
    }
}

static bool has_match_teams(const Database *db)
{
    return db->team_count >= 2;
}

static void record_show_error(RecordMatchState *state, const char *utf8)
{
    ui_utf8_to_wide(utf8, state->error,
                    sizeof(state->error) / sizeof(state->error[0]));
    InvalidateRect(state->hwnd, NULL, FALSE);
}

static void record_show_error_wide(RecordMatchState *state,
                                   const wchar_t *error)
{
    ui_format_wide(state->error,
                   sizeof(state->error) / sizeof(state->error[0]), L"%ls",
                   error);
    InvalidateRect(state->hwnd, NULL, FALSE);
}

static void record_selected_other_team(RecordMatchState *state,
                                       bool changed_home)
{
    HWND changed = changed_home ? state->home_team : state->away_team;
    HWND other = changed_home ? state->away_team : state->home_team;
    uint64_t changed_id = ui_combo_selected_data(changed);
    if (SendMessageW(other, CB_GETCURSEL, 0, 0) == CB_ERR) {
        return;
    }
    uint64_t other_id = ui_combo_selected_data(other);
    if (changed_id != other_id) {
        return;
    }
    int count = (int)SendMessageW(other, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; ++i) {
        uint64_t id = (uint64_t)SendMessageW(other, CB_GETITEMDATA,
                                            (WPARAM)i, 0);
        if (id != changed_id) {
            SendMessageW(other, CB_SETCURSEL, (WPARAM)i, 0);
            return;
        }
    }
}

static uint64_t current_home_team(RecordMatchState *state)
{
    return ui_combo_selected_data(state->home_team);
}

static uint64_t current_away_team(RecordMatchState *state)
{
    return ui_combo_selected_data(state->away_team);
}

static void record_free_rows(RecordMatchState *state)
{
    free(state->rows);
    state->rows = NULL;
    state->row_count = 0;
    state->home_row_count = 0;
}

static HWND record_list_for_row(const RecordMatchState *state, size_t row)
{
    return row < state->home_row_count ? state->home_list : state->away_list;
}

static size_t record_row_start(const RecordMatchState *state, HWND list)
{
    return list == state->home_list ? 0 : state->home_row_count;
}

static bool record_row_from_item(const RecordMatchState *state, HWND list,
                                 int item, size_t *out_row)
{
    if (state == NULL || list == NULL || item < 0 || out_row == NULL) {
        return false;
    }
    LVITEMW item_data;
    memset(&item_data, 0, sizeof(item_data));
    item_data.mask = LVIF_PARAM;
    item_data.iItem = item;
    if (!ListView_GetItem(list, &item_data)) {
        return false;
    }
    LRESULT data = item_data.lParam;
    if (data == (LRESULT)-1 || (size_t)data >= state->row_count) {
        return false;
    }
    *out_row = (size_t)data;
    return true;
}

static bool record_find_row(const RecordMatchState *state, uint64_t player_id,
                            size_t *out_row)
{
    for (size_t i = 0; i < state->row_count; ++i) {
        if (state->rows[i].player_id == player_id) {
            if (out_row != NULL) {
                *out_row = i;
            }
            return true;
        }
    }
    return false;
}

static bool record_capture_rows(const RecordMatchState *state,
                                LineupSnapshot **out_snapshot,
                                size_t *out_count)
{
    *out_snapshot = NULL;
    *out_count = 0;
    if (state->row_count == 0) {
        return true;
    }
    LineupSnapshot *snapshot = calloc(state->row_count, sizeof(*snapshot));
    if (snapshot == NULL) {
        return false;
    }
    for (size_t i = 0; i < state->row_count; ++i) {
        snapshot[i].player_id = state->rows[i].player_id;
        snapshot[i].selected = state->rows[i].selected;
        snapshot[i].minutes = state->rows[i].minutes;
        snapshot[i].performance = state->rows[i].performance;
    }
    *out_snapshot = snapshot;
    *out_count = state->row_count;
    return true;
}

static void record_apply_snapshot(LineupRow *row,
                                  const LineupSnapshot *preserve,
                                  size_t preserve_count)
{
    if (preserve == NULL) {
        return;
    }
    for (size_t i = 0; i < preserve_count; ++i) {
        if (preserve[i].player_id != row->player_id) {
            continue;
        }
        row->selected = preserve[i].selected;
        row->minutes = preserve[i].minutes;
        row->performance = preserve[i].performance;
        return;
    }
}

static void record_cancel_cell_edit(RecordMatchState *state);
static bool record_commit_cell_edit(RecordMatchState *state, bool accept);

static void lineup_row_init(LineupRow *row, const Player *player,
                            uint64_t team_id)
{
    memset(row, 0, sizeof(*row));
    row->player_id = player->id;
    row->team_id = team_id;
    row->selected = false;
    row->minutes = 90;
    row->performance = RATING_NEUTRAL_PERFORMANCE;
    row->current_rating = player->current_rating;
    format_player_name(player, row->player_name, sizeof(row->player_name));
}

static int compare_lineup_rows_by_name(const void *left, const void *right)
{
    const LineupRow *a = (const LineupRow *)left;
    const LineupRow *b = (const LineupRow *)right;
    int result = strcmp(a->player_name, b->player_name);
    if (result != 0) {
        return result;
    }
    if (a->player_id < b->player_id) {
        return -1;
    }
    if (a->player_id > b->player_id) {
        return 1;
    }
    return 0;
}

static bool record_has_player(const RecordMatchState *state, uint64_t player_id)
{
    return record_find_row(state, player_id, NULL);
}

static void record_rebuild_rows(RecordMatchState *state,
                                const LineupSnapshot *preserve,
                                size_t preserve_count,
                                uint64_t select_player_id)
{
    record_cancel_cell_edit(state);
    record_free_rows(state);
    ListView_DeleteAllItems(state->home_list);
    ListView_DeleteAllItems(state->away_list);
    uint64_t home_id = current_home_team(state);
    uint64_t away_id = current_away_team(state);
    if (home_id == 0 || away_id == 0 || home_id == away_id) {
        InvalidateRect(state->hwnd, NULL, FALSE);
        return;
    }
    Database *db = &state->app->db;
    if (state->editing) {
        const Match *match = db_find_match(db, state->match_id);
        if (match == NULL) {
            record_show_error_wide(state, L"This match no longer exists.");
            return;
        }
        home_id = match->home_team_id;
        away_id = match->away_team_id;
    }

    size_t capacity = 0;
    if (state->editing) {
        capacity += db_match_appearance_count(db, state->match_id);
    }
    capacity += db_team_player_count(db, home_id, true);
    capacity += db_team_player_count(db, away_id, true);
    if (capacity == 0) {
        InvalidateRect(state->hwnd, NULL, FALSE);
        return;
    }
    state->rows = calloc(capacity, sizeof(*state->rows));
    if (state->rows == NULL) {
        record_show_error_wide(state, L"Not enough memory to load the line-ups.");
        return;
    }

    const uint64_t teams[2] = { home_id, away_id };
    for (int side = 0; side < 2; ++side) {
        size_t side_start = state->row_count;

        if (state->editing) {
            /*
             * Start from the stored line-up. This includes players who have
             * since transferred or been archived, and keeps their historical
             * club side rather than their current club.
             */
            for (size_t i = 0; i < db->appearance_count; ++i) {
                const Appearance *appearance = &db->appearances[i];
                if (appearance->match_id != state->match_id ||
                    appearance->team_id != teams[side]) {
                    continue;
                }
                const Player *player =
                    db_find_player(db, appearance->player_id);
                if (player == NULL) {
                    continue;
                }
                LineupRow *row = &state->rows[state->row_count++];
                lineup_row_init(row, player, teams[side]);
                row->selected = true;
                row->minutes = appearance->minutes;
                row->performance = appearance->performance;
                record_apply_snapshot(row, preserve, preserve_count);
            }
        }

        for (size_t i = 0; i < db->player_count; ++i) {
            const Player *player = &db->players[i];
            if (!player->active || player->team_id != teams[side] ||
                record_has_player(state, player->id)) {
                continue;
            }
            LineupRow *row = &state->rows[state->row_count++];
            lineup_row_init(row, player, teams[side]);
            record_apply_snapshot(row, preserve, preserve_count);
        }
        qsort(&state->rows[side_start], state->row_count - side_start,
              sizeof(*state->rows), compare_lineup_rows_by_name);
        if (side == 0) {
            state->home_row_count = state->row_count;
        }
    }

    state->building = true;
    bool insert_failed = false;
    for (size_t i = 0; i < state->row_count && !insert_failed; ++i) {
        LineupRow *row = &state->rows[i];
        HWND list = record_list_for_row(state, i);
        char minutes[16];
        char performance[16];
        char rating[16];
        snprintf(minutes, sizeof(minutes), "%d", row->minutes);
        snprintf(performance, sizeof(performance), "%.2f",
                 row->performance);
        snprintf(rating, sizeof(rating), "%.1f", row->current_rating);
        const char *columns[LINEUP_COLUMN_COUNT] = {
            row->player_name, minutes, performance, rating,
        };
        int list_index = ui_list_add_row(list, columns, LINEUP_COLUMN_COUNT,
                                         (LPARAM)i);
        if (list_index < 0) {
            record_show_error_wide(
                state, L"Could not add a player to a line-up list.");
            insert_failed = true;
            break;
        }
        ListView_SetCheckState(list, list_index, row->selected);
    }
    if (!insert_failed && select_player_id != 0) {
        size_t selected_row = 0;
        if (record_find_row(state, select_player_id, &selected_row)) {
            state->rows[selected_row].selected = true;
            HWND list = record_list_for_row(state, selected_row);
            int item = (int)(selected_row - record_row_start(state, list));
            ListView_SetCheckState(list, item, TRUE);
        } else {
            record_show_error_wide(
                state,
                L"The player was saved but is not available for this line-up.");
        }
    }
    state->building = false;
    InvalidateRect(state->hwnd, NULL, FALSE);
}

static void record_apply_checkbox(RecordMatchState *state, HWND list,
                                  int item)
{
    if (state->building) {
        return;
    }
    size_t row = 0;
    if (!record_row_from_item(state, list, item, &row)) {
        return;
    }
    state->rows[row].selected =
        ListView_GetCheckState(list, item) != 0;
}

static LRESULT CALLBACK cell_edit_subclass(HWND hwnd, UINT message,
                                           WPARAM wparam, LPARAM lparam,
                                           UINT_PTR subclass_id,
                                           DWORD_PTR reference_data)
{
    (void)subclass_id;
    RecordMatchState *state = (RecordMatchState *)reference_data;
    switch (message) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    case WM_KEYDOWN:
        if (wparam == VK_RETURN) {
            PostMessageW(state->hwnd, WM_RECORD_COMMIT_CELL, TRUE,
                         (LPARAM)state->cell_edit_generation);
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            PostMessageW(state->hwnd, WM_RECORD_COMMIT_CELL, FALSE,
                         (LPARAM)state->cell_edit_generation);
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        PostMessageW(state->hwnd, WM_RECORD_COMMIT_CELL, TRUE,
                     (LPARAM)state->cell_edit_generation);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, cell_edit_subclass, subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

static void record_begin_cell_edit(RecordMatchState *state, HWND list,
                                   int item, int subitem)
{
    size_t row = 0;
    if (!record_row_from_item(state, list, item, &row) ||
        (subitem != LINEUP_COLUMN_MINUTES &&
         subitem != LINEUP_COLUMN_PERFORMANCE)) {
        return;
    }
    if (state->cell_edit != NULL &&
        !record_commit_cell_edit(state, true)) {
        return;
    }
    state->cell_edit_generation++;
    if (state->cell_edit_generation == 0) {
        state->cell_edit_generation = 1;
    }
    RECT cell;
    cell.left = subitem;
    cell.top = LVIR_BOUNDS;
    if (!ListView_GetSubItemRect(list, item, subitem, LVIR_BOUNDS,
                                 &cell)) {
        return;
    }
    MapWindowPoints(list, state->hwnd, (POINT *)&cell, 2);
    InflateRect(&cell, -1, -1);
    HWND edit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
            ES_AUTOHSCROLL | ES_CENTER,
        cell.left, cell.top, cell.right - cell.left, cell.bottom - cell.top,
        state->hwnd, NULL, state->app->instance, NULL);
    if (edit == NULL) {
        return;
    }
    SetWindowPos(edit, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(edit, WM_SETFONT,
                 (WPARAM)state->app->ui.font_body, TRUE);
    SetPropW(edit, CELL_EDITOR_PROPERTY, (HANDLE)1);
    wchar_t text[32];
    if (subitem == LINEUP_COLUMN_MINUTES) {
        ui_format_wide(text, sizeof(text) / sizeof(text[0]), L"%d",
                       state->rows[row].minutes);
    } else {
        ui_format_wide(text, sizeof(text) / sizeof(text[0]), L"%.2f",
                       state->rows[row].performance);
    }
    SetWindowTextW(edit, text);
    SetWindowSubclass(edit, cell_edit_subclass, 1, (DWORD_PTR)state);
    state->cell_edit = edit;
    state->cell_list = list;
    state->cell_item = item;
    state->cell_row = row;
    state->cell_subitem = subitem;
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);
}

static bool record_commit_cell_edit(RecordMatchState *state, bool accept)
{
    if (state->cell_edit == NULL) {
        return true;
    }
    HWND edit = state->cell_edit;
    HWND list = state->cell_list;
    int item = state->cell_item;
    size_t row = state->cell_row;
    int subitem = state->cell_subitem;
    wchar_t text[64];
    GetWindowTextW(edit, text, (int)(sizeof(text) / sizeof(text[0])));

    if (accept && list != NULL && row < state->row_count) {
        char utf8[128];
        ui_wide_to_utf8(text, utf8, sizeof(utf8));
        if (subitem == LINEUP_COLUMN_MINUTES) {
            int minutes = 0;
            if (!util_parse_int(utf8, 1, 120, &minutes)) {
                record_show_error_wide(
                    state, L"Minutes played must be a whole number from 1 to 120.");
                SetFocus(edit);
                SendMessageW(edit, EM_SETSEL, 0, -1);
                return false;
            }
            state->rows[row].minutes = minutes;
        } else if (subitem == LINEUP_COLUMN_PERFORMANCE) {
            double performance = 0.0;
            if (!util_parse_double(utf8, 1.0, 10.0, &performance)) {
                record_show_error_wide(
                    state, L"Performance must be between 1.0 and 10.0.");
                SetFocus(edit);
                SendMessageW(edit, EM_SETSEL, 0, -1);
                return false;
            }
            state->rows[row].performance =
                round(performance * 100.0) / 100.0;
        } else {
            return true;
        }
        state->error[0] = L'\0';
        wchar_t display[32];
        LVITEMW value;
        memset(&value, 0, sizeof(value));
        if (subitem == LINEUP_COLUMN_MINUTES) {
            ui_format_wide(display, sizeof(display) / sizeof(display[0]),
                           L"%d", state->rows[row].minutes);
        } else {
            ui_format_wide(display, sizeof(display) / sizeof(display[0]),
                           L"%.2f", state->rows[row].performance);
        }
        value.iSubItem = subitem;
        value.pszText = display;
        SendMessageW(list, LVM_SETITEMTEXTW, (WPARAM)item,
                     (LPARAM)&value);
    }

    state->cell_edit = NULL;
    state->cell_list = NULL;
    state->cell_row = 0;
    state->cell_item = 0;
    DestroyWindow(edit);
    if (list != NULL) {
        SetFocus(list);
        ListView_SetItemState(list, item,
                              LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
    InvalidateRect(state->hwnd, NULL, FALSE);
    return true;
}

static void record_cancel_cell_edit(RecordMatchState *state)
{
    if (state->cell_edit != NULL) {
        HWND edit = state->cell_edit;
        state->cell_edit = NULL;
        state->cell_list = NULL;
        state->cell_row = 0;
        state->cell_item = 0;
        DestroyWindow(edit);
    }
}

static void record_select_all(RecordMatchState *state, bool selected)
{
    if (state->cell_edit != NULL &&
        !record_commit_cell_edit(state, true)) {
        return;
    }
    for (size_t i = 0; i < state->row_count; ++i) {
        state->rows[i].selected = selected;
    }
    state->building = true;
    const HWND lists[2] = { state->home_list, state->away_list };
    for (int side = 0; side < 2; ++side) {
        int count = ListView_GetItemCount(lists[side]);
        for (int i = 0; i < count; ++i) {
            ListView_SetCheckState(lists[side], i, selected);
        }
    }
    state->building = false;
    InvalidateRect(state->hwnd, NULL, FALSE);
}

static void record_add_player(RecordMatchState *state, bool home)
{
    if (state->cell_edit != NULL &&
        !record_commit_cell_edit(state, true)) {
        return;
    }
    uint64_t team_id =
        home ? current_home_team(state) : current_away_team(state);
    if (team_id == 0) {
        record_show_error_wide(state, L"Choose a team first.");
        return;
    }
    LineupSnapshot *snapshot = NULL;
    size_t snapshot_count = 0;
    if (!record_capture_rows(state, &snapshot, &snapshot_count)) {
        record_show_error_wide(state,
                               L"Not enough memory to add a player.");
        return;
    }
    uint64_t player_id =
        dialog_add_player_for_team(state->app, state->hwnd, team_id);
    if (player_id == 0) {
        free(snapshot);
        return;
    }
    state->error[0] = L'\0';
    record_rebuild_rows(state, snapshot, snapshot_count, player_id);
    free(snapshot);
}

static void record_select_default_teams(RecordMatchState *state)
{
    SendMessageW(state->home_team, CB_SETCURSEL, 0, 0);
    SendMessageW(state->away_team, CB_SETCURSEL, 1, 0);
}

static void record_layout(RecordMatchState *state, int width, int height)
{
    UiTheme *theme = &state->app->ui;
    int margin = ui_scale(theme, 28);
    int gap = ui_scale(theme, 10);
    int field_height = ui_scale(theme, 32);
    int label_height = ui_scale(theme, 18);
    int available = width - margin * 2;

    int date_width = ui_scale(theme, 134);
    int score_width = ui_scale(theme, 58);
    int dash_width = ui_scale(theme, 28);
    int combo_width = (available - date_width - score_width * 2 -
                       dash_width - gap * 4) / 2;
    if (combo_width < ui_scale(theme, 80)) {
        combo_width = ui_scale(theme, 80);
    }

    int label_y = ui_scale(theme, 94);
    int field_y = label_y + label_height + ui_scale(theme, 4);
    int x = margin;
    state->label_date = (RECT){ x, label_y, x + date_width,
                                label_y + label_height };
    MoveWindow(state->date, x, field_y, date_width, field_height, TRUE);
    x += date_width + gap;
    state->label_home_team = (RECT){ x, label_y, x + combo_width,
                                     label_y + label_height };
    MoveWindow(state->home_team, x, field_y, combo_width, field_height, TRUE);
    x += combo_width + gap;
    state->label_home_score = (RECT){ x, label_y, x + score_width,
                                      label_y + label_height };
    MoveWindow(state->home_score, x, field_y, score_width, field_height, TRUE);
    x += score_width + gap;
    int dash_x = x;
    x += dash_width + gap;
    state->label_away_score = (RECT){ x, label_y, x + score_width,
                                      label_y + label_height };
    MoveWindow(state->away_score, x, field_y, score_width, field_height, TRUE);
    x += score_width + gap;
    state->label_away_team = (RECT){ x, label_y, width - margin,
                                     label_y + label_height };
    int away_width = width - margin - x;
    if (away_width < 0) {
        away_width = 0;
    }
    MoveWindow(state->away_team, x, field_y,
               away_width, field_height, TRUE);
    (void)dash_x;

    int neutral_top = field_y + field_height + ui_scale(theme, 5);
    state->neutral_rect = (RECT){
        margin, neutral_top,
        margin + ui_scale(theme, 290),
        neutral_top + ui_scale(theme, 26),
    };
    state->info_rect = (RECT){ margin,
                               state->neutral_rect.bottom + ui_scale(theme, 1),
                               width - margin,
                               state->neutral_rect.bottom +
                                   ui_scale(theme, 21) };

    int section_y = state->info_rect.bottom + ui_scale(theme, 12);
    state->section_rect = (RECT){ margin, section_y,
                                  width - margin,
                                  section_y + ui_scale(theme, 22) };
    int select_height = ui_scale(theme, 28);
    int select_width = ui_scale(theme, 84);
    int select_y = section_y - ui_scale(theme, 3);
    MoveWindow(state->clear_all, width - margin - select_width,
               select_y, select_width, select_height, TRUE);
    MoveWindow(state->select_all,
               width - margin - select_width * 2 - ui_scale(theme, 6),
               select_y, select_width, select_height, TRUE);

    int column_gap = ui_scale(theme, 12);
    int column_width = (available - column_gap) / 2;
    if (column_width < 0) {
        column_width = 0;
    }
    int left_x = margin;
    int right_x = margin + column_width + column_gap;
    int heading_height = ui_scale(theme, 28);
    int heading_y = section_y + ui_scale(theme, 30);
    int add_width = ui_scale(theme, 104);
    if (add_width > column_width) {
        add_width = column_width;
    }
    int heading_gap = ui_scale(theme, 8);
    state->home_header_rect = (RECT){
        left_x, heading_y, left_x + column_width - add_width - heading_gap,
        heading_y + heading_height,
    };
    state->away_header_rect = (RECT){
        right_x, heading_y, right_x + column_width - add_width - heading_gap,
        heading_y + heading_height,
    };
    if (state->home_header_rect.right < state->home_header_rect.left) {
        state->home_header_rect.right = state->home_header_rect.left;
    }
    if (state->away_header_rect.right < state->away_header_rect.left) {
        state->away_header_rect.right = state->away_header_rect.left;
    }
    MoveWindow(state->home_add, left_x + column_width - add_width,
               heading_y, add_width, heading_height, TRUE);
    MoveWindow(state->away_add, right_x + column_width - add_width,
               heading_y, add_width, heading_height, TRUE);

    int list_top = heading_y + heading_height + ui_scale(theme, 6);
    int footer_top = height - ui_scale(theme, 82);
    if (footer_top < list_top) {
        footer_top = list_top;
    }
    state->home_list_frame = (RECT){ left_x, list_top,
                                     left_x + column_width, footer_top };
    state->away_list_frame = (RECT){ right_x, list_top,
                                     right_x + column_width, footer_top };
    int inset = ui_scale(theme, 8);
    int list_height = footer_top - list_top - inset * 2;
    if (list_height < 0) {
        list_height = 0;
    }
    const RECT frames[2] = { state->home_list_frame,
                             state->away_list_frame };
    const HWND lists[2] = { state->home_list, state->away_list };
    for (int side = 0; side < 2; ++side) {
        int list_width = frames[side].right - frames[side].left - inset * 2;
        if (list_width < 0) {
            list_width = 0;
        }
        MoveWindow(lists[side],
                   frames[side].left + inset,
                   frames[side].top + inset,
                   list_width, list_height, TRUE);
    }

    int button_height = ui_scale(theme, 36);
    int button_y = height - ui_scale(theme, 58);
    int save_width = ui_scale(theme, 140);
    int cancel_width = ui_scale(theme, 92);
    int right = width - margin;
    MoveWindow(state->save, right - save_width, button_y,
               save_width, button_height, TRUE);
    MoveWindow(state->cancel,
               right - save_width - ui_scale(theme, 10) - cancel_width,
               button_y, cancel_width, button_height, TRUE);
    MoveWindow(state->neutral_venue, state->neutral_rect.left,
               state->neutral_rect.top,
               state->neutral_rect.right - state->neutral_rect.left,
               state->neutral_rect.bottom - state->neutral_rect.top, TRUE);
    state->footer_line = (RECT){ margin, button_y - ui_scale(theme, 15),
                                 right, button_y - ui_scale(theme, 14) };
    state->error_rect = (RECT){ margin, button_y - ui_scale(theme, 2),
                                right - save_width - cancel_width -
                                    ui_scale(theme, 30),
                                height - ui_scale(theme, 8) };

    static const int widths[LINEUP_COLUMN_COUNT] = { 44, 16, 20, 20 };
    ui_list_set_percent_widths(state->home_list, widths, LINEUP_COLUMN_COUNT);
    ui_list_set_percent_widths(state->away_list, widths, LINEUP_COLUMN_COUNT);
}

static void record_draw_team_heading(RecordMatchState *state, HDC dc,
                                     RECT rect, bool home)
{
    uint64_t team_id =
        home ? current_home_team(state) : current_away_team(state);
    const Team *team = db_find_team(&state->app->db, team_id);
    wchar_t team_name[128] = L"";
    if (team != NULL) {
        ui_utf8_to_wide(team->name, team_name,
                        sizeof(team_name) / sizeof(team_name[0]));
    }
    wchar_t label[192];
    if (team_name[0] != L'\0') {
        ui_format_wide(label, sizeof(label) / sizeof(label[0]),
                       home ? L"Home: %ls" : L"Away: %ls", team_name);
    } else {
        ui_format_wide(label, sizeof(label) / sizeof(label[0]), L"%ls",
                       home ? L"Home team" : L"Away team");
    }
    ui_draw_text(dc, state->app->ui.font_body_bold, UI_COLOR_TEXT, rect,
                 label, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                            DT_END_ELLIPSIS);
}

static void record_paint(RecordMatchState *state, HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC target = BeginPaint(hwnd, &paint);
    RECT client;
    GetClientRect(hwnd, &client);
    UiCanvas canvas;
    if (ui_canvas_begin(&canvas, hwnd, target, &state->app->ui,
                        UI_COLOR_BG)) {
        dialog_draw_chrome(
            state->app, canvas.dc, client.right,
            state->editing ? L"Edit match" : L"Record a match",
            state->editing
                ? L"Update the fixture and line-ups. Ratings are replayed from history on save."
                : L"Set the fixture and line-ups. Ratings are calculated on save.");
        dialog_draw_label(state->app, canvas.dc, state->label_date.left,
                          state->label_date.top, L"Date");
        dialog_draw_label(state->app, canvas.dc,
                          state->label_home_team.left,
                          state->label_home_team.top, L"Home team");
        dialog_draw_label(state->app, canvas.dc,
                          state->label_home_score.left,
                          state->label_home_score.top, L"Goals");
        dialog_draw_label(state->app, canvas.dc,
                          state->label_away_score.left,
                          state->label_away_score.top, L"Goals");
        dialog_draw_label(state->app, canvas.dc,
                          state->label_away_team.left,
                          state->label_away_team.top, L"Away team");
        ui_draw_text(
            canvas.dc, state->app->ui.font_small, UI_COLOR_FAINT,
            state->info_rect,
            state->editing
                ? L"Double-click Minutes or Performance to edit. Add player creates a squad member for that side. Existing players keep their original club."
                : L"Tick everyone who played. Double-click Minutes or Performance to edit. Add player creates a squad member for that side; 6.0 is the default performance.",
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        ui_draw_text(canvas.dc, state->app->ui.font_body_bold,
                     UI_COLOR_TEXT, state->section_rect,
                     state->editing ? L"Match line-up" : L"Playing squads",
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        record_draw_team_heading(state, canvas.dc,
                                 state->home_header_rect, true);
        record_draw_team_heading(state, canvas.dc,
                                 state->away_header_rect, false);

        ui_fill_round_rect(canvas.dc, state->home_list_frame,
                           ui_scale(&state->app->ui, 9), UI_COLOR_CARD);
        ui_fill_round_rect(canvas.dc, state->away_list_frame,
                           ui_scale(&state->app->ui, 9), UI_COLOR_CARD);
        ui_draw_line(canvas.dc, state->footer_line.left,
                     state->footer_line.top, state->footer_line.right,
                     state->footer_line.top, UI_COLOR_BORDER);
        int home_score_y = state->label_date.top;
        ui_draw_text(canvas.dc, state->app->ui.font_h2, UI_COLOR_FAINT,
                     (RECT){ state->label_home_score.right,
                             home_score_y,
                             state->label_away_score.left,
                             state->label_home_score.bottom },
                     L"-", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        dialog_draw_error(state->app, canvas.dc, state->error_rect,
                          state->error);
        ui_canvas_end(&canvas);
    }
    EndPaint(hwnd, &paint);
}

static void record_save(RecordMatchState *state)
{
    if (state->cell_edit != NULL &&
        !record_commit_cell_edit(state, true)) {
        return;
    }
    char date[64];
    char home_goals_text[32];
    char away_goals_text[32];
    ui_edit_get_utf8(state->date, date, sizeof(date));
    ui_edit_get_utf8(state->home_score, home_goals_text,
                     sizeof(home_goals_text));
    ui_edit_get_utf8(state->away_score, away_goals_text,
                     sizeof(away_goals_text));
    int home_goals = 0;
    int away_goals = 0;
    if (!util_date_valid(date)) {
        record_show_error_wide(state,
                               L"Use a valid date in YYYY-MM-DD format.");
        return;
    }
    if (!util_parse_int(home_goals_text, 0, 99, &home_goals) ||
        !util_parse_int(away_goals_text, 0, 99, &away_goals)) {
        record_show_error_wide(state, L"Goals must be between 0 and 99.");
        return;
    }
    uint64_t home_id = current_home_team(state);
    uint64_t away_id = current_away_team(state);
    if (home_id == 0 || away_id == 0 || home_id == away_id) {
        record_show_error_wide(state, L"Choose two different teams.");
        return;
    }
    size_t selected_count = 0;
    size_t home_players = 0;
    size_t away_players = 0;
    for (size_t i = 0; i < state->row_count; ++i) {
        if (!state->rows[i].selected) {
            continue;
        }
        selected_count++;
        if (state->rows[i].team_id == home_id) {
            home_players++;
        } else {
            away_players++;
        }
    }
    if (home_players == 0 || away_players == 0) {
        record_show_error_wide(state,
                               L"Select at least one player for each team.");
        return;
    }
    MatchPlayerInput *lineup = calloc(selected_count, sizeof(*lineup));
    if (lineup == NULL) {
        record_show_error_wide(state,
                               L"Not enough memory to save the match.");
        return;
    }
    size_t used = 0;
    for (size_t i = 0; i < state->row_count; ++i) {
        if (!state->rows[i].selected) {
            continue;
        }
        lineup[used].player_id = state->rows[i].player_id;
        lineup[used].minutes = state->rows[i].minutes;
        lineup[used].performance = state->rows[i].performance;
        used++;
    }
    MatchInput input = {
        .date = date,
        .home_team_id = home_id,
        .away_team_id = away_id,
        .home_goals = home_goals,
        .away_goals = away_goals,
        .neutral_venue = ui_checkbox_get_checked(state->neutral_venue),
        .players = lineup,
        .player_count = selected_count,
    };
    Database backup;
    if (!app_tx_begin(state->app, &backup)) {
        free(lineup);
        return;
    }
    char error[512];
    uint64_t new_id = 0;
    bool ok;
    if (state->editing) {
        ok = db_update_match(&state->app->db, state->match_id, &input,
                             error, sizeof(error));
    } else {
        ok = db_add_match(&state->app->db, &input, &new_id,
                          error, sizeof(error));
    }
    free(lineup);
    if (!ok) {
        app_tx_abort(state->app, &backup);
        record_show_error(state, error);
        return;
    }
    if (!app_tx_commit(state->app, &backup)) {
        return;
    }
    state->saved_match_id = state->editing ? state->match_id : new_id;
    dialog_close(state->hwnd);
}

LRESULT CALLBACK record_match_dialog_proc(HWND hwnd, UINT message,
                                          WPARAM wparam,
                                          LPARAM lparam)
{
    RecordMatchState *state =
        (RecordMatchState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    case WM_SIZE:
        if (state != NULL && state->home_list != NULL) {
            record_layout(state, LOWORD(lparam), HIWORD(lparam));
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state != NULL) {
            record_paint(state, hwnd);
        }
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        return (LRESULT)dialog_control_color(message, wparam, state->app);
    case WM_RECORD_COMMIT_CELL:
        if (state != NULL && state->cell_edit != NULL &&
            (uint64_t)lparam == state->cell_edit_generation) {
            (void)record_commit_cell_edit(state, wparam != 0);
        }
        return 0;
    case WM_COMMAND:
        if (state == NULL) {
            break;
        }
        switch (LOWORD(wparam)) {
        case RECORD_HOME_TEAM_ID:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                record_selected_other_team(state, true);
                state->error[0] = L'\0';
                record_rebuild_rows(state, NULL, 0, 0);
                return 0;
            }
            break;
        case RECORD_AWAY_TEAM_ID:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                record_selected_other_team(state, false);
                state->error[0] = L'\0';
                record_rebuild_rows(state, NULL, 0, 0);
                return 0;
            }
            break;
        case RECORD_HOME_ADD_ID:
            record_add_player(state, true);
            return 0;
        case RECORD_AWAY_ADD_ID:
            record_add_player(state, false);
            return 0;
        case RECORD_SELECT_ALL_ID:
            record_select_all(state, true);
            return 0;
        case RECORD_CLEAR_ALL_ID:
            record_select_all(state, false);
            return 0;
        case RECORD_SAVE_ID:
            record_save(state);
            return 0;
        case RECORD_CANCEL_ID:
            dialog_close(hwnd);
            return 0;
        default:
            break;
        }
        break;
    case WM_NOTIFY: {
        if (state == NULL) {
            break;
        }
        NMHDR *header = (NMHDR *)lparam;
        if (header->idFrom != RECORD_LIST_ID &&
            header->idFrom != RECORD_AWAY_LIST_ID) {
            break;
        }
        HWND list = header->hwndFrom;
        if (header->code == NM_CUSTOMDRAW) {
            return ui_list_custom_draw((NMLVCUSTOMDRAW *)lparam,
                                       state->app->ui.row_images);
        }
        if (header->code == LVN_ITEMCHANGED) {
            NMLISTVIEW *change = (NMLISTVIEW *)lparam;
            if (!state->building) {
                record_apply_checkbox(state, list, change->iItem);
            }
            return 0;
        }
        if (header->code == NM_DBLCLK) {
            POINT point;
            GetCursorPos(&point);
            ScreenToClient(list, &point);
            LVHITTESTINFO hit;
            memset(&hit, 0, sizeof(hit));
            hit.pt = point;
            ListView_SubItemHitTest(list, &hit);
            if (hit.iItem >= 0 &&
                (hit.iSubItem == LINEUP_COLUMN_MINUTES ||
                 hit.iSubItem == LINEUP_COLUMN_PERFORMANCE)) {
                record_begin_cell_edit(state, list, hit.iItem,
                                       hit.iSubItem);
            }
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        dialog_close(hwnd);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static bool record_build(App *app, RecordMatchState *state)
{
    const Match *match = NULL;
    if (state->editing) {
        match = db_find_match(&app->db, state->match_id);
        if (match == NULL) {
            return false;
        }
    }
    UiTheme *theme = &app->ui;
    state->date = ui_edit_create(state->hwnd, RECORD_DATE_ID, L"",
                                 0, 0, 10, 10, theme->font_body, 10);
    state->home_team = ui_combo_create(state->hwnd, RECORD_HOME_TEAM_ID,
                                       0, 0, 10, 10, theme->font_body);
    state->away_team = ui_combo_create(state->hwnd, RECORD_AWAY_TEAM_ID,
                                       0, 0, 10, 10, theme->font_body);
    state->home_score = ui_edit_create(state->hwnd, RECORD_HOME_SCORE_ID,
                                       L"0", 0, 0, 10, 10,
                                       theme->font_body, 2);
    state->away_score = ui_edit_create(state->hwnd, RECORD_AWAY_SCORE_ID,
                                       L"0", 0, 0, 10, 10,
                                       theme->font_body, 2);
    state->home_list = ui_list_create(state->hwnd, RECORD_LIST_ID,
                                      0, 0, 10, 10, theme->font_body,
                                      theme->row_images);
    state->away_list = ui_list_create(state->hwnd, RECORD_AWAY_LIST_ID,
                                      0, 0, 10, 10, theme->font_body,
                                      theme->row_images);
    state->home_add = ui_button_create(
        state->hwnd, RECORD_HOME_ADD_ID, L"Add player",
        0, 0, 10, 10, UI_BUTTON_SOFT, theme->font_small_bold);
    state->away_add = ui_button_create(
        state->hwnd, RECORD_AWAY_ADD_ID, L"Add player",
        0, 0, 10, 10, UI_BUTTON_SOFT, theme->font_small_bold);
    state->select_all = ui_button_create(
        state->hwnd, RECORD_SELECT_ALL_ID, L"Select all",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_small_bold);
    state->clear_all = ui_button_create(
        state->hwnd, RECORD_CLEAR_ALL_ID, L"Clear all",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_small_bold);
    state->neutral_venue = ui_checkbox_create(
        state->hwnd, RECORD_NEUTRAL_VENUE_ID,
        L"Neutral venue (no home advantage)",
        0, 0, 10, 10, theme->font_body);
    state->save = ui_button_create(
        state->hwnd, RECORD_SAVE_ID,
        state->editing ? L"Save changes" : L"Record match",
        0, 0, 10, 10, UI_BUTTON_PRIMARY, theme->font_button);
    state->cancel = ui_button_create(
        state->hwnd, RECORD_CANCEL_ID, L"Cancel",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    if (state->date == NULL || state->home_team == NULL ||
        state->away_team == NULL || state->home_score == NULL ||
        state->away_score == NULL || state->home_list == NULL ||
        state->away_list == NULL || state->home_add == NULL ||
        state->away_add == NULL || state->select_all == NULL ||
        state->clear_all == NULL || state->neutral_venue == NULL ||
        state->save == NULL || state->cancel == NULL) {
        return false;
    }
    const HWND lists[2] = { state->home_list, state->away_list };
    for (int side = 0; side < 2; ++side) {
        SendMessageW(lists[side], LVM_SETEXTENDEDLISTVIEWSTYLE,
                     LVS_EX_CHECKBOXES, LVS_EX_CHECKBOXES);
        ui_list_add_column(lists[side], LINEUP_COLUMN_PLAYER,
                           L"Player", 220);
        ui_list_add_column(lists[side], LINEUP_COLUMN_MINUTES,
                           L"Minutes", 80);
        ui_list_add_column(lists[side], LINEUP_COLUMN_PERFORMANCE,
                           L"Perf", 100);
        ui_list_add_column(lists[side], LINEUP_COLUMN_RATING,
                           L"Rating", 100);
    }

    for (size_t i = 0; i < app->db.team_count; ++i) {
        const Team *team = &app->db.teams[i];
        ui_combo_add_utf8(state->home_team, team->name, team->id);
        ui_combo_add_utf8(state->away_team, team->name, team->id);
    }

    if (state->editing) {
        ui_combo_select_data(state->home_team, match->home_team_id);
        ui_combo_select_data(state->away_team, match->away_team_id);
        EnableWindow(state->home_team, FALSE);
        EnableWindow(state->away_team, FALSE);
        ui_edit_set_utf8(state->date, match->date);
        char score[16];
        snprintf(score, sizeof(score), "%d", match->home_goals);
        ui_edit_set_utf8(state->home_score, score);
        snprintf(score, sizeof(score), "%d", match->away_goals);
        ui_edit_set_utf8(state->away_score, score);
        ui_checkbox_set_checked(state->neutral_venue,
                                match->neutral_venue);
    } else {
        record_select_default_teams(state);
        char today[11];
        util_date_today(today);
        ui_edit_set_utf8(state->date, today);
    }
    ui_edit_set_cue(state->date, L"YYYY-MM-DD");

    RECT client;
    GetClientRect(state->hwnd, &client);
    record_layout(state, client.right, client.bottom);
    record_rebuild_rows(state, NULL, 0, 0);
    InvalidateRect(state->hwnd, NULL, FALSE);
    return true;
}

static uint64_t match_dialog_run(App *app, HWND owner, uint64_t match_id)
{
    RecordMatchState state;
    memset(&state, 0, sizeof(state));
    state.app = app;
    state.editing = match_id != 0;
    state.match_id = match_id;
    HWND dialog_owner = owner != NULL ? owner : app->main_hwnd;
    HWND hwnd = dialog_create_shell(
        app, dialog_owner, RECORD_DIALOG_CLASS,
        state.editing ? L"Edit match - PitchRate"
                      : L"Record match - PitchRate",
        980, 650, &state);
    if (hwnd == NULL) {
        return 0;
    }
    state.hwnd = hwnd;
    if (!record_build(app, &state)) {
        dialog_close(hwnd);
        record_free_rows(&state);
        return 0;
    }
    ui_run_modal(hwnd, dialog_owner, RECORD_SAVE_ID);
    uint64_t saved_match_id = state.saved_match_id;
    record_free_rows(&state);
    return saved_match_id;
}

uint64_t dialog_record_match(App *app)
{
    if (!has_match_teams(&app->db)) {
        MessageBoxW(app->main_hwnd,
                    L"At least two clubs are needed to record a match.",
                    L"PitchRate", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    return match_dialog_run(app, app->main_hwnd, 0);
}

bool dialog_edit_match(App *app, HWND owner, uint64_t match_id)
{
    if (app == NULL || match_id == 0 ||
        db_find_match(&app->db, match_id) == NULL) {
        return false;
    }
    return match_dialog_run(app, owner, match_id) != 0;
}

/* ------------------------------ Match details --------------------------- */

static void details_paint(MatchDetailsState *state, HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC target = BeginPaint(hwnd, &paint);
    RECT client;
    GetClientRect(hwnd, &client);
    UiCanvas canvas;
    const Match *match = db_find_match(&state->app->db, state->match_id);
    if (ui_canvas_begin(&canvas, hwnd, target, &state->app->ui,
                        UI_COLOR_BG)) {
        dialog_draw_chrome(
            state->app, canvas.dc, client.right, L"Match details",
            L"Team expectation, result movement and every player's change.");
        if (match != NULL) {
            UiTheme *theme = &state->app->ui;
            RECT scoreboard = state->scoreboard;
            ui_fill_round_rect(canvas.dc, scoreboard,
                               ui_scale(theme, 10), UI_COLOR_CARD);
            ui_stroke_round_rect(canvas.dc, scoreboard,
                                 ui_scale(theme, 10), UI_COLOR_CARD,
                                 UI_COLOR_BORDER);
            const Team *home = db_find_team(&state->app->db,
                                            match->home_team_id);
            const Team *away = db_find_team(&state->app->db,
                                            match->away_team_id);
            wchar_t home_name[128] = L"Unknown";
            wchar_t away_name[128] = L"Unknown";
            if (home != NULL) {
                ui_utf8_to_wide(home->name, home_name,
                                sizeof(home_name) / sizeof(home_name[0]));
            }
            if (away != NULL) {
                ui_utf8_to_wide(away->name, away_name,
                                sizeof(away_name) / sizeof(away_name[0]));
            }
            int center = (scoreboard.left + scoreboard.right) / 2;
            int side_width = (scoreboard.right - scoreboard.left) * 32 / 100;
            RECT home_rect = {
                scoreboard.left + ui_scale(theme, 22),
                scoreboard.top + ui_scale(theme, 20),
                scoreboard.left + ui_scale(theme, 22) + side_width,
                scoreboard.top + ui_scale(theme, 58),
            };
            RECT away_rect = {
                scoreboard.right - ui_scale(theme, 22) - side_width,
                scoreboard.top + ui_scale(theme, 20),
                scoreboard.right - ui_scale(theme, 22),
                scoreboard.top + ui_scale(theme, 58),
            };
            ui_draw_text(canvas.dc, theme->font_h2, UI_COLOR_TEXT, home_rect,
                         home_name, DT_RIGHT | DT_VCENTER | DT_SINGLELINE |
                                        DT_END_ELLIPSIS);
            ui_draw_text(canvas.dc, theme->font_h2, UI_COLOR_TEXT, away_rect,
                         away_name, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                                        DT_END_ELLIPSIS);
            wchar_t score[32];
            ui_format_wide(score, sizeof(score) / sizeof(score[0]),
                           L"%d  -  %d", match->home_goals,
                           match->away_goals);
            RECT score_rect = {
                center - ui_scale(theme, 90),
                scoreboard.top + ui_scale(theme, 8),
                center + ui_scale(theme, 90),
                scoreboard.top + ui_scale(theme, 64),
            };
            ui_draw_text(canvas.dc, theme->font_stat, UI_COLOR_TEXT,
                         score_rect, score,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            wchar_t date_value[16];
            wchar_t date_display[64];
            ui_utf8_to_wide(match->date, date_value,
                            sizeof(date_value) / sizeof(date_value[0]));
            if (match->neutral_venue) {
                ui_format_wide(date_display,
                               sizeof(date_display) / sizeof(date_display[0]),
                               L"%ls  |  Neutral venue", date_value);
            } else {
                ui_format_wide(date_display,
                               sizeof(date_display) / sizeof(date_display[0]),
                               L"%ls", date_value);
            }
            RECT date_rect = { center - ui_scale(theme, 140),
                               scoreboard.top + ui_scale(theme, 62),
                               center + ui_scale(theme, 140),
                               scoreboard.top + ui_scale(theme, 82) };
            ui_draw_text(canvas.dc, theme->font_small, UI_COLOR_MUTED,
                         date_rect, date_display,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                             DT_END_ELLIPSIS);

            wchar_t strength[128];
            ui_format_wide(strength,
                           sizeof(strength) / sizeof(strength[0]),
                           L"Line-up strength   %.1f  v  %.1f",
                           match->rating.home_strength,
                           match->rating.away_strength);
            RECT metric_y = { scoreboard.left + ui_scale(theme, 22),
                              scoreboard.top + ui_scale(theme, 84),
                              scoreboard.right - ui_scale(theme, 22),
                              scoreboard.top + ui_scale(theme, 104) };
            ui_draw_text(canvas.dc, theme->font_small, UI_COLOR_MUTED,
                         metric_y, strength,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            wchar_t expected[128];
            ui_format_wide(expected,
                           sizeof(expected) / sizeof(expected[0]),
                           L"Expected home result %.1f%%   |   Team movement %+.2f / %+.2f",
                           match->rating.home_expected * 100.0,
                           match->rating.home_team_delta,
                           match->rating.away_team_delta);
            RECT metric_second = metric_y;
            metric_second.top += ui_scale(theme, 21);
            metric_second.bottom += ui_scale(theme, 22);
            ui_draw_text(canvas.dc, theme->font_small, UI_COLOR_MUTED,
                         metric_second, expected,
                         DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                             DT_END_ELLIPSIS);
        }
        ui_fill_round_rect(canvas.dc, state->list_frame,
                           ui_scale(&state->app->ui, 9), UI_COLOR_CARD);
        ui_draw_line(canvas.dc, state->footer_line.left,
                     state->footer_line.top, state->footer_line.right,
                     state->footer_line.top, UI_COLOR_BORDER);
        ui_canvas_end(&canvas);
    }
    EndPaint(hwnd, &paint);
}

static void details_layout(MatchDetailsState *state, int width, int height)
{
    UiTheme *theme = &state->app->ui;
    int margin = ui_scale(theme, 28);
    state->scoreboard = (RECT){ margin, ui_scale(theme, 94),
                                width - margin, ui_scale(theme, 230) };
    int list_top = ui_scale(theme, 246);
    int list_bottom = height - ui_scale(theme, 90);
    if (list_bottom < list_top) {
        list_bottom = list_top;
    }
    state->list_frame = (RECT){ margin, list_top, width - margin,
                                list_bottom };
    int inset = ui_scale(theme, 8);
    int list_width = state->list_frame.right - state->list_frame.left -
                     inset * 2;
    int list_height = state->list_frame.bottom - state->list_frame.top -
                      inset * 2;
    if (list_width < 0) {
        list_width = 0;
    }
    if (list_height < 0) {
        list_height = 0;
    }
    MoveWindow(state->list,
               state->list_frame.left + inset,
               state->list_frame.top + inset,
               list_width, list_height, TRUE);
    int button_height = ui_scale(theme, 36);
    int button_y = height - ui_scale(theme, 60);
    int close_width = ui_scale(theme, 92);
    int delete_width = ui_scale(theme, 112);
    int edit_width = ui_scale(theme, 104);
    MoveWindow(state->close_button, width - margin - close_width, button_y,
               close_width, button_height, TRUE);
    MoveWindow(state->delete_button, margin, button_y,
               delete_width, button_height, TRUE);
    MoveWindow(state->edit_button,
               margin + delete_width + ui_scale(theme, 10), button_y,
               edit_width, button_height, TRUE);
    state->footer_line = (RECT){ margin, button_y - ui_scale(theme, 14),
                                 width - margin,
                                 button_y - ui_scale(theme, 13) };
    static const int widths[7] = { 15, 27, 8, 9, 12, 14, 15 };
    ui_list_set_percent_widths(state->list, widths, 7);
}

static void details_refresh(HWND list, App *app, uint64_t match_id)
{
    ListView_DeleteAllItems(list);
    const Match *match = db_find_match(&app->db, match_id);
    if (match == NULL) {
        return;
    }
    Database *db = &app->db;
    const uint64_t teams[2] = { match->home_team_id, match->away_team_id };
    for (int side = 0; side < 2; ++side) {
        const Team *team = db_find_team(db, teams[side]);
        char team_name[128];
        if (team != NULL) {
            snprintf(team_name, sizeof(team_name), "%s", team->name);
        } else {
            snprintf(team_name, sizeof(team_name), "-");
        }
        for (size_t i = 0; i < db->appearance_count; ++i) {
            const Appearance *appearance = &db->appearances[i];
            if (appearance->match_id != match_id ||
                appearance->team_id != teams[side]) {
                continue;
            }
            const Player *player = db_find_player(db, appearance->player_id);
            char player_name[160];
            if (player != NULL) {
                format_player_name(player, player_name,
                                   sizeof(player_name));
            } else {
                snprintf(player_name, sizeof(player_name), "Unknown player");
            }
            char minutes[16];
            char performance[16];
            char before[16];
            char change[16];
            char after[16];
            snprintf(minutes, sizeof(minutes), "%d", appearance->minutes);
            snprintf(performance, sizeof(performance), "%.2f",
                     appearance->performance);
            snprintf(before, sizeof(before), "%.2f",
                     appearance->rating_before);
            snprintf(change, sizeof(change), "%+.2f",
                     appearance->rating_delta);
            snprintf(after, sizeof(after), "%.2f",
                     appearance->rating_after);
            const char *columns[7] = {
                team_name, player_name, minutes, performance,
                before, change, after,
            };
            ui_list_add_row(list, columns, 7, 0);
        }
    }
}

LRESULT CALLBACK match_details_dialog_proc(HWND hwnd, UINT message,
                                           WPARAM wparam,
                                           LPARAM lparam)
{
    MatchDetailsState *state =
        (MatchDetailsState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    case WM_SIZE:
        if (state != NULL && state->list != NULL) {
            details_layout(state, LOWORD(lparam), HIWORD(lparam));
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state != NULL) {
            details_paint(state, hwnd);
        }
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        return (LRESULT)dialog_control_color(message, wparam, state->app);
    case WM_COMMAND:
        if (state == NULL) {
            break;
        }
        if (LOWORD(wparam) == DETAILS_CLOSE_ID) {
            dialog_close(hwnd);
            return 0;
        }
        if (LOWORD(wparam) == DETAILS_EDIT_ID) {
            if (dialog_edit_match(state->app, hwnd, state->match_id)) {
                details_refresh(state->list, state->app, state->match_id);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        if (LOWORD(wparam) == DETAILS_DELETE_ID) {
            const Match *match = db_find_match(&state->app->db,
                                               state->match_id);
            if (match == NULL) {
                return 0;
            }
            if (MessageBoxW(
                    hwnd,
                    L"Delete this match?\n\nAll player ratings will be replayed without it. This cannot be undone.",
                    L"Delete match",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                return 0;
            }
            Database backup;
            if (!app_tx_begin(state->app, &backup)) {
                return 0;
            }
            char error[512];
            if (!db_delete_match(&state->app->db, state->match_id,
                                 error, sizeof(error))) {
                app_tx_abort(state->app, &backup);
                app_show_error_utf8(state->app, L"Could not delete match",
                                    error);
                return 0;
            }
            if (!app_tx_commit(state->app, &backup)) {
                return 0;
            }
            dialog_close(hwnd);
            return 0;
        }
        break;
    case WM_NOTIFY: {
        if (state == NULL) {
            break;
        }
        NMHDR *header = (NMHDR *)lparam;
        if (header->idFrom == DETAILS_LIST_ID &&
            header->code == NM_CUSTOMDRAW) {
            return ui_list_custom_draw((NMLVCUSTOMDRAW *)lparam,
                                       state->app->ui.row_images);
        }
        break;
    }
    case WM_CLOSE:
        dialog_close(hwnd);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

void dialog_match_details(App *app, uint64_t match_id)
{
    const Match *match = db_find_match(&app->db, match_id);
    if (match == NULL) {
        return;
    }
    MatchDetailsState state;
    memset(&state, 0, sizeof(state));
    state.app = app;
    state.match_id = match_id;
    HWND hwnd = dialog_create_shell(
        app, app->main_hwnd, DETAILS_DIALOG_CLASS,
        L"Match details - PitchRate", 940, 660, &state);
    if (hwnd == NULL) {
        return;
    }
    state.hwnd = hwnd;
    UiTheme *theme = &app->ui;
    state.list = ui_list_create(hwnd, DETAILS_LIST_ID, 0, 0, 10, 10,
                                theme->font_body, theme->row_images);
    state.edit_button = ui_button_create(
        hwnd, DETAILS_EDIT_ID, L"Edit match",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    state.delete_button = ui_button_create(
        hwnd, DETAILS_DELETE_ID, L"Delete match",
        0, 0, 10, 10, UI_BUTTON_DANGER, theme->font_button);
    state.close_button = ui_button_create(
        hwnd, DETAILS_CLOSE_ID, L"Close",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    if (state.list == NULL || state.edit_button == NULL ||
        state.delete_button == NULL || state.close_button == NULL) {
        dialog_close(hwnd);
        return;
    }
    ui_list_add_column(state.list, 0, L"Team", 110);
    ui_list_add_column(state.list, 1, L"Player", 190);
    ui_list_add_column(state.list, 2, L"Min", 60);
    ui_list_add_column(state.list, 3, L"Perf", 60);
    ui_list_add_column(state.list, 4, L"Before", 80);
    ui_list_add_column(state.list, 5, L"Change", 80);
    ui_list_add_column(state.list, 6, L"After", 80);
    details_refresh(state.list, app, match_id);
    RECT client;
    GetClientRect(hwnd, &client);
    details_layout(&state, client.right, client.bottom);
    InvalidateRect(hwnd, NULL, FALSE);
    ui_run_modal(hwnd, app->main_hwnd, DETAILS_CLOSE_ID);
}
