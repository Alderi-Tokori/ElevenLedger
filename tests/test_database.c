#include "test_framework.h"

#include "core/database.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static bool setup_two_teams(Database *db, uint64_t *alpha, uint64_t *beta)
{
    char error[256];
    db_init(db);
    CHECK(db_add_team(db, "Alpha FC", "ALP", "Northbridge",
                      alpha, error, sizeof(error)));
    CHECK(db_add_team(db, "Beta United", "BET", "Riverside",
                      beta, error, sizeof(error)));
    return true;
}

static bool add_four_players(Database *db, uint64_t alpha, uint64_t beta,
                             uint64_t ids[4])
{
    char error[256];
    CHECK(db_add_player(db, alpha, "Ava", "Stone",
                        80.0, true, &ids[0], error, sizeof(error)));
    CHECK(db_add_player(db, alpha, "Noah", "Bennett",
                        70.0, true, &ids[1], error, sizeof(error)));
    CHECK(db_add_player(db, beta, "Mia", "Clarke",
                        75.0, true, &ids[2], error, sizeof(error)));
    CHECK(db_add_player(db, beta, "Leo", "Foster",
                        65.0, true, &ids[3], error, sizeof(error)));
    return true;
}

static const Appearance *find_appearance(const Database *db, uint64_t match_id,
                                         uint64_t player_id)
{
    for (size_t i = 0; i < db->appearance_count; ++i) {
        if (db->appearances[i].match_id == match_id &&
            db->appearances[i].player_id == player_id) {
            return &db->appearances[i];
        }
    }
    return NULL;
}

static bool test_team_crud_and_validation(void)
{
    Database db;
    db_init(&db);
    char error[256];
    uint64_t team_id = 0;

    CHECK(!db_add_team(&db, "", NULL, NULL, NULL, error, sizeof(error)));
    CHECK(db_add_team(&db, "  Northbridge FC  ", "NBR", " Northbridge ",
                      &team_id, error, sizeof(error)));
    CHECK(team_id == 1);
    CHECK_STRING(db.teams[0].name, "Northbridge FC");
    CHECK_STRING(db.teams[0].short_name, "NBR");
    CHECK_STRING(db.teams[0].city, "Northbridge");
    CHECK(!db_add_team(&db, "northbridge fc", NULL, NULL, NULL,
                       error, sizeof(error)));
    CHECK(strstr(error, "already exists") != NULL);

    uint64_t second_id = 0;
    CHECK(db_add_team(&db, "Riverside Athletic", "", "Riverside",
                      &second_id, error, sizeof(error)));
    CHECK(db_update_team(&db, second_id, "Riverside A.F.C.", "RIV",
                         "Riverside", error, sizeof(error)));
    CHECK_STRING(db.teams[1].name, "Riverside A.F.C.");
    CHECK(!db_update_team(&db, 999, "No Team", NULL, NULL,
                          error, sizeof(error)));
    CHECK(!db_update_team(&db, second_id, "Northbridge FC", NULL, NULL,
                          error, sizeof(error)));

    uint64_t player_id = 0;
    CHECK(db_add_player(&db, team_id, "Jamie", "Reed",
                        72.0, true, &player_id, error, sizeof(error)));
    CHECK(!db_delete_team(&db, team_id, error, sizeof(error)));
    CHECK(strstr(error, "still has players") != NULL);
    CHECK(db_delete_player(&db, player_id, error, sizeof(error)));
    CHECK(db_delete_team(&db, team_id, error, sizeof(error)));
    CHECK(db.team_count == 1);
    db_free(&db);
    return true;
}

static bool test_player_crud_and_archive(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    char error[256];
    uint64_t player_id = 0;
    CHECK(!db_add_player(&db, 999, "No", "Team",
                         70.0, true, NULL, error, sizeof(error)));
    CHECK(!db_add_player(&db, alpha, "", "",
                         70.0, true, NULL, error, sizeof(error)));
    CHECK(!db_add_player(&db, alpha, "Test", NULL,
                         INFINITY, true, NULL, error, sizeof(error)));
    CHECK(db_add_player(&db, alpha, "  Ada ", " Lovelace ",
                        82.46, true, &player_id,
                        error, sizeof(error)));
    const Player *player = db_find_player(&db, player_id);
    CHECK(player != NULL);
    CHECK_STRING(player->first_name, "Ada");
    CHECK_STRING(player->last_name, "Lovelace");
    CHECK_NEAR(player->base_rating, 82.46, 1e-12);
    CHECK_NEAR(player->current_rating, 82.46, 1e-12);
    CHECK(player->matches_played == 0);

    CHECK(db_update_player(&db, player_id, beta, "Ada", "Lovelace",
                           84.0, true, error, sizeof(error)));
    player = db_find_player(&db, player_id);
    CHECK(player->team_id == beta);
    CHECK_NEAR(player->current_rating, 84.0, 1e-12);

    CHECK(db_set_player_active(&db, player_id, false, error, sizeof(error)));
    CHECK(!db_find_player(&db, player_id)->active);
    CHECK(db_set_player_active(&db, player_id, true, error, sizeof(error)));
    CHECK(db_find_player(&db, player_id)->active);

    CHECK(db_delete_player(&db, player_id, error, sizeof(error)));
    CHECK(db_find_player(&db, player_id) == NULL);
    CHECK(db_delete_player(&db, player_id, error, sizeof(error)) == false);
    db_free(&db);
    return true;
}

static bool test_player_starting_rating_is_unbounded(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    char error[256];
    uint64_t high = 0;
    uint64_t low = 0;

    CHECK(db_add_player(&db, alpha, "High", "Rating", 150.0, true,
                        &high, error, sizeof(error)));
    CHECK_NEAR(db_find_player(&db, high)->base_rating, 150.0, 1e-12);
    CHECK_NEAR(db_find_player(&db, high)->current_rating, 150.0, 1e-12);
    CHECK(db_add_player(&db, beta, "Low", "Rating", -25.0, true,
                        &low, error, sizeof(error)));
    CHECK_NEAR(db_find_player(&db, low)->base_rating, -25.0, 1e-12);
    CHECK_NEAR(db_find_player(&db, low)->current_rating, -25.0, 1e-12);

    CHECK(db_update_player(&db, high, alpha, "High", "Rating", 120.5, true,
                           error, sizeof(error)));
    CHECK_NEAR(db_find_player(&db, high)->base_rating, 120.5, 1e-12);
    CHECK(db_validate(&db, error, sizeof(error)));
    db_free(&db);
    return true;
}

static bool test_last_added_player(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    CHECK(db_last_added_player(&db) == NULL);

    char error[256];
    uint64_t first = 0;
    uint64_t second = 0;
    CHECK(db_add_player(&db, alpha, "First", "Player", 50.0, true,
                        &first, error, sizeof(error)));
    CHECK(db_last_added_player(&db) != NULL);
    CHECK(db_last_added_player(&db)->id == first);
    CHECK(db_last_added_player(&db)->team_id == alpha);

    CHECK(db_add_player(&db, beta, "Second", "Player", 55.0, true,
                        &second, error, sizeof(error)));
    CHECK(db_last_added_player(&db)->id == second);
    CHECK(db_last_added_player(&db)->team_id == beta);

    CHECK(db_delete_player(&db, second, error, sizeof(error)));
    CHECK(db_last_added_player(&db)->id == first);
    CHECK(db_last_added_player(&db)->team_id == alpha);
    db_free(&db);
    return true;
}

