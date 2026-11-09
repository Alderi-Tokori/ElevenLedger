#include "ui/dialogs.h"

#include "core/database.h"
#include "core/import.h"
#include "core/rating.h"
#include "core/util.h"
#include "ui/controls.h"
#include "ui/dialog_common.h"
#include "ui/theme.h"

#include <commctrl.h>
#include <limits.h>
#include <math.h>
#include <process.h>
#include <shellapi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <winhttp.h>

#define WM_IMPORT_FETCH_DONE (WM_APP + 61)
#define WM_IMPORT_COMMIT_CELL (WM_APP + 62)
#define IMPORT_CELL_EDITOR_PROPERTY L"PitchRate.CellEditor"

enum {
    IMPORT_URL_EDIT_ID = 2500,
    IMPORT_URL_FETCH_ID,
    IMPORT_URL_CANCEL_ID,
    IMPORT_URL_OPEN_ID,
    IMPORT_URL_PASTE_ID,
};

enum {
    IMPORT_DATE_ID = 2510,
    IMPORT_HOME_TEAM_ID,
    IMPORT_AWAY_TEAM_ID,
    IMPORT_HOME_SCORE_ID,
    IMPORT_AWAY_SCORE_ID,
    IMPORT_HOME_LIST_ID,
    IMPORT_AWAY_LIST_ID,
    IMPORT_NEUTRAL_VENUE_ID,
    IMPORT_CHANGE_ACTION_ID,
    IMPORT_SAVE_ID,
    IMPORT_CANCEL_ID,
};

enum {
    IMPORT_DUPLICATE_ADD_ID = 2600,
    IMPORT_DUPLICATE_MOVE_ID,
    IMPORT_DUPLICATE_USE_ID,
};

enum {
    IMPORT_COLUMN_FIRST = 0,
    IMPORT_COLUMN_LAST,
    IMPORT_COLUMN_MINUTES,
    IMPORT_COLUMN_PERFORMANCE,
    IMPORT_COLUMN_STATUS,
    IMPORT_COLUMN_ACTION,
    IMPORT_COLUMN_COUNT,
};

typedef struct {
    wchar_t url[IMPORT_URL_MAX + 1];
    HWND notify;
    char *body;
    size_t length;
    wchar_t error[512];
    bool ok;
} ImportFetchJob;

typedef struct {
    App *app;
    HWND hwnd;
    HWND url;
    HWND fetch;
    HWND cancel;
    HWND open_browser;
    HWND paste;
    HANDLE thread;
    ImportFetchJob *job;
    bool fetching;
    bool cancel_requested;
    char *html;
    size_t html_length;
    wchar_t status[256];
    wchar_t error[512];
    RECT label_url;
    RECT hint_rect;
    RECT status_rect;
    RECT error_rect;
} ImportUrlState;

typedef enum {
    IMPORT_ROW_NEW = 0,
    IMPORT_ROW_SAME_CLUB,
    IMPORT_ROW_OTHER_CLUB,
    IMPORT_ROW_ARCHIVED,
} ImportRowStatus;

typedef struct {
    bool home;
    char first_name[PERSON_NAME_MAX + 1];
    char last_name[PERSON_NAME_MAX + 1];
    int minutes;
    double performance;
    ImportPlayerAction action;
    uint64_t existing_player_id;
    uint64_t matched_team_id;
    size_t match_count;
    bool archived;
    ImportRowStatus status;
    char matched_team_name[TEAM_NAME_MAX + 1];
    wchar_t status_text[192];
    wchar_t action_text[64];
} ImportRow;

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
    HWND neutral_venue;
    HWND change_action;
    HWND save;
    HWND cancel;
    ImportRow *rows;
    size_t row_count;
    size_t home_row_count;
    bool building;
    bool ready;
    HWND cell_edit;
    HWND cell_list;
    uint64_t cell_edit_generation;
    int cell_item;
    size_t cell_row;
    int cell_subitem;
    wchar_t error[512];
    uint64_t saved_match_id;
    RECT label_date;
    RECT label_home_team;
    RECT label_home_score;
    RECT label_away_score;
    RECT label_away_team;
    RECT neutral_rect;
    RECT legend_rect;
    RECT info_rect;
    RECT section_rect;
    RECT home_header_rect;
    RECT away_header_rect;
    RECT home_list_frame;
    RECT away_list_frame;
    RECT footer_line;
    RECT error_rect;
} ImportMatchState;

static void import_format_wide(wchar_t *buffer, size_t count,
                                  const wchar_t *format, ...)
{
    if (buffer == NULL || count == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    _vsnwprintf(buffer, count - 1, format, args);
    va_end(args);
    buffer[count - 1] = L'\0';
}

static void import_url_show_error(ImportUrlState *state,
                                  const wchar_t *message)
{
    import_format_wide(state->error,
                          sizeof(state->error) / sizeof(state->error[0]),
                          L"%ls", message);
    InvalidateRect(state->hwnd, NULL, FALSE);
}

static void import_url_set_status(ImportUrlState *state,
                                  const wchar_t *status)
{
    import_format_wide(state->status,
                          sizeof(state->status) / sizeof(state->status[0]),
                          L"%ls", status != NULL ? status : L"");
    InvalidateRect(state->hwnd, NULL, FALSE);
}

static bool import_http_get(const wchar_t *url, char **out_body,
                            size_t *out_length, wchar_t *error,
                            size_t error_count)
{    *out_body = NULL;
    *out_length = 0;

    URL_COMPONENTS parts;
    memset(&parts, 0, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.dwSchemeLength = (DWORD)-1;
    parts.dwHostNameLength = (DWORD)-1;
    parts.dwUrlPathLength = (DWORD)-1;
    parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url, 0, 0, &parts)) {
        import_format_wide(error, error_count,
                              L"The URL could not be parsed (Windows error %lu).",
                              (unsigned long)GetLastError());
        return false;
    }
    if (parts.lpszHostName == NULL || parts.dwHostNameLength == 0 ||
        parts.dwHostNameLength >= 256 || parts.lpszUrlPath == NULL ||
        parts.dwUrlPathLength == 0) {
        import_format_wide(error, error_count,
                              L"The URL is missing its host or match path.");
        return false;
    }

    wchar_t host[256];
    wcsncpy(host, parts.lpszHostName, parts.dwHostNameLength);
    host[parts.dwHostNameLength] = L'\0';
    size_t path_length = (size_t)parts.dwUrlPathLength +
                         (size_t)parts.dwExtraInfoLength;
    wchar_t *path = malloc((path_length + 1) * sizeof(*path));
    if (path == NULL) {
        import_format_wide(error, error_count,
                              L"Not enough memory to prepare the request.");
        return false;
    }
    wcsncpy(path, parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo != NULL && parts.dwExtraInfoLength > 0) {
        wcsncpy(path + parts.dwUrlPathLength, parts.lpszExtraInfo,
                parts.dwExtraInfoLength);
    }
    path[path_length] = L'\0';

    HINTERNET session = NULL;
    HINTERNET connection = NULL;
    HINTERNET request = NULL;
    char *body = NULL;
    size_t length = 0;
    size_t capacity = 0;
    bool ok = false;

    session = WinHttpOpen(
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
        L"AppleWebKit/537.36 (KHTML, like Gecko) "
        L"Chrome/131.0.0.0 Safari/537.36",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) {
        import_format_wide(error, error_count,
                              L"Could not start an HTTP session (Windows "
                              L"error %lu).", (unsigned long)GetLastError());
        goto cleanup;
    }
    WinHttpSetTimeouts(session, 6000, 6000, 12000, 15000);

    connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (connection == NULL) {
        import_format_wide(error, error_count,
                              L"Could not reach %ls (Windows error %lu).",
                              host, (unsigned long)GetLastError());
        goto cleanup;
    }
    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS
                      ? WINHTTP_FLAG_SECURE
                      : 0;
    request = WinHttpOpenRequest(connection, L"GET", path, NULL,
                                 WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (request == NULL) {
        import_format_wide(error, error_count,
                              L"Could not create the HTTP request (Windows "
                              L"error %lu).", (unsigned long)GetLastError());
        goto cleanup;
    }
    DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
    (void)WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION,
                           &decompression, sizeof(decompression));

    static const wchar_t headers[] =
        L"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,"
        L"image/avif,image/webp,*/*;q=0.8\r\n"
        L"Accept-Language: en-US,en;q=0.9,fr-FR;q=0.8,fr;q=0.7\r\n"
        L"Cache-Control: no-cache\r\n"
        L"Pragma: no-cache\r\n"
        L"Upgrade-Insecure-Requests: 1\r\n"
        L"Referer: https://www.whoscored.com/\r\n";
    if (!WinHttpSendRequest(request, headers, (DWORD)-1L,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        import_format_wide(error, error_count,
                              L"Could not send the request (Windows error "
                              L"%lu).", (unsigned long)GetLastError());
        goto cleanup;
    }
    if (!WinHttpReceiveResponse(request, NULL)) {
        import_format_wide(error, error_count,
                              L"WhoScored did not respond (Windows error "
                              L"%lu).", (unsigned long)GetLastError());
        goto cleanup;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_STATUS_CODE |
                                 WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status,
                             &status_size, WINHTTP_NO_HEADER_INDEX)) {
        import_format_wide(error, error_count,
                              L"Could not read the HTTP status (Windows "
                              L"error %lu).", (unsigned long)GetLastError());
        goto cleanup;
    }
    if (status != 200) {
        if (status == 403) {
            import_format_wide(
                error, error_count,
                L"WhoScored refused the request (HTTP 403). Choose Open in "
                L"browser, complete the check there, press Ctrl+U, copy all, "
                L"then choose Paste page source.");
        } else if (status == 404) {
            import_format_wide(error, error_count,
                                  L"WhoScored could not find that match "
                                  L"(HTTP 404). Check the URL.");
        } else if (status == 429) {
            import_format_wide(error, error_count,
                                  L"WhoScored is rate-limiting requests "
                                  L"(HTTP 429). Wait a moment and try again.");
        } else {
            import_format_wide(error, error_count,
                                  L"WhoScored returned HTTP %lu.",
                                  (unsigned long)status);
        }
        goto cleanup;
    }

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            import_format_wide(error, error_count,
                                  L"Could not read the page (Windows error "
                                  L"%lu).", (unsigned long)GetLastError());
            goto cleanup;
        }
        if (available == 0) {
            break;
        }
        if ((size_t)available > IMPORT_HTML_MAX - length) {
            import_format_wide(error, error_count,
                                  L"The match page is too large to import.");
            goto cleanup;
        }
        size_t needed = length + (size_t)available + 1;
        if (needed > capacity) {
            size_t next = capacity == 0 ? 65536 : capacity;
            while (next < needed) {
                next *= 2;
            }
            if (next > (size_t)IMPORT_HTML_MAX + 1) {
                next = (size_t)IMPORT_HTML_MAX + 1;
            }
            char *grown = realloc(body, next);
            if (grown == NULL) {
                import_format_wide(
                    error, error_count,
                    L"Not enough memory to download the match page.");
                goto cleanup;
            }
            body = grown;
            capacity = next;
        }
        DWORD read = 0;
        if (!WinHttpReadData(request, body + length, available, &read)) {
            import_format_wide(error, error_count,
                                  L"Could not finish reading the page "
                                  L"(Windows error %lu).",
                                  (unsigned long)GetLastError());
            goto cleanup;
        }
        if (read == 0) {
            break;
        }
        length += read;
    }

    if (length == 0) {
        import_format_wide(error, error_count,
                              L"WhoScored returned an empty page.");
        goto cleanup;
    }
    body[length] = '\0';
    if (!util_utf8_valid_n(body, length)) {
        import_format_wide(error, error_count,
                              L"The WhoScored page is not valid UTF-8 text.");
        goto cleanup;
    }
    *out_body = body;
    *out_length = length;
    body = NULL;
    ok = true;

