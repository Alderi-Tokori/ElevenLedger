#include "app.h"

#include "core/storage.h"
#include "core/util.h"
#include "ui/controls.h"
#include "ui/dialogs.h"
#include "ui/main_window.h"
#include "ui/page.h"
#include "ui/pages.h"
#include "ui/theme.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef PITCHRATE_VERSION
#define PITCHRATE_VERSION "1.14.0"
#endif

#define MAIN_WINDOW_CLASS_NAME L"PitchRateMainWindow"

static int system_dpi(void)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 != NULL) {
        typedef UINT(WINAPI *GetDpiForSystemFn)(void);
        FARPROC address = GetProcAddress(user32, "GetDpiForSystem");
        if (address != NULL) {
            GetDpiForSystemFn get_dpi = NULL;
            memcpy(&get_dpi, &address, sizeof(get_dpi));
            UINT dpi = get_dpi();
            if (dpi >= 72 && dpi <= 480) {
                return (int)dpi;
            }
        }
    }
    HDC screen = GetDC(NULL);
    if (screen == NULL) {
        return 96;
    }
    int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(NULL, screen);
    return dpi > 0 ? dpi : 96;
}

static void directory_from_path(const wchar_t *path, wchar_t *directory,
                                size_t count)
{
    if (directory == NULL || count == 0) {
        return;
    }
    directory[0] = L'\0';
    if (path == NULL) {
        return;
    }
    wcsncpy(directory, path, count - 1);
    directory[count - 1] = L'\0';
    wchar_t *last = NULL;
    for (wchar_t *cursor = directory; *cursor != L'\0'; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') {
            last = cursor;
        }
    }
    if (last == NULL) {
        directory[0] = L'.';
        directory[1] = L'\0';
    } else if (last == directory) {
        directory[1] = L'\0';
    } else if (last == directory + 2 && directory[1] == L':') {
        last[1] = L'\0';
    } else {
        *last = L'\0';
    }
}

static bool ensure_directory(const wchar_t *path, char *error,
                             size_t error_size)
{
    int result = SHCreateDirectoryExW(NULL, path, NULL);
    if (result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS ||
        result == ERROR_FILE_EXISTS) {
        return true;
    }
    snprintf(error, error_size,
             "Could not create the data folder (Windows error %d).", result);
    return false;
}

static bool resolve_data_path(App *app, const wchar_t *override,
                              char *error, size_t error_size)
{
    if (override != NULL && override[0] != L'\0') {
        DWORD length = GetFullPathNameW(override,
                                        (DWORD)(sizeof(app->data_path) /
                                                sizeof(app->data_path[0])),
                                        app->data_path, NULL);
        if (length == 0 || length >= sizeof(app->data_path) /
                                        sizeof(app->data_path[0])) {
            snprintf(error, error_size, "The selected data path is invalid or too long.");
            return false;
        }
    } else {
        wchar_t base[1024];
        DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", base,
                                               (DWORD)(sizeof(base) /
                                                       sizeof(base[0])));
        if (length == 0 || length >= sizeof(base) / sizeof(base[0])) {
            length = GetEnvironmentVariableW(L"USERPROFILE", base,
                                             (DWORD)(sizeof(base) /
                                                     sizeof(base[0])));
        }
        if (length == 0 || length >= sizeof(base) / sizeof(base[0])) {
            snprintf(error, error_size,
                     "Could not locate the current user's application data folder.");
            return false;
        }
        wchar_t folder[1024];
        if (swprintf(folder, sizeof(folder) / sizeof(folder[0]),
                     L"%ls\\PitchRate", base) < 0) {
            snprintf(error, error_size, "The data folder path is too long.");
            return false;
        }
        if (!ensure_directory(folder, error, error_size)) {
            return false;
        }
        if (swprintf(app->data_path,
                     sizeof(app->data_path) / sizeof(app->data_path[0]),
                     L"%ls\\data.frc", folder) < 0) {
            snprintf(error, error_size, "The data file path is too long.");
            return false;
        }
    }

    directory_from_path(app->data_path, app->data_directory,
                        sizeof(app->data_directory) /
                            sizeof(app->data_directory[0]));
    if (app->data_directory[0] != L'\0' &&
        wcscmp(app->data_directory, L".") != 0) {
        if (!ensure_directory(app->data_directory, error, error_size)) {
            return false;
        }
    }
    return true;
}

