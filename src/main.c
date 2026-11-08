#include "app.h"
#include "ui/theme.h"

#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static void enable_system_dpi_awareness(void)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32 == NULL) {
        return;
    }
    typedef BOOL(WINAPI *SetDpiAwarenessContextFn)(HANDLE context);
    FARPROC address = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (address != NULL) {
        SetDpiAwarenessContextFn set_context = NULL;
        memcpy(&set_context, &address, sizeof(set_context));
        /* DPI_AWARENESS_CONTEXT_SYSTEM_AWARE */
        if (set_context((HANDLE)(INT_PTR)-2)) {
            return;
        }
    }
    typedef BOOL(WINAPI *SetDpiAwareFn)(void);
    address = GetProcAddress(user32, "SetProcessDPIAware");
    if (address != NULL) {
        SetDpiAwareFn set_aware = NULL;
        memcpy(&set_aware, &address, sizeof(set_aware));
        set_aware();
    }
}

static void show_usage(HWND owner)
{
    MessageBoxW(
        owner,
        L"PitchRate - Football Rating Calculator\n\n"
        L"Usage:\n"
        L"  PitchRate.exe [--data <path-to-data-file>]\n\n"
        L"Options:\n"
        L"  --data <path>   Use a specific local data file.\n"
        L"  --help          Show this help.\n"
        L"  --version       Show the version.\n\n"
        L"Data is stored locally. Use Ctrl+1 through Ctrl+5 to switch pages.",
        L"PitchRate", MB_OK | MB_ICONINFORMATION);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous_instance,
                   LPSTR command_line, int show_command)
{
    (void)previous_instance;
    (void)command_line;

    enable_system_dpi_awareness();

    int argc = 0;
    LPWSTR *arguments = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (arguments == NULL) {
        MessageBoxW(NULL, L"Could not read the command line.", L"PitchRate",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    const wchar_t *data_path = NULL;
    bool show_help = false;
    bool show_version = false;
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(arguments[i], L"--data") == 0) {
            if (i + 1 >= argc) {
                MessageBoxW(NULL, L"--data requires a file path.",
                            L"PitchRate", MB_OK | MB_ICONERROR);
                LocalFree(arguments);
                return 2;
            }
            data_path = arguments[++i];
        } else if (wcsncmp(arguments[i], L"--data=", 7) == 0) {
            data_path = arguments[i] + 7;
        } else if (wcscmp(arguments[i], L"--help") == 0 ||
                   wcscmp(arguments[i], L"-h") == 0) {
            show_help = true;
        } else if (wcscmp(arguments[i], L"--version") == 0) {
            show_version = true;
        } else {
            wchar_t message[512];
            _snwprintf(message, sizeof(message) / sizeof(message[0]) - 1,
                       L"Unknown option: %ls", arguments[i]);
            message[sizeof(message) / sizeof(message[0]) - 1] = L'\0';
            MessageBoxW(NULL, message, L"PitchRate",
                        MB_OK | MB_ICONERROR);
            LocalFree(arguments);
            return 2;
        }
    }

    if (show_help) {
        show_usage(NULL);
        LocalFree(arguments);
        return 0;
    }
    if (show_version) {
        MessageBoxW(NULL, L"PitchRate v" PITCHRATE_VERSION, L"PitchRate",
                    MB_OK | MB_ICONINFORMATION);
        LocalFree(arguments);
        return 0;
    }

    App app;
    memset(&app, 0, sizeof(app));
    bool initialized = app_initialize(&app, instance, data_path, show_command);
    int exit_code = 1;
    if (initialized) {
        exit_code = app_run(&app);
    }
    app_shutdown(&app);
    LocalFree(arguments);
    return exit_code;
}
