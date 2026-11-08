#include "core/database.h"

#include "core/util.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 8
#define TEXT_TEMP_CAPACITY 256

static void set_error(char *error, size_t error_size, const char *format, ...)
{
    if (error == NULL || error_size == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(error, error_size, format, args);
    va_end(args);
}

static double round_rating(double value)
{
    return round(value * 100.0) / 100.0;
}

static bool grow_array(void **array, size_t element_size, size_t *capacity,
                       size_t needed)
{
    if (needed <= *capacity) {
        return true;
    }
    size_t new_capacity = *capacity == 0 ? INITIAL_CAPACITY : *capacity;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2;
    }
    if (element_size != 0 && new_capacity > SIZE_MAX / element_size) {
        return false;
    }
    void *resized = realloc(*array, new_capacity * element_size);
    if (resized == NULL) {
        return false;
    }
    *array = resized;
    *capacity = new_capacity;
    return true;
}

void db_init(Database *db)
{
    if (db == NULL) {
        return;
    }
    memset(db, 0, sizeof(*db));
    db->next_team_id = 1;
    db->next_player_id = 1;
    db->next_match_id = 1;
}

void db_free(Database *db)
{
    if (db == NULL) {
        return;
    }
    free(db->teams);
    free(db->players);
    free(db->matches);
    free(db->appearances);
    db_init(db);
}

bool db_clone(Database *destination, const Database *source)
{
    if (destination == NULL || source == NULL) {
        return false;
    }
    db_init(destination);
    if (source->team_count > 0) {
        destination->teams = malloc(source->team_count * sizeof(Team));
        if (destination->teams == NULL) {
            goto failure;
        }
        memcpy(destination->teams, source->teams,
               source->team_count * sizeof(Team));
        destination->team_count = source->team_count;
        destination->team_capacity = source->team_count;
    }
    if (source->player_count > 0) {
        destination->players = malloc(source->player_count * sizeof(Player));
        if (destination->players == NULL) {
            goto failure;
        }
        memcpy(destination->players, source->players,
               source->player_count * sizeof(Player));
        destination->player_count = source->player_count;
        destination->player_capacity = source->player_count;
    }
    if (source->match_count > 0) {
        destination->matches = malloc(source->match_count * sizeof(Match));
        if (destination->matches == NULL) {
            goto failure;
        }
        memcpy(destination->matches, source->matches,
               source->match_count * sizeof(Match));
        destination->match_count = source->match_count;
        destination->match_capacity = source->match_count;
    }
    if (source->appearance_count > 0) {
        destination->appearances =
            malloc(source->appearance_count * sizeof(Appearance));
        if (destination->appearances == NULL) {
            goto failure;
        }
        memcpy(destination->appearances, source->appearances,
               source->appearance_count * sizeof(Appearance));
        destination->appearance_count = source->appearance_count;
        destination->appearance_capacity = source->appearance_count;
    }
    destination->next_team_id = source->next_team_id;
    destination->next_player_id = source->next_player_id;
    destination->next_match_id = source->next_match_id;
    return true;

failure:
    db_free(destination);
    return false;
}

static Team *find_team_mut(Database *db, uint64_t id)
{
    for (size_t i = 0; i < db->team_count; ++i) {
        if (db->teams[i].id == id) {
            return &db->teams[i];
        }
    }
    return NULL;
}

static Player *find_player_mut(Database *db, uint64_t id)
{
    for (size_t i = 0; i < db->player_count; ++i) {
        if (db->players[i].id == id) {
            return &db->players[i];
        }
    }
    return NULL;
}

const Team *db_find_team(const Database *db, uint64_t id)
{
    if (db == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < db->team_count; ++i) {
        if (db->teams[i].id == id) {
            return &db->teams[i];
        }
    }
    return NULL;
}

const Player *db_find_player(const Database *db, uint64_t id)
{
    if (db == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < db->player_count; ++i) {
        if (db->players[i].id == id) {
            return &db->players[i];
        }
    }
    return NULL;
}

const Match *db_find_match(const Database *db, uint64_t id)
{
    if (db == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < db->match_count; ++i) {
        if (db->matches[i].id == id) {
            return &db->matches[i];
        }
    }
    return NULL;
}

