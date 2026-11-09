#include "test_framework.h"

#include "core/rating.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

static RatingPlayerInput player(double rating, int minutes, double performance)
{
    RatingPlayerInput input;
    input.rating = rating;
    input.minutes = minutes;
    input.performance = performance;
    return input;
}

static RatingTeamContext team_context(double total_participation,
                                      double average_performance)
{
    RatingTeamContext context;
    context.total_participation = total_participation;
    context.weighted_performance =
        total_participation * average_performance;
    return context;
}

static bool test_default_rating(void)
{
    CHECK_NEAR(RATING_DEFAULT, 50.0, 1e-12);
    return true;
}

static bool test_expected_score_curve(void)
{
    RatingPlayerInput home[] = { player(70.0, 90, 6.0) };
    RatingPlayerInput away[] = { player(70.0, 90, 6.0) };
    RatingMatchSummary summary;
    char error[256];
    CHECK(rating_compute_match(&summary, home, 1, away, 1, 0, 0, false,
                               error, sizeof(error)));
    CHECK(summary.home_expected > 0.5);
    CHECK_NEAR(summary.home_expected + summary.away_expected, 1.0, 1e-12);
    CHECK_NEAR(summary.away_expected, 1.0 - summary.home_expected, 1e-12);
    CHECK_NEAR(summary.margin_multiplier, 1.0, 1e-12);
    CHECK_NEAR(summary.home_result, 0.5, 1e-12);
    CHECK(summary.home_team_delta < 0.0);
    CHECK(summary.away_team_delta > 0.0);
    CHECK_NEAR(summary.home_team_delta, -summary.away_team_delta, 1e-12);

    RatingTeamContext neutral = team_context(1.0, 6.0);
    CHECK(rating_match_summary(&summary, 90.0, 50.0, &neutral, &neutral,
                               1, 1, false, error, sizeof(error)));
    CHECK(summary.home_expected > 0.9);
    CHECK(summary.home_team_delta < 0.0);
    CHECK(summary.away_team_delta > 0.0);
    return true;
}

static bool test_equal_home_win_values(void)
{
    RatingPlayerInput home[] = { player(70.0, 90, 6.0) };
    RatingPlayerInput away[] = { player(70.0, 90, 6.0) };
    RatingMatchSummary summary;
    char error[256];
    CHECK(rating_compute_match(&summary, home, 1, away, 1, 1, 0, false,
                               error, sizeof(error)));
    CHECK_NEAR(summary.home_expected, 0.557312, 0.00001);
    CHECK_NEAR(summary.margin_multiplier, 1.0, 1e-12);
    CHECK_NEAR(summary.home_team_delta, 7.083008, 0.0002);
    CHECK_NEAR(summary.away_team_delta, -7.083008, 0.0002);

    double delta = 0.0;
    double after = 0.0;
    CHECK(rating_player_change(&summary, true, 70.0, 90, 6.0, &delta, &after));
    CHECK_NEAR(delta, 7.083008, 0.0002);
    CHECK_NEAR(after, 77.083008, 0.0002);
    return true;
}