static void show_error_utf8(HWND owner, const wchar_t *title,
                            const char *message)
{
    wchar_t wide[2048];
    if (!ui_utf8_to_wide(message, wide, sizeof(wide) / sizeof(wide[0]))) {
        wide[0] = L'\0';
    }
    MessageBoxW(owner, wide, title, MB_OK | MB_ICONERROR);
}

void app_show_error_utf8(App *app, const wchar_t *title, const char *message)
{
    show_error_utf8(app != NULL ? app->main_hwnd : NULL, title, message);
}

void app_show_error(App *app, const wchar_t *title, const wchar_t *format, ...)
{
    wchar_t message[2048];
    va_list args;
    va_start(args, format);
    _vsnwprintf(message, sizeof(message) / sizeof(message[0]) - 1, format,
                args);
    message[sizeof(message) / sizeof(message[0]) - 1] = L'\0';
    va_end(args);
    MessageBoxW(app != NULL ? app->main_hwnd : NULL, message, title,
                MB_OK | MB_ICONERROR);
}

static bool create_pages(App *app)
{
    app->pages[PAGE_DASHBOARD] = dashboard_page_create(app);
    app->pages[PAGE_PLAYERS] = players_page_create(app);
    app->pages[PAGE_TEAMS] = teams_page_create(app);
    app->pages[PAGE_MATCHES] = matches_page_create(app);
    app->pages[PAGE_GUIDE] = guide_page_create(app);
    for (int i = 0; i < PAGE_COUNT; ++i) {
        if (app->pages[i] == NULL) {
            return false;
        }
    }
    return true;
}

static bool create_accelerators(App *app)
{
    ACCEL entries[PAGE_COUNT + 2];
    memset(entries, 0, sizeof(entries));
    for (int i = 0; i < PAGE_COUNT; ++i) {
        entries[i].fVirt = FVIRTKEY | FCONTROL;
        entries[i].key = (WORD)(L'1' + i);
        entries[i].cmd = (WORD)(CMD_NAVIGATE_FIRST + i);
    }
    entries[PAGE_COUNT].fVirt = FVIRTKEY;
    entries[PAGE_COUNT].key = VK_F1;
    entries[PAGE_COUNT].cmd = CMD_NAVIGATE_FIRST + PAGE_GUIDE;
    entries[PAGE_COUNT + 1].fVirt = FVIRTKEY | FCONTROL;
    entries[PAGE_COUNT + 1].key = (WORD)L'N';
    entries[PAGE_COUNT + 1].cmd = CMD_CREATE_ENTRY;
    app->accelerators = CreateAcceleratorTableW(entries, PAGE_COUNT + 2);
    return app->accelerators != NULL;
}

void app_layout_pages(App *app)
{
    if (app == NULL || app->main_hwnd == NULL) {
        return;
    }
    RECT client;
    GetClientRect(app->main_hwnd, &client);
    int sidebar = main_window_sidebar_width(app);
    for (int i = 0; i < PAGE_COUNT; ++i) {
        if (app->pages[i] != NULL && app->pages[i]->hwnd != NULL) {
            MoveWindow(app->pages[i]->hwnd, sidebar, 0,
                       client.right - sidebar, client.bottom, TRUE);
        }
    }
}

void app_navigate(App *app, PageId page)
{
    if (app == NULL || page < 0 || page >= PAGE_COUNT ||
        app->pages[page] == NULL) {
        return;
    }
    if (app->active_page != page) {
        Page *current = app->pages[app->active_page];
        if (current != NULL && current->hwnd != NULL) {
            ShowWindow(current->hwnd, SW_HIDE);
        }
        app->active_page = page;
        ShowWindow(app->pages[page]->hwnd, SW_SHOW);
    }
    app_layout_pages(app);
    ShowWindow(app->pages[page]->hwnd, SW_SHOW);
    if (app->pages[page]->on_refresh != NULL) {
        app->pages[page]->on_refresh(app->pages[page]);
    }
    SetFocus(app->pages[page]->hwnd);
    InvalidateRect(app->main_hwnd, NULL, FALSE);
}

void app_refresh_all(App *app)
{
    if (app == NULL) {
        return;
    }
    for (int i = 0; i < PAGE_COUNT; ++i) {
        Page *page = app->pages[i];
        if (page != NULL && page->hwnd != NULL && page->on_refresh != NULL) {
            page->on_refresh(page);
        }
    }
}