static bool test_record_match_and_delete(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[4];
    CHECK(add_four_players(&db, alpha, beta, players));
    char error[256];

    MatchPlayerInput lineup[] = {
        { players[0], 90, 7.55, 0 },
        { players[1], 60, 6.0, 0 },
        { players[2], 90, 6.0, 0 },
        { players[3], 90, 5.5, 0 },
    };
    MatchInput input;
    input.date = "2026-10-04";
    input.home_team_id = alpha;
    input.away_team_id = beta;
    input.home_goals = 2;
    input.away_goals = 0;
    input.neutral_venue = false;
    input.length = 90;
    input.players = lineup;
    input.player_count = 4;

    uint64_t match_id = 0;
    CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));
    CHECK(match_id == 1);
    CHECK(db.match_count == 1);
    CHECK(db.appearance_count == 4);
    const Match *match = db_find_match(&db, match_id);
    CHECK(match != NULL);
    CHECK_STRING(match->date, "2026-10-04");
    CHECK(match->rating.home_team_delta > 0.0);
    CHECK(match->rating.away_team_delta < 0.0);

    const Player *home_star = db_find_player(&db, players[0]);
    const Player *home_sub = db_find_player(&db, players[1]);
    const Player *away_one = db_find_player(&db, players[2]);
    CHECK(home_star->current_rating > 80.0);
    CHECK(home_sub->current_rating > 70.0);
    CHECK(away_one->current_rating < 75.0);
    CHECK(home_star->matches_played == 1);
    CHECK(home_star->minutes_played == 90);
    CHECK(home_sub->minutes_played == 60);

    const Appearance *star = find_appearance(&db, match_id, players[0]);
    CHECK(star != NULL);
    CHECK_NEAR(star->rating_before, 80.0, 1e-12);
    CHECK_NEAR(star->performance, 7.55, 1e-12);
    CHECK_NEAR(star->rating_after,
               db_find_player(&db, players[0])->current_rating, 1e-12);
    CHECK(star->rating_delta > 0.0);
    double match_total = 0.0;
    for (size_t i = 0; i < db.appearance_count; ++i) {
        if (db.appearances[i].match_id == match_id) {
            match_total += db.appearances[i].rating_delta;
        }
    }
    CHECK_NEAR(match_total, 0.0, 1e-9);

    double player_total = 0.0;
    for (int i = 0; i < 4; ++i) {
        player_total += db_find_player(&db, players[i])->current_rating;
    }
    CHECK_NEAR(player_total, 80.0 + 70.0 + 75.0 + 65.0, 1e-9);

    TeamStats stats;
    db_team_stats(&db, alpha, &stats);
    CHECK(stats.active_players == 2);
    CHECK(stats.matches == 1);
    CHECK(stats.wins == 1);
    CHECK(stats.draws == 0);
    CHECK(stats.losses == 0);
    db_team_stats(&db, beta, &stats);
    CHECK(stats.losses == 1);

    CHECK(db_delete_match(&db, match_id, error, sizeof(error)));
    CHECK(db.match_count == 0);
    CHECK(db.appearance_count == 0);
    CHECK_NEAR(db_find_player(&db, players[0])->current_rating, 80.0, 1e-12);
    CHECK_NEAR(db_find_player(&db, players[2])->current_rating, 75.0, 1e-12);
    CHECK(db_find_player(&db, players[0])->matches_played == 0);
    CHECK(!db_delete_match(&db, match_id, error, sizeof(error)));

    CHECK(!db_delete_team(&db, alpha, error, sizeof(error)));
    CHECK(strstr(error, "match history") != NULL || strstr(error, "players") != NULL);
    db_free(&db);
    return true;
}

static bool test_match_team_average_delta(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[4];
    CHECK(add_four_players(&db, alpha, beta, players));
    char error[256];

    MatchPlayerInput lineup[] = {
        { players[0], 90, 7.0, 0 },
        { players[1], 90, 6.0, 0 },
        { players[2], 90, 6.0, 0 },
        { players[3], 90, 5.0, 0 },
    };
    MatchInput input = {
        .date = "2026-10-04",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 2,
        .away_goals = 0,
        .length = 90,
        .players = lineup,
        .player_count = 4,
    };
    uint64_t match_id = 0;
    CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));
    CHECK(db_match_appearance_count(&db, match_id) == 4);

    double home_average = 0.0;
    double away_average = 0.0;
    CHECK(db_match_team_average_delta(&db, match_id, alpha, &home_average));
    CHECK(db_match_team_average_delta(&db, match_id, beta, &away_average));
    const Match *match = db_find_match(&db, match_id);
    CHECK(match != NULL);
    CHECK_NEAR(home_average, match->rating.home_team_delta / 2.0, 1e-12);
    CHECK_NEAR(away_average, match->rating.away_team_delta / 2.0, 1e-12);
    CHECK(!db_match_team_average_delta(&db, match_id, 999999,
                                       &home_average));
    db_free(&db);
    return true;
}

static bool test_match_validation(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[4];
    CHECK(add_four_players(&db, alpha, beta, players));
    char error[256];

    MatchPlayerInput lineup[] = {
        { players[0], 90, 6.0, 0 },
        { players[1], 90, 6.0, 0 },
        { players[2], 90, 6.0, 0 },
        { players[3], 90, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-10-04",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 1,
        .length = 90,
        .players = lineup,
        .player_count = 4,
    };
    input.date = "2026-02-30";
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));
    input.date = "2026-10-04";
    input.home_team_id = beta;
    input.away_team_id = beta;
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));
    input.home_team_id = alpha;
    input.away_team_id = beta;
    input.home_goals = 100;
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));
    input.home_goals = 1;

    MatchPlayerInput duplicate[] = {
        { players[0], 90, 6.0, 0 },
        { players[0], 90, 6.0, 0 },
        { players[2], 90, 6.0, 0 },
    };
    input.players = duplicate;
    input.player_count = 3;
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));
    CHECK(strstr(error, "more than once") != NULL);

    MatchPlayerInput bad_minutes[] = {
        { players[0], 0, 6.0, 0 },
        { players[2], 90, 6.0, 0 },
    };
    input.players = bad_minutes;
    input.player_count = 2;
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));

    MatchPlayerInput bad_performance[] = {
        { players[0], 90, 11.0, 0 },
        { players[2], 90, 6.0, 0 },
    };
    input.players = bad_performance;
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));

    MatchPlayerInput only_home[] = {
        { players[0], 90, 6.0, 0 },
        { players[1], 90, 6.0, 0 },
    };
    input.players = only_home;
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));
    CHECK(strstr(error, "each team") != NULL);

    CHECK(db_set_player_active(&db, players[3], false, error, sizeof(error)));
    input.players = lineup;
    input.player_count = 4;
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));
    CHECK(strstr(error, "archived") != NULL);
    CHECK(db_set_player_active(&db, players[3], true, error, sizeof(error)));

    uint64_t third = 0;
    uint64_t outsider = 0;
    CHECK(db_add_team(&db, "Gamma Town", "GAM", "East", &third,
                      error, sizeof(error)));
    CHECK(db_add_player(&db, third, "Eve", "Cross",
                        70.0, true, &outsider, error, sizeof(error)));
    MatchPlayerInput wrong_team[] = {
        { players[0], 90, 6.0, 0 },
        { outsider, 90, 6.0, 0 },
        { players[2], 90, 6.0, 0 },
    };
    input.players = wrong_team;
    input.player_count = 3;
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));

    CHECK(db.match_count == 0);
    CHECK(db.appearance_count == 0);
    db_free(&db);
    return true;
}

