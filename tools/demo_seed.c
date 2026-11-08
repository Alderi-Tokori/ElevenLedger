#include "core/database.h"
#include "core/storage.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

typedef struct {
    const char *team_name;
    const char *short_name;
    const char *city;
    const char *players[11][2];
    double ratings[11];
    Position positions[11];
} DemoTeam;

static bool add_team_players(Database *db, const DemoTeam *team,
                             uint64_t *team_id, uint64_t player_ids[11],
                             char *error, size_t error_size)
{
    if (!db_add_team(db, team->team_name, team->short_name, team->city,
                     team_id, error, error_size)) {
        return false;
    }
    for (size_t i = 0; i < 11; ++i) {
        if (!db_add_player(db, *team_id, team->players[i][0],
                           team->players[i][1], team->positions[i],
                           team->ratings[i], true, &player_ids[i],
                           error, error_size)) {
            return false;
        }
    }
    return true;
}

static bool record_match(Database *db, const char *date,
                         uint64_t home_team, const uint64_t home_players[11],
                         uint64_t away_team, const uint64_t away_players[11],
                         int home_goals, int away_goals,
                         char *error, size_t error_size)
{
    MatchPlayerInput lineup[22];
    for (size_t i = 0; i < 11; ++i) {
        lineup[i].player_id = home_players[i];
        lineup[i].minutes = 90;
        lineup[i].performance = 6.2 + (double)((i * 7 + home_goals * 3) % 22) / 10.0;
        lineup[11 + i].player_id = away_players[i];
        lineup[11 + i].minutes = 90;
        lineup[11 + i].performance = 6.0 + (double)((i * 5 + away_goals * 2) % 20) / 10.0;
    }
    lineup[7].minutes = 63;
    lineup[8].minutes = 25;
    lineup[9].minutes = 80;
    lineup[18].minutes = 55;
    lineup[20].minutes = 90;

    MatchInput input = {
        .date = date,
        .home_team_id = home_team,
        .away_team_id = away_team,
        .home_goals = home_goals,
        .away_goals = away_goals,
        .players = lineup,
        .player_count = 22,
    };
    return db_add_match(db, &input, NULL, error, error_size);
}

int main(int argc, char **argv)
{
    bool force = false;
    if (argc == 3 && strcmp(argv[2], "--force") == 0) {
        force = true;
    } else if (argc != 2) {
        fprintf(stderr,
                "Usage: pitchrate_demo_seed <output.frc> [--force]\n");
        return 2;
    }
    wchar_t output_path[1024];
    int converted = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, output_path,
                                        (int)(sizeof(output_path) /
                                              sizeof(output_path[0])));
    if (converted == 0) {
        fprintf(stderr, "Could not convert the output path.\n");
        return 1;
    }
    if (!force && GetFileAttributesW(output_path) != INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr,
                "Refusing to overwrite existing file: %s\n"
                "Pass --force to replace it intentionally.\n",
                argv[1]);
        return 3;
    }

    const DemoTeam northbridge = {
        .team_name = "Northbridge FC",
        .short_name = "NBR",
        .city = "Northbridge",
        .players = {
            { "Oliver", "Hart" }, { "Theo", "Marsh" }, { "Callum", "Reid" },
            { "Marcus", "Cole" }, { "Elias", "Vance" }, { "Jonah", "Price" },
            { "Rafael", "Silva" }, { "Noah", "Bennett" }, { "Kai", "Turner" },
            { "Samir", "Haddad" }, { "Lucas", "Ferreira" },
        },
        .ratings = { 79.0, 74.0, 76.0, 73.0, 77.0, 72.0,
                     81.0, 75.0, 78.0, 82.0, 80.0 },
        .positions = {
            POSITION_GOALKEEPER, POSITION_DEFENDER, POSITION_DEFENDER,
            POSITION_DEFENDER, POSITION_DEFENDER, POSITION_MIDFIELDER,
            POSITION_MIDFIELDER, POSITION_MIDFIELDER, POSITION_FORWARD,
            POSITION_FORWARD, POSITION_FORWARD,
        },
    };
    const DemoTeam riverside = {
        .team_name = "Riverside Athletic",
        .short_name = "RIV",
        .city = "Riverside",
        .players = {
            { "Jamie", "Reed" }, { "Finn", "Oakes" }, { "Tomas", "Novak" },
            { "Mason", "Diaz" }, { "Evan", "Brooks" }, { "Ravi", "Shah" },
            { "Ines", "Costa" }, { "Dylan", "Mercer" }, { "Owen", "Blake" },
            { "Hugo", "Laurent" }, { "Nico", "Romano" },
        },
        .ratings = { 76.0, 72.0, 75.0, 71.0, 70.0, 74.0,
                     73.0, 77.0, 75.0, 78.0, 76.0 },
        .positions = {
            POSITION_GOALKEEPER, POSITION_DEFENDER, POSITION_DEFENDER,
            POSITION_DEFENDER, POSITION_DEFENDER, POSITION_MIDFIELDER,
            POSITION_MIDFIELDER, POSITION_MIDFIELDER, POSITION_FORWARD,
            POSITION_FORWARD, POSITION_FORWARD,
        },
    };

    Database db;
    db_init(&db);
    char error[512];
    uint64_t northbridge_id = 0;
    uint64_t riverside_id = 0;
    uint64_t northbridge_players[11];
    uint64_t riverside_players[11];
    bool ok = add_team_players(&db, &northbridge, &northbridge_id,
                               northbridge_players, error, sizeof(error)) &&
              add_team_players(&db, &riverside, &riverside_id,
                               riverside_players, error, sizeof(error));
    if (ok) {
        ok = record_match(&db, "2026-08-15", northbridge_id,
                          northbridge_players, riverside_id, riverside_players,
                          2, 1, error, sizeof(error));
    }
    if (ok) {
        ok = record_match(&db, "2026-08-22", riverside_id, riverside_players,
                          northbridge_id, northbridge_players,
                          0, 0, error, sizeof(error));
    }
    if (ok) {
        ok = record_match(&db, "2026-08-29", northbridge_id,
                          northbridge_players, riverside_id, riverside_players,
                          3, 0, error, sizeof(error));
    }
    if (ok) {
        ok = record_match(&db, "2026-09-05", riverside_id, riverside_players,
                          northbridge_id, northbridge_players,
                          1, 1, error, sizeof(error));
    }
    if (ok) {
        ok = storage_save(output_path, &db, error, sizeof(error));
    }
    if (!ok) {
        fprintf(stderr, "Demo seed failed: %s\n", error);
        db_free(&db);
        return 1;
    }
    printf("Wrote demo data to %s (%zu teams, %zu players, %zu matches).\n",
           argv[1], db.team_count, db.player_count, db.match_count);
    db_free(&db);
    return 0;
}