cleanup:
    free(body);
    free(path);
    if (request != NULL) {
        WinHttpCloseHandle(request);
    }
    if (connection != NULL) {
        WinHttpCloseHandle(connection);
    }
    if (session != NULL) {
        WinHttpCloseHandle(session);
    }
    return ok;
}

static unsigned __stdcall import_fetch_thread(void *parameter)
{
    ImportFetchJob *job = (ImportFetchJob *)parameter;
    job->ok = import_http_get(job->url, &job->body, &job->length,
                              job->error,
                              sizeof(job->error) / sizeof(job->error[0]));
    PostMessageW(job->notify, WM_IMPORT_FETCH_DONE, 0, 0);
    return 0;
}

static void import_url_layout(ImportUrlState *state, int width, int height)
{
    UiTheme *theme = &state->app->ui;
    int margin = ui_scale(theme, 28);
    int label_height = ui_scale(theme, 18);
    int field_height = ui_scale(theme, 34);
    int y = ui_scale(theme, 96);
    state->label_url = (RECT){ margin, y, width - margin,
                               y + label_height };
    MoveWindow(state->url, margin, y + label_height + ui_scale(theme, 5),
               width - margin * 2, field_height, TRUE);
    y += label_height + field_height + ui_scale(theme, 12);
    state->hint_rect = (RECT){ margin, y, width - margin,
                               y + ui_scale(theme, 36) };
    y += ui_scale(theme, 44);
    state->status_rect = (RECT){ margin, y, width - margin,
                                 y + ui_scale(theme, 24) };
    int error_top = y + ui_scale(theme, 26);
    int button_height = ui_scale(theme, 36);
    int button_y = height - ui_scale(theme, 58);
    int error_bottom = error_top + ui_scale(theme, 50);
    if (error_bottom > button_y - ui_scale(theme, 6)) {
        error_bottom = button_y - ui_scale(theme, 6);
    }
    if (error_bottom < error_top) {
        error_bottom = error_top;
    }
    state->error_rect = (RECT){ margin, error_top, width - margin,
                                error_bottom };
    int fetch_width = ui_scale(theme, 168);
    int cancel_width = ui_scale(theme, 92);
    int paste_width = ui_scale(theme, 168);
    int open_width = ui_scale(theme, 132);
    int right = width - margin;
    int fetch_x = right - fetch_width;
    int cancel_x = fetch_x - ui_scale(theme, 10) - cancel_width;
    int paste_x = cancel_x - ui_scale(theme, 10) - paste_width;
    int open_x = paste_x - ui_scale(theme, 10) - open_width;
    MoveWindow(state->fetch, fetch_x, button_y,
               fetch_width, button_height, TRUE);
    MoveWindow(state->cancel, cancel_x, button_y, cancel_width,
               button_height, TRUE);
    MoveWindow(state->paste, paste_x, button_y, paste_width,
               button_height, TRUE);
    MoveWindow(state->open_browser, open_x, button_y, open_width,
               button_height, TRUE);
}

static LRESULT import_url_paint(ImportUrlState *state, HWND hwnd)
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
            L"Import a match from WhoScored",
            L"Download one match page, review it, then confirm the import.");
        dialog_draw_label(state->app, canvas.dc, state->label_url.left,
                          state->label_url.top, L"WhoScored match URL");
        ui_draw_text(
            canvas.dc, state->app->ui.font_small, UI_COLOR_FAINT,
            state->hint_rect,
            L"Paste a WhoScored match URL, or use Open in browser when "
            L"WhoScored blocks the download.\n"
            L"After the browser shows the page, press Ctrl+U, copy all, then "
            L"choose Paste page source.",
            DT_LEFT | DT_TOP | DT_WORDBREAK);
        ui_draw_text(canvas.dc, state->app->ui.font_small,
                     state->fetching ? UI_COLOR_ACCENT : UI_COLOR_MUTED,
                     state->status_rect, state->status,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                         DT_END_ELLIPSIS);
        ui_draw_text(canvas.dc, state->app->ui.font_small, UI_COLOR_DANGER,
                     state->error_rect, state->error,
                     DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
        ui_canvas_end(&canvas);
    }
    EndPaint(hwnd, &paint);
    return 0;
}

static void import_url_finish_fetch(ImportUrlState *state)
{
    if (state->thread == NULL) {
        return;
    }
    WaitForSingleObject(state->thread, INFINITE);
    CloseHandle(state->thread);
    state->thread = NULL;
    ImportFetchJob *job = state->job;
    state->job = NULL;
    state->fetching = false;
    if (job == NULL) {
        return;
    }
    if (state->cancel_requested) {
        free(job->body);
        free(job);
        dialog_close(state->hwnd);
        return;
    }
    if (!job->ok) {
        import_format_wide(
            state->error,
            sizeof(state->error) / sizeof(state->error[0]),
            L"%ls", job->error[0] != L'\0' ? job->error
                                            : L"The download failed.");
        free(job->body);
        free(job);
        EnableWindow(state->url, TRUE);
        EnableWindow(state->fetch, TRUE);
        EnableWindow(state->open_browser, TRUE);
        EnableWindow(state->paste, TRUE);
        EnableWindow(state->cancel, TRUE);
        import_url_set_status(state, L"Check the URL and try again.");
        SetFocus(state->fetch);
        return;
    }
    state->html = job->body;
    state->html_length = job->length;
    job->body = NULL;
    free(job);
    dialog_close(state->hwnd);
}

static void import_trim_wide_in_place(wchar_t *text)
{
    if (text == NULL) {
        return;
    }
    wchar_t *start = text;
    while (*start == L' ' || *start == L'\t' || *start == L'\r' ||
           *start == L'\n') {
        ++start;
    }
    if (start != text) {
        memmove(text, start, (wcslen(start) + 1) * sizeof(*text));
    }
    size_t length = wcslen(text);
    while (length > 0) {
        wchar_t value = text[length - 1];
        if (value != L' ' && value != L'\t' && value != L'\r' &&
            value != L'\n') {
            break;
        }
        text[--length] = L'\0';
    }
}

static void import_url_start_fetch(ImportUrlState *state)
{
    if (state->fetching) {
        return;
    }
    wchar_t wide_url[IMPORT_URL_MAX + 1];
    ui_edit_get_wide(state->url, wide_url,
                     sizeof(wide_url) / sizeof(wide_url[0]));
    import_trim_wide_in_place(wide_url);
    SetWindowTextW(state->url, wide_url);
    char utf8_url[IMPORT_URL_MAX * 4 + 1];
    ui_wide_to_utf8(wide_url, utf8_url, sizeof(utf8_url));
    if (!import_url_valid(utf8_url)) {
        import_url_show_error(
            state,
            L"Enter a full WhoScored match URL, for example "
            L"https://fr.whoscored.com/matches/1984066/live/...");
        SetFocus(state->url);
        return;
    }

    ImportFetchJob *job = calloc(1, sizeof(*job));
    if (job == NULL) {
        import_url_show_error(state,
                              L"Not enough memory to start the download.");
        return;
    }
    wcsncpy(job->url, wide_url,
            sizeof(job->url) / sizeof(job->url[0]) - 1);
    job->url[sizeof(job->url) / sizeof(job->url[0]) - 1] = L'\0';
    job->notify = state->hwnd;

    state->error[0] = L'\0';
    state->status[0] = L'\0';
    state->job = job;
    state->fetching = true;
    state->cancel_requested = false;
    EnableWindow(state->url, FALSE);
    EnableWindow(state->fetch, FALSE);
    EnableWindow(state->open_browser, FALSE);
    EnableWindow(state->paste, FALSE);
    EnableWindow(state->cancel, TRUE);
    import_url_set_status(state, L"Downloading the match page...");
    state->thread = (HANDLE)_beginthreadex(
        NULL, 0, import_fetch_thread, job, 0, NULL);
    if (state->thread == NULL) {
        state->fetching = false;
        state->job = NULL;
        free(job);
        EnableWindow(state->url, TRUE);
        EnableWindow(state->fetch, TRUE);
        EnableWindow(state->open_browser, TRUE);
        EnableWindow(state->paste, TRUE);
        import_url_show_error(state,
                              L"Could not start the download thread.");
        import_url_set_status(state, L"Try again.");
        return;
    }
    SetFocus(state->cancel);
}