static bool test_backdated_matches_replay_chronologically(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[2];
    char error[256];
    CHECK(db_add_player(&db, alpha, "Home", "Player",
                        70.0, true, &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player",
                        70.0, true, &players[1], error, sizeof(error)));

    MatchPlayerInput lineup[] = {
        { players[0], 90, 6.0, 0 },
        { players[1], 90, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-03-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
        .length = 90,
        .players = lineup,
        .player_count = 2,
    };
    uint64_t march_id = 0;
    CHECK(db_add_match(&db, &input, &march_id, error, sizeof(error)));
    double rating_after_march = db_find_player(&db, players[0])->current_rating;
    CHECK(rating_after_march > 70.0);

    input.date = "2026-01-10";
    input.home_goals = 0;
    input.away_goals = 1;
    uint64_t january_id = 0;
    CHECK(db_add_match(&db, &input, &january_id, error, sizeof(error)));
    CHECK(db.match_count == 2);
    CHECK_STRING(db.matches[0].date, "2026-01-10");
    CHECK_STRING(db.matches[1].date, "2026-03-10");

    const Appearance *january = find_appearance(&db, january_id, players[0]);
    const Appearance *march = find_appearance(&db, march_id, players[0]);
    CHECK(january != NULL && march != NULL);
    CHECK_NEAR(january->rating_before, 70.0, 1e-12);
    CHECK(january->rating_delta < 0.0);
    CHECK_NEAR(march->rating_before, january->rating_after, 1e-12);
    CHECK_NEAR(db_find_player(&db, players[0])->current_rating,
               march->rating_after, 1e-12);
    CHECK(db_find_player(&db, players[0])->matches_played == 2);
    CHECK(db_find_player(&db, players[0])->minutes_played == 180);

    Database clean;
    CHECK(setup_two_teams(&clean, &alpha, &beta));
    uint64_t clean_players[2];
    CHECK(db_add_player(&clean, alpha, "Home", "Player",
                        70.0, true, &clean_players[0], error, sizeof(error)));
    CHECK(db_add_player(&clean, beta, "Away", "Player",
                        70.0, true, &clean_players[1], error, sizeof(error)));
    MatchPlayerInput clean_lineup[] = {
        { clean_players[0], 90, 6.0, 0 },
        { clean_players[1], 90, 6.0, 0 },
    };
    MatchInput clean_input = input;
    clean_input.date = "2026-01-10";
    clean_input.players = clean_lineup;
    clean_input.player_count = 2;
    CHECK(db_add_match(&clean, &clean_input, NULL, error, sizeof(error)));
    clean_input.date = "2026-03-10";
    clean_input.home_goals = 1;
    clean_input.away_goals = 0;
    CHECK(db_add_match(&clean, &clean_input, NULL, error, sizeof(error)));
    CHECK_NEAR(db_find_player(&db, players[0])->current_rating,
               db_find_player(&clean, clean_players[0])->current_rating,
               1e-12);
    db_free(&clean);

    CHECK(db_update_player(&db, players[0], alpha, "Home", "Player",
                           75.0, true,
                           error, sizeof(error)));
    CHECK_NEAR(find_appearance(&db, january_id, players[0])->rating_before,
               75.0, 1e-12);
    CHECK_NEAR(db_find_player(&db, players[0])->current_rating,
               find_appearance(&db, march_id, players[0])->rating_after, 1e-12);

    CHECK(db_delete_match(&db, march_id, error, sizeof(error)));
    CHECK(db.match_count == 1);
    CHECK_NEAR(db_find_player(&db, players[0])->current_rating,
               find_appearance(&db, january_id, players[0])->rating_after,
               1e-12);
    db_free(&db);
    return true;
}

static bool test_delete_player_with_history_blocked(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[2];
    char error[256];
    CHECK(db_add_player(&db, alpha, "One", "Player",
                        70.0, true, &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Two", "Player",
                        70.0, true, &players[1], error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { players[0], 90, 6.0, 0 },
        { players[1], 90, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-10-04",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 0,
        .away_goals = 0,
        .length = 90,
        .players = lineup,
        .player_count = 2,
    };
    CHECK(db_add_match(&db, &input, NULL, error, sizeof(error)));
    CHECK(!db_delete_player(&db, players[0], error, sizeof(error)));
    CHECK(strstr(error, "Archive") != NULL);
    CHECK(db_set_player_active(&db, players[0], false, error, sizeof(error)));
    CHECK(!db_find_player(&db, players[0])->active);
    CHECK(db_validate(&db, error, sizeof(error)));
    db_free(&db);
    return true;
}

static bool test_clone_and_id_continuity(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[2];
    char error[256];
    CHECK(db_add_player(&db, alpha, "One", "Player",
                        70.0, true, &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Two", "Player",
                        70.0, true, &players[1], error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { players[0], 90, 7.0, 0 },
        { players[1], 90, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-10-04",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
        .length = 90,
        .players = lineup,
        .player_count = 2,
    };
    CHECK(db_add_match(&db, &input, NULL, error, sizeof(error)));

    Database copy;
    CHECK(db_clone(&copy, &db));
    CHECK(copy.team_count == db.team_count);
    CHECK(copy.player_count == db.player_count);
    CHECK(copy.match_count == db.match_count);
    CHECK(copy.appearance_count == db.appearance_count);
    CHECK_NEAR(copy.players[0].current_rating,
               db.players[0].current_rating, 1e-12);
    CHECK(db_delete_match(&copy, copy.matches[0].id, error, sizeof(error)));
    CHECK(copy.match_count == 0);
    CHECK(db.match_count == 1);

    uint64_t next_team = 0;
    CHECK(db_add_team(&copy, "Gamma", "", "", &next_team,
                      error, sizeof(error)));
    CHECK(next_team == 3);
    db_free(&copy);
    db_free(&db);
    return true;
}

static bool test_match_total_minutes_guard(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    char error[256];
    uint64_t home_players[12];
    for (int i = 0; i < 12; ++i) {
        char first[16];
        char last[16];
        snprintf(first, sizeof(first), "Home%d", i);
        snprintf(last, sizeof(last), "Player");
        CHECK(db_add_player(&db, alpha, first, last,
                            70.0, true, &home_players[i],
                            error, sizeof(error)));
    }
    uint64_t away_player = 0;
    CHECK(db_add_player(&db, beta, "Away", "Player",
                        70.0, true, &away_player, error, sizeof(error)));

    MatchPlayerInput lineup[13];
    for (size_t i = 0; i < 12; ++i) {
        lineup[i].player_id = home_players[i];
        lineup[i].minutes = 120;
        lineup[i].performance = 6.0;
        lineup[i].sent_off = 0;
    }
    lineup[12].player_id = away_player;
    lineup[12].minutes = 1;
    lineup[12].performance = 6.0;
    lineup[12].sent_off = 0;
    MatchInput input = {
        .date = "2026-10-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
        .length = 120,
        .players = lineup,
        .player_count = 13,
    };
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));
    CHECK(strstr(error, "1,320") != NULL);
    CHECK(db.match_count == 0);
    CHECK(db.appearance_count == 0);

    MatchPlayerInput valid_lineup[] = {
        { home_players[0], 90, 6.0, 0 },
        { away_player, 90, 6.0, 0 },
    };
    MatchInput valid = input;
    valid.players = valid_lineup;
    valid.player_count = 2;
    uint64_t match_id = 0;
    CHECK(db_add_match(&db, &valid, &match_id, error, sizeof(error)));
    CHECK(db.match_count == 1);
    CHECK(db.appearance_count == 2);
    CHECK(!db_update_match(&db, match_id, &input, error, sizeof(error)));
    CHECK(strstr(error, "1,320") != NULL);
    CHECK(db.match_count == 1);
    CHECK(db.appearance_count == 2);
    db_free(&db);
    return true;
}

static bool test_neutral_venue_match(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[2];
    char error[256];
    CHECK(db_add_player(&db, alpha, "Home", "Player",
                        70.0, true, &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player",
                        70.0, true, &players[1], error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { players[0], 90, 6.0, 0 },
        { players[1], 90, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-06-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 0,
        .away_goals = 0,
        .neutral_venue = true,
        .length = 90,
        .players = lineup,
        .player_count = 2,
    };
    uint64_t match_id = 0;
    CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));
    const Match *match = db_find_match(&db, match_id);
    CHECK(match != NULL);
    CHECK(match->neutral_venue);
    CHECK_NEAR(match->rating.home_expected, 0.5, 1e-12);
    CHECK_NEAR(match->rating.home_team_delta, 0.0, 1e-12);
    CHECK_NEAR(db_find_player(&db, players[0])->current_rating, 70.0, 1e-12);

    input.date = "2026-06-17";
    input.neutral_venue = false;
    input.home_goals = 1;
    CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));
    match = db_find_match(&db, match_id);
    CHECK(match != NULL);
    CHECK(!match->neutral_venue);
    CHECK(match->rating.home_expected > 0.5);
    CHECK(match->rating.home_team_delta > 0.0);
    db_free(&db);
    return true;
}

static bool test_recompute_sorts_matches_by_date(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[2];
    char error[256];
    CHECK(db_add_player(&db, alpha, "Home", "Player",
                        70.0, true, &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player",
                        70.0, true, &players[1], error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { players[0], 90, 6.0, 0 },
        { players[1], 90, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-01-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
        .length = 90,
        .players = lineup,
        .player_count = 2,
    };
    CHECK(db_add_match(&db, &input, NULL, error, sizeof(error)));
    input.date = "2026-03-10";
    input.home_goals = 0;
    input.away_goals = 1;
    CHECK(db_add_match(&db, &input, NULL, error, sizeof(error)));
    double expected_rating = db_find_player(&db, players[0])->current_rating;

    Match temporary = db.matches[0];
    db.matches[0] = db.matches[1];
    db.matches[1] = temporary;
    CHECK_STRING(db.matches[0].date, "2026-03-10");
    CHECK(db_recompute_ratings(&db, error, sizeof(error)));
    CHECK_STRING(db.matches[0].date, "2026-01-10");
    CHECK_STRING(db.matches[1].date, "2026-03-10");
    CHECK_NEAR(db_find_player(&db, players[0])->current_rating,
               expected_rating, 1e-12);
    db_free(&db);
    return true;
}

static bool test_recompute_rejects_invalid_database(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[2];
    char error[256];
    CHECK(db_add_player(&db, alpha, "Home", "Player",
                        70.0, true, &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player",
                        70.0, true, &players[1], error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { players[0], 90, 6.0, 0 },
        { players[1], 90, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-10-04",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
        .length = 90,
        .players = lineup,
        .player_count = 2,
    };
    CHECK(db_add_match(&db, &input, NULL, error, sizeof(error)));
    db.appearances[0].player_id = 999999;
    CHECK(!db_recompute_ratings(&db, error, sizeof(error)));
    CHECK(strstr(error, "Invalid match appearance") != NULL);
    CHECK(!db_validate(&db, error, sizeof(error)));
    db_free(&db);
    return true;
}

static bool test_player_transfer_preserves_historical_team(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t gamma = 0;
    char error[256];
    CHECK(db_add_team(&db, "Gamma Town", "GAM", "East", &gamma,
                      error, sizeof(error)));
    uint64_t home_player = 0;
    uint64_t away_player = 0;
    CHECK(db_add_player(&db, alpha, "Home", "Player",
                        72.0, true, &home_player, error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player",
                        68.0, true, &away_player, error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { home_player, 90, 6.5, 0 },
        { away_player, 90, 5.5, 0 },
    };
    MatchInput input = {
        .date = "2026-05-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
        .length = 90,
        .players = lineup,
        .player_count = 2,
    };
    uint64_t match_id = 0;
    CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));
    double rating_before_transfer =
        db_find_player(&db, home_player)->current_rating;

    CHECK(db_update_player(&db, home_player, gamma, "Home", "Player",
                           72.0, true,
                           error, sizeof(error)));
    CHECK(db_find_player(&db, home_player)->team_id == gamma);
    CHECK_NEAR(db_find_player(&db, home_player)->current_rating,
               rating_before_transfer, 1e-12);
    CHECK(db_validate(&db, error, sizeof(error)));
    const Appearance *appearance = find_appearance(&db, match_id, home_player);
    CHECK(appearance != NULL);
    CHECK(appearance->team_id == alpha);

    uint64_t gamma_opponent = 0;
    CHECK(db_add_player(&db, gamma, "New", "Teammate",
                        69.0, true, &gamma_opponent, error, sizeof(error)));
    MatchPlayerInput next_lineup[] = {
        { home_player, 90, 6.0, 0 },
        { gamma_opponent, 90, 6.0, 0 },
        { away_player, 90, 6.0, 0 },
    };
    MatchInput next = {
        .date = "2026-05-17",
        .home_team_id = gamma,
        .away_team_id = beta,
        .home_goals = 2,
        .away_goals = 0,
        .length = 90,
        .players = next_lineup,
        .player_count = 3,
    };
    CHECK(db_add_match(&db, &next, NULL, error, sizeof(error)));
    CHECK(db_find_player(&db, home_player)->matches_played == 2);
    CHECK(db.appearances[0].team_id == alpha ||
          db.appearances[1].team_id == alpha);
    CHECK(db_validate(&db, error, sizeof(error)));
    db_free(&db);
    return true;
}

static bool test_update_match_replays_chronology(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[4];
    CHECK(add_four_players(&db, alpha, beta, players));
    char error[256];

    MatchPlayerInput lineup[] = {
        { players[0], 90, 7.0, 0 },
        { players[1], 60, 6.0, 0 },
        { players[2], 90, 6.0, 0 },
        { players[3], 90, 5.0, 0 },
    };
    MatchInput input = {
        .date = "2026-10-04",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 2,
        .away_goals = 0,
        .length = 90,
        .players = lineup,
        .player_count = 4,
    };
    uint64_t match_id = 0;
    CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));

    MatchInput later = input;
    later.date = "2026-11-01";
    later.home_goals = 1;
    later.away_goals = 1;
    uint64_t later_id = 0;
    CHECK(db_add_match(&db, &later, &later_id, error, sizeof(error)));
    CHECK(match_id != later_id);

    uint64_t next_id_before = db.next_match_id;
    MatchPlayerInput revised[] = {
        { players[0], 45, 8.5, 0 },
        { players[1], 90, 5.0, 0 },
        { players[2], 90, 7.0, 0 },
        { players[3], 30, 6.0, 0 },
    };
    MatchInput edit = input;
    edit.date = "2026-09-15";
    edit.home_goals = 0;
    edit.away_goals = 3;
    edit.neutral_venue = true;
    edit.players = revised;
    edit.player_count = 4;
    CHECK(db_update_match(&db, match_id, &edit, error, sizeof(error)));
    CHECK(db.next_match_id == next_id_before);
    CHECK(db.match_count == 2);
    CHECK(db.appearance_count == 8);
    CHECK(db.matches[0].id == match_id);
    CHECK(db.matches[1].id == later_id);

    const Match *updated = db_find_match(&db, match_id);
    CHECK(updated != NULL);
    CHECK_STRING(updated->date, "2026-09-15");
    CHECK(updated->home_goals == 0);
    CHECK(updated->away_goals == 3);
    CHECK(updated->neutral_venue);
    CHECK(updated->rating.home_expected > 0.5);
    CHECK(updated->rating.away_team_delta > 0.0);

    const Appearance *star = find_appearance(&db, match_id, players[0]);
    CHECK(star != NULL);
    CHECK(star->minutes == 45);
    CHECK_NEAR(star->performance, 8.5, 1e-12);
    CHECK_NEAR(star->rating_before, 80.0, 1e-12);
    const Appearance *later_star =
        find_appearance(&db, later_id, players[0]);
    CHECK(later_star != NULL);
    CHECK_NEAR(later_star->rating_before, star->rating_after, 1e-12);

    double first_total = 0.0;
    for (size_t i = 0; i < db.appearance_count; ++i) {
        if (db.appearances[i].match_id == match_id) {
            first_total += db.appearances[i].rating_delta;
        }
    }
    CHECK_NEAR(first_total, 0.0, 1e-9);
    CHECK(db_find_player(&db, players[0])->matches_played == 2);
    CHECK(db_find_player(&db, players[0])->minutes_played == 135);
    CHECK(db_validate(&db, error, sizeof(error)));
    db_free(&db);
    return true;
}

static bool test_update_match_preserves_transferred_and_archived_players(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t gamma = 0;
    char error[256];
    CHECK(db_add_team(&db, "Gamma Town", "GAM", "East", &gamma,
                      error, sizeof(error)));

    uint64_t home_player = 0;
    uint64_t away_player = 0;
    uint64_t new_home = 0;
    uint64_t outsider = 0;
    CHECK(db_add_player(&db, alpha, "Home", "Player", 72.0, true,
                        &home_player, error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player", 68.0, true,
                        &away_player, error, sizeof(error)));
    CHECK(db_add_player(&db, alpha, "New", "Teammate", 60.0, true,
                        &new_home, error, sizeof(error)));
    CHECK(db_add_player(&db, gamma, "Gamma", "Player", 70.0, true,
                        &outsider, error, sizeof(error)));

    MatchPlayerInput lineup[] = {
        { home_player, 90, 6.5, 0 },
        { away_player, 90, 5.5, 0 },
    };
    MatchInput input = {
        .date = "2026-05-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
        .length = 90,
        .players = lineup,
        .player_count = 2,
    };
    uint64_t match_id = 0;
    CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));

    CHECK(db_update_player(&db, home_player, gamma, "Home", "Player",
                           72.0, true, error, sizeof(error)));
    CHECK(db_set_player_active(&db, away_player, false,
                               error, sizeof(error)));

    MatchPlayerInput revised[] = {
        { home_player, 75, 7.5, 0 },
        { new_home, 90, 6.0, 0 },
        { away_player, 80, 5.0, 0 },
    };
    MatchInput edit = input;
    edit.date = "2026-05-10";
    edit.home_goals = 2;
    edit.away_goals = 1;
    edit.players = revised;
    edit.player_count = 3;
    CHECK(db_update_match(&db, match_id, &edit, error, sizeof(error)));
    CHECK(db_validate(&db, error, sizeof(error)));

    const Appearance *transferred =
        find_appearance(&db, match_id, home_player);
    const Appearance *archived =
        find_appearance(&db, match_id, away_player);
    const Appearance *added = find_appearance(&db, match_id, new_home);
    CHECK(transferred != NULL && transferred->team_id == alpha);
    CHECK(archived != NULL && archived->team_id == beta);
    CHECK(added != NULL && added->team_id == alpha);
    CHECK(db_find_player(&db, home_player)->team_id == gamma);
    CHECK(!db_find_player(&db, away_player)->active);
    CHECK(transferred->minutes == 75);
    CHECK_NEAR(db_find_player(&db, home_player)->current_rating,
               transferred->rating_after, 1e-12);

    MatchPlayerInput invalid[] = {
        { home_player, 90, 6.0, 0 },
        { away_player, 90, 6.0, 0 },
        { outsider, 90, 6.0, 0 },
    };
    MatchInput bad = edit;
    bad.players = invalid;
    bad.player_count = 3;
    size_t appearances_before = db.appearance_count;
    CHECK(!db_update_match(&db, match_id, &bad, error, sizeof(error)));
    CHECK(strstr(error, "does not belong") != NULL);
    CHECK(db.appearance_count == appearances_before);
    CHECK_STRING(db_find_match(&db, match_id)->date, "2026-05-10");

    MatchPlayerInput moved_side[] = {
        { home_player, 90, 6.0, 0 },
        { outsider, 90, 6.0, 0 },
    };
    MatchInput changed_teams = bad;
    changed_teams.home_team_id = gamma;
    changed_teams.away_team_id = beta;
    changed_teams.players = moved_side;
    changed_teams.player_count = 2;
    CHECK(!db_update_match(&db, match_id, &changed_teams,
                           error, sizeof(error)));
    CHECK(strstr(error, "original club") != NULL);
    db_free(&db);
    return true;
}