static bool test_goal_margin_amplifies_change(void)
{
    RatingTeamContext neutral = team_context(1.0, 6.0);
    double strengths[] = { 70.0, 71.0 };
    RatingMatchSummary one_goal;
    RatingMatchSummary two_goals;
    RatingMatchSummary three_goals;
    RatingMatchSummary four_goals;
    RatingMatchSummary away_two_goals;
    char error[256];
    CHECK(rating_match_summary(&one_goal, strengths[0], strengths[1],
                               &neutral, &neutral, 1, 0,
                               false, error, sizeof(error)));
    CHECK(rating_match_summary(&two_goals, strengths[0], strengths[1],
                               &neutral, &neutral, 2, 0,
                               false, error, sizeof(error)));
    CHECK(rating_match_summary(&three_goals, strengths[0], strengths[1],
                               &neutral, &neutral, 3, 0,
                               false, error, sizeof(error)));
    CHECK(rating_match_summary(&four_goals, strengths[0], strengths[1],
                               &neutral, &neutral, 4, 0,
                               false, error, sizeof(error)));
    CHECK(rating_match_summary(&away_two_goals, strengths[0], strengths[1],
                               &neutral, &neutral, 0, 2,
                               false, error, sizeof(error)));
    CHECK_NEAR(one_goal.margin_multiplier, 1.0, 1e-12);
    CHECK_NEAR(two_goals.margin_multiplier, 1.2, 1e-12);
    CHECK_NEAR(three_goals.margin_multiplier, 1.4, 1e-12);
    CHECK_NEAR(four_goals.margin_multiplier, 1.4, 1e-12);
    CHECK_NEAR(away_two_goals.margin_multiplier, 1.2, 1e-12);
    CHECK(one_goal.home_team_delta < two_goals.home_team_delta);
    CHECK(two_goals.home_team_delta < three_goals.home_team_delta);
    CHECK_NEAR(three_goals.home_team_delta, four_goals.home_team_delta,
               1e-12);
    return true;
}

static bool test_performance_adjustment(void)
{
    RatingPlayerInput home[] = {
        player(70.0, 90, 6.0),
        player(70.0, 90, 6.0),
    };
    RatingPlayerInput away[] = {
        player(70.0, 90, 6.0),
        player(70.0, 90, 6.0),
    };
    RatingMatchSummary summary;
    char error[256];
    CHECK(rating_compute_match(&summary, home, 2, away, 2, 1, 0, false,
                               error, sizeof(error)));
    CHECK_NEAR(summary.home_context.weighted_performance, 12.0, 1e-12);

    double neutral_delta = 0.0;
    double neutral_after = 0.0;
    double high_delta = 0.0;
    double high_after = 0.0;
    double low_delta = 0.0;
    double low_after = 0.0;
    CHECK(rating_player_change(&summary, true, 70.0, 90, 6.0,
                               &neutral_delta, &neutral_after));
    CHECK(rating_player_change(&summary, true, 70.0, 90, 9.0,
                               &high_delta, &high_after));
    CHECK(rating_player_change(&summary, true, 70.0, 90, 2.0,
                               &low_delta, &low_after));
    CHECK(high_delta > neutral_delta);
    CHECK(low_delta < neutral_delta);
    CHECK_NEAR(high_delta - neutral_delta, 2.25, 1e-12);
    CHECK_NEAR(low_delta - neutral_delta, -3.0, 1e-12);
    CHECK_NEAR(high_after, 70.0 + high_delta, 1e-12);
    return true;
}

static bool test_performance_is_zero_sum_within_team(void)
{
    RatingPlayerInput home[] = {
        player(70.0, 90, 9.0),
        player(70.0, 90, 2.0),
    };
    RatingPlayerInput away[] = {
        player(70.0, 90, 4.0),
        player(70.0, 90, 8.0),
    };
    RatingMatchSummary summary;
    char error[256];
    CHECK(rating_compute_match(&summary, home, 2, away, 2, 2, 1, false,
                               error, sizeof(error)));

    double home_total = 0.0;
    double away_total = 0.0;
    for (int i = 0; i < 2; ++i) {
        double delta = 0.0;
        double after = 0.0;
        CHECK(rating_player_change(&summary, true, home[i].rating,
                                   home[i].minutes, home[i].performance,
                                   &delta, &after));
        home_total += delta;
        CHECK(rating_player_change(&summary, false, away[i].rating,
                                   away[i].minutes, away[i].performance,
                                   &delta, &after));
        away_total += delta;
    }
    CHECK_NEAR(home_total, summary.home_team_delta, 1e-12);
    CHECK_NEAR(away_total, summary.away_team_delta, 1e-12);
    CHECK_NEAR(home_total + away_total, 0.0, 1e-12);
    return true;
}

