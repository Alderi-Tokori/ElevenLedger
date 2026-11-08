#include "ui/page.h"

#include "core/rating.h"
#include "ui/controls.h"
#include "ui/pages.h"
#include "ui/theme.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum {
    GUIDE_OPEN_FOLDER_ID = 1400,
};

typedef struct {
    Page base;
    HWND open_folder_button;
} GuidePage;

static void guide_layout(Page *page, int width, int height)
{
    (void)width;
    (void)height;
    GuidePage *guide = (GuidePage *)page;
    UiTheme *theme = &page->app->ui;
    int margin = ui_scale(theme, 28);
    page->card = page_card_rect(page);
    int width_button = ui_scale(theme, 152);
    int height_button = ui_scale(theme, 36);
    MoveWindow(guide->open_folder_button,
               page->width - margin - width_button,
               ui_scale(theme, 24), width_button, height_button, TRUE);
}

static void draw_step(HDC dc, UiTheme *theme, RECT area, int number,
                      const wchar_t *title, const wchar_t *body)
{
    int circle = ui_scale(theme, 26);
    RECT number_rect = {
        area.left, area.top + ui_scale(theme, 1),
        area.left + circle, area.top + circle,
    };
    ui_fill_round_rect(dc, number_rect, circle / 2, UI_COLOR_ACCENT_SOFT);
    wchar_t label[8];
    ui_format_wide(label, sizeof(label) / sizeof(label[0]), L"%d", number);
    ui_draw_text(dc, theme->font_small_bold, UI_COLOR_ACCENT, number_rect,
                 label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT title_rect = {
        area.left + circle + ui_scale(theme, 12),
        area.top - ui_scale(theme, 2),
        area.right,
        area.top + ui_scale(theme, 20),
    };
    ui_draw_text(dc, theme->font_body_bold, UI_COLOR_TEXT, title_rect, title,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT body_rect = {
        title_rect.left,
        title_rect.bottom + ui_scale(theme, 2),
        area.right,
        area.bottom,
    };
    if (body_rect.bottom > body_rect.top) {
        ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, body_rect, body,
                     DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
}

static void draw_example(Page *page, HDC dc, RECT panel)
{
    UiTheme *theme = &page->app->ui;
    RECT shadow = panel;
    OffsetRect(&shadow, 0, ui_scale(theme, 1));
    ui_fill_round_rect(dc, shadow, ui_scale(theme, 10), RGB(238, 242, 245));
    ui_stroke_round_rect(dc, panel, ui_scale(theme, 10),
                         RGB(248, 250, 251), RGB(231, 237, 242));

    RECT title = {
        panel.left + ui_scale(theme, 18),
        panel.top + ui_scale(theme, 14),
        panel.right - ui_scale(theme, 18),
        panel.top + ui_scale(theme, 38),
    };
    ui_draw_text(dc, theme->font_body_bold, UI_COLOR_TEXT, title,
                 L"Worked example",
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RatingPlayerInput home[] = {
        { .rating = 70.0, .minutes = 90, .performance = 7.0 },
        { .rating = 70.0, .minutes = 90, .performance = 6.0 },
    };
    RatingPlayerInput away[] = {
        { .rating = 70.0, .minutes = 90, .performance = 6.0 },
        { .rating = 70.0, .minutes = 90, .performance = 6.0 },
    };
    RatingMatchSummary summary;
    char error[128];
    double delta = 0.0;
    double after = 0.0;
    bool valid = rating_compute_match(&summary, home, 2, away, 2, 2, 0,
                                      false, error, sizeof(error)) &&
                 rating_player_change(&summary, true, 70.0, 90, 7.0,
                                      &delta, &after);
    if (!valid) {
        return;
    }
    double team_average =
        summary.home_context.weighted_performance /
        summary.home_context.total_participation;
    double performance_adjustment =
        (7.0 - team_average) * RATING_PERFORMANCE_WEIGHT;

    RECT text = {
        panel.left + ui_scale(theme, 18),
        panel.top + ui_scale(theme, 44),
        panel.right - ui_scale(theme, 18),
        panel.bottom - ui_scale(theme, 14),
    };
    wchar_t body[512];
    ui_format_wide(
        body, sizeof(body) / sizeof(body[0]),
        L"Two 70.0 teams in a normal home match. Home wins 2-0. Two home players complete 90 minutes: 7.0 and 6.0.\n\n"
        L"Expected home result: %.1f%%\n"
        L"Team result pool: %+.2f\n"
        L"Team average performance: %.2f\n"
        L"7.0 player result share: %+.2f\n"
        L"Performance adjustment: %+.2f\n"
        L"Player rating: 70.00 -> %.2f",
        summary.home_expected * 100.0, summary.home_team_delta,
        team_average, delta - performance_adjustment,
        performance_adjustment, after);
    ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, text, body,
                 DT_LEFT | DT_TOP | DT_WORDBREAK);
}

static void draw_data_panel(Page *page, HDC dc, RECT panel)
{
    UiTheme *theme = &page->app->ui;
    RECT shadow = panel;
    OffsetRect(&shadow, 0, ui_scale(theme, 1));
    ui_fill_round_rect(dc, shadow, ui_scale(theme, 10), RGB(238, 242, 245));
    ui_stroke_round_rect(dc, panel, ui_scale(theme, 10), UI_COLOR_CARD,
                         RGB(231, 237, 242));

    RECT title = {
        panel.left + ui_scale(theme, 18),
        panel.top + ui_scale(theme, 14),
        panel.right - ui_scale(theme, 18),
        panel.top + ui_scale(theme, 38),
    };
    ui_draw_text(dc, theme->font_body_bold, UI_COLOR_TEXT, title, L"Your data",
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT body = {
        panel.left + ui_scale(theme, 18),
        panel.top + ui_scale(theme, 43),
        panel.right - ui_scale(theme, 18),
        panel.bottom - ui_scale(theme, 12),
    };
    wchar_t text[1024];
    ui_format_wide(text, sizeof(text) / sizeof(text[0]),
                   L"Stored only on this PC and saved automatically after every change. "
                   L"A checksummed backup of the previous save is kept.\n\n%ls",
                   page->app->data_path);
    ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, body, text,
                 DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
}

static void guide_paint(Page *page, HDC dc)
{
    UiTheme *theme = &page->app->ui;
    page_draw_card(page, dc);

    int pad = ui_scale(theme, 24);
    int left = page->card.left + pad;
    int right = page->card.right - pad;
    int top = page->card.top + ui_scale(theme, 18);
    int bottom = page->card.bottom - pad;
    int content_width = right - left;
    int gap = ui_scale(theme, 24);
    int left_width = (content_width - gap) * 58 / 100;
    int right_left = left + left_width + gap;

    RECT heading = {
        left, top, right, top + ui_scale(theme, 30),
    };
    ui_draw_text(dc, theme->font_h2, UI_COLOR_TEXT, heading,
                 L"The rating model",
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT subheading = {
        left, heading.bottom - ui_scale(theme, 2), right,
        heading.bottom + ui_scale(theme, 22),
    };
    ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, subheading,
                 L"Simple, transparent and applied consistently to every appearance.",
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    int steps_top = subheading.bottom + ui_scale(theme, 16);
    int step_height = ui_scale(theme, 78);
    int circles = 4;
    int steps_height = step_height * circles;
    int available_steps = bottom - steps_top - ui_scale(theme, 104);
    if (steps_height > available_steps && available_steps > 0) {
        step_height = available_steps / circles;
        steps_height = step_height * circles;
    }
    const wchar_t *step_titles[] = {
        L"Team strength",
        L"Expected result",
        L"Shared team movement",
        L"Individual player movement",
    };
    const wchar_t *step_bodies[] = {
        L"The line-up rating is a minutes-weighted average of the players on the pitch. More time means more influence.",
        L"An Elo-style expectation compares the two line-ups before kick-off. Normal matches add a 4.0-point home advantage; neutral-venue matches apply none.",
        L"Team movement = 16 x (actual result - expected result) x goal-margin factor. A draw has margin 1.00; a 3+ goal win reaches 1.45. The winners' total gain equals the losers' total loss.",
        L"Each team shares its result movement across the line-up by minutes. Individual performance is measured against the team average and shifts points between team-mates. Every match conserves rating points, with no fixed upper or lower rating limit.",
    };
    for (int i = 0; i < circles; ++i) {
        RECT area = {
            left,
            steps_top + i * step_height,
            left + left_width,
            steps_top + i * step_height + step_height - ui_scale(theme, 6),
        };
        draw_step(dc, theme, area, i + 1, step_titles[i], step_bodies[i]);
    }

    int note_top = steps_top + steps_height + ui_scale(theme, 6);
    RECT note = { left, note_top, left + left_width, bottom };
    ui_draw_text(dc, theme->font_small_bold, UI_COLOR_TEXT, note,
                 L"Good to know",
                 DT_LEFT | DT_TOP | DT_SINGLELINE);
    RECT note_body = note;
    note_body.top += ui_scale(theme, 20);
    ui_draw_text(
        dc, theme->font_small, UI_COLOR_MUTED, note_body,
        L"Every match is replayed in date order whenever history changes. Editing or deleting a match or changing a starting rating therefore recalculates every later appearance. Editing keeps each existing line-up player with the club they represented, even after a transfer. Archived players remain in the record. New line-ups are limited to 1,320 player-minutes per team, and neutral venues receive no home advantage.",
        DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

    int data_height = ui_scale(theme, 156);
    RECT data_panel = {
        right_left, bottom - data_height, right, bottom,
    };
    RECT example_panel = {
        right_left, steps_top, right, data_panel.top - ui_scale(theme, 14),
    };
    if (example_panel.bottom > example_panel.top + ui_scale(theme, 80)) {
        draw_example(page, dc, example_panel);
    }
    draw_data_panel(page, dc, data_panel);
}

static void guide_refresh(Page *page)
{
    InvalidateRect(page->hwnd, NULL, FALSE);
}

static LRESULT guide_message(Page *page, UINT message, WPARAM wparam,
                             LPARAM lparam)
{
    (void)page;
    (void)lparam;
    if (message == WM_COMMAND && LOWORD(wparam) == GUIDE_OPEN_FOLDER_ID) {
        app_open_data_folder(page->app);
        return 0;
    }
    return 0;
}

static void guide_destroy(Page *page)
{
    (void)page;
}

Page *guide_page_create(App *app)
{
    GuidePage *guide = calloc(1, sizeof(*guide));
    if (guide == NULL) {
        return NULL;
    }
    if (!page_create(&guide->base, app, PAGE_GUIDE,
                     L"Rating guide",
                     L"The calculation behind every player rating, in plain language.",
                     NULL, guide_layout, guide_paint, guide_refresh,
                     guide_destroy, guide_message)) {
        free(guide);
        return NULL;
    }
    UiTheme *theme = &app->ui;
    guide->open_folder_button = ui_button_create(
        guide->base.hwnd, GUIDE_OPEN_FOLDER_ID, L"Open data folder",
        0, 0, 10, 10, UI_BUTTON_SECONDARY, theme->font_button);
    if (guide->open_folder_button == NULL) {
        page_destroy(&guide->base);
        free(guide);
        return NULL;
    }
    return &guide->base;
}
