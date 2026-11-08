#include "ui/dialogs.h"

#include "core/database.h"
#include "core/util.h"
#include "resource.h"
#include "ui/controls.h"
#include "ui/dialog_common.h"
#include "ui/theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define TEAM_DIALOG_CLASS L"PitchRateTeamDialog"
#define PLAYER_DIALOG_CLASS L"PitchRatePlayerDialog"
#define MATCH_DIALOG_CLASS L"PitchRateMatchDialog"
#define MATCH_DETAILS_DIALOG_CLASS L"PitchRateMatchDetailsDialog"

enum {
    TEAM_DIALOG_NAME_ID = 2000,
    TEAM_DIALOG_SHORT_ID,
    TEAM_DIALOG_CITY_ID,
    TEAM_DIALOG_SAVE_ID,
    TEAM_DIALOG_CANCEL_ID,
};

enum {
    PLAYER_DIALOG_FIRST_ID = 2100,
    PLAYER_DIALOG_LAST_ID,
    PLAYER_DIALOG_TEAM_ID,
    PLAYER_DIALOG_RATING_ID,
    PLAYER_DIALOG_ACTIVE_ID,
    PLAYER_DIALOG_SAVE_ID,
    PLAYER_DIALOG_CANCEL_ID,
};

enum {
    PLAYER_DUPLICATE_ADD_ID = 2400,
    PLAYER_DUPLICATE_MOVE_ID,
};

typedef enum {
    PLAYER_DUPLICATE_CANCEL = 0,
    PLAYER_DUPLICATE_ADD,
    PLAYER_DUPLICATE_MOVE
} PlayerDuplicateChoice;

typedef struct {
    App *app;
    HWND hwnd;
    uint64_t team_id;
    uint64_t saved_id;
    HWND name;
    HWND short_name;
    HWND city;
    HWND save;
    HWND cancel;
    RECT label_name;
    RECT label_short;
    RECT label_city;
    RECT helper_short;
    RECT error_rect;
    wchar_t error[512];
} TeamDialogState;

typedef struct {
    App *app;
    HWND hwnd;
    uint64_t player_id;
    uint64_t saved_id;
    HWND first_name;
    HWND last_name;
    HWND team;
    HWND rating;
    HWND active;
    HWND save;
    HWND cancel;
    RECT label_first;
    RECT label_last;
    RECT label_team;
    RECT label_rating;
    RECT rating_hint;
    RECT rating_note;
    RECT error_rect;
    wchar_t error[512];
} PlayerDialogState;

bool dialog_register_class(HINSTANCE instance, const wchar_t *name,
                           WNDPROC proc)
{
    WNDCLASSEXW window_class;
    memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    window_class.lpfnWndProc = proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP));
    window_class.hbrBackground = NULL;
    window_class.lpszClassName = name;
    return RegisterClassExW(&window_class) != 0;
}

void dialogs_register_classes(HINSTANCE instance)
{
    dialog_register_class(instance, TEAM_DIALOG_CLASS, team_dialog_proc);
    dialog_register_class(instance, PLAYER_DIALOG_CLASS, player_dialog_proc);
    dialog_register_class(instance, MATCH_DIALOG_CLASS,
                          record_match_dialog_proc);
    dialog_register_class(instance, MATCH_DETAILS_DIALOG_CLASS,
                          match_details_dialog_proc);
}

HWND dialog_create_shell(App *app, const wchar_t *class_name,
                         const wchar_t *title, int client_width,
                         int client_height, void *state)
{
    UiTheme *theme = &app->ui;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN |
                  WS_CLIPSIBLINGS;
    DWORD extended = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;

    int width = ui_scale(theme, client_width);
    int height = ui_scale(theme, client_height);
    RECT outer = { 0, 0, width, height };
    AdjustWindowRectEx(&outer, style, FALSE, extended);
    int frame_width = (outer.right - outer.left) - width;
    int frame_height = (outer.bottom - outer.top) - height;

    MONITORINFO monitor;
    memset(&monitor, 0, sizeof(monitor));
    monitor.cbSize = sizeof(monitor);
    HMONITOR handle = MonitorFromWindow(
        app->main_hwnd != NULL ? app->main_hwnd : GetDesktopWindow(),
        MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfoW(handle, &monitor)) {
        int max_width = monitor.rcWork.right - monitor.rcWork.left -
                        ui_scale(theme, 32);
        int max_height = monitor.rcWork.bottom - monitor.rcWork.top -
                         ui_scale(theme, 32);
        if (width + frame_width > max_width) {
            width = max_width - frame_width;
        }
        if (height + frame_height > max_height) {
            height = max_height - frame_height;
        }
    }
    if (width < ui_scale(theme, 360)) {
        width = ui_scale(theme, 360);
    }
    if (height < ui_scale(theme, 280)) {
        height = ui_scale(theme, 280);
    }

    HWND hwnd = CreateWindowExW(
        extended, class_name, title, style,
        CW_USEDEFAULT, CW_USEDEFAULT, width + frame_width, height + frame_height,
        app->main_hwnd, NULL, app->instance, state);
    if (hwnd != NULL) {
        SetWindowTextW(hwnd, title);
        ui_center_window(hwnd, app->main_hwnd);
    }
    return hwnd;
}

