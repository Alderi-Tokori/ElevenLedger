#ifndef ELEVENLEDGER_UI_DIALOGS_H
#define ELEVENLEDGER_UI_DIALOGS_H

#include "app.h"

#include <stdint.h>

void dialogs_register_classes(HINSTANCE instance);

/* Pass team_id 0 for a new team. Returns the created or edited team ID. */
uint64_t dialog_edit_team(App *app, uint64_t team_id);
/* Pass player_id 0 for a new player. Returns the created or edited player ID. */
uint64_t dialog_edit_player(App *app, uint64_t player_id);
/*
 * Opens the Add player form for a fixed team, owned by another dialog.
 * Returns the new player ID, or the moved player's ID when the duplicate-name
 * flow reassigns an existing player to the team.
 */
uint64_t dialog_add_player_for_team(App *app, HWND owner, uint64_t team_id);
/* Returns the new match ID, or 0 when cancelled. */
uint64_t dialog_record_match(App *app);
/* Reads WhoScored page source from the clipboard and returns the confirmed new match ID. */
uint64_t dialog_import_match(App *app);
/* Returns true when the existing match was updated. */
bool dialog_edit_match(App *app, HWND owner, uint64_t match_id);
void dialog_match_details(App *app, uint64_t match_id);

#endif