static bool test_minutes_weighting(void)
{
    RatingPlayerInput home[] = {
        player(70.0, 90, 6.0),
        player(70.0, 45, 6.0),
    };
    RatingPlayerInput away[] = { player(70.0, 90, 6.0) };
    RatingMatchSummary summary;
    char error[256];
    CHECK(rating_compute_match(&summary, home, 2, away, 1, 1, 0, false,
                               error, sizeof(error)));
    CHECK_NEAR(summary.home_context.total_participation, 1.5, 1e-12);

    double full_delta = 0.0;
    double full_after = 0.0;
    double half_delta = 0.0;
    double half_after = 0.0;
    CHECK(rating_player_change(&summary, true, 70.0, 90, 6.0,
                               &full_delta, &full_after));
    CHECK(rating_player_change(&summary, true, 70.0, 45, 6.0,
                               &half_delta, &half_after));
    CHECK_NEAR(half_delta, full_delta * 0.5, 1e-12);
    CHECK_NEAR(full_delta, summary.home_team_delta * 2.0 / 3.0, 1e-12);

    RatingPlayerInput extra_home[] = {
        player(70.0, 120, 6.0),
        player(70.0, 90, 6.0),
    };
    CHECK(rating_compute_match(&summary, extra_home, 2, away, 1, 1, 0, false,
                               error, sizeof(error)));
    double first_delta = 0.0;
    double second_delta = 0.0;
    double unused = 0.0;
    CHECK(rating_player_change(&summary, true, 70.0, 120, 6.0,
                               &first_delta, &unused));
    CHECK(rating_player_change(&summary, true, 70.0, 90, 6.0,
                               &second_delta, &unused));
    CHECK_NEAR(first_delta, second_delta, 1e-12);
    return true;
}

static bool test_lineup_strength_weighted_by_minutes(void)
{
    RatingPlayerInput players[] = {
        player(80.0, 90, 6.0),
        player(20.0, 1, 6.0),
    };
    bool valid = false;
    double strength = rating_team_strength(players, 2, &valid);
    CHECK(valid);
    CHECK(strength > 79.0 && strength < 80.0);
    CHECK(!rating_team_strength(players, 0, &valid));
    CHECK(!valid);

    double profile_strength = 0.0;
    RatingTeamContext context;
    CHECK(rating_team_profile(players, 2, &profile_strength, &context));
    CHECK_NEAR(profile_strength, strength, 1e-12);
    CHECK_NEAR(context.total_participation, 1.0 + 1.0 / 90.0, 1e-12);
    CHECK_NEAR(context.weighted_performance, 6.0 + 6.0 / 90.0, 1e-12);
    return true;
}

static bool test_ratings_are_not_capped(void)
{
    RatingTeamContext neutral = team_context(1.0, 6.0);
    RatingMatchSummary summary;
    char error[256];
    CHECK(rating_match_summary(&summary, 70.0, 70.0, &neutral, &neutral,
                               3, 0, false, error, sizeof(error)));

    double delta = 0.0;
    double after = 0.0;
    CHECK(rating_player_change(&summary, true, 98.5, 90, 6.0, &delta, &after));
    CHECK(delta > 9.9);
    CHECK(after > 100.0);
    CHECK_NEAR(after, 98.5 + delta, 1e-12);

    CHECK(rating_match_summary(&summary, 70.0, 70.0, &neutral, &neutral,
                               0, 3, false, error, sizeof(error)));
    CHECK(rating_player_change(&summary, true, 1.2, 90, 6.0, &delta, &after));
    CHECK(delta < -10.0);
    CHECK(after < 1.0);

    /* A heavy underdog win is not limited to a 12-point movement. */
    CHECK(rating_match_summary(&summary, 1.0, 99.0, &neutral, &neutral,
                               3, 0, false, error, sizeof(error)));
    CHECK(rating_player_change(&summary, true, 50.0, 90, 6.0, &delta, &after));
    CHECK(delta > 12.0);
    CHECK_NEAR(after, 50.0 + delta, 1e-12);
    return true;
}