static bool test_update_match_validation(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[4];
    CHECK(add_four_players(&db, alpha, beta, players));
    char error[256];

    MatchPlayerInput lineup[] = {
        { players[0], 90, 6.0, 0 },
        { players[2], 90, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-10-04",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 1,
        .length = 90,
        .players = lineup,
        .player_count = 2,
    };
    uint64_t match_id = 0;
    CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));
    size_t appearances_before = db.appearance_count;

    MatchInput bad = input;
    CHECK(!db_update_match(&db, 999, &bad, error, sizeof(error)));
    CHECK(strstr(error, "not found") != NULL);

    bad.date = "2026-02-30";
    CHECK(!db_update_match(&db, match_id, &bad, error, sizeof(error)));
    CHECK(strstr(error, "valid date") != NULL);
    CHECK_STRING(db_find_match(&db, match_id)->date, "2026-10-04");

    bad = input;
    bad.home_team_id = beta;
    bad.away_team_id = beta;
    CHECK(!db_update_match(&db, match_id, &bad, error, sizeof(error)));

    bad = input;
    bad.home_goals = 100;
    CHECK(!db_update_match(&db, match_id, &bad, error, sizeof(error)));

    bad = input;
    bad.players = NULL;
    CHECK(!db_update_match(&db, match_id, &bad, error, sizeof(error)));

    MatchPlayerInput duplicate[] = {
        { players[0], 90, 6.0, 0 },
        { players[0], 90, 6.0, 0 },
        { players[2], 90, 6.0, 0 },
    };
    bad = input;
    bad.players = duplicate;
    bad.player_count = 3;
    CHECK(!db_update_match(&db, match_id, &bad, error, sizeof(error)));
    CHECK(strstr(error, "more than once") != NULL);

    MatchPlayerInput bad_minutes[] = {
        { players[0], 0, 6.0, 0 },
        { players[2], 90, 6.0, 0 },
    };
    bad = input;
    bad.players = bad_minutes;
    CHECK(!db_update_match(&db, match_id, &bad, error, sizeof(error)));

    MatchPlayerInput bad_performance[] = {
        { players[0], 90, 11.0, 0 },
        { players[2], 90, 6.0, 0 },
    };
    bad = input;
    bad.players = bad_performance;
    CHECK(!db_update_match(&db, match_id, &bad, error, sizeof(error)));

    MatchPlayerInput only_home[] = {
        { players[0], 90, 6.0, 0 },
        { players[1], 90, 6.0, 0 },
    };
    bad = input;
    bad.players = only_home;
    CHECK(!db_update_match(&db, match_id, &bad, error, sizeof(error)));
    CHECK(strstr(error, "each team") != NULL);

    uint64_t substitute = 0;
    CHECK(db_add_player(&db, alpha, "Late", "Substitute", 60.0, true,
                        &substitute, error, sizeof(error)));
    CHECK(db_set_player_active(&db, substitute, false, error, sizeof(error)));
    MatchPlayerInput archived[] = {
        { players[0], 90, 6.0, 0 },
        { players[2], 90, 6.0, 0 },
        { substitute, 90, 6.0, 0 },
    };
    bad = input;
    bad.players = archived;
    bad.player_count = 3;
    CHECK(!db_update_match(&db, match_id, &bad, error, sizeof(error)));
    CHECK(strstr(error, "archived") != NULL);

    CHECK(db.appearance_count == appearances_before);
    CHECK_STRING(db_find_match(&db, match_id)->date, "2026-10-04");
    CHECK(db_validate(&db, error, sizeof(error)));
    db_free(&db);
    return true;
}

