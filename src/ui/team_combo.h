#ifndef ELEVENLEDGER_UI_TEAM_COMBO_H
#define ELEVENLEDGER_UI_TEAM_COMBO_H

#include "core/database.h"
#include "ui/theme.h"

#include <windows.h>

/*
 * Visual state for a team dropdown created with
 * ui_combo_create_owner_draw. Keep one instance per combo for the lifetime
 * of the control and route WM_DRAWITEM through ui_team_combo_draw_item.
 */
typedef struct {
    HWND combo;
    const Database *db;
    const UiTheme *theme;
    int hover_index;
    bool tracking;
} UiTeamCombo;

void ui_team_combo_init(UiTeamCombo *view, HWND combo, const Database *db,
                        const UiTheme *theme);
bool ui_team_combo_draw_item(UiTeamCombo *view, DRAWITEMSTRUCT *item);
void ui_team_combo_reset_hover(UiTeamCombo *view);

/* Adds every club in the database to the combo, sorted by name. */
void ui_team_combo_add_teams(HWND combo, const Database *db);

#endif