static bool test_invalid_inputs(void)
{
    RatingPlayerInput bad_ratings[] = { player(INFINITY, 90, 6.0) };
    RatingPlayerInput good[] = { player(70.0, 90, 6.0) };
    RatingPlayerInput bad_minutes[] = { player(70.0, 0, 6.0) };
    RatingPlayerInput bad_performance[] = { player(70.0, 90, 11.0) };
    RatingPlayerInput unbounded[] = { player(-250.0, 90, 6.0) };
    RatingMatchSummary summary;
    char error[256];
    RatingTeamContext neutral = team_context(1.0, 6.0);

    CHECK(!rating_compute_match(&summary, bad_ratings, 1, good, 1, 1, 0,
                                false, error, sizeof(error)));
    CHECK(!rating_compute_match(&summary, good, 1, bad_minutes, 1, 1, 0,
                                false, error, sizeof(error)));
    CHECK(!rating_compute_match(&summary, good, 1, bad_performance, 1, 1, 0,
                                false, error, sizeof(error)));
    CHECK(!rating_compute_match(&summary, NULL, 0, good, 1, 1, 0,
                                false, error, sizeof(error)));
    CHECK(rating_compute_match(&summary, unbounded, 1, good, 1, 1, 0,
                               false, error, sizeof(error)));
    CHECK(!rating_match_summary(&summary, INFINITY, 70.0,
                                &neutral, &neutral, 1, 0, false,
                                error, sizeof(error)));
    CHECK(!rating_match_summary(&summary, 70.0, NAN,
                                &neutral, &neutral, 1, 0, false,
                                error, sizeof(error)));
    CHECK(!rating_match_summary(&summary, 70.0, 70.0,
                                NULL, &neutral, 1, 0, false,
                                error, sizeof(error)));
    RatingTeamContext empty = team_context(0.0, 6.0);
    CHECK(!rating_match_summary(&summary, 70.0, 70.0,
                                &empty, &neutral, 1, 0, false,
                                error, sizeof(error)));
    RatingTeamContext broken = team_context(1.0, 6.0);
    broken.weighted_performance = INFINITY;
    CHECK(!rating_match_summary(&summary, 70.0, 70.0,
                                &broken, &neutral, 1, 0, false,
                                error, sizeof(error)));
    CHECK(!rating_match_summary(&summary, 70.0, 70.0, &neutral, &neutral,
                                -1, 0, false, error, sizeof(error)));
    CHECK(!rating_match_summary(&summary, 70.0, 70.0, &neutral, &neutral,
                                0, 100, false, error, sizeof(error)));

    CHECK(rating_match_summary(&summary, 70.0, 70.0, &neutral, &neutral,
                               1, 0, false, error, sizeof(error)));
    double delta = 0.0;
    double after = 0.0;
    CHECK(!rating_player_change(&summary, true, 70.0, 0, 6.0, &delta, &after));
    CHECK(!rating_player_change(&summary, true, 70.0, 90, 0.0, &delta, &after));
    CHECK(!rating_player_change(&summary, true, INFINITY, 90, 6.0,
                                &delta, &after));
    CHECK(!rating_player_change(NULL, true, 70.0, 90, 6.0, &delta, &after));
    CHECK(!rating_team_profile(NULL, 0, NULL, NULL));
    return true;
}

static bool test_underdog_win(void)
{
    RatingPlayerInput home[] = { player(45.0, 90, 6.0) };
    RatingPlayerInput away[] = { player(95.0, 90, 6.0) };
    RatingMatchSummary summary;
    char error[256];
    CHECK(rating_compute_match(&summary, home, 1, away, 1, 2, 1, false,
                               error, sizeof(error)));
    CHECK(summary.home_expected < 0.1);
    CHECK(summary.home_team_delta > 10.0);
    CHECK(summary.away_team_delta < -10.0);
    return true;
}