static bool test_ratings_before_match(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    char error[256];
    uint64_t players[2];
    CHECK(db_add_player(&db, alpha, "Home", "Player", 80.0, true,
                        &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player", 60.0, true,
                        &players[1], error, sizeof(error)));

    MatchPlayerInput lineup[] = {
        { players[0], 90, 6.0, 0 },
        { players[1], 90, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-01-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 2,
        .away_goals = 0,
        .length = 90,
        .players = lineup,
        .player_count = 2,
    };
    uint64_t same_date_first = 0;
    CHECK(db_add_match(&db, &input, &same_date_first, error, sizeof(error)));
    input.home_goals = 1;
    input.away_goals = 1;
    uint64_t same_date_second = 0;
    CHECK(db_add_match(&db, &input, &same_date_second, error, sizeof(error)));
    input.date = "2026-02-10";
    input.home_goals = 0;
    input.away_goals = 2;
    uint64_t later_match = 0;
    CHECK(db_add_match(&db, &input, &later_match, error, sizeof(error)));

    double ratings[2] = { 0.0, 0.0 };
    CHECK(db_ratings_before_match(&db, "2026-01-10", same_date_first,
                                  ratings, error, sizeof(error)));
    CHECK_NEAR(ratings[0], 80.0, 1e-12);
    CHECK_NEAR(ratings[1], 60.0, 1e-12);

    CHECK(db_ratings_before_match(&db, "2026-01-10", same_date_second,
                                  ratings, error, sizeof(error)));
    const Appearance *first_home =
        find_appearance(&db, same_date_first, players[0]);
    const Appearance *first_away =
        find_appearance(&db, same_date_first, players[1]);
    CHECK(first_home != NULL && first_away != NULL);
    CHECK_NEAR(ratings[0], first_home->rating_after, 1e-12);
    CHECK_NEAR(ratings[1], first_away->rating_after, 1e-12);

    CHECK(db_ratings_before_match(&db, "2026-02-10", later_match,
                                  ratings, error, sizeof(error)));
    const Appearance *second_home =
        find_appearance(&db, same_date_second, players[0]);
    const Appearance *second_away =
        find_appearance(&db, same_date_second, players[1]);
    CHECK(second_home != NULL && second_away != NULL);
    CHECK_NEAR(ratings[0], second_home->rating_after, 1e-12);
    CHECK_NEAR(ratings[1], second_away->rating_after, 1e-12);

    double current_home = db_find_player(&db, players[0])->current_rating;
    double current_away = db_find_player(&db, players[1])->current_rating;
    CHECK(db_ratings_before_match(&db, "2026-03-01", db.next_match_id,
                                  ratings, error, sizeof(error)));
    CHECK_NEAR(ratings[0], current_home, 1e-12);
    CHECK_NEAR(ratings[1], current_away, 1e-12);

    CHECK(!db_ratings_before_match(&db, "not-a-date", db.next_match_id,
                                   ratings, error, sizeof(error)));
    CHECK(!db_ratings_before_match(&db, "2026-03-01", db.next_match_id,
                                   NULL, error, sizeof(error)));
    CHECK_NEAR(db_find_player(&db, players[0])->current_rating,
               current_home, 1e-12);
    CHECK_NEAR(db_find_player(&db, players[1])->current_rating,
               current_away, 1e-12);
    db_free(&db);
    return true;
}

static bool test_sent_off_weighting_and_validation(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    char error[256];
    uint64_t sent_off = 0;
    uint64_t full_match = 0;
    uint64_t opponent = 0;
    CHECK(db_add_player(&db, alpha, "Sent", "Off", 70.0, true, &sent_off,
                        error, sizeof(error)));
    CHECK(db_add_player(&db, alpha, "Full", "Match", 70.0, true, &full_match,
                        error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player", 70.0, true, &opponent,
                        error, sizeof(error)));

    MatchPlayerInput lineup[] = {
        { sent_off, 30, 6.0, 30 },
        { full_match, 90, 6.0, 0 },
        { opponent, 90, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-10-04",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 0,
        .away_goals = 1,
        .neutral_venue = false,
        .length = 99,
        .players = lineup,
        .player_count = 3,
    };
    uint64_t match_id = 0;
    CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));
    const Match *match = db_find_match(&db, match_id);
    CHECK(match != NULL);
    CHECK(match->length == 99);
    double sent_delta = 0.0;
    double total = 0.0;
    for (size_t i = 0; i < db.appearance_count; ++i) {
        const Appearance *appearance = &db.appearances[i];
        CHECK(appearance->sent_off == (appearance->player_id == sent_off
                                          ? 30
                                          : 0));
        if (appearance->player_id == sent_off) {
            sent_delta = appearance->rating_delta;
        }
        total += appearance->rating_delta;
    }
    CHECK_NEAR(total, 0.0, 1e-12);

    db_free(&db);
    CHECK(setup_two_teams(&db, &alpha, &beta));
    CHECK(db_add_player(&db, alpha, "Sent", "Off", 70.0, true, &sent_off,
                        error, sizeof(error)));
    CHECK(db_add_player(&db, alpha, "Full", "Match", 70.0, true, &full_match,
                        error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player", 70.0, true, &opponent,
                        error, sizeof(error)));
    MatchPlayerInput actual_lineup[] = {
        { sent_off, 30, 6.0, 0 },
        { full_match, 90, 6.0, 0 },
        { opponent, 90, 6.0, 0 },
    };
    input.players = actual_lineup;
    CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));
    double actual_delta = 0.0;
    bool found_actual = false;
    for (size_t i = 0; i < db.appearance_count; ++i) {
        if (db.appearances[i].match_id == match_id &&
            db.appearances[i].player_id == sent_off) {
            actual_delta = db.appearances[i].rating_delta;
            found_actual = true;
        }
    }
    CHECK(found_actual);
    CHECK(sent_delta < actual_delta);

    db_free(&db);
    CHECK(setup_two_teams(&db, &alpha, &beta));
    CHECK(db_add_player(&db, alpha, "Sent", "Off", 70.0, true, &sent_off,
                        error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player", 70.0, true, &opponent,
                        error, sizeof(error)));
    MatchPlayerInput bad_lineup[] = {
        { sent_off, 30, 6.0, 20 },
        { opponent, 90, 6.0, 0 },
    };
    MatchInput bad = input;
    bad.length = 90;
    bad.players = bad_lineup;
    bad.player_count = 2;
    CHECK(!db_add_match(&db, &bad, NULL, error, sizeof(error)));
    bad_lineup[0].sent_off = 95;
    CHECK(!db_add_match(&db, &bad, NULL, error, sizeof(error)));
    db_free(&db);
    return true;
}