bool app_tx_begin(App *app, Database *backup)
{
    if (app == NULL || backup == NULL) {
        return false;
    }
    memset(backup, 0, sizeof(*backup));
    if (!db_clone(backup, &app->db)) {
        app_show_error(app, L"PitchRate",
                       L"Not enough memory to save this change.");
        return false;
    }
    return true;
}

bool app_tx_commit(App *app, Database *backup)
{
    if (app == NULL || backup == NULL) {
        return false;
    }
    char error[1024];
    if (!storage_save(app->data_path, &app->db, error, sizeof(error))) {
        db_free(&app->db);
        app->db = *backup;
        memset(backup, 0, sizeof(*backup));
        app_refresh_all(app);
        app_show_error_utf8(app, L"Could not save data", error);
        return false;
    }
    db_free(backup);
    app_refresh_all(app);
    return true;
}

void app_tx_abort(App *app, Database *backup)
{
    if (app == NULL || backup == NULL) {
        return;
    }
    db_free(&app->db);
    app->db = *backup;
    memset(backup, 0, sizeof(*backup));
    app_refresh_all(app);
}

void app_open_data_folder(App *app)
{
    if (app == NULL) {
        return;
    }
    HINSTANCE result = ShellExecuteW(app->main_hwnd, L"open",
                                     app->data_directory, NULL, NULL,
                                     SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32) {
        app_show_error(app, L"Could not open data folder",
                       L"Windows could not open:\n%ls", app->data_directory);
    }
}