static void import_url_cancel(ImportUrlState *state)
{
    if (state->fetching) {
        state->cancel_requested = true;
        import_url_set_status(state, L"Cancelling the download...");
        EnableWindow(state->cancel, FALSE);
        return;
    }
    dialog_close(state->hwnd);
}

static bool import_clipboard_read(HWND owner, char **out_text,
                                  size_t *out_length, wchar_t *error,
                                  size_t error_count)
{
    *out_text = NULL;
    *out_length = 0;
    if (!OpenClipboard(owner)) {
        import_format_wide(error, error_count,
                           L"Could not open the clipboard. Close other "
                           L"clipboard tools and try again.");
        return false;
    }

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle == NULL) {
        CloseClipboard();
        import_format_wide(
            error, error_count,
            L"The clipboard does not contain text. Open the WhoScored page "
            L"in your browser, press Ctrl+U, select all and copy.");
        return false;
    }
    SIZE_T byte_count = GlobalSize(handle);
    const wchar_t *wide = (const wchar_t *)GlobalLock(handle);
    if (wide == NULL || byte_count < sizeof(wchar_t)) {
        if (wide != NULL) {
            GlobalUnlock(handle);
        }
        CloseClipboard();
        import_format_wide(error, error_count,
                           L"Could not read the clipboard text.");
        return false;
    }

    size_t wide_count = (size_t)byte_count / sizeof(wchar_t);
    size_t text_length = 0;
    while (text_length < wide_count && wide[text_length] != L'\0') {
        ++text_length;
    }
    if (text_length == 0) {
        GlobalUnlock(handle);
        CloseClipboard();
        import_format_wide(error, error_count,
                           L"The clipboard text is empty.");
        return false;
    }
    if (text_length > (size_t)IMPORT_HTML_MAX ||
        text_length > (size_t)INT_MAX / 4) {
        GlobalUnlock(handle);
        CloseClipboard();
        import_format_wide(error, error_count,
                           L"The clipboard text is too large to import.");
        return false;
    }

    int needed = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide, (int)text_length, NULL, 0,
        NULL, NULL);
    if (needed <= 0 || (size_t)needed > (size_t)IMPORT_HTML_MAX) {
        GlobalUnlock(handle);
        CloseClipboard();
        import_format_wide(
            error, error_count,
            needed <= 0 ? L"The clipboard text is not valid Unicode."
                        : L"The clipboard text is too large to import.");
        return false;
    }
    char *text = malloc((size_t)needed + 1);
    if (text == NULL) {
        GlobalUnlock(handle);
        CloseClipboard();
        import_format_wide(error, error_count,
                           L"Not enough memory to read the clipboard text.");
        return false;
    }
    int written = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide, (int)text_length, text, needed,
        NULL, NULL);
    GlobalUnlock(handle);
    CloseClipboard();
    if (written != needed) {
        free(text);
        import_format_wide(error, error_count,
                           L"Could not convert the clipboard text.");
        return false;
    }
    text[needed] = '\0';
    *out_text = text;
    *out_length = (size_t)needed;
    return true;
}

static void import_url_open_browser(ImportUrlState *state)
{
    if (state->fetching) {
        return;
    }
    wchar_t wide_url[IMPORT_URL_MAX + 1];
    ui_edit_get_wide(state->url, wide_url,
                     sizeof(wide_url) / sizeof(wide_url[0]));
    import_trim_wide_in_place(wide_url);
    SetWindowTextW(state->url, wide_url);
    char utf8_url[IMPORT_URL_MAX * 4 + 1];
    ui_wide_to_utf8(wide_url, utf8_url, sizeof(utf8_url));
    if (!import_url_valid(utf8_url)) {
        import_url_show_error(
            state,
            L"Enter a full WhoScored match URL before opening the browser.");
        SetFocus(state->url);
        return;
    }
    HINSTANCE result = ShellExecuteW(state->hwnd, L"open", wide_url, NULL,
                                     NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        import_url_show_error(
            state, L"Windows could not open the URL in the default browser.");
        return;
    }
    state->error[0] = L'\0';
    import_url_set_status(
        state,
        L"Complete the browser check, press Ctrl+U to view the source, "
        L"copy all, then choose Paste page source.");
}

static void import_url_paste_source(ImportUrlState *state)
{
    if (state->fetching) {
        return;
    }
    char *text = NULL;
    size_t length = 0;
    wchar_t error[512];
    if (!import_clipboard_read(state->hwnd, &text, &length, error,
                               sizeof(error) / sizeof(error[0]))) {
        import_url_show_error(state, error);
        return;
    }
    if (!util_utf8_valid_n(text, length)) {
        free(text);
        import_url_show_error(state,
                              L"The clipboard text is not valid UTF-8.");
        return;
    }
    if (strstr(text, "matchCentreData") == NULL) {
        free(text);
        import_url_show_error(
            state,
            L"The clipboard does not contain WhoScored page source. Open "
            L"the match in your browser, press Ctrl+U, select all and copy, "
            L"then try again.");
        return;
    }
    state->error[0] = L'\0';
    state->html = text;
    state->html_length = length;
    dialog_close(state->hwnd);
}