static bool test_sent_off_win_uses_actual_minutes(void)
{
    double deltas[2] = { 0.0, 0.0 };
    for (int sent = 0; sent < 2; ++sent) {
        Database db;
        uint64_t alpha = 0;
        uint64_t beta = 0;
        CHECK(setup_two_teams(&db, &alpha, &beta));
        char error[256];
        uint64_t sent_off = 0;
        uint64_t team_mate = 0;
        uint64_t opponent = 0;
        CHECK(db_add_player(&db, alpha, "Sent", "Off", 70.0, true,
                            &sent_off, error, sizeof(error)));
        CHECK(db_add_player(&db, alpha, "Full", "Match", 70.0, true,
                            &team_mate, error, sizeof(error)));
        CHECK(db_add_player(&db, beta, "Away", "Player", 70.0, true,
                            &opponent, error, sizeof(error)));
        MatchPlayerInput lineup[] = {
            { sent_off, 30, 6.0, sent ? 30 : 0 },
            { team_mate, 90, 6.0, 0 },
            { opponent, 90, 6.0, 0 },
        };
        MatchInput input = {
            .date = "2026-10-04",
            .home_team_id = alpha,
            .away_team_id = beta,
            .home_goals = 1,
            .away_goals = 0,
            .neutral_venue = false,
            .length = 99,
            .players = lineup,
            .player_count = 3,
        };
        uint64_t match_id = 0;
        CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));
        bool found = false;
        for (size_t i = 0; i < db.appearance_count; ++i) {
            if (db.appearances[i].match_id == match_id &&
                db.appearances[i].player_id == sent_off) {
                deltas[sent] = db.appearances[i].rating_delta;
                found = true;
            }
        }
        CHECK(found);
        db_free(&db);
    }
    CHECK_NEAR(deltas[0], deltas[1], 1e-12);
    return true;
}

static bool test_find_player_by_name(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    char error[256];
    uint64_t ada = 0;
    uint64_t duplicate = 0;
    uint64_t cher = 0;
    uint64_t grace = 0;

    CHECK(db_add_player(&db, alpha, "Ada", "Lovelace", 80.0, true,
                        &ada, error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "  ada ", " lovelace ", 70.0, true,
                        &duplicate, error, sizeof(error)));
    CHECK(db_add_player(&db, alpha, "Cher", "", 60.0, true,
                        &cher, error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Grace", "Hopper", 65.0, true,
                        &grace, error, sizeof(error)));

    size_t count = 0;
    const Player *found = db_find_player_by_name(&db, " ADA ", "Lovelace",
                                                 &count);
    CHECK(found != NULL);
    CHECK(found->id == ada);
    CHECK(count == 2);

    found = db_find_player_by_name(&db, "Cher", NULL, &count);
    CHECK(found != NULL);
    CHECK(found->id == cher);
    CHECK(count == 1);

    found = db_find_player_by_name(&db, "Grace", "Hopper", NULL);
    CHECK(found != NULL);
    CHECK(found->id == grace);

    found = db_find_player_by_name(&db, "Ada", "Byron", &count);
    CHECK(found == NULL);
    CHECK(count == 0);

    char too_long[PERSON_NAME_MAX + 2];
    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    found = db_find_player_by_name(&db, too_long, "", &count);
    CHECK(found == NULL);
    CHECK(count == 0);

    db_free(&db);
    return true;
}

