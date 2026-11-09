#include "core/import.h"

#include "core/util.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *format, ...)
{
    if (error == NULL || error_size == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(error, error_size, format, args);
    va_end(args);
    error[error_size - 1] = '\0';
}

static bool clean_text(const char *source, size_t max_bytes, bool allow_empty,
                       char *output, size_t output_size, const char *label,
                       char *error, size_t error_size)
{
    char temp[512];
    const char *text = source == NULL ? "" : source;
    if (strlen(text) >= sizeof(temp)) {
        set_error(error, error_size, "%s is too long.", label);
        return false;
    }
    util_str_copy(temp, sizeof(temp), text);
    char *trimmed = util_trim(temp);
    if (!util_text_valid(trimmed, max_bytes, allow_empty)) {
        if (!allow_empty && trimmed[0] == '\0') {
            set_error(error, error_size, "%s is required.", label);
        } else {
            set_error(error, error_size,
                      "%s contains invalid characters or is too long.", label);
        }
        return false;
    }
    util_str_copy(output, output_size, trimmed);
    return true;
}

static bool clean_player_name(const ImportPlayerPlan *player,
                              char *first, size_t first_size,
                              char *last, size_t last_size,
                              char *error, size_t error_size)
{
    if (!clean_text(player->first_name, PERSON_NAME_MAX, true, first,
                    first_size, "First name", error, error_size) ||
        !clean_text(player->last_name, PERSON_NAME_MAX, true, last,
                    last_size, "Last name", error, error_size)) {
        return false;
    }
    if (first[0] == '\0' && last[0] == '\0') {
        set_error(error, error_size, "Enter a player name.");
        return false;
    }
    return true;
}

static bool existing_id_used_in(const ImportPlayerPlan *players, size_t count,
                                uint64_t player_id)
{
    for (size_t i = 0; i < count; ++i) {
        if (players[i].action == IMPORT_PLAYER_USE_EXISTING &&
            players[i].existing_player_id == player_id) {
            return true;
        }
    }
    return false;
}

static bool validate_player(const Database *db,
                            const ImportPlayerPlan *player,
                            uint64_t *out_existing_id,
                            char *error, size_t error_size)
{
    char first[PERSON_NAME_MAX + 1];
    char last[PERSON_NAME_MAX + 1];
    if (!clean_player_name(player, first, sizeof(first), last, sizeof(last),
                           error, error_size)) {
        return false;
    }
    if (player->minutes < 1 || player->minutes > 120) {
        set_error(error, error_size,
                  "Minutes played must be between 1 and 120.");
        return false;
    }
    if (!isfinite(player->performance) || player->performance < 1.0 ||
        player->performance > 10.0) {
        set_error(error, error_size,
                  "Performance must be between 1.0 and 10.0.");
        return false;
    }

    if (out_existing_id != NULL) {
        *out_existing_id = 0;
    }
    if (player->action != IMPORT_PLAYER_CREATE &&
        player->action != IMPORT_PLAYER_USE_EXISTING) {
        set_error(error, error_size, "Invalid player import action.");
        return false;
    }
    if (player->action == IMPORT_PLAYER_CREATE) {
        return true;
    }
    if (player->existing_player_id == 0) {
        set_error(error, error_size,
                  "Select which existing player to use.");
        return false;
    }
    const Player *existing = db_find_player(db, player->existing_player_id);
    if (existing == NULL) {
        set_error(error, error_size,
                  "A selected existing player no longer exists.");
        return false;
    }
    if (!existing->active) {
        set_error(error, error_size,
                  "\"%s %s\" is archived. Create a separate player instead.",
                  existing->first_name, existing->last_name);
        return false;
    }
    if (!util_ascii_iequal(existing->first_name, first) ||
        !util_ascii_iequal(existing->last_name, last)) {
        set_error(error, error_size,
                  "The selected existing player no longer matches the "
                  "imported name.");
        return false;
    }
    if (out_existing_id != NULL) {
        *out_existing_id = existing->id;
    }
    return true;
}

static bool validate_side(const Database *db,
                          const ImportPlayerPlan *players, size_t count,
                          bool check_home_duplicates,
                          const ImportPlan *plan,
                          char *error, size_t error_size)
{
    if (count == 0 || players == NULL) {
        set_error(error, error_size,
                  "Select at least one player for each team.");
        return false;
    }
    uint64_t total_minutes = 0;
    for (size_t i = 0; i < count; ++i) {
        const ImportPlayerPlan *player = &players[i];
        uint64_t existing_id = 0;
        if (!validate_player(db, player, &existing_id, error, error_size)) {
            return false;
        }
        total_minutes += (uint64_t)player->minutes;
        if (existing_id == 0) {
            continue;
        }
        bool used = false;
        if (check_home_duplicates) {
            used = existing_id_used_in(plan->home_players, i, existing_id);
        } else {
            used = existing_id_used_in(plan->home_players,
                                       plan->home_player_count, existing_id) ||
                   existing_id_used_in(plan->away_players, i, existing_id);
        }
        if (used) {
            set_error(error, error_size,
                      "The same existing player is selected more than once.");
            return false;
        }
    }
    if (total_minutes > 11u * 120u) {
        set_error(error, error_size,
                  "A team cannot have more than 1,320 player-minutes in one "
                  "match.");
        return false;
    }
    return true;
}

static bool validate_plan(const Database *db, const ImportPlan *plan,
                          char *home_name, size_t home_name_size,
                          char *away_name, size_t away_name_size,
                          char *date, size_t date_size,
                          char *error, size_t error_size)
{
    if (db == NULL || plan == NULL) {
        set_error(error, error_size, "Invalid match import.");
        return false;
    }
    char date_temp[64];
    if (!clean_text(plan->date, MATCH_DATE_MAX, false, date_temp,
                    sizeof(date_temp), "Match date", error, error_size)) {
        return false;
    }
    if (!util_date_valid(date_temp)) {
        set_error(error, error_size,
                  "Use a valid date in YYYY-MM-DD format.");
        return false;
    }
    util_str_copy(date, date_size, date_temp);

    if (!clean_text(plan->home_team_name, TEAM_NAME_MAX, false, home_name,
                    home_name_size, "Home team", error, error_size) ||
        !clean_text(plan->away_team_name, TEAM_NAME_MAX, false, away_name,
                    away_name_size, "Away team", error, error_size)) {
        return false;
    }
    if (util_ascii_iequal(home_name, away_name)) {
        set_error(error, error_size, "Choose two different teams.");
        return false;
    }
    if (plan->home_goals < 0 || plan->home_goals > 99 ||
        plan->away_goals < 0 || plan->away_goals > 99) {
        set_error(error, error_size, "Scores must be between 0 and 99.");
        return false;
    }
    if (plan->home_players == NULL || plan->away_players == NULL) {
        set_error(error, error_size,
                  "Select at least one player for each team.");
        return false;
    }
    if (!validate_side(db, plan->home_players, plan->home_player_count, true,
                       plan, error, error_size) ||
        !validate_side(db, plan->away_players,
                       plan->away_player_count, false, plan,
                       error, error_size)) {
        return false;
    }
    return true;
}

static bool resolve_team(Database *db, const char *name, uint64_t *out_id,
                         char *error, size_t error_size)
{
    const Team *existing = db_find_team_by_name(db, name);
    if (existing != NULL) {
        *out_id = existing->id;
        return true;
    }
    uint64_t new_id = 0;
    if (!db_add_team(db, name, "", "", &new_id, error, error_size)) {
        return false;
    }
    *out_id = new_id;
    return true;
}

static bool append_players(Database *db, uint64_t team_id,
                           const ImportPlayerPlan *players, size_t count,
                           MatchPlayerInput *lineup, size_t *used,
                           char *error, size_t error_size)
{
    for (size_t i = 0; i < count; ++i) {
        const ImportPlayerPlan *plan = &players[i];
        char first[PERSON_NAME_MAX + 1];
        char last[PERSON_NAME_MAX + 1];
        if (!clean_player_name(plan, first, sizeof(first), last, sizeof(last),
                               error, error_size)) {
            return false;
        }

        uint64_t player_id = 0;
        if (plan->action == IMPORT_PLAYER_CREATE) {
            if (!db_add_player(db, team_id, first, last, RATING_DEFAULT, true,
                               &player_id, error, error_size)) {
                return false;
            }
        } else {
            const Player *existing =
                db_find_player(db, plan->existing_player_id);
            if (existing == NULL) {
                set_error(error, error_size,
                          "A selected existing player no longer exists.");
                return false;
            }
            player_id = existing->id;
            if (existing->team_id != team_id) {
                char existing_first[PERSON_NAME_MAX + 1];
                char existing_last[PERSON_NAME_MAX + 1];
                double base_rating = existing->base_rating;
                bool active = existing->active;
                util_str_copy(existing_first, sizeof(existing_first),
                              existing->first_name);
                util_str_copy(existing_last, sizeof(existing_last),
                              existing->last_name);
                if (!db_update_player(db, player_id, team_id, existing_first,
                                      existing_last, base_rating, active,
                                      error, error_size)) {
                    return false;
                }
            }
        }

        MatchPlayerInput *entry = &lineup[*used];
        memset(entry, 0, sizeof(*entry));
        entry->player_id = player_id;
        entry->minutes = plan->minutes;
        entry->performance = round(plan->performance * 100.0) / 100.0;
        ++*used;
    }
    return true;
}

bool db_import_match(Database *db, const ImportPlan *plan,
                     uint64_t *out_match_id,
                     char *error, size_t error_size)
{
    if (out_match_id != NULL) {
        *out_match_id = 0;
    }
    char home_name[TEAM_NAME_MAX + 1];
    char away_name[TEAM_NAME_MAX + 1];
    char date[MATCH_DATE_MAX + 1];
    if (!validate_plan(db, plan, home_name, sizeof(home_name), away_name,
                       sizeof(away_name), date, sizeof(date), error,
                       error_size)) {
        return false;
    }

    uint64_t home_id = 0;
    uint64_t away_id = 0;
    if (!resolve_team(db, home_name, &home_id, error, error_size) ||
        !resolve_team(db, away_name, &away_id, error, error_size)) {
        return false;
    }
    if (home_id == away_id) {
        set_error(error, error_size, "Choose two different teams.");
        return false;
    }

    size_t total_players = plan->home_player_count + plan->away_player_count;
    MatchPlayerInput *lineup = calloc(total_players, sizeof(*lineup));
    if (lineup == NULL) {
        set_error(error, error_size,
                  "Not enough memory to import the match.");
        return false;
    }
    size_t used = 0;
    if (!append_players(db, home_id, plan->home_players,
                        plan->home_player_count, lineup, &used,
                        error, error_size) ||
        !append_players(db, away_id, plan->away_players,
                        plan->away_player_count, lineup, &used,
                        error, error_size)) {
        free(lineup);
        return false;
    }

    MatchInput input = {
        .date = date,
        .home_team_id = home_id,
        .away_team_id = away_id,
        .home_goals = plan->home_goals,
        .away_goals = plan->away_goals,
        .neutral_venue = plan->neutral_venue,
        .players = lineup,
        .player_count = used,
    };
    char match_error[512];
    bool ok = db_add_match(db, &input, out_match_id, match_error,
                           sizeof(match_error));
    free(lineup);
    if (!ok) {
        set_error(error, error_size, "%s", match_error);
        return false;
    }
    return true;
}
