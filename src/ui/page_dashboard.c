#include "ui/page.h"

#include "ui/pages.h"
#include "ui/theme.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

typedef struct {
    Page base;
} DashboardPage;

static void format_count(wchar_t *buffer, size_t count, uint64_t value)
{
    wchar_t digits[32];
    ui_format_wide(digits, sizeof(digits) / sizeof(digits[0]), L"%llu",
                   (unsigned long long)value);
    size_t length = wcslen(digits);
    size_t written = 0;
    for (size_t i = 0; i < length && written + 1 < count; ++i) {
        if (i > 0 && ((length - i) % 3) == 0) {
            buffer[written++] = L',';
        }
        buffer[written++] = digits[i];
    }
    buffer[written] = L'\0';
}

static void player_name(const Player *player, wchar_t *buffer, size_t count)
{
    wchar_t first[128];
    wchar_t last[128];
    ui_utf8_to_wide(player->first_name, first,
                    sizeof(first) / sizeof(first[0]));
    ui_utf8_to_wide(player->last_name, last,
                    sizeof(last) / sizeof(last[0]));
    if (first[0] != L'\0' && last[0] != L'\0') {
        ui_format_wide(buffer, count, L"%ls %ls", first, last);
    } else if (first[0] != L'\0') {
        ui_format_wide(buffer, count, L"%ls", first);
    } else {
        ui_format_wide(buffer, count, L"%ls", last);
    }
}

static const wchar_t *team_compact(const Team *team, wchar_t *buffer,
                                   size_t count)
{
    if (team == NULL) {
        return L"Unknown";
    }
    const char *source = team->short_name[0] != '\0' ? team->short_name
                                                      : team->name;
    if (!ui_utf8_to_wide(source, buffer, count)) {
        buffer[0] = L'\0';
    }
    return buffer;
}

static void draw_panel(HDC dc, UiTheme *theme, RECT rect)
{
    RECT shadow = rect;
    OffsetRect(&shadow, 0, ui_scale(theme, 2));
    ui_fill_round_rect(dc, shadow, ui_scale(theme, 11),
                       RGB(232, 237, 242));
    ui_stroke_round_rect(dc, rect, ui_scale(theme, 11), UI_COLOR_CARD,
                         RGB(233, 238, 243));
}