void dialog_draw_chrome(App *app, HDC dc, int width,
                        const wchar_t *title, const wchar_t *subtitle)
{
    UiTheme *theme = &app->ui;
    int margin = ui_scale(theme, 28);

    RECT accent = { margin, ui_scale(theme, 22), margin + ui_scale(theme, 3),
                    ui_scale(theme, 51) };
    ui_fill_round_rect(dc, accent, ui_scale(theme, 2), UI_COLOR_ACCENT);

    RECT title_rect = { margin + ui_scale(theme, 13), ui_scale(theme, 16),
                        width - margin, ui_scale(theme, 50) };
    ui_draw_text(dc, theme->font_h2, UI_COLOR_TEXT, title_rect, title,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT subtitle_rect = { title_rect.left, ui_scale(theme, 49),
                           width - margin, ui_scale(theme, 69) };
    ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, subtitle_rect,
                 subtitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                                DT_END_ELLIPSIS);

    ui_draw_line(dc, margin, ui_scale(theme, 82), width - margin,
                 ui_scale(theme, 82), UI_COLOR_BORDER);
}

void dialog_draw_label(App *app, HDC dc, int x, int y,
                       const wchar_t *text)
{
    UiTheme *theme = &app->ui;
    RECT rect = { x, y, x + ui_scale(theme, 400), y + ui_scale(theme, 18) };
    ui_draw_text(dc, theme->font_small_bold, UI_COLOR_MUTED, rect, text,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void dialog_draw_error(App *app, HDC dc, RECT rect, const wchar_t *text)
{
    if (text == NULL || text[0] == L'\0') {
        return;
    }
    UiTheme *theme = &app->ui;
    ui_draw_text(dc, theme->font_small, UI_COLOR_DANGER, rect, text,
                 DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
}

HBRUSH dialog_control_color(UINT message, WPARAM wparam, App *app)
{
    HDC dc = (HDC)wparam;
    if (message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX) {
        SetTextColor(dc, UI_COLOR_TEXT);
        SetBkColor(dc, UI_COLOR_FIELD_BG);
        return app->ui.brush_field;
    }
    if (message == WM_CTLCOLORBTN) {
        SetTextColor(dc, UI_COLOR_TEXT);
        SetBkColor(dc, UI_COLOR_BG);
        return app->ui.brush_bg;
    }
    return NULL;
}

static LRESULT team_dialog_paint(TeamDialogState *state, HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC target = BeginPaint(hwnd, &paint);
    RECT client;
    GetClientRect(hwnd, &client);
    UiCanvas canvas;
    if (ui_canvas_begin(&canvas, hwnd, target, &state->app->ui,
                        UI_COLOR_BG)) {
        dialog_draw_chrome(state->app, canvas.dc, client.right,
                           state->team_id == 0 ? L"Add team" : L"Edit team",
                           state->team_id == 0
                               ? L"Create a club to organise your players."
                               : L"Update the club's details.");
        dialog_draw_label(state->app, canvas.dc, state->label_name.left,
                          state->label_name.top, L"Team name");
        dialog_draw_label(state->app, canvas.dc, state->label_short.left,
                          state->label_short.top, L"Short name");
        dialog_draw_label(state->app, canvas.dc, state->label_city.left,
                          state->label_city.top, L"City");
        RECT helper = state->helper_short;
        ui_draw_text(canvas.dc, state->app->ui.font_small,
                     UI_COLOR_FAINT, helper,
                     L"Optional. Used on compact scoreboards.",
                     DT_LEFT | DT_TOP | DT_SINGLELINE);
        dialog_draw_error(state->app, canvas.dc, state->error_rect,
                          state->error);
        ui_canvas_end(&canvas);
    }
    EndPaint(hwnd, &paint);
    return 0;
}

static void team_dialog_layout(TeamDialogState *state, int width, int height)
{
    UiTheme *theme = &state->app->ui;
    int margin = ui_scale(theme, 28);
    int content_width = width - margin * 2;
    int label_height = ui_scale(theme, 18);
    int field_height = ui_scale(theme, 32);

    int y = ui_scale(theme, 98);
    state->label_name = (RECT){ margin, y, width - margin,
                                y + label_height };
    MoveWindow(state->name, margin, y + label_height + ui_scale(theme, 4),
               content_width, field_height, TRUE);
    y += label_height + field_height + ui_scale(theme, 18);
    state->label_short = (RECT){ margin, y, width - margin,
                                 y + label_height };
    MoveWindow(state->short_name, margin, y + label_height + ui_scale(theme, 4),
               ui_scale(theme, 170), field_height, TRUE);
    state->helper_short = (RECT){ margin + ui_scale(theme, 182),
                                 y + label_height + ui_scale(theme, 6),
                                 width - margin,
                                 y + label_height + ui_scale(theme, 28) };
    y += label_height + field_height + ui_scale(theme, 22);
    state->label_city = (RECT){ margin, y, width - margin,
                                y + label_height };
    MoveWindow(state->city, margin, y + label_height + ui_scale(theme, 4),
               content_width, field_height, TRUE);

    int button_height = ui_scale(theme, 36);
    int button_y = height - ui_scale(theme, 56);
    int save_width = ui_scale(theme, 116);
    int cancel_width = ui_scale(theme, 92);
    int right = width - margin;
    MoveWindow(state->save, right - save_width, button_y,
               save_width, button_height, TRUE);
    MoveWindow(state->cancel,
               right - save_width - ui_scale(theme, 10) - cancel_width,
               button_y, cancel_width, button_height, TRUE);
    state->error_rect = (RECT){ margin, button_y - ui_scale(theme, 22),
                               right - save_width - ui_scale(theme, 20),
                               button_y - ui_scale(theme, 2) };
}

static void team_dialog_show_error(TeamDialogState *state,
                                   const char *utf8_error)
{
    ui_utf8_to_wide(utf8_error, state->error,
                    sizeof(state->error) / sizeof(state->error[0]));
    InvalidateRect(state->hwnd, NULL, FALSE);
}

static void team_dialog_save(TeamDialogState *state)
{
    char name[256];
    char short_name[128];
    char city[256];
    ui_edit_get_utf8(state->name, name, sizeof(name));
    ui_edit_get_utf8(state->short_name, short_name, sizeof(short_name));
    ui_edit_get_utf8(state->city, city, sizeof(city));

    Database backup;
    if (!app_tx_begin(state->app, &backup)) {
        return;
    }
    char error[512];
    uint64_t saved = 0;
    bool ok;
    if (state->team_id == 0) {
        ok = db_add_team(&state->app->db, name, short_name, city, &saved,
                         error, sizeof(error));
    } else {
        ok = db_update_team(&state->app->db, state->team_id, name,
                            short_name, city, error, sizeof(error));
        saved = state->team_id;
    }
    if (!ok) {
        app_tx_abort(state->app, &backup);
        team_dialog_show_error(state, error);
        return;
    }
    if (!app_tx_commit(state->app, &backup)) {
        return;
    }
    state->saved_id = saved;
    DestroyWindow(state->hwnd);
}

LRESULT CALLBACK team_dialog_proc(HWND hwnd, UINT message,
                                  WPARAM wparam, LPARAM lparam)
{
    TeamDialogState *state =
        (TeamDialogState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    case WM_SIZE:
        if (state != NULL && state->save != NULL) {
            team_dialog_layout(state, LOWORD(lparam), HIWORD(lparam));
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state != NULL) {
            return team_dialog_paint(state, hwnd);
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        return (LRESULT)dialog_control_color(message, wparam, state->app);
    case WM_COMMAND:
        if (state == NULL) {
            break;
        }
        switch (LOWORD(wparam)) {
        case TEAM_DIALOG_SAVE_ID:
            team_dialog_save(state);
            return 0;
        case TEAM_DIALOG_CANCEL_ID:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void team_dialog_show_error_direct(TeamDialogState *state,
                                          const wchar_t *error)
{
    ui_format_wide(state->error,
                   sizeof(state->error) / sizeof(state->error[0]), L"%ls",
                   error);
    InvalidateRect(state->hwnd, NULL, FALSE);
}

uint64_t dialog_edit_team(App *app, uint64_t team_id)
{
    TeamDialogState state;
    memset(&state, 0, sizeof(state));
    state.app = app;
    state.team_id = team_id;

    HWND hwnd = dialog_create_shell(
        app, TEAM_DIALOG_CLASS,
        team_id == 0 ? L"Add team - PitchRate" : L"Edit team - PitchRate",
        500, 390, &state);
    if (hwnd == NULL) {
        return 0;
    }
    state.hwnd = hwnd;

    UiTheme *theme = &app->ui;
    state.name = ui_edit_create(hwnd, TEAM_DIALOG_NAME_ID, L"",
                                0, 0, 10, 10, theme->font_body, 80);
    state.short_name = ui_edit_create(hwnd, TEAM_DIALOG_SHORT_ID, L"",
                                      0, 0, 10, 10, theme->font_body, 24);
    state.city = ui_edit_create(hwnd, TEAM_DIALOG_CITY_ID, L"",
                                0, 0, 10, 10, theme->font_body, 80);
    state.save = ui_button_create(
        hwnd, TEAM_DIALOG_SAVE_ID,
        team_id == 0 ? L"Add team" : L"Save changes",
        0, 0, 10, 10, UI_BUTTON_PRIMARY, theme->font_button);
    state.cancel = ui_button_create(
        hwnd, TEAM_DIALOG_CANCEL_ID, L"Cancel",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    if (state.name == NULL || state.short_name == NULL || state.city == NULL ||
        state.save == NULL || state.cancel == NULL) {
        DestroyWindow(hwnd);
        return 0;
    }
    const Team *team = db_find_team(&app->db, team_id);
    if (team != NULL) {
        ui_edit_set_utf8(state.name, team->name);
        ui_edit_set_utf8(state.short_name, team->short_name);
        ui_edit_set_utf8(state.city, team->city);
    }
    if (team == NULL && team_id != 0) {
        team_dialog_show_error_direct(&state, L"Team not found.");
    }
    RECT client;
    GetClientRect(hwnd, &client);
    team_dialog_layout(&state, client.right, client.bottom);
    InvalidateRect(hwnd, NULL, FALSE);

    ui_run_modal(hwnd, app->main_hwnd, TEAM_DIALOG_SAVE_ID);
    return state.saved_id;
}

static void player_full_name(const Player *player, wchar_t *buffer,
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

static PlayerDuplicateChoice player_duplicate_prompt(
    App *app, HWND owner, const Player *duplicate, uint64_t target_team_id,
    size_t duplicate_count)
{
    wchar_t name[160];
    player_full_name(duplicate, name, sizeof(name) / sizeof(name[0]));

    const Team *current_team = db_find_team(&app->db, duplicate->team_id);
    const Team *target_team = db_find_team(&app->db, target_team_id);
    wchar_t current_name[128] = L"an unknown club";
    wchar_t target_name[128] = L"the selected club";
    if (current_team != NULL) {
        ui_utf8_to_wide(current_team->name, current_name,
                        sizeof(current_name) / sizeof(current_name[0]));
    }
    if (target_team != NULL) {
        ui_utf8_to_wide(target_team->name, target_name,
                        sizeof(target_name) / sizeof(target_name[0]));
    }
    bool can_move = target_team != NULL &&
                    target_team_id != duplicate->team_id;

    wchar_t intro[512];
    if (duplicate_count > 1) {
        ui_format_wide(intro, sizeof(intro) / sizeof(intro[0]),
                       L"%ls already exists in %ls. %zu players use this "
                       L"name; the earliest one is shown below.",
                       name, current_name, duplicate_count);
    } else {
        ui_format_wide(intro, sizeof(intro) / sizeof(intro[0]),
                       L"%ls already exists in %ls.",
                       name, current_name);
    }

    wchar_t content[2048];
    wchar_t details[1024];
    ui_format_wide(details, sizeof(details) / sizeof(details[0]),
                   L"%ls\n\nCurrent rating: %.1f after %u matches\n"
                   L"Starting rating: %.2f\nStatus: %ls",
                   intro, duplicate->current_rating, duplicate->matches_played,
                   duplicate->base_rating,
                   duplicate->active ? L"Available for selection" : L"Archived");
    wchar_t guidance[1024];
    if (can_move) {
        ui_format_wide(guidance, sizeof(guidance) / sizeof(guidance[0]),
                       L"\n\nChoose \"Add duplicate\" to create a separate "
                       L"player in %ls. Their appearances and ratings will be "
                       L"tracked independently.\n\nChoose \"Move existing "
                       L"player\" to change %ls's club to %ls instead. Their "
                       L"name, starting rating and match history stay "
                       L"unchanged.",
                       target_name, name, target_name);
    } else {
        ui_format_wide(guidance, sizeof(guidance) / sizeof(guidance[0]),
                       L"\n\n%ls already plays for %ls. Choose \"Add "
                       L"duplicate\" to keep two separate records, or Cancel "
                       L"to go back and change the name.",
                       name, current_name);
    }
    ui_format_wide(content, sizeof(content) / sizeof(content[0]),
                   L"%ls%ls", details, guidance);

    TASKDIALOG_BUTTON buttons[2];
    memset(buttons, 0, sizeof(buttons));
    UINT button_count = 0;
    buttons[button_count].nButtonID = PLAYER_DUPLICATE_ADD_ID;
    buttons[button_count].pszButtonText = L"Add duplicate";
    ++button_count;
    if (can_move) {
        buttons[button_count].nButtonID = PLAYER_DUPLICATE_MOVE_ID;
        buttons[button_count].pszButtonText = L"Move existing player";
        ++button_count;
    }

    TASKDIALOGCONFIG config;
    memset(&config, 0, sizeof(config));
    config.cbSize = sizeof(config);
    config.hwndParent = owner != NULL ? owner : app->main_hwnd;
    config.hInstance = app->instance;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION |
                     TDF_POSITION_RELATIVE_TO_WINDOW;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = L"Duplicate player - PitchRate";
    config.pszMainIcon = TD_WARNING_ICON;
    config.pszMainInstruction = L"A player with this name already exists";
    config.pszContent = content;
    config.cButtons = button_count;
    config.pButtons = buttons;
    config.nDefaultButton = IDCANCEL;

    int pressed = IDCANCEL;
    if (SUCCEEDED(TaskDialogIndirect(&config, &pressed, NULL, NULL))) {
        if (pressed == PLAYER_DUPLICATE_ADD_ID) {
            return PLAYER_DUPLICATE_ADD;
        }
        if (pressed == PLAYER_DUPLICATE_MOVE_ID) {
            return PLAYER_DUPLICATE_MOVE;
        }
        return PLAYER_DUPLICATE_CANCEL;
    }

    wchar_t fallback[2304];
    if (can_move) {
        ui_format_wide(fallback, sizeof(fallback) / sizeof(fallback[0]),
                       L"%ls\n\nYes: add a duplicate player in %ls.\n"
                       L"No: move %ls to %ls.\n"
                       L"Cancel: go back to the Add player form.",
                       details, target_name, name, target_name);
        int answer = MessageBoxW(config.hwndParent, fallback,
                                 L"Duplicate player",
                                 MB_YESNOCANCEL | MB_ICONWARNING |
                                     MB_DEFBUTTON3);
        if (answer == IDYES) {
            return PLAYER_DUPLICATE_ADD;
        }
        if (answer == IDNO) {
            return PLAYER_DUPLICATE_MOVE;
        }
        return PLAYER_DUPLICATE_CANCEL;
    }

    ui_format_wide(fallback, sizeof(fallback) / sizeof(fallback[0]),
                   L"%ls\n\nYes: add a duplicate player.\n"
                   L"No: go back to the Add player form.",
                   details);
    int answer = MessageBoxW(config.hwndParent, fallback, L"Duplicate player",
                             MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    return answer == IDYES ? PLAYER_DUPLICATE_ADD : PLAYER_DUPLICATE_CANCEL;
}

static LRESULT player_dialog_paint(PlayerDialogState *state, HWND hwnd)
{
    PAINTSTRUCT paint;
    HDC target = BeginPaint(hwnd, &paint);
    RECT client;
    GetClientRect(hwnd, &client);
    UiCanvas canvas;
    if (ui_canvas_begin(&canvas, hwnd, target, &state->app->ui,
                        UI_COLOR_BG)) {
        dialog_draw_chrome(state->app, canvas.dc, client.right,
                           state->player_id == 0 ? L"Add player"
                                                 : L"Edit player",
                           state->player_id == 0
                               ? L"Add a player to a club squad and set a starting rating."
                               : L"Update squad details or replay the rating baseline.");
        dialog_draw_label(state->app, canvas.dc, state->label_first.left,
                          state->label_first.top, L"First name");
        dialog_draw_label(state->app, canvas.dc, state->label_last.left,
                          state->label_last.top, L"Last name");
        dialog_draw_label(state->app, canvas.dc, state->label_team.left,
                          state->label_team.top, L"Team");
        dialog_draw_label(state->app, canvas.dc, state->label_rating.left,
                          state->label_rating.top, L"Starting rating");

        ui_draw_text(canvas.dc, state->app->ui.font_small, UI_COLOR_FAINT,
                     state->rating_note,
                     L"This baseline is replayed through all match history.",
                     DT_LEFT | DT_TOP | DT_SINGLELINE);
        const Player *player = db_find_player(&state->app->db,
                                              state->player_id);
        if (player != NULL) {
            wchar_t hint[128];
            ui_format_wide(hint, sizeof(hint) / sizeof(hint[0]),
                           L"Current rating: %.1f after %u matches",
                           player->current_rating, player->matches_played);
            ui_draw_text(canvas.dc, state->app->ui.font_small,
                         ui_rating_color(player->current_rating),
                         state->rating_hint, hint,
                         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        dialog_draw_error(state->app, canvas.dc, state->error_rect,
                          state->error);
        ui_canvas_end(&canvas);
    }
    EndPaint(hwnd, &paint);
    return 0;
}

static void player_dialog_layout(PlayerDialogState *state, int width,
                                 int height)
{
    UiTheme *theme = &state->app->ui;
    int margin = ui_scale(theme, 28);
    int gap = ui_scale(theme, 14);
    int content_width = width - margin * 2;
    int column_width = (content_width - gap) / 2;
    int label_height = ui_scale(theme, 18);
    int field_height = ui_scale(theme, 32);

    int y = ui_scale(theme, 96);
    state->label_first = (RECT){ margin, y, margin + column_width,
                                 y + label_height };
    state->label_last = (RECT){ margin + column_width + gap, y,
                                width - margin, y + label_height };
    int field_y = y + label_height + ui_scale(theme, 4);
    MoveWindow(state->first_name, margin, field_y,
               column_width, field_height, TRUE);
    MoveWindow(state->last_name, margin + column_width + gap, field_y,
               column_width, field_height, TRUE);

    y = field_y + field_height + ui_scale(theme, 16);
    state->label_team = (RECT){ margin, y, width - margin,
                                y + label_height };
    field_y = y + label_height + ui_scale(theme, 4);
    MoveWindow(state->team, margin, field_y,
               content_width, field_height, TRUE);

    y = field_y + field_height + ui_scale(theme, 16);
    state->label_rating = (RECT){ margin, y, margin + column_width,
                                  y + label_height };
    field_y = y + label_height + ui_scale(theme, 4);
    int rating_width = ui_scale(theme, 130);
    MoveWindow(state->rating, margin, field_y, rating_width, field_height, TRUE);
    state->rating_hint = (RECT){ margin + rating_width + ui_scale(theme, 14),
                                 field_y, width - margin,
                                 field_y + field_height };
    state->rating_note = (RECT){ margin, field_y + field_height +
                                      ui_scale(theme, 4),
                                 width - margin,
                                 field_y + field_height +
                                      ui_scale(theme, 22) };

    int active_y = height - ui_scale(theme, 124);
    if (active_y < state->rating_note.bottom + ui_scale(theme, 6)) {
        active_y = state->rating_note.bottom + ui_scale(theme, 6);
    }
    MoveWindow(state->active, margin, active_y,
               ui_scale(theme, 280), ui_scale(theme, 24), TRUE);

    int button_height = ui_scale(theme, 36);
    int button_y = height - ui_scale(theme, 56);
    int save_width = ui_scale(theme, 116);
    int cancel_width = ui_scale(theme, 92);
    int right = width - margin;
    MoveWindow(state->save, right - save_width, button_y,
               save_width, button_height, TRUE);
    MoveWindow(state->cancel,
               right - save_width - ui_scale(theme, 10) - cancel_width,
               button_y, cancel_width, button_height, TRUE);
    state->error_rect = (RECT){ margin, button_y - ui_scale(theme, 34),
                               right - save_width - ui_scale(theme, 20),
                               button_y - ui_scale(theme, 4) };
}

static void player_dialog_save(PlayerDialogState *state)
{
    char first[256];
    char last[256];
    char rating_text[64];
    ui_edit_get_utf8(state->first_name, first, sizeof(first));
    ui_edit_get_utf8(state->last_name, last, sizeof(last));
    ui_edit_get_utf8(state->rating, rating_text, sizeof(rating_text));

    double rating = 0.0;
    if (!util_parse_finite_double(rating_text, &rating)) {
        ui_format_wide(state->error,
                       sizeof(state->error) / sizeof(state->error[0]),
                       L"Starting rating must be a finite number.");
        InvalidateRect(state->hwnd, NULL, FALSE);
        return;
    }
    uint64_t team_id = ui_combo_selected_data(state->team);
    if (team_id == 0) {
        ui_format_wide(state->error,
                       sizeof(state->error) / sizeof(state->error[0]),
                       L"Select a team.");
        InvalidateRect(state->hwnd, NULL, FALSE);
        return;
    }
    bool active = ui_checkbox_get_checked(state->active);

    if (state->player_id == 0) {
        size_t duplicate_count = 0;
        const Player *duplicate = db_find_player_by_name(
            &state->app->db, first, last, &duplicate_count);
        if (duplicate != NULL) {
            PlayerDuplicateChoice choice = player_duplicate_prompt(
                state->app, state->hwnd, duplicate, team_id,
                duplicate_count);
            if (choice == PLAYER_DUPLICATE_CANCEL) {
                return;
            }
            if (choice == PLAYER_DUPLICATE_MOVE) {
                uint64_t duplicate_id = duplicate->id;
                char duplicate_first[PERSON_NAME_MAX + 1];
                char duplicate_last[PERSON_NAME_MAX + 1];
                util_str_copy(duplicate_first, sizeof(duplicate_first),
                              duplicate->first_name);
                util_str_copy(duplicate_last, sizeof(duplicate_last),
                              duplicate->last_name);
                double duplicate_rating = duplicate->base_rating;
                bool duplicate_active = duplicate->active;

                Database move_backup;
                if (!app_tx_begin(state->app, &move_backup)) {
                    return;
                }
                char move_error[512];
                if (!db_update_player(&state->app->db, duplicate_id,
                                      team_id, duplicate_first,
                                      duplicate_last, duplicate_rating,
                                      duplicate_active, move_error,
                                      sizeof(move_error))) {
                    app_tx_abort(state->app, &move_backup);
                    ui_utf8_to_wide(move_error, state->error,
                                    sizeof(state->error) /
                                        sizeof(state->error[0]));
                    InvalidateRect(state->hwnd, NULL, FALSE);
                    return;
                }
                if (!app_tx_commit(state->app, &move_backup)) {
                    return;
                }
                state->saved_id = duplicate_id;
                DestroyWindow(state->hwnd);
                return;
            }
        }
    }

    Database backup;
    if (!app_tx_begin(state->app, &backup)) {
        return;
    }
    char error[512];
    uint64_t saved = 0;
    bool ok;
    if (state->player_id == 0) {
        ok = db_add_player(&state->app->db, team_id, first, last,
                           rating, active, &saved, error, sizeof(error));
    } else {
        ok = db_update_player(&state->app->db, state->player_id, team_id,
                              first, last, rating, active,
                              error, sizeof(error));
        saved = state->player_id;
    }
    if (!ok) {
        app_tx_abort(state->app, &backup);
        ui_utf8_to_wide(error, state->error,
                        sizeof(state->error) / sizeof(state->error[0]));
        InvalidateRect(state->hwnd, NULL, FALSE);
        return;
    }
    if (!app_tx_commit(state->app, &backup)) {
        return;
    }
    state->saved_id = saved;
    DestroyWindow(state->hwnd);
}

LRESULT CALLBACK player_dialog_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam)
{
    PlayerDialogState *state =
        (PlayerDialogState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    case WM_SIZE:
        if (state != NULL && state->save != NULL) {
            player_dialog_layout(state, LOWORD(lparam), HIWORD(lparam));
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state != NULL) {
            return player_dialog_paint(state, hwnd);
        }
        break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
        return (LRESULT)dialog_control_color(message, wparam, state->app);
    case WM_COMMAND:
        if (state == NULL) {
            break;
        }
        switch (LOWORD(wparam)) {
        case PLAYER_DIALOG_SAVE_ID:
            player_dialog_save(state);
            return 0;
        case PLAYER_DIALOG_CANCEL_ID:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

uint64_t dialog_edit_player(App *app, uint64_t player_id)
{
    PlayerDialogState state;
    memset(&state, 0, sizeof(state));
    state.app = app;
    state.player_id = player_id;

    HWND hwnd = dialog_create_shell(
        app, PLAYER_DIALOG_CLASS,
        player_id == 0 ? L"Add player - PitchRate"
                       : L"Edit player - PitchRate",
        560, 470, &state);
    if (hwnd == NULL) {
        return 0;
    }
    state.hwnd = hwnd;

    UiTheme *theme = &app->ui;
    state.first_name = ui_edit_create(hwnd, PLAYER_DIALOG_FIRST_ID, L"",
                                      0, 0, 10, 10, theme->font_body, 48);
    state.last_name = ui_edit_create(hwnd, PLAYER_DIALOG_LAST_ID, L"",
                                     0, 0, 10, 10, theme->font_body, 48);
    state.team = ui_combo_create(hwnd, PLAYER_DIALOG_TEAM_ID,
                                 0, 0, 10, 10, theme->font_body);
    wchar_t default_rating[32];
    ui_format_wide(default_rating,
                   sizeof(default_rating) / sizeof(default_rating[0]),
                   L"%.2f", RATING_DEFAULT);
    state.rating = ui_edit_create(hwnd, PLAYER_DIALOG_RATING_ID,
                                  default_rating,
                                  0, 0, 10, 10, theme->font_body, 16);
    state.active = ui_checkbox_create(hwnd, PLAYER_DIALOG_ACTIVE_ID,
                                      L"Available for selection",
                                      0, 0, 10, 10, theme->font_body);
    state.save = ui_button_create(
        hwnd, PLAYER_DIALOG_SAVE_ID,
        player_id == 0 ? L"Add player" : L"Save changes",
        0, 0, 10, 10, UI_BUTTON_PRIMARY, theme->font_button);
    state.cancel = ui_button_create(
        hwnd, PLAYER_DIALOG_CANCEL_ID, L"Cancel",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    if (state.first_name == NULL || state.last_name == NULL ||
        state.team == NULL ||
        state.rating == NULL || state.active == NULL ||
        state.save == NULL || state.cancel == NULL) {
        DestroyWindow(hwnd);
        return 0;
    }

    for (size_t i = 0; i < app->db.team_count; ++i) {
        ui_combo_add_utf8(state.team, app->db.teams[i].name,
                          app->db.teams[i].id);
    }
    ui_checkbox_set_checked(state.active, true);

    const Player *player = db_find_player(&app->db, player_id);
    if (player != NULL) {
        ui_edit_set_utf8(state.first_name, player->first_name);
        ui_edit_set_utf8(state.last_name, player->last_name);
        ui_combo_select_data(state.team, player->team_id);
        wchar_t rating_text[32];
        ui_format_wide(rating_text,
                       sizeof(rating_text) / sizeof(rating_text[0]),
                       L"%.2f", player->base_rating);
        SetWindowTextW(state.rating, rating_text);
        ui_checkbox_set_checked(state.active, player->active);
    } else {
        const Player *previous = db_last_added_player(&app->db);
        if (previous != NULL &&
            db_find_team(&app->db, previous->team_id) != NULL) {
            ui_combo_select_data(state.team, previous->team_id);
        } else if (app->db.team_count > 0) {
            SendMessageW(state.team, CB_SETCURSEL, 0, 0);
        }
        if (player_id != 0) {
            ui_format_wide(state.error,
                           sizeof(state.error) / sizeof(state.error[0]),
                           L"Player not found.");
        }
    }

    RECT client;
    GetClientRect(hwnd, &client);
    player_dialog_layout(&state, client.right, client.bottom);
    InvalidateRect(hwnd, NULL, FALSE);
    ui_run_modal(hwnd, app->main_hwnd, PLAYER_DIALOG_SAVE_ID);
    return state.saved_id;
}