static bool test_neutral_venue_removes_home_advantage(void)
{
    RatingPlayerInput home[] = { player(70.0, 90, 6.0) };
    RatingPlayerInput away[] = { player(70.0, 90, 6.0) };
    RatingMatchSummary neutral;
    RatingMatchSummary normal;
    char error[256];
    CHECK(rating_compute_match(&neutral, home, 1, away, 1, 0, 0, true,
                               error, sizeof(error)));
    CHECK_NEAR(neutral.home_expected, 0.5, 1e-12);
    CHECK_NEAR(neutral.away_expected, 0.5, 1e-12);
    CHECK_NEAR(neutral.home_team_delta, 0.0, 1e-12);
    CHECK_NEAR(neutral.away_team_delta, 0.0, 1e-12);

    CHECK(rating_compute_match(&neutral, home, 1, away, 1, 1, 0, true,
                               error, sizeof(error)));
    CHECK(rating_compute_match(&normal, home, 1, away, 1, 1, 0, false,
                               error, sizeof(error)));
    CHECK_NEAR(neutral.home_expected, 0.5, 1e-12);
    CHECK_NEAR(normal.home_expected, 0.557312, 0.00001);
    CHECK(neutral.home_team_delta > normal.home_team_delta);
    CHECK_NEAR(neutral.home_team_delta, 8.0, 1e-12);
    CHECK_NEAR(neutral.away_team_delta, -8.0, 1e-12);

    RatingTeamContext neutral_context = team_context(1.0, 6.0);
    CHECK(rating_match_summary(&neutral, 80.0, 60.0,
                               &neutral_context, &neutral_context,
                               1, 1, true, error, sizeof(error)));
    CHECK_NEAR(neutral.home_expected, 1.0 /
                                      (1.0 + pow(10.0, (60.0 - 80.0) / 40.0)),
               1e-12);
    return true;
}

static bool test_zero_sum_with_unequal_lineups(void)
{
    RatingPlayerInput home[] = {
        player(74.0, 90, 8.0),
        player(67.0, 45, 5.0),
        player(61.0, 30, 6.0),
    };
    RatingPlayerInput away[] = {
        player(72.0, 90, 4.0),
        player(66.0, 60, 7.0),
    };
    RatingMatchSummary summary;
    char error[256];
    CHECK(rating_compute_match(&summary, home, 3, away, 2, 2, 1, false,
                               error, sizeof(error)));
    CHECK_NEAR(summary.home_team_delta, -summary.away_team_delta, 1e-12);

    double home_total = 0.0;
    double away_total = 0.0;
    for (int i = 0; i < 3; ++i) {
        double delta = 0.0;
        double after = 0.0;
        CHECK(rating_player_change(&summary, true, home[i].rating,
                                   home[i].minutes, home[i].performance,
                                   &delta, &after));
        CHECK_NEAR(after, home[i].rating + delta, 1e-12);
        home_total += delta;
    }
    for (int i = 0; i < 2; ++i) {
        double delta = 0.0;
        double after = 0.0;
        CHECK(rating_player_change(&summary, false, away[i].rating,
                                   away[i].minutes, away[i].performance,
                                   &delta, &after));
        CHECK_NEAR(after, away[i].rating + delta, 1e-12);
        away_total += delta;
    }
    CHECK_NEAR(home_total, summary.home_team_delta, 1e-12);
    CHECK_NEAR(away_total, summary.away_team_delta, 1e-12);
    CHECK_NEAR(home_total + away_total, 0.0, 1e-12);
    return true;
}

