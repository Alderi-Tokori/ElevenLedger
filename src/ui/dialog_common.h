#ifndef PITCHRATE_UI_DIALOG_COMMON_H
#define PITCHRATE_UI_DIALOG_COMMON_H

#include "app.h"
#include "ui/theme.h"

#include <windows.h>

LRESULT CALLBACK team_dialog_proc(HWND hwnd, UINT message,
                                  WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK player_dialog_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK record_match_dialog_proc(HWND hwnd, UINT message,
                                          WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK match_details_dialog_proc(HWND hwnd, UINT message,
                                           WPARAM wparam, LPARAM lparam);

bool dialog_register_class(HINSTANCE instance, const wchar_t *name,
                           WNDPROC proc);
HWND dialog_create_shell(App *app, const wchar_t *class_name,
                         const wchar_t *title, int client_width,
                         int client_height, void *state);
void dialog_draw_chrome(App *app, HDC dc, int width,
                        const wchar_t *title, const wchar_t *subtitle);
void dialog_draw_label(App *app, HDC dc, int x, int y,
                       const wchar_t *text);
void dialog_draw_error(App *app, HDC dc, RECT rect, const wchar_t *text);
HBRUSH dialog_control_color(UINT message, WPARAM wparam,
                            App *app);

#endif
