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
};

enum {
    DETAILS_LIST_ID = 2300,
    DETAILS_DELETE_ID,
    DETAILS_CLOSE_ID,
};

typedef struct {
    uint64_t player_id;
    uint64_t team_id;
    char player_name[160];
    char team_name[128];
    bool selected;
    int minutes;
    double performance;
    double current_rating;
} LineupRow;

typedef struct {
    App *app;
    HWND hwnd;
    HWND date;
    HWND home_team;
    HWND away_team;
    HWND home_score;
    HWND away_score;
    HWND list;
    HWND select_all;
    HWND clear_all;
    HWND neutral_venue;
    HWND save;
    HWND cancel;
    LineupRow *rows;
    size_t row_count;
    bool building;
    HWND cell_edit;
    uint64_t cell_edit_generation;
    int cell_item;
    int cell_subitem;
    wchar_t error[512];
    uint64_t new_match_id;
    RECT label_date;
    RECT label_home_team;
    RECT label_home_score;
    RECT label_away_score;
    RECT label_away_team;
    RECT neutral_rect;
    RECT info_rect;
    RECT section_rect;
    RECT list_frame;
    RECT footer_line;
    RECT error_rect;
} RecordMatchState;

typedef struct {
    App *app;
    HWND hwnd;
    uint64_t match_id;
    HWND list;
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

static bool playable_team_count(const Database *db)
{
    size_t count = 0;
    for (size_t i = 0; i < db->team_count; ++i) {
        if (db_team_player_count(db, db->teams[i].id, true) > 0) {
            count++;
        }
    }
    return count >= 2;
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
}

static void record_cancel_cell_edit(RecordMatchState *state);
static bool record_commit_cell_edit(RecordMatchState *state, bool accept);

static int compare_players_by_name(const void *left, const void *right)
{
    const Player *a = *(const Player *const *)left;
    const Player *b = *(const Player *const *)right;
    int result = strcmp(a->last_name, b->last_name);
    if (result != 0) {
        return result;
    }
    result = strcmp(a->first_name, b->first_name);
    if (result != 0) {
        return result;
    }
    if (a->id < b->id) {
        return -1;
    }
    if (a->id > b->id) {
        return 1;
    }
    return 0;
}

static void record_rebuild_rows(RecordMatchState *state)
{
    record_cancel_cell_edit(state);
    record_free_rows(state);
    ListView_DeleteAllItems(state->list);
    uint64_t home_id = current_home_team(state);
    uint64_t away_id = current_away_team(state);
    if (home_id == 0 || away_id == 0 || home_id == away_id) {
        InvalidateRect(state->hwnd, NULL, FALSE);
        return;
    }
    Database *db = &state->app->db;
    size_t count = db_team_player_count(db, home_id, true) +
                   db_team_player_count(db, away_id, true);
    if (count == 0) {
        InvalidateRect(state->hwnd, NULL, FALSE);
        return;
    }
    state->rows = calloc(count, sizeof(LineupRow));
    if (state->rows == NULL) {
        record_show_error_wide(state, L"Not enough memory to load the line-ups.");
        return;
    }

    state->building = true;
    bool insert_failed = false;
    const uint64_t teams[2] = { home_id, away_id };
    for (int side = 0; side < 2 && !insert_failed; ++side) {
        const Team *team = db_find_team(db, teams[side]);
        size_t team_player_count = db_team_player_count(db, teams[side], true);
        const Player **team_players = NULL;
        if (team_player_count > 0) {
            team_players = malloc(team_player_count * sizeof(*team_players));
            if (team_players == NULL) {
                record_show_error_wide(
                    state, L"Not enough memory to load the line-ups.");
                insert_failed = true;
                break;
            }
            size_t used = 0;
            for (size_t i = 0; i < db->player_count; ++i) {
                const Player *player = &db->players[i];
                if (player->active && player->team_id == teams[side]) {
                    team_players[used++] = player;
                }
            }
            qsort(team_players, team_player_count, sizeof(*team_players),
                  compare_players_by_name);
        }
        for (size_t player_index = 0;
             player_index < team_player_count && !insert_failed;
             ++player_index) {
            const Player *player = team_players[player_index];
            LineupRow *row = &state->rows[state->row_count];
            memset(row, 0, sizeof(*row));
            row->player_id = player->id;
            row->team_id = player->team_id;
            row->selected = false;
            row->minutes = 90;
            row->performance = RATING_NEUTRAL_PERFORMANCE;
            row->current_rating = player->current_rating;
            format_player_name(player, row->player_name,
                               sizeof(row->player_name));
            if (team != NULL) {
                snprintf(row->team_name, sizeof(row->team_name), "%s",
                         team->name);
            } else {
                snprintf(row->team_name, sizeof(row->team_name), "-");
            }
            char minutes[16];
            char performance[16];
            char rating[16];
            snprintf(minutes, sizeof(minutes), "%d", row->minutes);
            snprintf(performance, sizeof(performance), "%.2f",
                     row->performance);
            snprintf(rating, sizeof(rating), "%.1f",
                     row->current_rating);
            const char *columns[5] = {
                row->player_name, row->team_name, minutes, performance,
                rating,
            };
            int list_index = ui_list_add_row(
                state->list, columns, 5, (LPARAM)state->row_count);
            if (list_index < 0) {
                record_show_error_wide(
                    state, L"Could not add a player to the line-up list.");
                insert_failed = true;
                break;
            }
            ListView_SetCheckState(state->list, list_index, FALSE);
            state->row_count++;
        }
        free(team_players);
    }
    state->building = false;
    InvalidateRect(state->hwnd, NULL, FALSE);
}

static void record_apply_checkbox(RecordMatchState *state, int item)
{
    if (state->building || item < 0 || (size_t)item >= state->row_count) {
        return;
    }
    state->rows[item].selected =
        ListView_GetCheckState(state->list, item) != 0;
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

static void record_begin_cell_edit(RecordMatchState *state, int item,
                                   int subitem)
{
    if (item < 0 || (size_t)item >= state->row_count ||
        (subitem != 2 && subitem != 3)) {
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
    if (!ListView_GetSubItemRect(state->list, item, subitem, LVIR_BOUNDS,
                                 &cell)) {
        return;
    }
    MapWindowPoints(state->list, state->hwnd, (POINT *)&cell, 2);
    InflateRect(&cell, -1, -1);
    HWND edit = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_CENTER,
        cell.left, cell.top, cell.right - cell.left, cell.bottom - cell.top,
        state->hwnd, NULL, state->app->instance, NULL);
    if (edit == NULL) {
        return;
    }
    SendMessageW(edit, WM_SETFONT,
                 (WPARAM)state->app->ui.font_body, TRUE);
    SetPropW(edit, CELL_EDITOR_PROPERTY, (HANDLE)1);
    wchar_t text[32];
    if (subitem == 2) {
        ui_format_wide(text, sizeof(text) / sizeof(text[0]), L"%d",
                       state->rows[item].minutes);
    } else {
        ui_format_wide(text, sizeof(text) / sizeof(text[0]), L"%.2f",
                       state->rows[item].performance);
    }
    SetWindowTextW(edit, text);
    SetWindowSubclass(edit, cell_edit_subclass, 1, (DWORD_PTR)state);
    state->cell_edit = edit;
    state->cell_item = item;
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
    int item = state->cell_item;
    int subitem = state->cell_subitem;
    wchar_t text[64];
    GetWindowTextW(edit, text, (int)(sizeof(text) / sizeof(text[0])));

    if (accept && item >= 0 && (size_t)item < state->row_count) {
        char utf8[128];
        ui_wide_to_utf8(text, utf8, sizeof(utf8));
        if (subitem == 2) {
            int minutes = 0;
            if (!util_parse_int(utf8, 1, 120, &minutes)) {
                record_show_error_wide(
                    state, L"Minutes played must be a whole number from 1 to 120.");
                SetFocus(edit);
                SendMessageW(edit, EM_SETSEL, 0, -1);
                return false;
            }
            state->rows[item].minutes = minutes;
        } else if (subitem == 3) {
            double performance = 0.0;
            if (!util_parse_double(utf8, 1.0, 10.0, &performance)) {
                record_show_error_wide(
                    state, L"Performance must be between 1.0 and 10.0.");
                SetFocus(edit);
                SendMessageW(edit, EM_SETSEL, 0, -1);
                return false;
            }
            state->rows[item].performance =
                round(performance * 100.0) / 100.0;
        } else {
            return true;
        }
        state->error[0] = L'\0';
        wchar_t display[32];
        LVITEMW value;
        memset(&value, 0, sizeof(value));
        if (subitem == 2) {
            ui_format_wide(display, sizeof(display) / sizeof(display[0]),
                           L"%d", state->rows[item].minutes);
        } else {
            ui_format_wide(display, sizeof(display) / sizeof(display[0]),
                           L"%.2f", state->rows[item].performance);
        }
        value.iSubItem = subitem;
        value.pszText = display;
        SendMessageW(state->list, LVM_SETITEMTEXTW, (WPARAM)item,
                     (LPARAM)&value);
    }

    state->cell_edit = NULL;
    DestroyWindow(edit);
    SetFocus(state->list);
    ListView_SetItemState(state->list, item,
                          LVIS_SELECTED | LVIS_FOCUSED,
                          LVIS_SELECTED | LVIS_FOCUSED);
    InvalidateRect(state->hwnd, NULL, FALSE);
    return true;
}

static void record_cancel_cell_edit(RecordMatchState *state)
{
    if (state->cell_edit != NULL) {
        HWND edit = state->cell_edit;
        state->cell_edit = NULL;
        DestroyWindow(edit);
    }
}

static void record_select_all(RecordMatchState *state, bool selected)
{
    if (state->cell_edit != NULL &&
        !record_commit_cell_edit(state, true)) {
        return;
    }
    state->building = true;
    for (int i = 0; i < ListView_GetItemCount(state->list); ++i) {
        ListView_SetCheckState(state->list, i, selected);
        if ((size_t)i < state->row_count) {
            state->rows[i].selected = selected;
        }
    }
    state->building = false;
    InvalidateRect(state->hwnd, NULL, FALSE);
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

    int list_top = section_y + ui_scale(theme, 28);
    int footer_top = height - ui_scale(theme, 82);
    if (footer_top < list_top) {
        footer_top = list_top;
    }
    state->list_frame = (RECT){ margin, list_top, width - margin,
                                footer_top };
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

    static const int widths[5] = { 30, 24, 12, 14, 20 };
    ui_list_set_percent_widths(state->list, widths, 5);
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
            state->app, canvas.dc, client.right, L"Record a match",
            L"Set the fixture and line-ups. Ratings are calculated on save.");
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
            L"Tick everyone who played. Double-click Minutes or Performance to edit. Performance is compared with the team average; 6.0 is the default.",
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        ui_draw_text(canvas.dc, state->app->ui.font_body_bold,
                     UI_COLOR_TEXT, state->section_rect, L"Playing squads",
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        ui_fill_round_rect(canvas.dc, state->list_frame,
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
                               L"Not enough memory to record the match.");
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
    bool ok = db_add_match(&state->app->db, &input, &new_id,
                           error, sizeof(error));
    free(lineup);
    if (!ok) {
        app_tx_abort(state->app, &backup);
        record_show_error(state, error);
        return;
    }
    if (!app_tx_commit(state->app, &backup)) {
        return;
    }
    state->new_match_id = new_id;
    DestroyWindow(state->hwnd);
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
        if (state != NULL && state->list != NULL) {
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
                record_rebuild_rows(state);
                return 0;
            }
            break;
        case RECORD_AWAY_TEAM_ID:
            if (HIWORD(wparam) == CBN_SELCHANGE) {
                record_selected_other_team(state, false);
                state->error[0] = L'\0';
                record_rebuild_rows(state);
                return 0;
            }
            break;
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
            DestroyWindow(hwnd);
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
        if (header->idFrom != RECORD_LIST_ID) {
            break;
        }
        if (header->code == NM_CUSTOMDRAW) {
            return ui_list_custom_draw((NMLVCUSTOMDRAW *)lparam,
                                       state->app->ui.row_images);
        }
        if (header->code == LVN_ITEMCHANGED) {
            NMLISTVIEW *change = (NMLISTVIEW *)lparam;
            if (!state->building) {
                record_apply_checkbox(state, change->iItem);
            }
            return 0;
        }
        if (header->code == NM_DBLCLK) {
            POINT point;
            GetCursorPos(&point);
            ScreenToClient(state->list, &point);
            LVHITTESTINFO hit;
            memset(&hit, 0, sizeof(hit));
            hit.pt = point;
            ListView_SubItemHitTest(state->list, &hit);
            if (hit.iItem >= 0 &&
                (hit.iSubItem == 2 || hit.iSubItem == 3)) {
                record_begin_cell_edit(state, hit.iItem, hit.iSubItem);
            }
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static bool record_build(App *app, RecordMatchState *state)
{
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
    state->list = ui_list_create(state->hwnd, RECORD_LIST_ID,
                                 0, 0, 10, 10, theme->font_body,
                                 theme->row_images);
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
        state->hwnd, RECORD_SAVE_ID, L"Record match",
        0, 0, 10, 10, UI_BUTTON_PRIMARY, theme->font_button);
    state->cancel = ui_button_create(
        state->hwnd, RECORD_CANCEL_ID, L"Cancel",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    if (state->date == NULL || state->home_team == NULL ||
        state->away_team == NULL || state->home_score == NULL ||
        state->away_score == NULL || state->list == NULL ||
        state->select_all == NULL || state->clear_all == NULL ||
        state->neutral_venue == NULL ||
        state->save == NULL || state->cancel == NULL) {
        return false;
    }
    SendMessageW(state->list, LVM_SETEXTENDEDLISTVIEWSTYLE,
                 LVS_EX_CHECKBOXES, LVS_EX_CHECKBOXES);
    ui_list_add_column(state->list, 0, L"Player", 220);
    ui_list_add_column(state->list, 1, L"Team", 170);
    ui_list_add_column(state->list, 2, L"Minutes", 80);
    ui_list_add_column(state->list, 3, L"Performance", 100);
    ui_list_add_column(state->list, 4, L"Current rating", 100);

    for (size_t i = 0; i < app->db.team_count; ++i) {
        const Team *team = &app->db.teams[i];
        if (db_team_player_count(&app->db, team->id, true) == 0) {
            continue;
        }
        ui_combo_add_utf8(state->home_team, team->name, team->id);
        ui_combo_add_utf8(state->away_team, team->name, team->id);
    }
    record_select_default_teams(state);

    char today[11];
    util_date_today(today);
    ui_edit_set_utf8(state->date, today);
    ui_edit_set_cue(state->date, L"YYYY-MM-DD");

    RECT client;
    GetClientRect(state->hwnd, &client);
    record_layout(state, client.right, client.bottom);
    record_rebuild_rows(state);
    InvalidateRect(state->hwnd, NULL, FALSE);
    return true;
}

uint64_t dialog_record_match(App *app)
{
    if (!playable_team_count(&app->db)) {
        MessageBoxW(app->main_hwnd,
                    L"At least two teams with active players are needed to record a match.",
                    L"PitchRate", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    RecordMatchState state;
    memset(&state, 0, sizeof(state));
    state.app = app;
    HWND hwnd = dialog_create_shell(
        app, RECORD_DIALOG_CLASS, L"Record match - PitchRate",
        980, 650, &state);
    if (hwnd == NULL) {
        return 0;
    }
    state.hwnd = hwnd;
    if (!record_build(app, &state)) {
        DestroyWindow(hwnd);
        record_free_rows(&state);
        return 0;
    }
    ui_run_modal(hwnd, app->main_hwnd, RECORD_SAVE_ID);
    record_free_rows(&state);
    return state.new_match_id;
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
    MoveWindow(state->close_button, width - margin - close_width, button_y,
               close_width, button_height, TRUE);
    MoveWindow(state->delete_button, margin, button_y,
               delete_width, button_height, TRUE);
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
            DestroyWindow(hwnd);
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
            DestroyWindow(hwnd);
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
        DestroyWindow(hwnd);
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
        app, DETAILS_DIALOG_CLASS, L"Match details - PitchRate",
        940, 660, &state);
    if (hwnd == NULL) {
        return;
    }
    state.hwnd = hwnd;
    UiTheme *theme = &app->ui;
    state.list = ui_list_create(hwnd, DETAILS_LIST_ID, 0, 0, 10, 10,
                                theme->font_body, theme->row_images);
    state.delete_button = ui_button_create(
        hwnd, DETAILS_DELETE_ID, L"Delete match",
        0, 0, 10, 10, UI_BUTTON_DANGER, theme->font_button);
    state.close_button = ui_button_create(
        hwnd, DETAILS_CLOSE_ID, L"Close",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    if (state.list == NULL || state.delete_button == NULL ||
        state.close_button == NULL) {
        DestroyWindow(hwnd);
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
