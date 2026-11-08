#include "test_framework.h"

#include "core/database.h"

#include <string.h>

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
    CHECK(db_add_player(db, alpha, "Ava", "Stone", POSITION_GOALKEEPER,
                        80.0, true, &ids[0], error, sizeof(error)));
    CHECK(db_add_player(db, alpha, "Noah", "Bennett", POSITION_MIDFIELDER,
                        70.0, true, &ids[1], error, sizeof(error)));
    CHECK(db_add_player(db, beta, "Mia", "Clarke", POSITION_DEFENDER,
                        75.0, true, &ids[2], error, sizeof(error)));
    CHECK(db_add_player(db, beta, "Leo", "Foster", POSITION_FORWARD,
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
    CHECK(db_add_player(&db, team_id, "Jamie", "Reed", POSITION_DEFENDER,
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
    CHECK(!db_add_player(&db, 999, "No", "Team", POSITION_FORWARD,
                         70.0, true, NULL, error, sizeof(error)));
    CHECK(!db_add_player(&db, alpha, "", "", POSITION_FORWARD,
                         70.0, true, NULL, error, sizeof(error)));
    CHECK(!db_add_player(&db, alpha, "Test", NULL, (Position)99,
                         70.0, true, NULL, error, sizeof(error)));
    CHECK(!db_add_player(&db, alpha, "Test", NULL, POSITION_FORWARD,
                         100.0, true, NULL, error, sizeof(error)));
    CHECK(db_add_player(&db, alpha, "  Ada ", " Lovelace ",
                        POSITION_FORWARD, 82.46, true, &player_id,
                        error, sizeof(error)));
    const Player *player = db_find_player(&db, player_id);
    CHECK(player != NULL);
    CHECK_STRING(player->first_name, "Ada");
    CHECK_STRING(player->last_name, "Lovelace");
    CHECK_NEAR(player->base_rating, 82.46, 1e-12);
    CHECK_NEAR(player->current_rating, 82.46, 1e-12);
    CHECK(player->matches_played == 0);

    CHECK(db_update_player(&db, player_id, beta, "Ada", "Lovelace",
                           POSITION_MIDFIELDER, 84.0, true,
                           error, sizeof(error)));
    player = db_find_player(&db, player_id);
    CHECK(player->team_id == beta);
    CHECK(player->position == POSITION_MIDFIELDER);
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
        { players[0], 90, 7.55 },
        { players[1], 60, 6.0 },
        { players[2], 90, 6.0 },
        { players[3], 90, 5.5 },
    };
    MatchInput input;
    input.date = "2026-10-04";
    input.home_team_id = alpha;
    input.away_team_id = beta;
    input.home_goals = 2;
    input.away_goals = 0;
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
        { players[0], 90, 6.0 },
        { players[1], 90, 6.0 },
        { players[2], 90, 6.0 },
        { players[3], 90, 6.0 },
    };
    MatchInput input = {
        .date = "2026-10-04",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 1,
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
        { players[0], 90, 6.0 },
        { players[0], 90, 6.0 },
        { players[2], 90, 6.0 },
    };
    input.players = duplicate;
    input.player_count = 3;
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));
    CHECK(strstr(error, "more than once") != NULL);

    MatchPlayerInput bad_minutes[] = {
        { players[0], 0, 6.0 },
        { players[2], 90, 6.0 },
    };
    input.players = bad_minutes;
    input.player_count = 2;
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));

    MatchPlayerInput bad_performance[] = {
        { players[0], 90, 11.0 },
        { players[2], 90, 6.0 },
    };
    input.players = bad_performance;
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));

    MatchPlayerInput only_home[] = {
        { players[0], 90, 6.0 },
        { players[1], 90, 6.0 },
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
    CHECK(db_add_player(&db, third, "Eve", "Cross", POSITION_DEFENDER,
                        70.0, true, &outsider, error, sizeof(error)));
    MatchPlayerInput wrong_team[] = {
        { players[0], 90, 6.0 },
        { outsider, 90, 6.0 },
        { players[2], 90, 6.0 },
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
    CHECK(db_add_player(&db, alpha, "Home", "Player", POSITION_FORWARD,
                        70.0, true, &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player", POSITION_DEFENDER,
                        70.0, true, &players[1], error, sizeof(error)));

    MatchPlayerInput lineup[] = {
        { players[0], 90, 6.0 },
        { players[1], 90, 6.0 },
    };
    MatchInput input = {
        .date = "2026-03-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
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
    CHECK(db_add_player(&clean, alpha, "Home", "Player", POSITION_FORWARD,
                        70.0, true, &clean_players[0], error, sizeof(error)));
    CHECK(db_add_player(&clean, beta, "Away", "Player", POSITION_DEFENDER,
                        70.0, true, &clean_players[1], error, sizeof(error)));
    MatchPlayerInput clean_lineup[] = {
        { clean_players[0], 90, 6.0 },
        { clean_players[1], 90, 6.0 },
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
                           POSITION_FORWARD, 75.0, true,
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
    CHECK(db_add_player(&db, alpha, "One", "Player", POSITION_FORWARD,
                        70.0, true, &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Two", "Player", POSITION_DEFENDER,
                        70.0, true, &players[1], error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { players[0], 90, 6.0 },
        { players[1], 90, 6.0 },
    };
    MatchInput input = {
        .date = "2026-10-04",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 0,
        .away_goals = 0,
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
    CHECK(db_add_player(&db, alpha, "One", "Player", POSITION_FORWARD,
                        70.0, true, &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Two", "Player", POSITION_DEFENDER,
                        70.0, true, &players[1], error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { players[0], 90, 7.0 },
        { players[1], 90, 6.0 },
    };
    MatchInput input = {
        .date = "2026-10-04",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
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
        CHECK(db_add_player(&db, alpha, first, last, POSITION_MIDFIELDER,
                            70.0, true, &home_players[i],
                            error, sizeof(error)));
    }
    uint64_t away_player = 0;
    CHECK(db_add_player(&db, beta, "Away", "Player", POSITION_DEFENDER,
                        70.0, true, &away_player, error, sizeof(error)));

    MatchPlayerInput lineup[13];
    for (size_t i = 0; i < 12; ++i) {
        lineup[i].player_id = home_players[i];
        lineup[i].minutes = 120;
        lineup[i].performance = 6.0;
    }
    lineup[12].player_id = away_player;
    lineup[12].minutes = 1;
    lineup[12].performance = 6.0;
    MatchInput input = {
        .date = "2026-10-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
        .players = lineup,
        .player_count = 13,
    };
    CHECK(!db_add_match(&db, &input, NULL, error, sizeof(error)));
    CHECK(strstr(error, "1,320") != NULL);
    CHECK(db.match_count == 0);
    CHECK(db.appearance_count == 0);
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
    CHECK(db_add_player(&db, alpha, "Home", "Player", POSITION_FORWARD,
                        70.0, true, &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player", POSITION_DEFENDER,
                        70.0, true, &players[1], error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { players[0], 90, 6.0 },
        { players[1], 90, 6.0 },
    };
    MatchInput input = {
        .date = "2026-01-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
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
    CHECK(db_add_player(&db, alpha, "Home", "Player", POSITION_FORWARD,
                        72.0, true, &home_player, error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player", POSITION_DEFENDER,
                        68.0, true, &away_player, error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { home_player, 90, 6.5 },
        { away_player, 90, 5.5 },
    };
    MatchInput input = {
        .date = "2026-05-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 1,
        .away_goals = 0,
        .players = lineup,
        .player_count = 2,
    };
    uint64_t match_id = 0;
    CHECK(db_add_match(&db, &input, &match_id, error, sizeof(error)));
    double rating_before_transfer =
        db_find_player(&db, home_player)->current_rating;

    CHECK(db_update_player(&db, home_player, gamma, "Home", "Player",
                           POSITION_FORWARD, 72.0, true,
                           error, sizeof(error)));
    CHECK(db_find_player(&db, home_player)->team_id == gamma);
    CHECK_NEAR(db_find_player(&db, home_player)->current_rating,
               rating_before_transfer, 1e-12);
    CHECK(db_validate(&db, error, sizeof(error)));
    const Appearance *appearance = find_appearance(&db, match_id, home_player);
    CHECK(appearance != NULL);
    CHECK(appearance->team_id == alpha);

    uint64_t gamma_opponent = 0;
    CHECK(db_add_player(&db, gamma, "New", "Teammate", POSITION_MIDFIELDER,
                        69.0, true, &gamma_opponent, error, sizeof(error)));
    MatchPlayerInput next_lineup[] = {
        { home_player, 90, 6.0 },
        { gamma_opponent, 90, 6.0 },
        { away_player, 90, 6.0 },
    };
    MatchInput next = {
        .date = "2026-05-17",
        .home_team_id = gamma,
        .away_team_id = beta,
        .home_goals = 2,
        .away_goals = 0,
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

static bool test_team_stats_are_minutes_and_results_aware(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_two_teams(&db, &alpha, &beta));
    uint64_t players[2];
    char error[256];
    CHECK(db_add_player(&db, alpha, "A", "Player", POSITION_FORWARD,
                        60.0, true, &players[0], error, sizeof(error)));
    CHECK(db_add_player(&db, alpha, "B", "Player", POSITION_DEFENDER,
                        80.0, true, &players[1], error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "C", "Player", POSITION_MIDFIELDER,
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

const TestCase g_database_tests[] = {
    TEST_CASE(test_team_crud_and_validation),
    TEST_CASE(test_player_crud_and_archive),
    TEST_CASE(test_record_match_and_delete),
    TEST_CASE(test_match_validation),
    TEST_CASE(test_backdated_matches_replay_chronologically),
    TEST_CASE(test_delete_player_with_history_blocked),
    TEST_CASE(test_clone_and_id_continuity),
    TEST_CASE(test_match_total_minutes_guard),
    TEST_CASE(test_recompute_sorts_matches_by_date),
    TEST_CASE(test_player_transfer_preserves_historical_team),
    TEST_CASE(test_team_stats_are_minutes_and_results_aware),
};

const size_t g_database_test_count =
    sizeof(g_database_tests) / sizeof(g_database_tests[0]);
