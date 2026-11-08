#ifndef PITCHRATE_UI_PAGE_H
#define PITCHRATE_UI_PAGE_H

#include "app.h"

#include <windows.h>

struct Page {
    App *app;
    HWND hwnd;
    PageId id;
    const wchar_t *title;
    const wchar_t *subtitle;
    int width;
    int height;
    RECT card;
    WORD create_command;

    void (*on_create)(Page *page);
    void (*on_layout)(Page *page, int width, int height);
    void (*on_paint)(Page *page, HDC dc);
    void (*on_refresh)(Page *page);
    void (*on_destroy)(Page *page);
    LRESULT (*on_message)(Page *page, UINT message, WPARAM wparam,
                          LPARAM lparam);
};

bool page_register_class(HINSTANCE instance);
bool page_create(Page *page, App *app, PageId id,
                 const wchar_t *title, const wchar_t *subtitle,
                 void (*on_create)(Page *page),
                 void (*on_layout)(Page *page, int width, int height),
                 void (*on_paint)(Page *page, HDC dc),
                 void (*on_refresh)(Page *page),
                 void (*on_destroy)(Page *page),
                 LRESULT (*on_message)(Page *page, UINT message,
                                       WPARAM wparam, LPARAM lparam));
void page_destroy(Page *page);

void page_draw_shell(Page *page, HDC dc);
void page_draw_card(Page *page, HDC dc);
RECT page_card_rect(const Page *page);
RECT page_list_rect(const Page *page);
void page_draw_empty_state(Page *page, HDC dc, const wchar_t *title,
                           const wchar_t *subtitle);
void page_draw_toolbar_label(Page *page, HDC dc, int x, int y,
                             const wchar_t *text);

#endif
