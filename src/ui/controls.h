#ifndef ELEVENLEDGER_UI_CONTROLS_H
#define ELEVENLEDGER_UI_CONTROLS_H

#include "ui/theme.h"

#include <windows.h>
#include <commctrl.h>
#include <stdint.h>

typedef enum {
    UI_BUTTON_PRIMARY = 0,
    UI_BUTTON_SECONDARY = 1,
    UI_BUTTON_DANGER = 2,
    UI_BUTTON_GHOST = 3,
    UI_BUTTON_SOFT = 4
} UiButtonStyle;

void ui_register_control_classes(HINSTANCE instance);

HWND ui_button_create(HWND parent, int id, const wchar_t *text,
                      int x, int y, int width, int height,
                      UiButtonStyle style, HFONT font);
void ui_button_set_text(HWND button, const wchar_t *text);

HWND ui_edit_create(HWND parent, int id, const wchar_t *text,
                    int x, int y, int width, int height, HFONT font,
                    int limit);
void ui_edit_set_cue(HWND edit, const wchar_t *cue);
void ui_edit_get_utf8(HWND edit, char *buffer, size_t buffer_size);
void ui_edit_set_utf8(HWND edit, const char *text);
void ui_edit_get_wide(HWND edit, wchar_t *buffer, size_t buffer_size);

HWND ui_combo_create(HWND parent, int id, int x, int y, int width, int height,
                     HFONT font);
int ui_combo_add_utf8(HWND combo, const char *text, uint64_t data);
int ui_combo_add_wide(HWND combo, const wchar_t *text, uint64_t data);
void ui_combo_select_data(HWND combo, uint64_t data);
uint64_t ui_combo_selected_data(HWND combo);

HWND ui_checkbox_create(HWND parent, int id, const wchar_t *text,
                        int x, int y, int width, int height, HFONT font);
void ui_checkbox_set_checked(HWND checkbox, bool checked);
bool ui_checkbox_get_checked(HWND checkbox);

HWND ui_list_create(HWND parent, int id, int x, int y, int width, int height,
                    HFONT font, HIMAGELIST row_images, bool sortable);
void ui_list_add_column(HWND list, int index, const wchar_t *title, int width);
void ui_list_set_percent_widths(HWND list, const int *percents, int count);
int ui_list_add_row(HWND list, const char **utf8_columns, int column_count,
                    LPARAM item_data);
int ui_list_insert_row(HWND list, int index, const char **utf8_columns,
                       int column_count, LPARAM item_data);
void ui_list_set_row(HWND list, int index, const char **utf8_columns,
                     int column_count, LPARAM item_data);
uint64_t ui_list_hash_columns(const char *const *utf8_columns,
                              int column_count);
LRESULT ui_list_custom_draw(NMLVCUSTOMDRAW *custom_draw, HIMAGELIST row_images);
LRESULT ui_list_custom_draw_rating(NMLVCUSTOMDRAW *custom_draw,
                                   const UiTheme *theme, int rating_column);
bool ui_list_selected_data(HWND list, LPARAM *out_data);
void ui_list_set_sort_indicator(HWND list, int column, bool ascending);
int ui_text_compare(const wchar_t *left, const wchar_t *right);

void ui_center_window(HWND window, HWND owner);

/* Runs a modal message loop for a custom top-level window. */
void ui_run_modal(HWND dialog, HWND owner, int default_command_id);

#endif
