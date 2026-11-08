#ifndef PITCHRATE_UI_MAIN_WINDOW_H
#define PITCHRATE_UI_MAIN_WINDOW_H

#include <windows.h>
#include <stdbool.h>

#define CMD_NAVIGATE_FIRST 40001
#define CMD_CREATE_ENTRY 40000

struct App;

bool main_window_register_class(HINSTANCE instance);
HWND main_window_create(struct App *app);
void main_window_draw_navigation(struct App *app, HDC dc);
int main_window_sidebar_width(const struct App *app);

#endif