static bool single_instance_guard(App *app)
{
    app->instance_mutex = CreateMutexW(NULL, FALSE,
                                       L"Local\\PitchRate.SingleInstance");
    if (app->instance_mutex == NULL) {
        return true;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(MAIN_WINDOW_CLASS_NAME, NULL);
        if (existing != NULL) {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(app->instance_mutex);
        app->instance_mutex = NULL;
        MessageBoxW(NULL, L"PitchRate is already running.",
                    L"PitchRate", MB_OK | MB_ICONINFORMATION);
        return false;
    }
    return true;
}

bool app_initialize(App *app, HINSTANCE instance, const wchar_t *data_path,
                    int show_command)
{
    if (app == NULL) {
        return false;
    }
    memset(app, 0, sizeof(*app));
    app->instance = instance;
    app->active_page = PAGE_DASHBOARD;
    app->hovered_nav = -1;

    SetCurrentProcessExplicitAppUserModelID(L"PitchRate.FootballRating");

    if (!single_instance_guard(app)) {
        return false;
    }

    char error[1024];
    if (!resolve_data_path(app, data_path, error, sizeof(error))) {
        show_error_utf8(NULL, L"PitchRate", error);
        return false;
    }

    StorageStatus status = storage_load(app->data_path, &app->db, error,
                                        sizeof(error));
    if (status == STORAGE_RECOVERED) {
        wchar_t warning[2048];
        wchar_t detail[2048];
        ui_utf8_to_wide(error, detail,
                        sizeof(detail) / sizeof(detail[0]));
        ui_format_wide(warning, sizeof(warning) / sizeof(warning[0]),
                       L"The data file was damaged or missing.\n\n%ls\n\n"
                       L"PitchRate will restore the last good backup now.",
                       detail);
        MessageBoxW(NULL, warning, L"Recovered backup",
                    MB_OK | MB_ICONWARNING);
        if (!storage_repair_from_backup(app->data_path, error,
                                        sizeof(error))) {
            wchar_t failure[2048];
            wchar_t detail2[2048];
            ui_utf8_to_wide(error, detail2,
                            sizeof(detail2) / sizeof(detail2[0]));
            ui_format_wide(failure, sizeof(failure) / sizeof(failure[0]),
                           L"PitchRate could not restore the backup, so it "
                           L"has stopped to protect your data.\n\n%ls",
                           detail2);
            MessageBoxW(NULL, failure, L"Could not restore data",
                        MB_OK | MB_ICONERROR);
            return false;
        }
    } else if (status == STORAGE_INCOMPATIBLE_VERSION) {
        wchar_t message[2048];
        ui_format_wide(
            message, sizeof(message) / sizeof(message[0]),
            L"The data file uses a different PitchRate data format.\n\n%ls\n\n"
            L"Open it with a compatible PitchRate version. The file has not been changed.",
            app->data_path);
        MessageBoxW(NULL, message, L"Incompatible data version",
                    MB_OK | MB_ICONERROR);
        return false;
    } else if (status != STORAGE_OK && status != STORAGE_NOT_FOUND) {
        wchar_t title[512];
        wchar_t message[2048];
        wchar_t detail[2048];
        ui_utf8_to_wide(error, detail,
                        sizeof(detail) / sizeof(detail[0]));
        ui_format_wide(title, sizeof(title) / sizeof(title[0]),
                       L"Could not load PitchRate data");
        ui_format_wide(message, sizeof(message) / sizeof(message[0]),
                       L"PitchRate could not load:\n%ls\n\n%ls\n\n"
                       L"Your file has not been changed.",
                       app->data_path, detail);
        MessageBoxW(NULL, message, title, MB_OK | MB_ICONERROR);
        return false;
    }

    INITCOMMONCONTROLSEX common = {
        .dwSize = sizeof(common),
        .dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES |
                 ICC_BAR_CLASSES,
    };
    InitCommonControlsEx(&common);

    int dpi = system_dpi();
    if (!ui_theme_init(&app->ui, dpi)) {
        MessageBoxW(NULL, L"Could not initialise the interface resources.",
                    L"PitchRate", MB_OK | MB_ICONERROR);
        return false;
    }
    app->icon_large = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP),
                                        IMAGE_ICON, ui_scale(&app->ui, 32),
                                        ui_scale(&app->ui, 32), LR_DEFAULTCOLOR);
    app->icon_small = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_APP),
                                        IMAGE_ICON, ui_scale(&app->ui, 16),
                                        ui_scale(&app->ui, 16), LR_DEFAULTCOLOR);
    ui_register_control_classes(instance);
    if (!page_register_class(instance) ||
        !main_window_register_class(instance)) {
        MessageBoxW(NULL, L"Could not register the application window.",
                    L"PitchRate", MB_OK | MB_ICONERROR);
        return false;
    }
    dialogs_register_classes(instance);

    app->main_hwnd = main_window_create(app);
    if (app->main_hwnd == NULL) {
        MessageBoxW(NULL, L"Could not create the application window.",
                    L"PitchRate", MB_OK | MB_ICONERROR);
        return false;
    }
    if (!create_pages(app)) {
        MessageBoxW(NULL, L"Could not create one of the application pages.",
                    L"PitchRate", MB_OK | MB_ICONERROR);
        return false;
    }
    if (!create_accelerators(app)) {
        MessageBoxW(NULL, L"Could not create keyboard shortcuts.",
                    L"PitchRate", MB_OK | MB_ICONERROR);
        return false;
    }
    app_layout_pages(app);
    app_navigate(app, PAGE_DASHBOARD);
    ShowWindow(app->main_hwnd, show_command);
    UpdateWindow(app->main_hwnd);
    SetForegroundWindow(app->main_hwnd);
    return true;
}

int app_run(App *app)
{
    MSG message = { 0 };
    int result;
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        if ((app->accelerators == NULL ||
             !TranslateAcceleratorW(app->main_hwnd, app->accelerators,
                                    &message)) &&
            !IsDialogMessageW(app->main_hwnd, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (result == -1) {
        return 1;
    }
    return (int)message.wParam;
}

void app_shutdown(App *app)
{
    if (app == NULL) {
        return;
    }
    for (int i = 0; i < PAGE_COUNT; ++i) {
        if (app->pages[i] != NULL) {
            page_destroy(app->pages[i]);
            free(app->pages[i]);
            app->pages[i] = NULL;
        }
    }
    if (app->main_hwnd != NULL && IsWindow(app->main_hwnd)) {
        DestroyWindow(app->main_hwnd);
        app->main_hwnd = NULL;
    }
    if (app->accelerators != NULL) {
        DestroyAcceleratorTable(app->accelerators);
        app->accelerators = NULL;
    }
    if (app->icon_large != NULL) {
        DestroyIcon(app->icon_large);
        app->icon_large = NULL;
    }
    if (app->icon_small != NULL) {
        DestroyIcon(app->icon_small);
        app->icon_small = NULL;
    }
    ui_theme_destroy(&app->ui);
    db_free(&app->db);
    if (app->instance_mutex != NULL) {
        CloseHandle(app->instance_mutex);
        app->instance_mutex = NULL;
    }
}
