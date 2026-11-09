#ifndef ELEVENLEDGER_UI_DIALOG_COMMON_H
#define ELEVENLEDGER_UI_DIALOG_COMMON_H

#include "app.h"
#include "core/rating.h"
#include "ui/theme.h"

#include <stddef.h>
#include <windows.h>

#define ELEVENLEDGER_IMPORT_SOURCE_DIALOG_CLASS L"ElevenLedgerImportSourceDialog"
#define ELEVENLEDGER_IMPORT_MATCH_DIALOG_CLASS L"ElevenLedgerImportMatchDialog"

typedef struct {
    bool home_valid;
    bool away_valid;
    bool summary_valid;
    double home_strength;
    double away_strength;
    double home_performance;
    double away_performance;
    double home_expected;
    double home_win_delta;
    double home_loss_delta;
} DialogRatingPreview;

void dialog_rating_preview_update(DialogRatingPreview *preview,
                                  const RatingPlayerInput *home,
                                  size_t home_count,
                                  const RatingPlayerInput *away,
                                  size_t away_count, bool neutral_venue);
void dialog_rating_preview_draw(App *app, HDC dc, RECT rect,
                                const DialogRatingPreview *preview);

LRESULT CALLBACK team_dialog_proc(HWND hwnd, UINT message,
                                  WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK player_dialog_proc(HWND hwnd, UINT message,
                                    WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK record_match_dialog_proc(HWND hwnd, UINT message,
                                          WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK match_details_dialog_proc(HWND hwnd, UINT message,
                                           WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK import_source_dialog_proc(HWND hwnd, UINT message,
                                           WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK import_match_dialog_proc(HWND hwnd, UINT message,
                                          WPARAM wparam, LPARAM lparam);

bool dialog_register_class(HINSTANCE instance, const wchar_t *name,
                           WNDPROC proc);
HWND dialog_create_shell(App *app, HWND owner, const wchar_t *class_name,
                         const wchar_t *title, int client_width,
                         int client_height, void *state);
void dialog_close(HWND dialog);
void dialog_draw_chrome(App *app, HDC dc, int width,
                        const wchar_t *title, const wchar_t *subtitle);
void dialog_draw_label(App *app, HDC dc, int x, int y,
                       const wchar_t *text);
void dialog_draw_error(App *app, HDC dc, RECT rect, const wchar_t *text);
HBRUSH dialog_control_color(UINT message, WPARAM wparam,
                            App *app);

#endif