const Player *db_last_added_player(const Database *db)
{
    if (db == NULL) {
        return NULL;
    }
    const Player *latest = NULL;
    for (size_t i = 0; i < db->player_count; ++i) {
        if (latest == NULL || db->players[i].id > latest->id) {
            latest = &db->players[i];
        }
    }
    return latest;
}

static bool normalize_player_name(const char *source, char *output,
                                  size_t output_size)
{
    char temp[TEXT_TEMP_CAPACITY];
    const char *text = source == NULL ? "" : source;
    if (strlen(text) >= sizeof(temp)) {
        return false;
    }
    util_str_copy(temp, sizeof(temp), text);
    char *trimmed = util_trim(temp);
    if (!util_text_valid(trimmed, PERSON_NAME_MAX, true)) {
        return false;
    }
    util_str_copy(output, output_size, trimmed);
    return true;
}

const Player *db_find_player_by_name(const Database *db,
                                     const char *first_name,
                                     const char *last_name,
                                     size_t *out_count)
{
    if (out_count != NULL) {
        *out_count = 0;
    }
    if (db == NULL) {
        return NULL;
    }
    char first[PERSON_NAME_MAX + 1];
    char last[PERSON_NAME_MAX + 1];
    if (!normalize_player_name(first_name, first, sizeof(first)) ||
        !normalize_player_name(last_name, last, sizeof(last))) {
        return NULL;
    }

    const Player *match = NULL;
    size_t count = 0;
    for (size_t i = 0; i < db->player_count; ++i) {
        const Player *player = &db->players[i];
        if (util_ascii_iequal(player->first_name, first) &&
            util_ascii_iequal(player->last_name, last)) {
            if (match == NULL) {
                match = player;
            }
            ++count;
        }
    }
    if (out_count != NULL) {
        *out_count = count;
    }
    return match;
}

