#ifndef PITCHRATE_CORE_IMPORT_H
#define PITCHRATE_CORE_IMPORT_H

#include "core/database.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IMPORT_HTML_MAX (16u * 1024u * 1024u)
#define IMPORT_MAX_PLAYERS 40

typedef struct {
    char first_name[PERSON_NAME_MAX + 1];
    char last_name[PERSON_NAME_MAX + 1];
    int minutes;
    int sent_off;
} ImportedPlayer;

typedef struct {
    char date[MATCH_DATE_MAX + 1];
    char home_team[TEAM_NAME_MAX + 1];
    char away_team[TEAM_NAME_MAX + 1];
    int home_goals;
    int away_goals;
    /* Final-whistle minute, including stoppage time. */
    int length;
    ImportedPlayer *home_players;
    size_t home_player_count;
    ImportedPlayer *away_players;
    size_t away_player_count;
} ImportedMatch;

void imported_match_free(ImportedMatch *match);

/*
 * Reads WhoScored match page HTML and extracts the fixture and the players
 * who appeared. Unused substitutes are omitted. The caller owns the returned
 * player arrays and must call imported_match_free.
 */
bool imported_match_parse_html(const char *html, size_t length,
                               ImportedMatch *out,
                               char *error, size_t error_size);

typedef enum {
    IMPORT_PLAYER_CREATE = 0,
    IMPORT_PLAYER_USE_EXISTING
} ImportPlayerAction;

typedef struct {
    const char *first_name;
    const char *last_name;
    int minutes;
    int sent_off;
    double performance;
    ImportPlayerAction action;
    /* Required only for IMPORT_PLAYER_USE_EXISTING. */
    uint64_t existing_player_id;
} ImportPlayerPlan;

typedef struct {
    const char *date;
    const char *home_team_name;
    const char *away_team_name;
    int home_goals;
    int away_goals;
    bool neutral_venue;
    /* Final-whistle minute, including stoppage time. */
    int length;
    const ImportPlayerPlan *home_players;
    size_t home_player_count;
    const ImportPlayerPlan *away_players;
    size_t away_player_count;
} ImportPlan;

/*
 * Creates any missing clubs, creates new players or moves explicitly selected
 * existing players, and records the match. Domain validation runs before any
 * mutation. The caller must wrap this in an app transaction so allocation or
 * save failures can restore the complete previous database.
 */
bool db_import_match(Database *db, const ImportPlan *plan,
                     uint64_t *out_match_id,
                     char *error, size_t error_size);

#endif