static bool test_randomized_invariants(void)
{
    uint32_t seed = 0x9E3779B9u;
    char error[256];
    for (int iteration = 0; iteration < 500; ++iteration) {
        RatingPlayerInput home[11];
        RatingPlayerInput away[11];
        for (int i = 0; i < 11; ++i) {
            seed = seed * 1664525u + 1013904223u;
            home[i].rating = 1.0 + (double)(seed % 9801u) / 100.0;
            seed = seed * 1664525u + 1013904223u;
            home[i].minutes = 1 + (int)(seed % 120u);
            seed = seed * 1664525u + 1013904223u;
            home[i].performance = 1.0 + (double)(seed % 901u) / 100.0;
            seed = seed * 1664525u + 1013904223u;
            away[i].rating = 1.0 + (double)(seed % 9801u) / 100.0;
            seed = seed * 1664525u + 1013904223u;
            away[i].minutes = 1 + (int)(seed % 120u);
            seed = seed * 1664525u + 1013904223u;
            away[i].performance = 1.0 + (double)(seed % 901u) / 100.0;
        }
        seed = seed * 1664525u + 1013904223u;
        int home_goals = (int)(seed % 6u);
        seed = seed * 1664525u + 1013904223u;
        int away_goals = (int)(seed % 6u);
        RatingMatchSummary summary;
        CHECK(rating_compute_match(&summary, home, 11, away, 11,
                                   home_goals, away_goals, false,
                                   error, sizeof(error)));
        CHECK(summary.home_expected >= 0.0 && summary.home_expected <= 1.0);
        CHECK_NEAR(summary.home_expected + summary.away_expected, 1.0, 1e-12);
        CHECK_NEAR(summary.home_team_delta, -summary.away_team_delta, 1e-12);
        CHECK(summary.margin_multiplier >= 1.0 &&
              summary.margin_multiplier <= 1.4);
        CHECK(isfinite(summary.home_strength));
        CHECK(isfinite(summary.away_strength));

        double home_total = 0.0;
        double away_total = 0.0;
        for (int i = 0; i < 11; ++i) {
            double delta = 0.0;
            double after = 0.0;
            CHECK(rating_player_change(&summary, true, home[i].rating,
                                       home[i].minutes, home[i].performance,
                                       &delta, &after));
            CHECK(isfinite(delta));
            CHECK(isfinite(after));
            CHECK_NEAR(after, home[i].rating + delta, 1e-9);
            home_total += delta;

            double high_delta = 0.0;
            double low_delta = 0.0;
            double unused = 0.0;
            CHECK(rating_player_change(&summary, true, home[i].rating,
                                       home[i].minutes, 10.0,
                                       &high_delta, &unused));
            CHECK(rating_player_change(&summary, true, home[i].rating,
                                       home[i].minutes, 1.0,
                                       &low_delta, &unused));
            CHECK(high_delta >= low_delta);

            CHECK(rating_player_change(&summary, false, away[i].rating,
                                       away[i].minutes, away[i].performance,
                                       &delta, &after));
            CHECK(isfinite(delta));
            CHECK(isfinite(after));
            CHECK_NEAR(after, away[i].rating + delta, 1e-9);
            away_total += delta;
        }
        CHECK_NEAR(home_total, summary.home_team_delta, 1e-9);
        CHECK_NEAR(away_total, summary.away_team_delta, 1e-9);
        CHECK_NEAR(home_total + away_total, 0.0, 1e-9);
    }
    return true;
}

const TestCase g_rating_tests[] = {
    TEST_CASE(test_default_rating),
    TEST_CASE(test_expected_score_curve),
    TEST_CASE(test_equal_home_win_values),
    TEST_CASE(test_goal_margin_amplifies_change),
    TEST_CASE(test_performance_adjustment),
    TEST_CASE(test_performance_is_zero_sum_within_team),
    TEST_CASE(test_minutes_weighting),
    TEST_CASE(test_lineup_strength_weighted_by_minutes),
    TEST_CASE(test_ratings_are_not_capped),
    TEST_CASE(test_invalid_inputs),
    TEST_CASE(test_underdog_win),
    TEST_CASE(test_neutral_venue_removes_home_advantage),
    TEST_CASE(test_zero_sum_with_unequal_lineups),
    TEST_CASE(test_randomized_invariants),
};

const size_t g_rating_test_count =
    sizeof(g_rating_tests) / sizeof(g_rating_tests[0]);
