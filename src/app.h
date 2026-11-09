#ifndef ELEVENLEDGER_APP_H
#define ELEVENLEDGER_APP_H

#include "core/database.h"
#include "ui/theme.h"

#include <windows.h>
#include <commctrl.h>

typedef enum {
    PAGE_DASHBOARD = 0,
    PAGE_PLAYERS,
    PAGE_TEAMS,
    PAGE_MATCHES,
    PAGE_GUIDE,
    PAGE_COUNT
} PageId;

typedef struct Page Page;
typedef struct SaveJob SaveJob;

#define APP_WM_SAVE_COMPLETE (WM_APP + 1)

typedef struct App {
    HINSTANCE instance;
    HWND main_hwnd;
    HACCEL accelerators;
    HANDLE instance_mutex;
    Database db;
    Database pending_backup;
    SaveJob *save_job;
    uint64_t next_save_job_id;
    bool save_pending;
    bool close_when_saved;
    UiTheme ui;
    Page *pages[PAGE_COUNT];
    PageId active_page;
    int hovered_nav;
    bool navigating;
    wchar_t data_path[1024];
    wchar_t data_directory[1024];
    HICON icon_large;
    HICON icon_small;
} App;

bool app_initialize(App *app, HINSTANCE instance, const wchar_t *data_path,
                    int show_command);
int app_run(App *app);
void app_shutdown(App *app);

void app_navigate(App *app, PageId page);
void app_layout_pages(App *app);
void app_refresh_active(App *app);
bool app_tx_begin(App *app, Database *backup);
bool app_tx_commit(App *app, Database *backup);
void app_tx_abort(App *app, Database *backup);
void app_handle_save_complete(App *app, WPARAM id);
void app_show_error(App *app, const wchar_t *title, const wchar_t *format, ...);
void app_show_error_utf8(App *app, const wchar_t *title, const char *message);
void app_open_data_folder(App *app);

#endif
