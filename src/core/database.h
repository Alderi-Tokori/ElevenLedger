#ifndef PITCHRATE_CORE_DATABASE_H
#define PITCHRATE_CORE_DATABASE_H

#include "core/rating.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Mutating functions expect a valid Database. The desktop application wraps
 * every user mutation in an app transaction snapshot so that a rejected
 * save can restore the complete previous state.
 */

#define TEAM_NAME_MAX 63
#define TEAM_SHORT_MAX 15
#define TEAM_CITY_MAX 63
#define PERSON_NAME_MAX 47
#define MATCH_DATE_MAX 10

typedef struct {
    uint64_t id;
    char name[TEAM_NAME_MAX + 1];
    char short_name[TEAM_SHORT_MAX + 1];
    char city[TEAM_CITY_MAX + 1];
} Team;

typedef struct {
    uint64_t id;
    uint64_t team_id;
    char first_name[PERSON_NAME_MAX + 1];
    char last_name[PERSON_NAME_MAX + 1];
    double base_rating;
    bool active;
    /* Derived by db_recompute_ratings. */
    double current_rating;
    uint32_t matches_played;
    uint32_t minutes_played;
} Player;

typedef struct {
    uint64_t id;
    char date[MATCH_DATE_MAX + 1];
    uint64_t home_team_id;
    uint64_t away_team_id;
    int home_goals;
    int away_goals;
    bool neutral_venue;
    /* Elapsed final-whistle minute, including stoppage time. */
    int length;
    /* Derived team-level details, retained for history and previews. */
    RatingMatchSummary rating;
} Match;

typedef struct {
    uint64_t match_id;
    uint64_t player_id;
    uint64_t team_id;
    int minutes;
    double performance;
    /* Sending-off minute, or 0 when the player completed his appearance. */
    int sent_off;
    /* Derived rating snapshots for history and auditability. */
    double rating_before;
    double rating_delta;
    double rating_after;
} Appearance;

typedef struct {
    Team *teams;
    size_t team_count;
    size_t team_capacity;

    Player *players;
    size_t player_count;
    size_t player_capacity;

    Match *matches;
    size_t match_count;
    size_t match_capacity;

    Appearance *appearances;
    size_t appearance_count;
    size_t appearance_capacity;

    uint64_t next_team_id;
    uint64_t next_player_id;
    uint64_t next_match_id;
} Database;

typedef struct {
    uint64_t player_id;
    int minutes;
    double performance;
    /* Sending-off minute, or 0 when the player was not sent off. */
    int sent_off;
} MatchPlayerInput;

typedef struct {
    const char *date;
    uint64_t home_team_id;
    uint64_t away_team_id;
    int home_goals;
    int away_goals;
    bool neutral_venue;
    int length;
    const MatchPlayerInput *players;
    size_t player_count;
} MatchInput;

typedef struct {
    size_t active_players;
    size_t roster_size;
    double average_rating;
    int wins;
    int draws;
    int losses;
    size_t matches;
} TeamStats;

void db_init(Database *db);
void db_free(Database *db);
bool db_clone(Database *destination, const Database *source);

const Team *db_find_team(const Database *db, uint64_t id);
/* Trims the name and matches existing clubs ASCII case-insensitively. */
const Team *db_find_team_by_name(const Database *db, const char *name);
const Player *db_find_player(const Database *db, uint64_t id);
const Match *db_find_match(const Database *db, uint64_t id);

/* Highest player ID, i.e. the most recently created player. */
const Player *db_last_added_player(const Database *db);

/*
 * Finds the earliest-created player whose trimmed full name matches the
 * supplied names (ASCII case-insensitive). When out_count is not NULL it
 * receives the total number of players using that name.
 */
const Player *db_find_player_by_name(const Database *db,
                                     const char *first_name,
                                     const char *last_name,
                                     size_t *out_count);

/*
 * Finds an active player with the supplied trimmed full name in one club.
 * Returns NULL when that club has no matching player.
 */
const Player *db_find_active_player_by_name_in_team(
    const Database *db, const char *first_name, const char *last_name,
    uint64_t team_id);

bool db_add_team(Database *db, const char *name, const char *short_name,
                 const char *city, uint64_t *out_id,
                 char *error, size_t error_size);
bool db_update_team(Database *db, uint64_t id, const char *name,
                    const char *short_name, const char *city,
                    char *error, size_t error_size);
bool db_delete_team(Database *db, uint64_t id,
                    char *error, size_t error_size);

bool db_add_player(Database *db, uint64_t team_id,
                   const char *first_name, const char *last_name,
                   double base_rating, bool active,
                   uint64_t *out_id, char *error, size_t error_size);
bool db_update_player(Database *db, uint64_t id, uint64_t team_id,
                      const char *first_name, const char *last_name,
                      double base_rating, bool active,
                      char *error, size_t error_size);
bool db_set_player_active(Database *db, uint64_t id, bool active,
                          char *error, size_t error_size);
bool db_delete_player(Database *db, uint64_t id,
                      char *error, size_t error_size);

bool db_add_match(Database *db, const MatchInput *input, uint64_t *out_id,
                  char *error, size_t error_size);
/*
 * Replaces the fixture and line-up of an existing match while preserving its
 * ID. Players already in the match keep the team stored on their appearance,
 * so a player who has since transferred or been archived still counts for the
 * club they represented. A player who was not in the original line-up must be
 * active and currently belong to one of the two selected teams. Ratings are
 * replayed after the change.
 */
bool db_update_match(Database *db, uint64_t id, const MatchInput *input,
                     char *error, size_t error_size);
bool db_delete_match(Database *db, uint64_t id,
                     char *error, size_t error_size);

/*
 * Replays every match in chronological order from each player's base rating.
 * This keeps the whole ledger consistent after any historical edit or delete.
 * Mutations that change history should be performed inside the UI transaction
 * wrapper so that any failure can restore the complete prior database.
 */
bool db_recompute_ratings(Database *db, char *error, size_t error_size);

/*
 * Computes each player's rating immediately before the chronological match
 * slot identified by (date, match_id). Earlier dates are included, as are
 * same-date matches with a lower ID; the identified match and everything after
 * it are excluded. Pass the database's next match ID to preview a new match.
 * The ratings array receives db->player_count values in player array order.
 * The database's matches and appearances must already be in their normal
 * chronological / match-ID order, which every database mutation guarantees.
 */
bool db_ratings_before_match(const Database *db, const char *date,
                             uint64_t match_id, double *ratings,
                             char *error, size_t error_size);
bool db_validate(const Database *db, char *error, size_t error_size);

void db_sort_matches(Database *db);

void db_team_stats(const Database *db, uint64_t team_id, TeamStats *out);
size_t db_team_player_count(const Database *db, uint64_t team_id,
                            bool active_only);
bool db_team_has_matches(const Database *db, uint64_t team_id);
size_t db_match_appearance_count(const Database *db, uint64_t match_id);

#endif