static void draw_panel_title(HDC dc, UiTheme *theme, RECT panel,
                             const wchar_t *title, const wchar_t *caption)
{
    RECT title_rect = {
        panel.left + ui_scale(theme, 20),
        panel.top + ui_scale(theme, 16),
        panel.right - ui_scale(theme, 20),
        panel.top + ui_scale(theme, 40),
    };
    ui_draw_text(dc, theme->font_body_bold, UI_COLOR_TEXT, title_rect, title,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (caption != NULL) {
        RECT caption_rect = title_rect;
        caption_rect.top = title_rect.top - ui_scale(theme, 1);
        ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, caption_rect,
                     caption,
                     DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

static void draw_stat_card(HDC dc, UiTheme *theme, RECT card,
                           COLORREF accent, const wchar_t *value,
                           const wchar_t *label, const wchar_t *detail)
{
    draw_panel(dc, theme, card);
    RECT accent_bar = {
        card.left, card.top + ui_scale(theme, 20),
        card.left + ui_scale(theme, 3), card.top + ui_scale(theme, 45),
    };
    ui_fill_round_rect(dc, accent_bar, ui_scale(theme, 2), accent);

    RECT label_rect = {
        card.left + ui_scale(theme, 18),
        card.top + ui_scale(theme, 15),
        card.right - ui_scale(theme, 14),
        card.top + ui_scale(theme, 35),
    };
    ui_draw_text(dc, theme->font_small_bold, UI_COLOR_MUTED, label_rect, label,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT value_rect = {
        card.left + ui_scale(theme, 18),
        card.top + ui_scale(theme, 36),
        card.right - ui_scale(theme, 14),
        card.top + ui_scale(theme, 76),
    };
    ui_draw_text(dc, theme->font_stat, UI_COLOR_TEXT, value_rect, value,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (detail != NULL) {
        RECT detail_rect = {
            card.left + ui_scale(theme, 18),
            card.top + ui_scale(theme, 76),
            card.right - ui_scale(theme, 14),
            card.bottom - ui_scale(theme, 6),
        };
        ui_draw_text(dc, theme->font_small, UI_COLOR_FAINT, detail_rect,
                     detail, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

static void draw_top_players(Page *page, HDC dc, RECT panel)
{
    UiTheme *theme = &page->app->ui;
    Database *db = &page->app->db;
    draw_panel(dc, theme, panel);
    draw_panel_title(dc, theme, panel, L"Top rated players", L"Current rating");
    int row_top = panel.top + ui_scale(theme, 48);
    int row_height = ui_scale(theme, 36);
    size_t max_rows = 5;
    if (row_height > 0) {
        int available = panel.bottom - row_top - ui_scale(theme, 8);
        size_t fitting = available > 0
                             ? (size_t)(available / row_height)
                             : 0;
        if (max_rows > fitting) {
            max_rows = fitting;
        }
    }

    const Player *top[5] = { 0 };
    size_t found = 0;
    for (size_t rank = 0; rank < max_rows; ++rank) {
        const Player *best = NULL;
        for (size_t i = 0; i < db->player_count; ++i) {
            const Player *candidate = &db->players[i];
            if (!candidate->active) {
                continue;
            }
            bool already = false;
            for (size_t j = 0; j < found; ++j) {
                if (top[j] == candidate) {
                    already = true;
                    break;
                }
            }
            if (already) {
                continue;
            }
            if (best == NULL ||
                candidate->current_rating > best->current_rating ||
                (candidate->current_rating == best->current_rating &&
                 candidate->id < best->id)) {
                best = candidate;
            }
        }
        if (best == NULL) {
            break;
        }
        top[found++] = best;
    }

    if (found == 0) {
        RECT empty = {
            panel.left + ui_scale(theme, 20),
            row_top,
            panel.right - ui_scale(theme, 20),
            panel.bottom - ui_scale(theme, 12),
        };
        ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, empty,
                     L"Add players to start building your leaderboard.",
                     DT_LEFT | DT_TOP | DT_WORDBREAK);
        return;
    }

    for (size_t i = 0; i < found; ++i) {
        const Player *player = top[i];
        RECT row = {
            panel.left + ui_scale(theme, 20),
            row_top + (int)i * row_height,
            panel.right - ui_scale(theme, 20),
            row_top + (int)(i + 1) * row_height,
        };
        if (i > 0) {
            ui_draw_line(dc, row.left, row.top, row.right, row.top,
                         RGB(241, 244, 247));
        }

        RECT rank_rect = {
            row.left,
            row.top,
            row.left + ui_scale(theme, 24),
            row.bottom,
        };
        wchar_t rank[8];
        ui_format_wide(rank, sizeof(rank) / sizeof(rank[0]), L"%zu", i + 1);
        ui_draw_text(dc, theme->font_small_bold, UI_COLOR_FAINT, rank_rect,
                     rank, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        wchar_t name[160];
        player_name(player, name, sizeof(name) / sizeof(name[0]));
        int row_width = row.right - row.left;
        RECT name_rect = {
            row.left + ui_scale(theme, 26),
            row.top,
            row.left + row_width * 46 / 100,
            row.bottom,
        };
        ui_draw_text(dc, theme->font_body_bold, UI_COLOR_TEXT, name_rect, name,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        const Team *team = db_find_team(db, player->team_id);
        wchar_t team_name[160];
        if (team != NULL) {
            ui_utf8_to_wide(team->name, team_name,
                            sizeof(team_name) / sizeof(team_name[0]));
        } else {
            ui_format_wide(team_name,
                           sizeof(team_name) / sizeof(team_name[0]),
                           L"-");
        }
        RECT team_rect = {
            row.left + row_width * 48 / 100,
            row.top,
            row.right - ui_scale(theme, 62),
            row.bottom,
        };
        ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, team_rect, team_name,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT badge = {
            row.right - ui_scale(theme, 50),
            row.top + ui_scale(theme, 5),
            row.right,
            row.bottom - ui_scale(theme, 5),
        };
        ui_draw_rating_badge(dc, theme, badge, player->current_rating);
    }
}

static void draw_recent_matches(Page *page, HDC dc, RECT panel)
{
    UiTheme *theme = &page->app->ui;
    Database *db = &page->app->db;
    draw_panel(dc, theme, panel);
    draw_panel_title(dc, theme, panel, L"Recent results", L"Latest first");
    int row_top = panel.top + ui_scale(theme, 48);
    int row_height = ui_scale(theme, 48);
    size_t max_rows = 4;
    if (row_height > 0) {
        int available = panel.bottom - row_top - ui_scale(theme, 8);
        size_t fitting = available > 0
                             ? (size_t)(available / row_height)
                             : 0;
        if (max_rows > fitting) {
            max_rows = fitting;
        }
    }

    if (db->match_count == 0) {
        RECT empty = {
            panel.left + ui_scale(theme, 20),
            row_top,
            panel.right - ui_scale(theme, 20),
            panel.bottom - ui_scale(theme, 12),
        };
        ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, empty,
                     L"Record a match to see scorelines and rating movement.",
                     DT_LEFT | DT_TOP | DT_WORDBREAK);
        return;
    }

    size_t shown = db->match_count < max_rows ? db->match_count : max_rows;
    if (shown == 0) {
        return;
    }
    for (size_t offset = 0; offset < shown; ++offset) {
        const Match *match = &db->matches[db->match_count - 1 - offset];
        RECT row = {
            panel.left + ui_scale(theme, 20),
            row_top + (int)offset * row_height,
            panel.right - ui_scale(theme, 20),
            row_top + (int)(offset + 1) * row_height,
        };
        if (offset > 0) {
            ui_draw_line(dc, row.left, row.top, row.right, row.top,
                         RGB(241, 244, 247));
        }

        wchar_t home[64];
        wchar_t away[64];
        const Team *home_team = db_find_team(db, match->home_team_id);
        const Team *away_team = db_find_team(db, match->away_team_id);
        team_compact(home_team, home, sizeof(home) / sizeof(home[0]));
        team_compact(away_team, away, sizeof(away) / sizeof(away[0]));

        RECT fixture = {
            row.left,
            row.top + ui_scale(theme, 5),
            row.right,
            row.top + ui_scale(theme, 26),
        };
        wchar_t fixture_text[192];
        ui_format_wide(fixture_text,
                       sizeof(fixture_text) / sizeof(fixture_text[0]),
                       L"%ls  %d - %d  %ls", home, match->home_goals,
                       match->away_goals, away);
        ui_draw_text(dc, theme->font_body_bold, UI_COLOR_TEXT, fixture,
                     fixture_text,
                     DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT detail = fixture;
        detail.top = row.top + ui_scale(theme, 25);
        detail.bottom = row.bottom;
        wchar_t detail_text[160];
        const wchar_t *result = match->home_goals > match->away_goals
                                    ? L"Home win"
                                    : (match->home_goals < match->away_goals
                                           ? L"Away win"
                                           : L"Draw");
        wchar_t date[16];
        ui_utf8_to_wide(match->date, date,
                        sizeof(date) / sizeof(date[0]));
        if (match->neutral_venue) {
            ui_format_wide(detail_text,
                           sizeof(detail_text) / sizeof(detail_text[0]),
                           L"%ls  |  %ls  |  Neutral venue", date, result);
        } else {
            ui_format_wide(detail_text,
                           sizeof(detail_text) / sizeof(detail_text[0]),
                           L"%ls  |  %ls", date, result);
        }
        ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, detail,
                     detail_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                                      DT_END_ELLIPSIS);
    }
}

static void draw_model_panel(Page *page, HDC dc, RECT panel)
{
    UiTheme *theme = &page->app->ui;
    draw_panel(dc, theme, panel);
    draw_panel_title(dc, theme, panel, L"How ratings work",
                     L"Every match is applied chronologically");

    RECT intro = {
        panel.left + ui_scale(theme, 20),
        panel.top + ui_scale(theme, 45),
        panel.right - ui_scale(theme, 20),
        panel.top + ui_scale(theme, 66),
    };
    ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, intro,
                 L"The team result sets the shared movement. Minutes and personal performance shape each player's share.",
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    int content_top = panel.top + ui_scale(theme, 82);
    int content_bottom = panel.bottom - ui_scale(theme, 16);
    int available = panel.right - panel.left - ui_scale(theme, 40);
    int column_gap = ui_scale(theme, 24);
    int column_width = (available - column_gap * 2) / 3;

    static const wchar_t *titles[3] = {
        L"1  Team result",
        L"2  Minutes played",
        L"3  Performance",
    };
    static const wchar_t *bodies[3] = {
        L"Win, draw and loss are shared by the whole line-up.",
        L"Impact scales with time on the pitch, from cameos to full matches.",
        L"A 1-10 match rating adds an individual adjustment.",
    };
    COLORREF colors[3] = {
        UI_COLOR_ACCENT,
        UI_COLOR_BLUE,
        UI_COLOR_AMBER,
    };
    for (int i = 0; i < 3; ++i) {
        int left = panel.left + ui_scale(theme, 20) +
                   i * (column_width + column_gap);
        RECT accent = {
            left, content_top,
            left + ui_scale(theme, 3), content_top + ui_scale(theme, 20),
        };
        ui_fill_round_rect(dc, accent, ui_scale(theme, 2), colors[i]);
        RECT title_rect = {
            left + ui_scale(theme, 12),
            content_top - ui_scale(theme, 2),
            left + column_width,
            content_top + ui_scale(theme, 22),
        };
        ui_draw_text(dc, theme->font_small_bold, UI_COLOR_TEXT, title_rect,
                     titles[i], DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        RECT body_rect = {
            left + ui_scale(theme, 12),
            content_top + ui_scale(theme, 22),
            left + column_width,
            content_bottom,
        };
        if (body_rect.bottom > body_rect.top) {
            ui_draw_text(dc, theme->font_small, UI_COLOR_MUTED, body_rect,
                         bodies[i], DT_LEFT | DT_TOP | DT_WORDBREAK);
        }
    }
}

static void dashboard_paint(Page *page, HDC dc)
{
    UiTheme *theme = &page->app->ui;
    Database *db = &page->app->db;

    int margin = ui_scale(theme, 28);
    int content_left = margin;
    int content_right = page->width - margin;
    int content_width = content_right - content_left;
    int top = ui_scale(theme, 104);
    int bottom = page->height - margin;

    int stat_gap = ui_scale(theme, 14);
    int stat_height = ui_scale(theme, 102);
    int stat_width = (content_width - stat_gap * 3) / 4;

    size_t active_players = 0;
    uint64_t minutes = 0;
    for (size_t i = 0; i < db->player_count; ++i) {
        if (db->players[i].active) {
            active_players++;
        }
        minutes += db->players[i].minutes_played;
    }
    wchar_t value[64];
    wchar_t detail[64];

    format_count(value, sizeof(value) / sizeof(value[0]),
                 (uint64_t)active_players);
    ui_format_wide(detail, sizeof(detail) / sizeof(detail[0]), L"%zu total",
                   db->player_count);
    draw_stat_card(dc, theme,
                   (RECT){ content_left, top,
                           content_left + stat_width, top + stat_height },
                   UI_COLOR_ACCENT, value, L"Active players", detail);

    format_count(value, sizeof(value) / sizeof(value[0]),
                 (uint64_t)db->team_count);
    ui_format_wide(detail, sizeof(detail) / sizeof(detail[0]),
                   db->team_count == 1 ? L"Club" : L"Clubs");
    int second_left = content_left + stat_width + stat_gap;
    draw_stat_card(dc, theme,
                   (RECT){ second_left, top,
                           second_left + stat_width, top + stat_height },
                   UI_COLOR_BLUE, value, L"Teams", detail);

    format_count(value, sizeof(value) / sizeof(value[0]),
                 (uint64_t)db->match_count);
    ui_format_wide(detail, sizeof(detail) / sizeof(detail[0]),
                   L"With rating history");
    int third_left = second_left + stat_width + stat_gap;
    draw_stat_card(dc, theme,
                   (RECT){ third_left, top,
                           third_left + stat_width, top + stat_height },
                   UI_COLOR_AMBER, value, L"Matches", detail);

    format_count(value, sizeof(value) / sizeof(value[0]), minutes);
    ui_format_wide(detail, sizeof(detail) / sizeof(detail[0]),
                   L"Minutes tracked");
    int fourth_left = third_left + stat_width + stat_gap;
    draw_stat_card(dc, theme,
                   (RECT){ fourth_left, top, content_right,
                           top + stat_height },
                   UI_COLOR_DANGER, value, L"Minutes", detail);

    int middle_top = top + stat_height + ui_scale(theme, 16);
    int middle_bottom = middle_top + ui_scale(theme, 246);
    if (middle_bottom > bottom) {
        middle_bottom = bottom;
    }
    int top_width = (content_width - ui_scale(theme, 16)) * 57 / 100;
    RECT top_panel = { content_left, middle_top,
                       content_left + top_width, middle_bottom };
    RECT recent_panel = { top_panel.right + ui_scale(theme, 16), middle_top,
                          content_right, middle_bottom };
    draw_top_players(page, dc, top_panel);
    draw_recent_matches(page, dc, recent_panel);

    int model_top = middle_bottom + ui_scale(theme, 16);
    RECT model_panel = { content_left, model_top, content_right, bottom };
    if (model_panel.bottom > model_panel.top + ui_scale(theme, 100)) {
        draw_model_panel(page, dc, model_panel);
    }
}

static void dashboard_layout(Page *page, int width, int height)
{
    (void)page;
    (void)width;
    (void)height;
}

static LRESULT dashboard_message(Page *page, UINT message, WPARAM wparam,
                                 LPARAM lparam)
{
    (void)page;
    (void)message;
    (void)wparam;
    (void)lparam;
    return 0;
}

static void dashboard_refresh(Page *page)
{
    InvalidateRect(page->hwnd, NULL, FALSE);
}

static void dashboard_destroy(Page *page)
{
    (void)page;
}

Page *dashboard_page_create(App *app)
{
    DashboardPage *dashboard = calloc(1, sizeof(*dashboard));
    if (dashboard == NULL) {
        return NULL;
    }
    if (!page_create(&dashboard->base, app, PAGE_DASHBOARD,
                     L"Dashboard",
                     L"A live snapshot of your squads, results and rating movement.",
                     NULL, dashboard_layout, dashboard_paint,
                     dashboard_refresh, dashboard_destroy,
                     dashboard_message)) {
        free(dashboard);
        return NULL;
    }
    return &dashboard->base;
}