LRESULT CALLBACK import_url_dialog_proc(HWND hwnd, UINT message,
                                        WPARAM wparam, LPARAM lparam)
{
    ImportUrlState *state =
        (ImportUrlState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    case WM_SIZE:
        if (state != NULL && state->fetch != NULL) {
            import_url_layout(state, LOWORD(lparam), HIWORD(lparam));
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state != NULL) {
            return import_url_paint(state, hwnd);
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
        if (LOWORD(wparam) == IMPORT_URL_EDIT_ID &&
            HIWORD(wparam) == EN_CHANGE) {
            if (!state->fetching && state->error[0] != L'\0') {
                state->error[0] = L'\0';
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }
        if (LOWORD(wparam) == IMPORT_URL_OPEN_ID) {
            import_url_open_browser(state);
            return 0;
        }
        if (LOWORD(wparam) == IMPORT_URL_PASTE_ID) {
            import_url_paste_source(state);
            return 0;
        }
        if (LOWORD(wparam) == IMPORT_URL_FETCH_ID) {
            import_url_start_fetch(state);
            return 0;
        }
        if (LOWORD(wparam) == IMPORT_URL_CANCEL_ID) {
            import_url_cancel(state);
            return 0;
        }
        break;
    case WM_IMPORT_FETCH_DONE:
        if (state != NULL) {
            import_url_finish_fetch(state);
        }
        return 0;
    case WM_CLOSE:
        if (state != NULL) {
            import_url_cancel(state);
        }
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static bool import_url_dialog_run(App *app, char **out_html,
                                  size_t *out_length)
{
    *out_html = NULL;
    *out_length = 0;
    ImportUrlState state;
    memset(&state, 0, sizeof(state));
    state.app = app;
    HWND hwnd = dialog_create_shell(
        app, app->main_hwnd, PITCHRATE_IMPORT_URL_DIALOG_CLASS,
        L"Import match - PitchRate", 720, 350, &state);
    if (hwnd == NULL) {
        return false;
    }
    state.hwnd = hwnd;
    UiTheme *theme = &app->ui;
    state.url = ui_edit_create(hwnd, IMPORT_URL_EDIT_ID, L"",
                               0, 0, 10, 10, theme->font_body,
                               IMPORT_URL_MAX);
    state.open_browser = ui_button_create(
        hwnd, IMPORT_URL_OPEN_ID, L"Open in browser",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    state.paste = ui_button_create(
        hwnd, IMPORT_URL_PASTE_ID, L"Paste page source",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    state.cancel = ui_button_create(
        hwnd, IMPORT_URL_CANCEL_ID, L"Cancel",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    state.fetch = ui_button_create(
        hwnd, IMPORT_URL_FETCH_ID, L"Download match",
        0, 0, 10, 10, UI_BUTTON_PRIMARY, theme->font_button);
    if (state.url == NULL || state.fetch == NULL || state.cancel == NULL ||
        state.open_browser == NULL || state.paste == NULL) {
        dialog_close(hwnd);
        return false;
    }
    ui_edit_set_cue(state.url,
                    L"https://fr.whoscored.com/matches/1984066/live/...");
    RECT client;
    GetClientRect(hwnd, &client);
    import_url_layout(&state, client.right, client.bottom);
    InvalidateRect(hwnd, NULL, FALSE);
    ui_run_modal(hwnd, app->main_hwnd, IMPORT_URL_FETCH_ID);

    if (state.thread != NULL) {
        WaitForSingleObject(state.thread, INFINITE);
        CloseHandle(state.thread);
        state.thread = NULL;
    }
    if (state.job != NULL) {
        free(state.job->body);
        free(state.job);
        state.job = NULL;
    }
    if (state.html != NULL) {
        *out_html = state.html;
        *out_length = state.html_length;
        state.html = NULL;
        state.html_length = 0;
    }
    return *out_html != NULL;
}

static uint64_t import_team_id_from_edit(App *app, HWND edit)
{
    char name[256];
    ui_edit_get_utf8(edit, name, sizeof(name));
    const Team *team = db_find_team_by_name(&app->db, name);
    return team != NULL ? team->id : 0;
}

static uint64_t import_row_team_id(ImportMatchState *state,
                                   const ImportRow *row)
{
    return import_team_id_from_edit(state->app,
                                    row->home ? state->home_team
                                              : state->away_team);
}

static HWND import_list_for_row(const ImportMatchState *state, size_t row)
{
    return row < state->home_row_count ? state->home_list
                                       : state->away_list;
}

static int import_item_for_row(const ImportMatchState *state, size_t row)
{
    if (row < state->home_row_count) {
        return (int)row;
    }
    return (int)(row - state->home_row_count);
}

static bool import_row_from_item(ImportMatchState *state, HWND list,
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

static bool import_selected_row(ImportMatchState *state, size_t *out_row)
{
    const HWND lists[2] = { state->home_list, state->away_list };
    for (int side = 0; side < 2; ++side) {
        int item = ListView_GetNextItem(lists[side], -1, LVNI_SELECTED);
        if (item >= 0) {
            return import_row_from_item(state, lists[side], item, out_row);
        }
    }
    return false;
}

static void import_note_error(ImportMatchState *state, const char *error)
{
    ui_utf8_to_wide(error, state->error,
                    sizeof(state->error) / sizeof(state->error[0]));
    InvalidateRect(state->hwnd, NULL, FALSE);
}

static void import_show_error_wide(ImportMatchState *state,
                                   const wchar_t *error)
{
    import_format_wide(state->error,
                          sizeof(state->error) / sizeof(state->error[0]),
                          L"%ls", error);
    InvalidateRect(state->hwnd, NULL, FALSE);
}

static void import_row_status_text(ImportRow *row)
{
    wchar_t team_name[TEAM_NAME_MAX + 1] = L"unknown club";
    if (row->matched_team_name[0] != '\0') {
        ui_utf8_to_wide(row->matched_team_name, team_name,
                        sizeof(team_name) / sizeof(team_name[0]));
    }
    switch (row->status) {
    case IMPORT_ROW_NEW:
        import_format_wide(
            row->status_text,
            sizeof(row->status_text) / sizeof(row->status_text[0]),
            L"New player");
        break;
    case IMPORT_ROW_SAME_CLUB:
        if (row->match_count > 1) {
            import_format_wide(
                row->status_text,
                sizeof(row->status_text) / sizeof(row->status_text[0]),
                L"In this squad (%zu)", row->match_count);
        } else {
            import_format_wide(
                row->status_text,
                sizeof(row->status_text) / sizeof(row->status_text[0]),
                L"In this squad");
        }
        break;
    case IMPORT_ROW_OTHER_CLUB:
        if (row->match_count > 1) {
            import_format_wide(
                row->status_text,
                sizeof(row->status_text) / sizeof(row->status_text[0]),
                L"%ls (%zu)", team_name, row->match_count);
        } else {
            import_format_wide(
                row->status_text,
                sizeof(row->status_text) / sizeof(row->status_text[0]),
                L"%ls", team_name);
        }
        break;
    case IMPORT_ROW_ARCHIVED:
    default:
        if (row->match_count > 1) {
            import_format_wide(
                row->status_text,
                sizeof(row->status_text) / sizeof(row->status_text[0]),
                L"Archived: %ls (%zu)", team_name, row->match_count);
        } else {
            import_format_wide(
                row->status_text,
                sizeof(row->status_text) / sizeof(row->status_text[0]),
                L"Archived: %ls", team_name);
        }
        break;
    }

    if (row->action == IMPORT_PLAYER_CREATE) {
        import_format_wide(
            row->action_text,
            sizeof(row->action_text) / sizeof(row->action_text[0]),
            L"Create");
    } else if (row->status == IMPORT_ROW_OTHER_CLUB) {
        import_format_wide(
            row->action_text,
            sizeof(row->action_text) / sizeof(row->action_text[0]),
            L"Move");
    } else {
        import_format_wide(
            row->action_text,
            sizeof(row->action_text) / sizeof(row->action_text[0]),
            L"Use");
    }
}

static void import_recompute_row(ImportMatchState *state, ImportRow *row)
{
    uint64_t target_team = import_row_team_id(state, row);
    ImportPlayerAction previous_action = row->action;
    uint64_t previous_existing = row->existing_player_id;

    size_t count = 0;
    const Player *existing = db_find_player_by_name(
        &state->app->db, row->first_name, row->last_name, &count);
    row->match_count = count;
    row->existing_player_id = 0;
    row->matched_team_id = 0;
    row->matched_team_name[0] = '\0';
    row->archived = false;

    if (existing == NULL) {
        row->status = IMPORT_ROW_NEW;
        row->action = IMPORT_PLAYER_CREATE;
        import_row_status_text(row);
        return;
    }

    row->existing_player_id = existing->id;
    row->matched_team_id = existing->team_id;
    const Team *team = db_find_team(&state->app->db, existing->team_id);
    if (team != NULL) {
        util_str_copy(row->matched_team_name,
                      sizeof(row->matched_team_name), team->name);
    }
    row->archived = !existing->active;

    if (row->archived) {
        row->status = IMPORT_ROW_ARCHIVED;
        row->action = IMPORT_PLAYER_CREATE;
    } else if (target_team != 0 && existing->team_id == target_team) {
        row->status = IMPORT_ROW_SAME_CLUB;
        if (previous_action == IMPORT_PLAYER_CREATE &&
            previous_existing == existing->id) {
            row->action = IMPORT_PLAYER_CREATE;
        } else {
            row->action = IMPORT_PLAYER_USE_EXISTING;
        }
    } else {
        row->status = IMPORT_ROW_OTHER_CLUB;
        if (previous_action == IMPORT_PLAYER_USE_EXISTING &&
            previous_existing == existing->id) {
            row->action = IMPORT_PLAYER_USE_EXISTING;
        } else {
            row->action = IMPORT_PLAYER_CREATE;
        }
    }
    import_row_status_text(row);
}

static void import_set_row_text(HWND list, int item, int subitem,
                                const wchar_t *text)
{
    LVITEMW value;
    memset(&value, 0, sizeof(value));
    value.iSubItem = subitem;
    value.pszText = (LPWSTR)text;
    SendMessageW(list, LVM_SETITEMTEXTW, (WPARAM)item,
                 (LPARAM)&value);
}

static void import_set_row_utf8(HWND list, int item, int subitem,
                                const char *text)
{
    wchar_t wide[512];
    if (!ui_utf8_to_wide(text, wide, sizeof(wide) / sizeof(wide[0]))) {
        wide[0] = L'\0';
    }
    import_set_row_text(list, item, subitem, wide);
}

static void import_row_update_list(ImportMatchState *state, size_t row_index)
{
    ImportRow *row = &state->rows[row_index];
    HWND list = import_list_for_row(state, row_index);
    int item = import_item_for_row(state, row_index);
    char minutes[16];
    char performance[16];
    snprintf(minutes, sizeof(minutes), "%d", row->minutes);
    snprintf(performance, sizeof(performance), "%.2f", row->performance);
    import_set_row_utf8(list, item, IMPORT_COLUMN_FIRST, row->first_name);
    import_set_row_utf8(list, item, IMPORT_COLUMN_LAST, row->last_name);
    import_set_row_utf8(list, item, IMPORT_COLUMN_MINUTES, minutes);
    import_set_row_utf8(list, item, IMPORT_COLUMN_PERFORMANCE, performance);
    import_set_row_text(list, item, IMPORT_COLUMN_STATUS, row->status_text);
    import_set_row_text(list, item, IMPORT_COLUMN_ACTION, row->action_text);
    InvalidateRect(list, NULL, FALSE);
}

static void import_recompute_side(ImportMatchState *state, bool home,
                                  bool update_lists)
{
    for (size_t i = 0; i < state->row_count; ++i) {
        ImportRow *row = &state->rows[i];
        if (row->home != home) {
            continue;
        }
        import_recompute_row(state, row);
        if (update_lists) {
            import_row_update_list(state, i);
        }
    }
}

static void import_recompute_all(ImportMatchState *state, bool update_lists)
{
    for (size_t i = 0; i < state->row_count; ++i) {
        import_recompute_row(state, &state->rows[i]);
        if (update_lists) {
            import_row_update_list(state, i);
        }
    }
}

static void import_update_action_button(ImportMatchState *state)
{
    size_t row_index = 0;
    bool enabled = false;
    if (import_selected_row(state, &row_index)) {
        ImportRow *row = &state->rows[row_index];
        enabled = (row->status == IMPORT_ROW_SAME_CLUB ||
                   row->status == IMPORT_ROW_OTHER_CLUB) &&
                  !row->archived;
    }
    EnableWindow(state->change_action, enabled);
}

static COLORREF import_row_background(ImportRowStatus status)
{
    switch (status) {
    case IMPORT_ROW_NEW:
        return UI_COLOR_BLUE_SOFT;
    case IMPORT_ROW_SAME_CLUB:
        return UI_COLOR_ACCENT_SOFT;
    case IMPORT_ROW_OTHER_CLUB:
        return UI_COLOR_AMBER_SOFT;
    case IMPORT_ROW_ARCHIVED:
    default:
        return UI_COLOR_DANGER_SOFT;
    }
}

static COLORREF import_row_foreground(ImportRowStatus status)
{
    switch (status) {
    case IMPORT_ROW_NEW:
        return UI_COLOR_BLUE;
    case IMPORT_ROW_SAME_CLUB:
        return UI_COLOR_ACCENT;
    case IMPORT_ROW_OTHER_CLUB:
        return UI_COLOR_AMBER;
    case IMPORT_ROW_ARCHIVED:
    default:
        return UI_COLOR_DANGER;
    }
}

static LRESULT import_custom_draw(ImportMatchState *state,
                                  NMLVCUSTOMDRAW *custom_draw)
{
    if (custom_draw == NULL) {
        return CDRF_DODEFAULT;
    }
    HWND list = custom_draw->nmcd.hdr.hwndFrom;
    switch (custom_draw->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: {
        LRESULT data = custom_draw->nmcd.lItemlParam;
        if (data == (LRESULT)-1 || (size_t)data >= state->row_count) {
            return CDRF_DODEFAULT;
        }
        ImportRow *row = &state->rows[(size_t)data];
        bool selected =
            (ListView_GetItemState(list, (int)custom_draw->nmcd.dwItemSpec,
                                   LVIS_SELECTED) &
             LVIS_SELECTED) != 0;
        custom_draw->clrText = UI_COLOR_TEXT;
        custom_draw->clrTextBk = selected
            ? UI_COLOR_SELECTION
            : import_row_background(row->status);
        return CDRF_NOTIFYSUBITEMDRAW;
    }
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        LRESULT data = custom_draw->nmcd.lItemlParam;
        if (data == (LRESULT)-1 || (size_t)data >= state->row_count) {
            return CDRF_DODEFAULT;
        }
        ImportRow *row = &state->rows[(size_t)data];
        bool selected =
            (ListView_GetItemState(list, (int)custom_draw->nmcd.dwItemSpec,
                                   LVIS_SELECTED) &
             LVIS_SELECTED) != 0;
        custom_draw->clrTextBk = selected
            ? UI_COLOR_SELECTION
            : import_row_background(row->status);
        if (!selected &&
            (custom_draw->iSubItem == IMPORT_COLUMN_STATUS ||
             custom_draw->iSubItem == IMPORT_COLUMN_ACTION)) {
            custom_draw->clrText = import_row_foreground(row->status);
        } else {
            custom_draw->clrText = UI_COLOR_TEXT;
        }
        return CDRF_NEWFONT;
    }
    default:
        return CDRF_DODEFAULT;
    }
}

static void import_begin_cell_edit(ImportMatchState *state, HWND list,
                                   int item, int subitem);

static void import_cancel_cell_edit(ImportMatchState *state);
static bool import_commit_cell_edit(ImportMatchState *state, bool accept);

static LRESULT CALLBACK import_cell_edit_subclass(HWND hwnd, UINT message,
                                                  WPARAM wparam,
                                                  LPARAM lparam,
                                                  UINT_PTR subclass_id,
                                                  DWORD_PTR reference_data)
{
    (void)subclass_id;
    ImportMatchState *state = (ImportMatchState *)reference_data;
    switch (message) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS;
    case WM_KEYDOWN:
        if (wparam == VK_RETURN) {
            PostMessageW(state->hwnd, WM_IMPORT_COMMIT_CELL, TRUE,
                         (LPARAM)state->cell_edit_generation);
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            PostMessageW(state->hwnd, WM_IMPORT_COMMIT_CELL, FALSE,
                         (LPARAM)state->cell_edit_generation);
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        PostMessageW(state->hwnd, WM_IMPORT_COMMIT_CELL, TRUE,
                     (LPARAM)state->cell_edit_generation);
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, import_cell_edit_subclass, subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

static void import_begin_cell_edit(ImportMatchState *state, HWND list,
                                   int item, int subitem)
{
    size_t row_index = 0;
    if (!import_row_from_item(state, list, item, &row_index) ||
        subitem < IMPORT_COLUMN_FIRST || subitem > IMPORT_COLUMN_PERFORMANCE) {
        return;
    }
    if (state->cell_edit != NULL &&
        !import_commit_cell_edit(state, true)) {
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
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS |
                  ES_AUTOHSCROLL;
    if (subitem == IMPORT_COLUMN_MINUTES ||
        subitem == IMPORT_COLUMN_PERFORMANCE) {
        style |= ES_CENTER;
    }
    HWND edit = CreateWindowExW(
        0, L"EDIT", L"", style, cell.left, cell.top,
        cell.right - cell.left, cell.bottom - cell.top, state->hwnd, NULL,
        state->app->instance, NULL);
    if (edit == NULL) {
        return;
    }
    SetWindowPos(edit, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(edit, WM_SETFONT, (WPARAM)state->app->ui.font_small, TRUE);
    SetPropW(edit, IMPORT_CELL_EDITOR_PROPERTY, (HANDLE)1);
    ImportRow *row = &state->rows[row_index];
    wchar_t text[128];
    if (subitem == IMPORT_COLUMN_FIRST) {
        ui_utf8_to_wide(row->first_name, text,
                        sizeof(text) / sizeof(text[0]));
    } else if (subitem == IMPORT_COLUMN_LAST) {
        ui_utf8_to_wide(row->last_name, text,
                        sizeof(text) / sizeof(text[0]));
    } else if (subitem == IMPORT_COLUMN_MINUTES) {
        ui_format_wide(text, sizeof(text) / sizeof(text[0]), L"%d",
                       row->minutes);
    } else {
        ui_format_wide(text, sizeof(text) / sizeof(text[0]), L"%.2f",
                       row->performance);
    }
    SetWindowTextW(edit, text);
    SetWindowSubclass(edit, import_cell_edit_subclass, 1, (DWORD_PTR)state);
    state->cell_edit = edit;
    state->cell_list = list;
    state->cell_item = item;
    state->cell_row = row_index;
    state->cell_subitem = subitem;
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);
}

static bool import_commit_cell_edit(ImportMatchState *state, bool accept)
{
    if (state->cell_edit == NULL) {
        return true;
    }
    HWND edit = state->cell_edit;
    HWND list = state->cell_list;
    int item = state->cell_item;
    size_t row_index = state->cell_row;
    int subitem = state->cell_subitem;
    wchar_t text[256];
    GetWindowTextW(edit, text, (int)(sizeof(text) / sizeof(text[0])));

    if (accept && list != NULL && row_index < state->row_count) {
        char utf8[512];
        ui_wide_to_utf8(text, utf8, sizeof(utf8));
        ImportRow *row = &state->rows[row_index];
        if (subitem == IMPORT_COLUMN_FIRST ||
            subitem == IMPORT_COLUMN_LAST) {
            char trimmed[512];
            util_str_copy(trimmed, sizeof(trimmed), utf8);
            char *clean = util_trim(trimmed);
            if (!util_text_valid(clean, PERSON_NAME_MAX, true)) {
                import_show_error_wide(
                    state,
                    L"Player names must be valid text of at most 47 bytes.");
                SetFocus(edit);
                SendMessageW(edit, EM_SETSEL, 0, -1);
                return false;
            }
            if (subitem == IMPORT_COLUMN_FIRST) {
                util_str_copy(row->first_name, sizeof(row->first_name),
                              clean);
            } else {
                util_str_copy(row->last_name, sizeof(row->last_name), clean);
            }
            if (row->first_name[0] == '\0' && row->last_name[0] == '\0') {
                import_show_error_wide(state, L"Enter a player name.");
                SetFocus(edit);
                SendMessageW(edit, EM_SETSEL, 0, -1);
                return false;
            }
            state->error[0] = L'\0';
            import_recompute_row(state, row);
            import_row_update_list(state, row_index);
        } else if (subitem == IMPORT_COLUMN_MINUTES) {
            int minutes = 0;
            if (!util_parse_int(utf8, 1, 120, &minutes)) {
                import_show_error_wide(
                    state,
                    L"Minutes played must be a whole number from 1 to 120.");
                SetFocus(edit);
                SendMessageW(edit, EM_SETSEL, 0, -1);
                return false;
            }
            state->error[0] = L'\0';
            row->minutes = minutes;
            import_row_update_list(state, row_index);
        } else if (subitem == IMPORT_COLUMN_PERFORMANCE) {
            double performance = 0.0;
            if (!util_parse_double(utf8, 1.0, 10.0, &performance)) {
                import_show_error_wide(
                    state,
                    L"Performance must be between 1.0 and 10.0.");
                SetFocus(edit);
                SendMessageW(edit, EM_SETSEL, 0, -1);
                return false;
            }
            state->error[0] = L'\0';
            row->performance = round(performance * 100.0) / 100.0;
            import_row_update_list(state, row_index);
        }
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
    import_update_action_button(state);
    InvalidateRect(state->hwnd, NULL, FALSE);
    return true;
}

static void import_cancel_cell_edit(ImportMatchState *state)
{
    if (state->cell_edit == NULL) {
        return;
    }
    HWND edit = state->cell_edit;
    state->cell_edit = NULL;
    state->cell_list = NULL;
    state->cell_row = 0;
    state->cell_item = 0;
    if (IsWindow(edit)) {
        DestroyWindow(edit);
    }
}

static void import_change_action(ImportMatchState *state)
{
    if (state->cell_edit != NULL &&
        !import_commit_cell_edit(state, true)) {
        return;
    }
    size_t row_index = 0;
    if (!import_selected_row(state, &row_index)) {
        return;
    }
    ImportRow *row = &state->rows[row_index];
    bool same_club = row->status == IMPORT_ROW_SAME_CLUB;
    bool other_club = row->status == IMPORT_ROW_OTHER_CLUB;
    if ((!same_club && !other_club) || row->archived) {
        return;
    }
    const Player *existing = db_find_player(&state->app->db,
                                            row->existing_player_id);
    if (existing == NULL) {
        import_show_error_wide(
            state, L"This player no longer exists in the database.");
        return;
    }

    wchar_t player_name[160];
    wchar_t current_team[TEAM_NAME_MAX + 1] = L"another club";
    wchar_t import_team[TEAM_NAME_MAX + 1] = L"this club";
    char first_last[PERSON_NAME_MAX * 2 + 2];
    snprintf(first_last, sizeof(first_last), "%s %s", row->first_name,
             row->last_name);
    ui_utf8_to_wide(first_last, player_name,
                    sizeof(player_name) / sizeof(player_name[0]));
    const Team *current = db_find_team(&state->app->db, existing->team_id);
    if (current != NULL) {
        ui_utf8_to_wide(current->name, current_team,
                        sizeof(current_team) / sizeof(current_team[0]));
    }
    const Team *target = db_find_team(
        &state->app->db, import_row_team_id(state, row));
    if (target != NULL) {
        ui_utf8_to_wide(target->name, import_team,
                        sizeof(import_team) / sizeof(import_team[0]));
    } else {
        ui_edit_get_wide(row->home ? state->home_team : state->away_team,
                         import_team,
                         sizeof(import_team) / sizeof(import_team[0]));
        if (import_team[0] == L'\0') {
            import_format_wide(
                import_team,
                sizeof(import_team) / sizeof(import_team[0]),
                L"this club");
        }
    }

    wchar_t content[2048];
    if (same_club) {
        import_format_wide(
            content, sizeof(content) / sizeof(content[0]),
            L"%ls is already in this squad (%ls).\n\n"
            L"Current rating: %.1f after %u matches\n"
            L"Starting rating: %.2f\n\n"
            L"Add separate player creates a second record in %ls. The "
            L"existing player and their match history stay unchanged.\n\n"
            L"Use existing player keeps the existing record and its history.",
            player_name, current_team, existing->current_rating,
            existing->matches_played, existing->base_rating, import_team);
    } else {
        import_format_wide(
            content, sizeof(content) / sizeof(content[0]),
            L"%ls already exists in %ls.\n\n"
            L"Current rating: %.1f after %u matches\n"
            L"Starting rating: %.2f\n\n"
            L"Add separate player creates a new record in %ls. The existing "
            L"player and their match history stay unchanged.\n\n"
            L"Move existing player changes %ls's club to %ls. Their name, "
            L"starting rating and match history stay unchanged.",
            player_name, current_team, existing->current_rating,
            existing->matches_played, existing->base_rating, import_team,
            player_name, import_team);
    }

    TASKDIALOG_BUTTON buttons[2];
    memset(buttons, 0, sizeof(buttons));
    buttons[0].nButtonID = IMPORT_DUPLICATE_ADD_ID;
    buttons[0].pszButtonText = L"Add separate player";
    if (same_club) {
        buttons[1].nButtonID = IMPORT_DUPLICATE_USE_ID;
        buttons[1].pszButtonText = L"Use existing player";
    } else {
        buttons[1].nButtonID = IMPORT_DUPLICATE_MOVE_ID;
        buttons[1].pszButtonText = L"Move existing player";
    }

    TASKDIALOGCONFIG config;
    memset(&config, 0, sizeof(config));
    config.cbSize = sizeof(config);
    config.hwndParent = state->hwnd;
    config.hInstance = state->app->instance;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION |
                     TDF_POSITION_RELATIVE_TO_WINDOW;
    config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    config.pszWindowTitle = L"Player import action - PitchRate";
    config.pszMainIcon = TD_WARNING_ICON;
    config.pszMainInstruction =
        same_club
            ? L"A player with this name is already in this squad"
            : L"A player with this name already exists";
    config.pszContent = content;
    config.cButtons = 2;
    config.pButtons = buttons;
    config.nDefaultButton = same_club ? IMPORT_DUPLICATE_USE_ID
                                      : IMPORT_DUPLICATE_ADD_ID;

    int pressed = IDCANCEL;
    if (SUCCEEDED(TaskDialogIndirect(&config, &pressed, NULL, NULL))) {
        if (pressed == IMPORT_DUPLICATE_ADD_ID) {
            row->action = IMPORT_PLAYER_CREATE;
        } else if (pressed == IMPORT_DUPLICATE_MOVE_ID ||
                   pressed == IMPORT_DUPLICATE_USE_ID) {
            row->action = IMPORT_PLAYER_USE_EXISTING;
        } else {
            return;
        }
    } else {
        wchar_t fallback[2304];
        if (same_club) {
            import_format_wide(
                fallback, sizeof(fallback) / sizeof(fallback[0]),
                L"%ls\n\nYes: add a separate player in %ls.\n"
                L"No: use the existing player.\n"
                L"Cancel: keep the current choice.",
                content, import_team);
        } else {
            import_format_wide(
                fallback, sizeof(fallback) / sizeof(fallback[0]),
                L"%ls\n\nYes: add a separate player in %ls.\n"
                L"No: move %ls to %ls.\n"
                L"Cancel: keep the current choice.",
                content, import_team, player_name, import_team);
        }
        UINT fallback_flags = MB_YESNOCANCEL | MB_ICONWARNING |
                              (same_club ? MB_DEFBUTTON2 : MB_DEFBUTTON1);
        int answer = MessageBoxW(state->hwnd, fallback,
                                 L"Player import action", fallback_flags);
        if (answer == IDYES) {
            row->action = IMPORT_PLAYER_CREATE;
        } else if (answer == IDNO) {
            row->action = IMPORT_PLAYER_USE_EXISTING;
        } else {
            return;
        }
    }
    import_row_status_text(row);
    import_row_update_list(state, row_index);
    import_update_action_button(state);
    InvalidateRect(state->hwnd, NULL, FALSE);
}

static void import_draw_legend(ImportMatchState *state, HDC dc)
{
    RECT rect = state->legend_rect;
    if (rect.right <= rect.left) {
        return;
    }
    struct {
        COLORREF color;
        const wchar_t *label;
    } items[4] = {
        { UI_COLOR_BLUE, L"New player" },
        { UI_COLOR_ACCENT, L"In this squad" },
        { UI_COLOR_AMBER, L"Other club" },
        { UI_COLOR_DANGER, L"Archived" },
    };
    int chip = ui_scale(&state->app->ui, 10);
    int gap = ui_scale(&state->app->ui, 6);
    int block_gap = ui_scale(&state->app->ui, 16);
    int x = rect.left;
    int y = rect.top + (rect.bottom - rect.top - chip) / 2;
    for (int i = 0; i < 4; ++i) {
        RECT box = { x, y, x + chip, y + chip };
        ui_fill_round_rect(dc, box, ui_scale(&state->app->ui, 3),
                           items[i].color);
        wchar_t label[64];
        import_format_wide(label, sizeof(label) / sizeof(label[0]),
                              L"%ls", items[i].label);
        int text_width = ui_scale(&state->app->ui, 82);
        RECT text = { x + chip + gap, rect.top, x + chip + gap + text_width,
                      rect.bottom };
        ui_draw_text(dc, state->app->ui.font_small, UI_COLOR_MUTED, text,
                     label, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                                DT_END_ELLIPSIS);
        x = text.right + block_gap;
        if (x >= rect.right) {
            break;
        }
    }
}

static void import_draw_team_heading(ImportMatchState *state, HDC dc,
                                     RECT rect, bool home)
{
    HWND edit = home ? state->home_team : state->away_team;
    wchar_t name[TEAM_NAME_MAX + 1];
    ui_edit_get_wide(edit, name, sizeof(name) / sizeof(name[0]));
    wchar_t label[160];
    if (name[0] != L'\0') {
        import_format_wide(label, sizeof(label) / sizeof(label[0]),
                              home ? L"Home: %ls" : L"Away: %ls", name);
    } else {
        import_format_wide(label, sizeof(label) / sizeof(label[0]),
                              L"%ls", home ? L"Home players"
                                           : L"Away players");
    }
    ui_draw_text(dc, state->app->ui.font_body_bold, UI_COLOR_TEXT, rect,
                 label, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                            DT_END_ELLIPSIS);
}

static void import_layout(ImportMatchState *state, int width, int height)
{
    UiTheme *theme = &state->app->ui;
    int margin = ui_scale(theme, 28);
    int gap = ui_scale(theme, 10);
    int field_height = ui_scale(theme, 32);
    int label_height = ui_scale(theme, 18);
    int available = width - margin * 2;

    int date_width = ui_scale(theme, 118);
    int score_width = ui_scale(theme, 56);
    int dash_width = ui_scale(theme, 24);
    int team_width = (available - date_width - score_width * 2 -
                      dash_width - gap * 4) / 2;
    if (team_width < ui_scale(theme, 100)) {
        team_width = ui_scale(theme, 100);
    }

    int label_y = ui_scale(theme, 94);
    int field_y = label_y + label_height + ui_scale(theme, 4);
    int x = margin;
    state->label_date = (RECT){ x, label_y, x + date_width,
                                label_y + label_height };
    MoveWindow(state->date, x, field_y, date_width, field_height, TRUE);
    x += date_width + gap;
    state->label_home_team = (RECT){ x, label_y, x + team_width,
                                     label_y + label_height };
    MoveWindow(state->home_team, x, field_y, team_width, field_height, TRUE);
    x += team_width + gap;
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
    MoveWindow(state->away_team, x, field_y, away_width, field_height, TRUE);
    (void)dash_x;

    int info_top = field_y + field_height + ui_scale(theme, 8);
    state->neutral_rect = (RECT){
        margin, info_top, margin + ui_scale(theme, 290),
        info_top + ui_scale(theme, 26),
    };
    state->legend_rect = (RECT){
        state->neutral_rect.right + ui_scale(theme, 14), info_top,
        width - margin, info_top + ui_scale(theme, 26),
    };
    if (state->legend_rect.right < state->legend_rect.left) {
        state->legend_rect.right = state->legend_rect.left;
    }
    state->info_rect = (RECT){ margin,
                               state->neutral_rect.bottom +
                                   ui_scale(theme, 1),
                               width - margin,
                               state->neutral_rect.bottom +
                                   ui_scale(theme, 21) };
    MoveWindow(state->neutral_venue, state->neutral_rect.left,
               state->neutral_rect.top,
               state->neutral_rect.right - state->neutral_rect.left,
               state->neutral_rect.bottom - state->neutral_rect.top, TRUE);

    int section_y = state->info_rect.bottom + ui_scale(theme, 10);
    state->section_rect = (RECT){ margin, section_y, width - margin,
                                  section_y + ui_scale(theme, 22) };

    int column_gap = ui_scale(theme, 12);
    int column_width = (available - column_gap) / 2;
    if (column_width < 0) {
        column_width = 0;
    }
    int left_x = margin;
    int right_x = margin + column_width + column_gap;
    int heading_height = ui_scale(theme, 26);
    int heading_y = section_y + ui_scale(theme, 28);
    state->home_header_rect = (RECT){ left_x, heading_y,
                                      left_x + column_width,
                                      heading_y + heading_height };
    state->away_header_rect = (RECT){ right_x, heading_y,
                                      right_x + column_width,
                                      heading_y + heading_height };

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
        MoveWindow(lists[side], frames[side].left + inset,
                   frames[side].top + inset, list_width, list_height, TRUE);
    }

    static const int widths[IMPORT_COLUMN_COUNT] = { 24, 24, 7, 8, 25, 12 };
    ui_list_set_percent_widths(state->home_list, widths,
                               IMPORT_COLUMN_COUNT);
    ui_list_set_percent_widths(state->away_list, widths,
                               IMPORT_COLUMN_COUNT);

    int button_height = ui_scale(theme, 36);
    int button_y = height - ui_scale(theme, 58);
    int save_width = ui_scale(theme, 150);
    int cancel_width = ui_scale(theme, 92);
    int action_width = ui_scale(theme, 168);
    int right = width - margin;
    MoveWindow(state->save, right - save_width, button_y,
               save_width, button_height, TRUE);
    MoveWindow(state->cancel,
               right - save_width - ui_scale(theme, 10) - cancel_width,
               button_y, cancel_width, button_height, TRUE);
    MoveWindow(state->change_action, margin, button_y, action_width,
               button_height, TRUE);
    state->footer_line = (RECT){ margin, button_y - ui_scale(theme, 15),
                                 right, button_y - ui_scale(theme, 14) };
    state->error_rect = (RECT){ margin, button_y - ui_scale(theme, 4),
                                right - save_width - cancel_width -
                                    ui_scale(theme, 30),
                                height - ui_scale(theme, 8) };
}

static void import_paint(ImportMatchState *state, HWND hwnd)
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
            L"Import match from WhoScored",
            L"Review the scraped fixture and line-ups. Nothing is created "
            L"until you confirm.");
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
        ui_draw_text(canvas.dc, state->app->ui.font_h2, UI_COLOR_FAINT,
                     (RECT){ state->label_home_score.right,
                             state->label_date.top,
                             state->label_away_score.left,
                             state->label_home_score.bottom },
                     L"-", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        import_draw_legend(state, canvas.dc);
        ui_draw_text(
            canvas.dc, state->app->ui.font_small, UI_COLOR_FAINT,
            state->info_rect,
            L"Double-click First name, Last name, Minutes or Performance to "
            L"edit. The cue updates as names and clubs change. Existing club "
            L"names are reused; new names create a club on import.",
            DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        ui_draw_text(canvas.dc, state->app->ui.font_body_bold,
                     UI_COLOR_TEXT, state->section_rect,
                     L"Imported line-ups",
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        import_draw_team_heading(state, canvas.dc,
                                 state->home_header_rect, true);
        import_draw_team_heading(state, canvas.dc,
                                 state->away_header_rect, false);

        ui_fill_round_rect(canvas.dc, state->home_list_frame,
                           ui_scale(&state->app->ui, 9), UI_COLOR_CARD);
        ui_fill_round_rect(canvas.dc, state->away_list_frame,
                           ui_scale(&state->app->ui, 9), UI_COLOR_CARD);
        ui_draw_line(canvas.dc, state->footer_line.left,
                     state->footer_line.top, state->footer_line.right,
                     state->footer_line.top, UI_COLOR_BORDER);
        dialog_draw_error(state->app, canvas.dc, state->error_rect,
                          state->error);
        ui_canvas_end(&canvas);
    }
    EndPaint(hwnd, &paint);
}

static void import_add_row_to_list(ImportMatchState *state, size_t index)
{
    ImportRow *row = &state->rows[index];
    HWND list = import_list_for_row(state, index);
    char minutes[16];
    char performance[16];
    char status[256];
    char action[64];
    snprintf(minutes, sizeof(minutes), "%d", row->minutes);
    snprintf(performance, sizeof(performance), "%.2f", row->performance);
    ui_wide_to_utf8(row->status_text, status, sizeof(status));
    ui_wide_to_utf8(row->action_text, action, sizeof(action));
    const char *columns[IMPORT_COLUMN_COUNT] = {
        row->first_name, row->last_name, minutes, performance,
        status, action,
    };
    int item = ui_list_add_row(list, columns, IMPORT_COLUMN_COUNT,
                               (LPARAM)index);
    if (item < 0) {
        import_show_error_wide(
            state, L"Could not add a player to the imported line-up.");
    }
}

static bool import_build(ImportMatchState *state, const ImportedMatch *match)
{
    UiTheme *theme = &state->app->ui;
    state->date = ui_edit_create(state->hwnd, IMPORT_DATE_ID, L"",
                                 0, 0, 10, 10, theme->font_body, 10);
    state->home_team = ui_edit_create(state->hwnd, IMPORT_HOME_TEAM_ID, L"",
                                      0, 0, 10, 10, theme->font_body, 80);
    state->away_team = ui_edit_create(state->hwnd, IMPORT_AWAY_TEAM_ID, L"",
                                      0, 0, 10, 10, theme->font_body, 80);
    state->home_score = ui_edit_create(state->hwnd, IMPORT_HOME_SCORE_ID,
                                       L"0", 0, 0, 10, 10,
                                       theme->font_body, 2);
    state->away_score = ui_edit_create(state->hwnd, IMPORT_AWAY_SCORE_ID,
                                       L"0", 0, 0, 10, 10,
                                       theme->font_body, 2);
    state->home_list = ui_list_create(state->hwnd, IMPORT_HOME_LIST_ID,
                                      0, 0, 10, 10, theme->font_small,
                                      theme->row_images, false);
    state->away_list = ui_list_create(state->hwnd, IMPORT_AWAY_LIST_ID,
                                      0, 0, 10, 10, theme->font_small,
                                      theme->row_images, false);
    state->neutral_venue = ui_checkbox_create(
        state->hwnd, IMPORT_NEUTRAL_VENUE_ID,
        L"Neutral venue (no home advantage)", 0, 0, 10, 10,
        theme->font_body);
    state->change_action = ui_button_create(
        state->hwnd, IMPORT_CHANGE_ACTION_ID, L"Change player action",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    state->save = ui_button_create(
        state->hwnd, IMPORT_SAVE_ID, L"Import match",
        0, 0, 10, 10, UI_BUTTON_PRIMARY, theme->font_button);
    state->cancel = ui_button_create(
        state->hwnd, IMPORT_CANCEL_ID, L"Cancel",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    if (state->date == NULL || state->home_team == NULL ||
        state->away_team == NULL || state->home_score == NULL ||
        state->away_score == NULL || state->home_list == NULL ||
        state->away_list == NULL || state->neutral_venue == NULL ||
        state->change_action == NULL || state->save == NULL ||
        state->cancel == NULL) {
        return false;
    }

    const HWND lists[2] = { state->home_list, state->away_list };
    for (int side = 0; side < 2; ++side) {
        SendMessageW(lists[side], LVM_SETEXTENDEDLISTVIEWSTYLE,
                     LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                         LVS_EX_LABELTIP,
                     LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                         LVS_EX_LABELTIP);
        ui_list_add_column(lists[side], IMPORT_COLUMN_FIRST,
                           L"First name", 130);
        ui_list_add_column(lists[side], IMPORT_COLUMN_LAST,
                           L"Last name", 130);
        ui_list_add_column(lists[side], IMPORT_COLUMN_MINUTES,
                           L"Min", 50);
        ui_list_add_column(lists[side], IMPORT_COLUMN_PERFORMANCE,
                           L"Perf", 50);
        ui_list_add_column(lists[side], IMPORT_COLUMN_STATUS,
                           L"Status", 150);
        ui_list_add_column(lists[side], IMPORT_COLUMN_ACTION,
                           L"Action", 90);
    }

    ui_edit_set_utf8(state->date, match->date);
    ui_edit_set_utf8(state->home_team, match->home_team);
    ui_edit_set_utf8(state->away_team, match->away_team);
    char score[16];
    snprintf(score, sizeof(score), "%d", match->home_goals);
    ui_edit_set_utf8(state->home_score, score);
    snprintf(score, sizeof(score), "%d", match->away_goals);
    ui_edit_set_utf8(state->away_score, score);
    ui_checkbox_set_checked(state->neutral_venue, false);
    ui_edit_set_cue(state->date, L"YYYY-MM-DD");

    state->row_count = match->home_player_count + match->away_player_count;
    state->home_row_count = match->home_player_count;
    state->rows = calloc(state->row_count, sizeof(*state->rows));
    if (state->rows == NULL) {
        import_show_error_wide(
            state, L"Not enough memory to load the imported line-ups.");
        return false;
    }
    for (size_t i = 0; i < state->row_count; ++i) {
        ImportRow *row = &state->rows[i];
        bool home = i < state->home_row_count;
        const ImportedPlayer *source =
            home ? &match->home_players[i]
                 : &match->away_players[i - state->home_row_count];
        row->home = home;
        util_str_copy(row->first_name, sizeof(row->first_name),
                      source->first_name);
        util_str_copy(row->last_name, sizeof(row->last_name),
                      source->last_name);
        row->minutes = source->minutes;
        row->performance = RATING_NEUTRAL_PERFORMANCE;
        row->action = IMPORT_PLAYER_CREATE;
    }
    import_recompute_all(state, false);

    state->building = true;
    for (size_t i = 0; i < state->row_count; ++i) {
        import_add_row_to_list(state, i);
    }
    state->building = false;

    RECT client;
    GetClientRect(state->hwnd, &client);
    import_layout(state, client.right, client.bottom);
    import_update_action_button(state);
    state->ready = true;
    InvalidateRect(state->hwnd, NULL, FALSE);
    return true;
}

static void import_save(ImportMatchState *state)
{
    if (state->cell_edit != NULL &&
        !import_commit_cell_edit(state, true)) {
        return;
    }
    char date[64];
    char home_goals_text[32];
    char away_goals_text[32];
    char home_team[256];
    char away_team[256];
    ui_edit_get_utf8(state->date, date, sizeof(date));
    ui_edit_get_utf8(state->home_team, home_team, sizeof(home_team));
    ui_edit_get_utf8(state->away_team, away_team, sizeof(away_team));
    ui_edit_get_utf8(state->home_score, home_goals_text,
                     sizeof(home_goals_text));
    ui_edit_get_utf8(state->away_score, away_goals_text,
                     sizeof(away_goals_text));
    int home_goals = 0;
    int away_goals = 0;
    if (!util_date_valid(date)) {
        import_show_error_wide(state,
                               L"Use a valid date in YYYY-MM-DD format.");
        return;
    }
    if (!util_parse_int(home_goals_text, 0, 99, &home_goals) ||
        !util_parse_int(away_goals_text, 0, 99, &away_goals)) {
        import_show_error_wide(state, L"Goals must be between 0 and 99.");
        return;
    }

    import_recompute_all(state, true);
    ImportPlayerPlan *home_plans = NULL;
    ImportPlayerPlan *away_plans = NULL;
    if (state->home_row_count > 0) {
        home_plans = calloc(state->home_row_count, sizeof(*home_plans));
    }
    if (state->row_count > state->home_row_count) {
        away_plans = calloc(state->row_count - state->home_row_count,
                            sizeof(*away_plans));
    }
    if ((state->home_row_count > 0 && home_plans == NULL) ||
        (state->row_count > state->home_row_count && away_plans == NULL)) {
        free(home_plans);
        free(away_plans);
        import_show_error_wide(state,
                               L"Not enough memory to build the import.");
        return;
    }
    size_t home_used = 0;
    size_t away_used = 0;
    for (size_t i = 0; i < state->row_count; ++i) {
        ImportRow *row = &state->rows[i];
        ImportPlayerPlan *plan =
            row->home ? &home_plans[home_used++]
                      : &away_plans[away_used++];
        plan->first_name = row->first_name;
        plan->last_name = row->last_name;
        plan->minutes = row->minutes;
        plan->performance = row->performance;
        plan->action = row->action;
        plan->existing_player_id = row->existing_player_id;
    }

    ImportPlan plan = {
        .date = date,
        .home_team_name = home_team,
        .away_team_name = away_team,
        .home_goals = home_goals,
        .away_goals = away_goals,
        .neutral_venue = ui_checkbox_get_checked(state->neutral_venue),
        .home_players = home_plans,
        .home_player_count = home_used,
        .away_players = away_plans,
        .away_player_count = away_used,
    };

    Database backup;
    if (!app_tx_begin(state->app, &backup)) {
        free(home_plans);
        free(away_plans);
        return;
    }
    char error[512];
    uint64_t match_id = 0;
    bool ok = db_import_match(&state->app->db, &plan, &match_id,
                              error, sizeof(error));
    free(home_plans);
    free(away_plans);
    if (!ok) {
        app_tx_abort(state->app, &backup);
        import_note_error(state, error);
        return;
    }
    if (!app_tx_commit(state->app, &backup)) {
        return;
    }
    state->saved_match_id = match_id;
    dialog_close(state->hwnd);
}

LRESULT CALLBACK import_match_dialog_proc(HWND hwnd, UINT message,
                                          WPARAM wparam, LPARAM lparam)
{
    ImportMatchState *state =
        (ImportMatchState *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (message) {
    case WM_NCCREATE: {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    case WM_SIZE:
        if (state != NULL && state->home_list != NULL) {
            import_layout(state, LOWORD(lparam), HIWORD(lparam));
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        if (state != NULL) {
            import_paint(state, hwnd);
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
        switch (LOWORD(wparam)) {
        case IMPORT_DATE_ID:
        case IMPORT_HOME_SCORE_ID:
        case IMPORT_AWAY_SCORE_ID:
            if (state->ready && HIWORD(wparam) == EN_CHANGE) {
                state->error[0] = L'\0';
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case IMPORT_HOME_TEAM_ID:
            if (state->ready && HIWORD(wparam) == EN_CHANGE) {
                state->error[0] = L'\0';
                import_recompute_side(state, true, true);
                import_update_action_button(state);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case IMPORT_AWAY_TEAM_ID:
            if (state->ready && HIWORD(wparam) == EN_CHANGE) {
                state->error[0] = L'\0';
                import_recompute_side(state, false, true);
                import_update_action_button(state);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        case IMPORT_CHANGE_ACTION_ID:
            import_change_action(state);
            return 0;
        case IMPORT_SAVE_ID:
            import_save(state);
            return 0;
        case IMPORT_CANCEL_ID:
            dialog_close(hwnd);
            return 0;
        default:
            break;
        }
        break;
    case WM_IMPORT_COMMIT_CELL:
        if (state != NULL && state->cell_edit != NULL &&
            (uint64_t)lparam == state->cell_edit_generation) {
            (void)import_commit_cell_edit(state, wparam != 0);
        }
        return 0;
    case WM_NOTIFY: {
        if (state == NULL) {
            break;
        }
        NMHDR *header = (NMHDR *)lparam;
        if (header->idFrom != IMPORT_HOME_LIST_ID &&
            header->idFrom != IMPORT_AWAY_LIST_ID) {
            break;
        }
        HWND list = header->hwndFrom;
        if (header->code == NM_CUSTOMDRAW) {
            return import_custom_draw(state,
                                      (NMLVCUSTOMDRAW *)lparam);
        }
        if (header->code == LVN_ITEMCHANGED) {
            if (!state->building) {
                import_update_action_button(state);
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
            if (hit.iItem >= 0) {
                size_t row_index = 0;
                if (import_row_from_item(state, list, hit.iItem,
                                         &row_index)) {
                    ImportRow *row = &state->rows[row_index];
                    if (hit.iSubItem == IMPORT_COLUMN_STATUS ||
                        hit.iSubItem == IMPORT_COLUMN_ACTION) {
                        if ((row->status == IMPORT_ROW_OTHER_CLUB ||
                             row->status == IMPORT_ROW_SAME_CLUB) &&
                            !row->archived) {
                            SetFocus(list);
                            ListView_SetItemState(
                                list, hit.iItem,
                                LVIS_SELECTED | LVIS_FOCUSED,
                                LVIS_SELECTED | LVIS_FOCUSED);
                            import_change_action(state);
                        }
                    } else if (hit.iSubItem >= IMPORT_COLUMN_FIRST &&
                               hit.iSubItem <= IMPORT_COLUMN_PERFORMANCE) {
                        import_begin_cell_edit(state, list, hit.iItem,
                                               hit.iSubItem);
                    }
                }
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

static uint64_t import_preview_run(App *app, const ImportedMatch *match)
{
    ImportMatchState state;
    memset(&state, 0, sizeof(state));
    state.app = app;
    HWND hwnd = dialog_create_shell(
        app, app->main_hwnd, PITCHRATE_IMPORT_MATCH_DIALOG_CLASS,
        L"Import match - PitchRate", 1040, 700, &state);
    if (hwnd == NULL) {
        return 0;
    }
    state.hwnd = hwnd;
    if (!import_build(&state, match)) {
        dialog_close(hwnd);
        free(state.rows);
        return 0;
    }
    ui_run_modal(hwnd, app->main_hwnd, IMPORT_SAVE_ID);
    uint64_t saved = state.saved_match_id;
    import_cancel_cell_edit(&state);
    free(state.rows);
    return saved;
}

uint64_t dialog_import_match(App *app)
{
    char *html = NULL;
    size_t length = 0;
    if (!import_url_dialog_run(app, &html, &length)) {
        return 0;
    }
    ImportedMatch match;
    char error[512];
    if (!imported_match_parse_html(html, length, &match, error,
                                   sizeof(error))) {
        free(html);
        app_show_error_utf8(app, L"Could not import the match", error);
        return 0;
    }
    free(html);
    uint64_t match_id = import_preview_run(app, &match);
    imported_match_free(&match);
    return match_id;
}