static bool prepare_text(const char *source, size_t max_bytes,
                         bool allow_empty, char *output, size_t output_size,
                         const char *label, char *error, size_t error_size)
{
    char temp[TEXT_TEMP_CAPACITY];
    util_str_copy(temp, sizeof(temp), source == NULL ? "" : source);
    if (strlen(source == NULL ? "" : source) >= sizeof(temp)) {
        set_error(error, error_size, "%s is too long.", label);
        return false;
    }
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

static bool team_name_exists(const Database *db, const char *name,
                             uint64_t except_id)
{
    for (size_t i = 0; i < db->team_count; ++i) {
        if (db->teams[i].id != except_id &&
            util_ascii_iequal(db->teams[i].name, name)) {
            return true;
        }
    }
    return false;
}

bool db_add_team(Database *db, const char *name, const char *short_name,
                 const char *city, uint64_t *out_id,
                 char *error, size_t error_size)
{
    if (db == NULL) {
        return false;
    }
    if (db->next_team_id == UINT64_MAX) {
        set_error(error, error_size, "Team ID space exhausted.");
        return false;
    }

    char clean_name[TEAM_NAME_MAX + 1];
    char clean_short[TEAM_SHORT_MAX + 1];
    char clean_city[TEAM_CITY_MAX + 1];
    if (!prepare_text(name, TEAM_NAME_MAX, false, clean_name,
                      sizeof(clean_name), "Team name", error, error_size) ||
        !prepare_text(short_name, TEAM_SHORT_MAX, true, clean_short,
                      sizeof(clean_short), "Short name", error, error_size) ||
        !prepare_text(city, TEAM_CITY_MAX, true, clean_city,
                      sizeof(clean_city), "City", error, error_size)) {
        return false;
    }
    if (team_name_exists(db, clean_name, 0)) {
        set_error(error, error_size, "A team named \"%s\" already exists.",
                  clean_name);
        return false;
    }
    if (!grow_array((void **)&db->teams, sizeof(Team), &db->team_capacity,
                    db->team_count + 1)) {
        set_error(error, error_size, "Not enough memory to add the team.");
        return false;
    }

    Team *team = &db->teams[db->team_count++];
    memset(team, 0, sizeof(*team));
    team->id = db->next_team_id++;
    util_str_copy(team->name, sizeof(team->name), clean_name);
    util_str_copy(team->short_name, sizeof(team->short_name), clean_short);
    util_str_copy(team->city, sizeof(team->city), clean_city);
    if (out_id != NULL) {
        *out_id = team->id;
    }
    return true;
}

bool db_update_team(Database *db, uint64_t id, const char *name,
                    const char *short_name, const char *city,
                    char *error, size_t error_size)
{
    if (db == NULL) {
        return false;
    }
    Team *team = find_team_mut(db, id);
    if (team == NULL) {
        set_error(error, error_size, "Team not found.");
        return false;
    }

    char clean_name[TEAM_NAME_MAX + 1];
    char clean_short[TEAM_SHORT_MAX + 1];
    char clean_city[TEAM_CITY_MAX + 1];
    if (!prepare_text(name, TEAM_NAME_MAX, false, clean_name,
                      sizeof(clean_name), "Team name", error, error_size) ||
        !prepare_text(short_name, TEAM_SHORT_MAX, true, clean_short,
                      sizeof(clean_short), "Short name", error, error_size) ||
        !prepare_text(city, TEAM_CITY_MAX, true, clean_city,
                      sizeof(clean_city), "City", error, error_size)) {
        return false;
    }
    if (team_name_exists(db, clean_name, id)) {
        set_error(error, error_size, "A team named \"%s\" already exists.",
                  clean_name);
        return false;
    }

    util_str_copy(team->name, sizeof(team->name), clean_name);
    util_str_copy(team->short_name, sizeof(team->short_name), clean_short);
    util_str_copy(team->city, sizeof(team->city), clean_city);
    return true;
}

bool db_delete_team(Database *db, uint64_t id,
                    char *error, size_t error_size)
{
    if (db == NULL) {
        return false;
    }
    size_t index = SIZE_MAX;
    for (size_t i = 0; i < db->team_count; ++i) {
        if (db->teams[i].id == id) {
            index = i;
            break;
        }
    }
    if (index == SIZE_MAX) {
        set_error(error, error_size, "Team not found.");
        return false;
    }
    if (db_team_player_count(db, id, false) > 0) {
        set_error(error, error_size,
                  "This team still has players. Move or delete them first.");
        return false;
    }
    if (db_team_has_matches(db, id)) {
        set_error(error, error_size,
                  "This team has match history and cannot be deleted.");
        return false;
    }
    memmove(&db->teams[index], &db->teams[index + 1],
            (db->team_count - index - 1) * sizeof(Team));
    db->team_count--;
    return true;
}

bool db_add_player(Database *db, uint64_t team_id,
                   const char *first_name, const char *last_name,
                   double base_rating, bool active,
                   uint64_t *out_id, char *error, size_t error_size)
{
    if (db == NULL) {
        return false;
    }
    if (db_find_team(db, team_id) == NULL) {
        set_error(error, error_size, "Select a valid team.");
        return false;
    }
    if (!isfinite(base_rating)) {
        set_error(error, error_size, "Starting rating must be a finite number.");
        return false;
    }
    double rounded_base = round_rating(base_rating);
    if (!isfinite(rounded_base)) {
        set_error(error, error_size, "Starting rating is too large.");
        return false;
    }
    if (db->next_player_id == UINT64_MAX) {
        set_error(error, error_size, "Player ID space exhausted.");
        return false;
    }

    char clean_first[PERSON_NAME_MAX + 1];
    char clean_last[PERSON_NAME_MAX + 1];
    if (!prepare_text(first_name, PERSON_NAME_MAX, true, clean_first,
                      sizeof(clean_first), "First name", error, error_size) ||
        !prepare_text(last_name, PERSON_NAME_MAX, true, clean_last,
                      sizeof(clean_last), "Last name", error, error_size)) {
        return false;
    }
    if (clean_first[0] == '\0' && clean_last[0] == '\0') {
        set_error(error, error_size, "Enter a player name.");
        return false;
    }

    if (!grow_array((void **)&db->players, sizeof(Player),
                    &db->player_capacity, db->player_count + 1)) {
        set_error(error, error_size, "Not enough memory to add the player.");
        return false;
    }

    Player *player = &db->players[db->player_count++];
    memset(player, 0, sizeof(*player));
    player->id = db->next_player_id++;
    player->team_id = team_id;
    util_str_copy(player->first_name, sizeof(player->first_name), clean_first);
    util_str_copy(player->last_name, sizeof(player->last_name), clean_last);
    player->base_rating = rounded_base;
    player->current_rating = player->base_rating;
    player->active = active;
    if (out_id != NULL) {
        *out_id = player->id;
    }
    return true;
}

static bool validate_player_names(const char *first, const char *last,
                                  char *error, size_t error_size)
{
    if (first[0] == '\0' && last[0] == '\0') {
        set_error(error, error_size, "Enter a player name.");
        return false;
    }
    return true;
}

bool db_update_player(Database *db, uint64_t id, uint64_t team_id,
                      const char *first_name, const char *last_name,
                      double base_rating, bool active,
                      char *error, size_t error_size)
{
    if (db == NULL) {
        return false;
    }
    Player *player = find_player_mut(db, id);
    if (player == NULL) {
        set_error(error, error_size, "Player not found.");
        return false;
    }
    if (db_find_team(db, team_id) == NULL) {
        set_error(error, error_size, "Select a valid team.");
        return false;
    }
    if (!isfinite(base_rating)) {
        set_error(error, error_size, "Starting rating must be a finite number.");
        return false;
    }
    double rounded_base = round_rating(base_rating);
    if (!isfinite(rounded_base)) {
        set_error(error, error_size, "Starting rating is too large.");
        return false;
    }

    char clean_first[PERSON_NAME_MAX + 1];
    char clean_last[PERSON_NAME_MAX + 1];
    if (!prepare_text(first_name, PERSON_NAME_MAX, true, clean_first,
                      sizeof(clean_first), "First name", error, error_size) ||
        !prepare_text(last_name, PERSON_NAME_MAX, true, clean_last,
                      sizeof(clean_last), "Last name", error, error_size) ||
        !validate_player_names(clean_first, clean_last, error, error_size)) {
        return false;
    }

    double previous_base = player->base_rating;
    player->team_id = team_id;
    util_str_copy(player->first_name, sizeof(player->first_name), clean_first);
    util_str_copy(player->last_name, sizeof(player->last_name), clean_last);
    player->base_rating = rounded_base;
    player->active = active;

    if (player->base_rating != previous_base) {
        if (!db_recompute_ratings(db, error, error_size)) {
            player->base_rating = previous_base;
            return false;
        }
    }
    return true;
}

bool db_set_player_active(Database *db, uint64_t id, bool active,
                          char *error, size_t error_size)
{
    Player *player = db == NULL ? NULL : find_player_mut(db, id);
    if (player == NULL) {
        set_error(error, error_size, "Player not found.");
        return false;
    }
    player->active = active;
    return true;
}

bool db_delete_player(Database *db, uint64_t id,
                      char *error, size_t error_size)
{
    if (db == NULL) {
        return false;
    }
    size_t index = SIZE_MAX;
    for (size_t i = 0; i < db->player_count; ++i) {
        if (db->players[i].id == id) {
            index = i;
            break;
        }
    }
    if (index == SIZE_MAX) {
        set_error(error, error_size, "Player not found.");
        return false;
    }
    for (size_t i = 0; i < db->appearance_count; ++i) {
        if (db->appearances[i].player_id == id) {
            set_error(error, error_size,
                      "This player has match history. Archive the player instead.");
            return false;
        }
    }
    memmove(&db->players[index], &db->players[index + 1],
            (db->player_count - index - 1) * sizeof(Player));
    db->player_count--;
    return true;
}

static int compare_matches(const void *a, const void *b)
{
    const Match *left = (const Match *)a;
    const Match *right = (const Match *)b;
    int date_compare = strcmp(left->date, right->date);
    if (date_compare != 0) {
        return date_compare;
    }
    if (left->id < right->id) {
        return -1;
    }
    if (left->id > right->id) {
        return 1;
    }
    return 0;
}

static int compare_appearances(const void *a, const void *b)
{
    const Appearance *left = (const Appearance *)a;
    const Appearance *right = (const Appearance *)b;
    if (left->match_id < right->match_id) {
        return -1;
    }
    if (left->match_id > right->match_id) {
        return 1;
    }
    if (left->player_id < right->player_id) {
        return -1;
    }
    if (left->player_id > right->player_id) {
        return 1;
    }
    return 0;
}

void db_sort_matches(Database *db)
{
    if (db == NULL || db->match_count < 2) {
        return;
    }
    qsort(db->matches, db->match_count, sizeof(Match), compare_matches);
}

static size_t appearance_lower_bound(const Database *db, uint64_t match_id)
{
    size_t low = 0;
    size_t high = db->appearance_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (db->appearances[middle].match_id < match_id) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

bool db_validate(const Database *db, char *error, size_t error_size)
{
    if (db == NULL) {
        set_error(error, error_size, "Internal error: database is missing.");
        return false;
    }
    if ((db->team_count > 0 && db->teams == NULL) ||
        (db->player_count > 0 && db->players == NULL) ||
        (db->match_count > 0 && db->matches == NULL) ||
        (db->appearance_count > 0 && db->appearances == NULL)) {
        set_error(error, error_size, "Internal error: invalid database arrays.");
        return false;
    }

    for (size_t i = 0; i < db->team_count; ++i) {
        const Team *team = &db->teams[i];
        if (team->id == 0 || !util_text_valid(team->name, TEAM_NAME_MAX, false)) {
            set_error(error, error_size, "Invalid team record.");
            return false;
        }
        if (!util_text_valid(team->short_name, TEAM_SHORT_MAX, true) ||
            !util_text_valid(team->city, TEAM_CITY_MAX, true)) {
            set_error(error, error_size, "Invalid team details for \"%s\".",
                      team->name);
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (db->teams[j].id == team->id) {
                set_error(error, error_size, "Duplicate team ID.");
                return false;
            }
            if (util_ascii_iequal(db->teams[j].name, team->name)) {
                set_error(error, error_size, "Duplicate team name \"%s\".",
                          team->name);
                return false;
            }
        }
    }

    for (size_t i = 0; i < db->player_count; ++i) {
        const Player *player = &db->players[i];
        if (player->id == 0 || db_find_team(db, player->team_id) == NULL ||
            !isfinite(player->base_rating) ||
            !util_text_valid(player->first_name, PERSON_NAME_MAX, true) ||
            !util_text_valid(player->last_name, PERSON_NAME_MAX, true) ||
            (player->first_name[0] == '\0' && player->last_name[0] == '\0')) {
            set_error(error, error_size, "Invalid player record.");
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (db->players[j].id == player->id) {
                set_error(error, error_size, "Duplicate player ID.");
                return false;
            }
        }
    }

    for (size_t i = 0; i < db->match_count; ++i) {
        const Match *match = &db->matches[i];
        if (match->id == 0 || !util_date_valid(match->date) ||
            match->home_team_id == match->away_team_id ||
            db_find_team(db, match->home_team_id) == NULL ||
            db_find_team(db, match->away_team_id) == NULL ||
            match->home_goals < 0 || match->home_goals > 99 ||
            match->away_goals < 0 || match->away_goals > 99) {
            set_error(error, error_size, "Invalid match record.");
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (db->matches[j].id == match->id) {
                set_error(error, error_size, "Duplicate match ID.");
                return false;
            }
        }
    }

    for (size_t i = 0; i < db->appearance_count; ++i) {
        const Appearance *appearance = &db->appearances[i];
        const Match *match = db_find_match(db, appearance->match_id);
        const Player *player = db_find_player(db, appearance->player_id);
        if (match == NULL || player == NULL ||
            (appearance->team_id != match->home_team_id &&
             appearance->team_id != match->away_team_id) ||
            appearance->minutes < 1 || appearance->minutes > 120 ||
            !isfinite(appearance->performance) ||
            appearance->performance < 1.0 || appearance->performance > 10.0) {
            set_error(error, error_size, "Invalid match appearance.");
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (db->appearances[j].match_id == appearance->match_id &&
                db->appearances[j].player_id == appearance->player_id) {
                set_error(error, error_size,
                          "A player appears twice in the same match.");
                return false;
            }
        }
    }

    for (size_t i = 0; i < db->match_count; ++i) {
        const Match *match = &db->matches[i];
        size_t home_count = 0;
        size_t away_count = 0;
        for (size_t j = 0; j < db->appearance_count; ++j) {
            if (db->appearances[j].match_id != match->id) {
                continue;
            }
            if (db->appearances[j].team_id == match->home_team_id) {
                home_count++;
            } else {
                away_count++;
            }
        }
        if (home_count == 0 || away_count == 0) {
            set_error(error, error_size,
                      "Every match needs players from both teams.");
            return false;
        }
    }
    return true;
}

bool db_recompute_ratings(Database *db, char *error, size_t error_size)
{
    if (!db_validate(db, error, error_size)) {
        return false;
    }
    db_sort_matches(db);

    for (size_t i = 0; i < db->player_count; ++i) {
        db->players[i].current_rating = db->players[i].base_rating;
        db->players[i].matches_played = 0;
        db->players[i].minutes_played = 0;
    }
    if (db->appearance_count > 1) {
        qsort(db->appearances, db->appearance_count, sizeof(Appearance),
              compare_appearances);
    }

    for (size_t match_index = 0; match_index < db->match_count; ++match_index) {
        Match *match = &db->matches[match_index];
        size_t start = appearance_lower_bound(db, match->id);
        size_t end = start;
        while (end < db->appearance_count &&
               db->appearances[end].match_id == match->id) {
            end++;
        }
        if (start == end) {
            set_error(error, error_size, "Match has no line-up.");
            return false;
        }

        double home_weight = 0.0;
        double away_weight = 0.0;
        double home_weighted_rating = 0.0;
        double away_weighted_rating = 0.0;
        double home_weighted_performance = 0.0;
        double away_weighted_performance = 0.0;

        for (size_t i = start; i < end; ++i) {
            Appearance *appearance = &db->appearances[i];
            Player *player = find_player_mut(db, appearance->player_id);
            if (player == NULL) {
                set_error(error, error_size, "Line-up references a missing player.");
                return false;
            }
            appearance->rating_before = player->current_rating;
            double weight = rating_participation(appearance->minutes);
            if (appearance->team_id == match->home_team_id) {
                home_weight += weight;
                home_weighted_rating += player->current_rating * weight;
                home_weighted_performance +=
                    appearance->performance * weight;
            } else {
                away_weight += weight;
                away_weighted_rating += player->current_rating * weight;
                away_weighted_performance +=
                    appearance->performance * weight;
            }
        }
        if (home_weight <= 0.0 || away_weight <= 0.0) {
            set_error(error, error_size, "Match line-up has no playing time.");
            return false;
        }

        RatingTeamContext home_context = {
            .total_participation = home_weight,
            .weighted_performance = home_weighted_performance,
        };
        RatingTeamContext away_context = {
            .total_participation = away_weight,
            .weighted_performance = away_weighted_performance,
        };
        double home_strength = home_weighted_rating / home_weight;
        double away_strength = away_weighted_rating / away_weight;
        RatingMatchSummary summary;
        if (!rating_match_summary(&summary, home_strength, away_strength,
                                  &home_context, &away_context,
                                  match->home_goals, match->away_goals,
                                  match->neutral_venue,
                                  error, error_size)) {
            return false;
        }
        match->rating = summary;

        for (size_t i = start; i < end; ++i) {
            Appearance *appearance = &db->appearances[i];
            Player *player = find_player_mut(db, appearance->player_id);
            bool home_team = appearance->team_id == match->home_team_id;
            double delta = 0.0;
            double after = 0.0;
            if (!rating_player_change(&summary, home_team,
                                      appearance->rating_before,
                                      appearance->minutes,
                                      appearance->performance,
                                      &delta, &after)) {
                set_error(error, error_size,
                          "Could not calculate a player rating.");
                return false;
            }
            appearance->rating_delta = delta;
            appearance->rating_after = after;
            player->current_rating = after;
            player->matches_played += 1;
            player->minutes_played += (uint32_t)appearance->minutes;
        }
    }
    return true;
}

bool db_add_match(Database *db, const MatchInput *input, uint64_t *out_id,
                  char *error, size_t error_size)
{
    if (db == NULL || input == NULL) {
        set_error(error, error_size, "Invalid match input.");
        return false;
    }
    if (db->next_match_id == UINT64_MAX) {
        set_error(error, error_size, "Match ID space exhausted.");
        return false;
    }

    char clean_date[MATCH_DATE_MAX + 1];
    char date_temp[TEXT_TEMP_CAPACITY];
    util_str_copy(date_temp, sizeof(date_temp),
                  input->date == NULL ? "" : input->date);
    char *trimmed_date = util_trim(date_temp);
    if (!util_date_valid(trimmed_date)) {
        set_error(error, error_size, "Use a valid date in YYYY-MM-DD format.");
        return false;
    }
    util_str_copy(clean_date, sizeof(clean_date), trimmed_date);

    const Team *home = db_find_team(db, input->home_team_id);
    const Team *away = db_find_team(db, input->away_team_id);
    if (home == NULL || away == NULL) {
        set_error(error, error_size, "Select both teams.");
        return false;
    }
    if (home->id == away->id) {
        set_error(error, error_size, "Choose two different teams.");
        return false;
    }
    if (input->home_goals < 0 || input->home_goals > 99 ||
        input->away_goals < 0 || input->away_goals > 99) {
        set_error(error, error_size, "Scores must be between 0 and 99.");
        return false;
    }
    if (input->players == NULL || input->player_count == 0) {
        set_error(error, error_size, "Select at least one player for each team.");
        return false;
    }

    size_t home_players = 0;
    size_t away_players = 0;
    uint64_t home_minutes = 0;
    uint64_t away_minutes = 0;
    for (size_t i = 0; i < input->player_count; ++i) {
        const MatchPlayerInput *entry = &input->players[i];
        const Player *player = db_find_player(db, entry->player_id);
        if (player == NULL) {
            set_error(error, error_size, "Line-up references an unknown player.");
            return false;
        }
        if (!player->active) {
            set_error(error, error_size, "Line-up includes an archived player.");
            return false;
        }
        if (player->team_id != input->home_team_id &&
            player->team_id != input->away_team_id) {
            set_error(error, error_size,
                      "A selected player does not belong to either team.");
            return false;
        }
        if (entry->minutes < 1 || entry->minutes > 120) {
            set_error(error, error_size, "Minutes played must be between 1 and 120.");
            return false;
        }
        if (!isfinite(entry->performance) ||
            entry->performance < 1.0 || entry->performance > 10.0) {
            set_error(error, error_size,
                      "Performance must be between 1.0 and 10.0.");
            return false;
        }
        if (player->team_id == input->home_team_id) {
            home_players++;
            home_minutes += (uint64_t)entry->minutes;
        } else {
            away_players++;
            away_minutes += (uint64_t)entry->minutes;
        }
        for (size_t j = 0; j < i; ++j) {
            if (input->players[j].player_id == entry->player_id) {
                set_error(error, error_size,
                          "A player is selected more than once.");
                return false;
            }
        }
    }
    if (home_players == 0 || away_players == 0) {
        set_error(error, error_size, "Select at least one player for each team.");
        return false;
    }
    if (home_minutes > 11u * 120u || away_minutes > 11u * 120u) {
        set_error(error, error_size,
                  "A team cannot have more than 1,320 player-minutes in one match.");
        return false;
    }

    if (!grow_array((void **)&db->matches, sizeof(Match), &db->match_capacity,
                    db->match_count + 1) ||
        !grow_array((void **)&db->appearances, sizeof(Appearance),
                    &db->appearance_capacity,
                    db->appearance_count + input->player_count)) {
        set_error(error, error_size, "Not enough memory to record the match.");
        return false;
    }

    Match *match = &db->matches[db->match_count++];
    memset(match, 0, sizeof(*match));
    uint64_t new_match_id = db->next_match_id++;
    match->id = new_match_id;
    util_str_copy(match->date, sizeof(match->date), clean_date);
    match->home_team_id = input->home_team_id;
    match->away_team_id = input->away_team_id;
    match->home_goals = input->home_goals;
    match->away_goals = input->away_goals;
    match->neutral_venue = input->neutral_venue;

    for (size_t i = 0; i < input->player_count; ++i) {
        const MatchPlayerInput *entry = &input->players[i];
        const Player *player = db_find_player(db, entry->player_id);
        Appearance *appearance = &db->appearances[db->appearance_count++];
        memset(appearance, 0, sizeof(*appearance));
        appearance->match_id = match->id;
        appearance->player_id = entry->player_id;
        appearance->team_id = player->team_id;
        appearance->minutes = entry->minutes;
        appearance->performance =
            round(entry->performance * 100.0) / 100.0;
    }

    db_sort_matches(db);
    if (!db_recompute_ratings(db, error, error_size)) {
        return false;
    }
    if (out_id != NULL) {
        *out_id = new_match_id;
    }
    return true;
}

bool db_delete_match(Database *db, uint64_t id,
                     char *error, size_t error_size)
{
    if (db == NULL) {
        return false;
    }
    size_t match_index = SIZE_MAX;
    for (size_t i = 0; i < db->match_count; ++i) {
        if (db->matches[i].id == id) {
            match_index = i;
            break;
        }
    }
    if (match_index == SIZE_MAX) {
        set_error(error, error_size, "Match not found.");
        return false;
    }

    size_t write_index = 0;
    for (size_t i = 0; i < db->appearance_count; ++i) {
        if (db->appearances[i].match_id != id) {
            if (write_index != i) {
                db->appearances[write_index] = db->appearances[i];
            }
            write_index++;
        }
    }
    db->appearance_count = write_index;
    memmove(&db->matches[match_index], &db->matches[match_index + 1],
            (db->match_count - match_index - 1) * sizeof(Match));
    db->match_count--;
    return db_recompute_ratings(db, error, error_size);
}

void db_team_stats(const Database *db, uint64_t team_id, TeamStats *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    if (db == NULL) {
        return;
    }
    double rating_sum = 0.0;
    for (size_t i = 0; i < db->player_count; ++i) {
        const Player *player = &db->players[i];
        if (player->team_id != team_id) {
            continue;
        }
        out->roster_size++;
        if (player->active) {
            out->active_players++;
            rating_sum += player->current_rating;
        }
    }
    if (out->active_players > 0) {
        out->average_rating = rating_sum / (double)out->active_players;
    }
    for (size_t i = 0; i < db->match_count; ++i) {
        const Match *match = &db->matches[i];
        bool is_home;
        if (match->home_team_id == team_id) {
            is_home = true;
        } else if (match->away_team_id == team_id) {
            is_home = false;
        } else {
            continue;
        }
        out->matches++;
        int goals_for = is_home ? match->home_goals : match->away_goals;
        int goals_against = is_home ? match->away_goals : match->home_goals;
        if (goals_for > goals_against) {
            out->wins++;
        } else if (goals_for < goals_against) {
            out->losses++;
        } else {
            out->draws++;
        }
    }
}

size_t db_team_player_count(const Database *db, uint64_t team_id,
                            bool active_only)
{
    size_t count = 0;
    if (db == NULL) {
        return 0;
    }
    for (size_t i = 0; i < db->player_count; ++i) {
        if (db->players[i].team_id == team_id &&
            (!active_only || db->players[i].active)) {
            count++;
        }
    }
    return count;
}

bool db_team_has_matches(const Database *db, uint64_t team_id)
{
    if (db == NULL) {
        return false;
    }
    for (size_t i = 0; i < db->match_count; ++i) {
        if (db->matches[i].home_team_id == team_id ||
            db->matches[i].away_team_id == team_id) {
            return true;
        }
    }
    return false;
}

size_t db_match_appearance_count(const Database *db, uint64_t match_id)
{
    size_t count = 0;
    if (db == NULL) {
        return 0;
    }
    for (size_t i = 0; i < db->appearance_count; ++i) {
        if (db->appearances[i].match_id == match_id) {
            count++;
        }
    }
    return count;
}