static bool test_find_active_player_by_name_in_team(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    char error[256];
    uint64_t away_ada = 0;
    uint64_t home_ada = 0;
    uint64_t archived = 0;

    CHECK(db_add_player(&db, beta, "Ada", "Lovelace", 80.0, true,
                        &away_ada, error, sizeof(error)));
    CHECK(db_add_player(&db, alpha, "  ada ", " lovelace ", 70.0, true,
                        &home_ada, error, sizeof(error)));
    CHECK(db_add_player(&db, alpha, "Old", "Timer", 60.0, true,
                        &archived, error, sizeof(error)));
    CHECK(db_set_player_active(&db, archived, false, error, sizeof(error)));

    const Player *found = db_find_active_player_by_name_in_team(
        &db, " ADA ", "LOVELACE", alpha);
    CHECK(found != NULL);
    CHECK(found->id == home_ada);

    found = db_find_active_player_by_name_in_team(
        &db, "Ada", "Lovelace", beta);
    CHECK(found != NULL);
    CHECK(found->id == away_ada);

    CHECK(db_find_active_player_by_name_in_team(
              &db, "Ada", "Lovelace", 999999) == NULL);
    CHECK(db_find_active_player_by_name_in_team(
              &db, "Old", "Timer", alpha) == NULL);
    CHECK(db_find_active_player_by_name_in_team(
              &db, "Nobody", "Here", alpha) == NULL);

    db_free(&db);
    return true;
}

static bool test_team_stats_are_minutes_and_results_aware(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[2];
    char error[256];
    CHECK(db_add_player(&db, alpha, "A", "Player",
                        60.0, true, &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, alpha, "B", "Player",
                        80.0, true, &players[1], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "C", "Player",
                        70.0, true, NULL, error, sizeof(error)));
    TeamStats stats;
    db_team_stats(&db, alpha, &stats);
    CHECK(stats.roster_size == 2);
    CHECK(stats.active_players == 2);
    CHECK_NEAR(stats.average_rating, 70.0, 1e-12);
    CHECK(stats.matches == 0);
    db_free(&db);
    return true;
}

static bool same_rating_summary(const RatingMatchSummary *left,
                                const RatingMatchSummary *right)
{
    return fabs(left->home_strength - right->home_strength) <= 1e-12 &&
           fabs(left->away_strength - right->away_strength) <= 1e-12 &&
           fabs(left->home_expected - right->home_expected) <= 1e-12 &&
           fabs(left->away_expected - right->away_expected) <= 1e-12 &&
           fabs(left->home_team_delta - right->home_team_delta) <= 1e-12 &&
           fabs(left->away_team_delta - right->away_team_delta) <= 1e-12 &&
           fabs(left->margin_multiplier - right->margin_multiplier) <= 1e-12 &&
           fabs(left->home_result - right->home_result) <= 1e-12 &&
           fabs(left->home_context.total_participation -
                right->home_context.total_participation) <= 1e-12 &&
           fabs(left->home_context.total_result_participation -
                right->home_context.total_result_participation) <= 1e-12 &&
           fabs(left->home_context.weighted_performance -
                right->home_context.weighted_performance) <= 1e-12 &&
           fabs(left->away_context.total_participation -
                right->away_context.total_participation) <= 1e-12 &&
           fabs(left->away_context.total_result_participation -
                right->away_context.total_result_participation) <= 1e-12 &&
           fabs(left->away_context.weighted_performance -
                right->away_context.weighted_performance) <= 1e-12;
}

static bool same_derived_state(const Database *left, const Database *right)
{
    if (left->player_count != right->player_count ||
        left->match_count != right->match_count ||
        left->appearance_count != right->appearance_count) {
        return false;
    }
    for (size_t i = 0; i < left->player_count; ++i) {
        const Player *a = &left->players[i];
        const Player *b = &right->players[i];
        if (a->id != b->id ||
            fabs(a->current_rating - b->current_rating) > 1e-12 ||
            a->matches_played != b->matches_played ||
            a->minutes_played != b->minutes_played) {
            return false;
        }
    }
    for (size_t i = 0; i < left->match_count; ++i) {
        const Match *a = &left->matches[i];
        const Match *b = &right->matches[i];
        if (a->id != b->id || !same_rating_summary(&a->rating, &b->rating)) {
            return false;
        }
    }
    for (size_t i = 0; i < left->appearance_count; ++i) {
        const Appearance *a = &left->appearances[i];
        const Appearance *b = &right->appearances[i];
        if (a->match_id != b->match_id || a->player_id != b->player_id ||
            a->team_id != b->team_id || a->minutes != b->minutes ||
            a->sent_off != b->sent_off ||
            fabs(a->performance - b->performance) > 1e-12 ||
            fabs(a->rating_before - b->rating_before) > 1e-12 ||
            fabs(a->rating_delta - b->rating_delta) > 1e-12 ||
            fabs(a->rating_after - b->rating_after) > 1e-12) {
            return false;
        }
    }
    return true;
}

static bool check_incremental_matches_full(Database *db, char *error,
                                           size_t error_size)
{
    Database full;
    if (!db_clone(&full, db)) {
        return false;
    }
    bool ok = db_recompute_ratings(&full, error, error_size) &&
              same_derived_state(db, &full);
    db_free(&full);
    return ok;
}

static bool test_incremental_replay_matches_full_recompute(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[4];
    CHECK(add_four_players(&db, alpha, beta, players));
    char error[512];

    MatchPlayerInput first_lineup[] = {
        { players[0], 90, 7.0, 0 },
        { players[2], 90, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-01-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
        .length = 90,
        .players = first_lineup,
        .player_count = 2,
    };
    CHECK(db_add_match(&db, &input, NULL, error, sizeof(error)));

    MatchPlayerInput second_lineup[] = {
        { players[1], 90, 6.0, 0 },
        { players[3], 90, 5.0, 0 },
    };
    input.date = "2026-02-10";
    input.home_goals = 0;
    input.away_goals = 1;
    input.players = second_lineup;
    uint64_t second = 0;
    CHECK(db_add_match(&db, &input, &second, error, sizeof(error)));

    MatchPlayerInput third_lineup[] = {
        { players[0], 90, 6.0, 0 },
        { players[1], 90, 6.0, 0 },
        { players[2], 90, 6.0, 0 },
        { players[3], 90, 6.0, 0 },
    };
    input.date = "2026-03-10";
    input.home_goals = 2;
    input.away_goals = 0;
    input.players = third_lineup;
    input.player_count = 4;
    uint64_t third = 0;
    CHECK(db_add_match(&db, &input, &third, error, sizeof(error)));
    CHECK(check_incremental_matches_full(&db, error, sizeof(error)));

    MatchPlayerInput fourth_lineup[] = {
        { players[0], 90, 6.5, 0 },
        { players[2], 90, 5.5, 0 },
    };
    input.date = "2026-04-10";
    input.home_goals = 1;
    input.away_goals = 1;
    input.players = fourth_lineup;
    input.player_count = 2;
    CHECK(db_add_match(&db, &input, NULL, error, sizeof(error)));
    CHECK(check_incremental_matches_full(&db, error, sizeof(error)));

    MatchPlayerInput revised[] = {
        { players[1], 90, 8.0, 0 },
        { players[3], 90, 4.0, 0 },
    };
    input.date = "2026-03-20";
    input.home_goals = 2;
    input.away_goals = 1;
    input.players = revised;
    input.player_count = 2;
    CHECK(db_update_match(&db, second, &input, error, sizeof(error)));
    CHECK(check_incremental_matches_full(&db, error, sizeof(error)));

    CHECK(db_update_player(&db, players[1], alpha, "Noah", "Bennett",
                           80.0, true, error, sizeof(error)));
    CHECK(check_incremental_matches_full(&db, error, sizeof(error)));

    CHECK(db_delete_match(&db, third, error, sizeof(error)));
    CHECK(check_incremental_matches_full(&db, error, sizeof(error)));

    db_free(&db);
    return true;
}

static bool test_ratings_before_match_excludes_edited_match(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    char error[256];
    uint64_t players[2];
    CHECK(db_add_player(&db, alpha, "Home", "Player", 50.0, true,
                        &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player", 50.0, true,
                        &players[1], error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { players[0], 90, 6.0, 0 },
        { players[1], 90, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-01-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 3,
        .away_goals = 0,
        .length = 90,
        .players = lineup,
        .player_count = 2,
    };
    uint64_t match_id = 0;
    CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));

    /*
     * The edit dialog previews the match being edited at the date typed in
     * the form. Moving the only match to a later date must still exclude the
     * match itself from its own pre-match ratings.
     */
    double ratings[2] = { 0.0, 0.0 };
    CHECK(db_ratings_before_match(&db, "2026-02-10", match_id, ratings,
                                  error, sizeof(error)));
    CHECK_NEAR(ratings[0], 50.0, 1e-12);
    CHECK_NEAR(ratings[1], 50.0, 1e-12);

    /*
     * Fixtures between the old and new date still count, but they must be
     * replayed without the edited match. The ratings captured from the
     * preview must match the stored ratings after the edit is removed.
     */
    MatchInput later = input;
    later.date = "2026-02-10";
    later.home_goals = 1;
    later.away_goals = 0;
    uint64_t later_match = 0;
    CHECK(db_add_match(&db, &later, &later_match, error, sizeof(error)));
    CHECK(db_ratings_before_match(&db, "2026-03-10", match_id, ratings,
                                  error, sizeof(error)));
    CHECK(db_delete_match(&db, match_id, error, sizeof(error)));
    const Appearance *later_home =
        find_appearance(&db, later_match, players[0]);
    const Appearance *later_away =
        find_appearance(&db, later_match, players[1]);
    CHECK(later_home != NULL && later_away != NULL);
    CHECK_NEAR(ratings[0], later_home->rating_after, 1e-12);
    CHECK_NEAR(ratings[1], later_away->rating_after, 1e-12);
    db_free(&db);
    return true;
}

static bool test_match_rejects_minutes_beyond_final_whistle(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    char error[256];
    uint64_t players[2];
    CHECK(db_add_player(&db, alpha, "Home", "Player", 50.0, true,
                        &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player", 50.0, true,
                        &players[1], error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { players[0], 120, 6.0, 0 },
        { players[1], 45, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-01-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
        .length = 45,
        .players = lineup,
        .player_count = 2,
    };
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));
    CHECK(strstr(error, "final whistle") != NULL);
    CHECK(db.match_count == 0);
    CHECK(db.appearance_count == 0);

    lineup[0].minutes = 45;
    uint64_t match_id = 0;
    CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));
    CHECK(db.match_count == 1);

    lineup[0].minutes = 46;
    CHECK(!db_update_match(&db, match_id, &input, error, sizeof(error)));
    CHECK(strstr(error, "final whistle") != NULL);
    CHECK(db.match_count == 1);
    CHECK(db.appearance_count == 2);
    const Appearance *home = find_appearance(&db, match_id, players[0]);
    CHECK(home != NULL);
    CHECK(home->minutes == 45);
    db_free(&db);
    return true;
}

static bool test_validate_scales_to_several_seasons(void)
{
    /* Five seasons of a 20-club league: 1,900 matches, 57,000 appearances. */
    enum { TEAMS = 20, SQUAD = 25, SEASONS = 5, PER_SIDE = 15 };
    Database db;
    db_init(&db);
    size_t match_total = (size_t)SEASONS * TEAMS * (TEAMS - 1);
    size_t appearance_total = match_total * PER_SIDE * 2;
    db.teams = calloc(TEAMS, sizeof(*db.teams));
    db.players = calloc(TEAMS * SQUAD, sizeof(*db.players));
    db.matches = calloc(match_total, sizeof(*db.matches));
    db.appearances = calloc(appearance_total, sizeof(*db.appearances));
    CHECK(db.teams != NULL && db.players != NULL && db.matches != NULL &&
          db.appearances != NULL);
    db.team_count = db.team_capacity = TEAMS;
    db.player_count = db.player_capacity = TEAMS * SQUAD;
    db.match_capacity = match_total;
    db.appearance_capacity = appearance_total;
    for (int i = 0; i < TEAMS; ++i) {
        db.teams[i].id = (uint64_t)i + 1;
        snprintf(db.teams[i].name, sizeof(db.teams[i].name), "Team %d", i);
    }
    for (int i = 0; i < TEAMS * SQUAD; ++i) {
        Player *player = &db.players[i];
        player->id = (uint64_t)i + 1;
        player->team_id = (uint64_t)(i / SQUAD) + 1;
        snprintf(player->first_name, sizeof(player->first_name), "P%d", i);
        player->base_rating = 50.0;
        player->active = true;
    }
    size_t match_index = 0;
    size_t appearance_index = 0;
    for (int season = 0; season < SEASONS; ++season) {
        for (int home = 0; home < TEAMS; ++home) {
            for (int away = 0; away < TEAMS; ++away) {
                if (home == away) {
                    continue;
                }
                Match *match = &db.matches[match_index];
                match->id = (uint64_t)match_index + 1;
                snprintf(match->date, sizeof(match->date), "%04d-%02d-%02d",
                         2015 + season, 1 + (int)(match_index % 12),
                         1 + (int)(match_index % 28));
                match->home_team_id = (uint64_t)home + 1;
                match->away_team_id = (uint64_t)away + 1;
                match->home_goals = (int)(match_index % 4);
                match->away_goals = (int)((match_index / 3) % 3);
                match->length = 95;
                for (int side = 0; side < 2; ++side) {
                    int team = side == 0 ? home : away;
                    for (int k = 0; k < PER_SIDE; ++k) {
                        Appearance *appearance =
                            &db.appearances[appearance_index++];
                        appearance->match_id = match->id;
                        appearance->player_id =
                            (uint64_t)(team * SQUAD + k) + 1;
                        appearance->team_id = (uint64_t)team + 1;
                        appearance->minutes = k < 11 ? 90 : 20;
                        appearance->performance = 6.5;
                    }
                }
                match_index++;
            }
        }
    }
    db.match_count = match_index;
    db.appearance_count = appearance_index;
    db.next_team_id = TEAMS + 1;
    db.next_player_id = TEAMS * SQUAD + 1;
    db.next_match_id = match_index + 1;

    char error[256];
    clock_t started = clock();
    CHECK(db_validate(&db, error, sizeof(error)));
    double seconds = (double)(clock() - started) / CLOCKS_PER_SEC;
    fprintf(stderr, "  db_validate on %u appearances took %.3f s\n",
            (unsigned)appearance_index, seconds);
    /* Each validation must fit inside one 60 fps frame on the UI thread. */
    CHECK(seconds < 0.016);
    db_free(&db);
    return true;
}

const TestCase g_database_tests[] = {
    TEST_CASE(test_team_crud_and_validation),
    TEST_CASE(test_player_crud_and_archive),
    TEST_CASE(test_player_starting_rating_is_unbounded),
    TEST_CASE(test_last_added_player),
    TEST_CASE(test_record_match_and_delete),
    TEST_CASE(test_match_team_average_delta),
    TEST_CASE(test_sent_off_weighting_and_validation),
    TEST_CASE(test_sent_off_win_uses_actual_minutes),
    TEST_CASE(test_match_validation),
    TEST_CASE(test_backdated_matches_replay_chronologically),
    TEST_CASE(test_delete_player_with_history_blocked),
    TEST_CASE(test_clone_and_id_continuity),
    TEST_CASE(test_match_total_minutes_guard),
    TEST_CASE(test_neutral_venue_match),
    TEST_CASE(test_recompute_sorts_matches_by_date),
    TEST_CASE(test_recompute_rejects_invalid_database),
    TEST_CASE(test_incremental_replay_matches_full_recompute),
    TEST_CASE(test_player_transfer_preserves_historical_team),
    TEST_CASE(test_update_match_replays_chronology),
    TEST_CASE(test_update_match_preserves_transferred_and_archived_players),
    TEST_CASE(test_update_match_validation),
    TEST_CASE(test_ratings_before_match),
    TEST_CASE(test_find_player_by_name),
    TEST_CASE(test_find_active_player_by_name_in_team),
    TEST_CASE(test_team_stats_are_minutes_and_results_aware),
    TEST_CASE(test_ratings_before_match_excludes_edited_match),
    TEST_CASE(test_match_rejects_minutes_beyond_final_whistle),
    TEST_CASE(test_validate_scales_to_several_seasons),
};

const size_t g_database_test_count =
    sizeof(g_database_tests) / sizeof(g_database_tests[0]);
