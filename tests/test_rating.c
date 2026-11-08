#include "test_framework.h"

#include "core/rating.h"

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

static bool test_default_rating(void)
{
    CHECK_NEAR(RATING_DEFAULT, 50.0, 1e-12);
    CHECK(RATING_DEFAULT >= RATING_MIN);
    CHECK(RATING_DEFAULT <= RATING_MAX);
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

    CHECK(rating_match_summary(&summary, 90.0, 50.0, 1, 1, false,
                               error, sizeof(error)));
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
    CHECK_NEAR(summary.home_expected, 0.557305, 0.00001);
    CHECK_NEAR(summary.margin_multiplier, 1.15, 1e-12);
    CHECK_NEAR(summary.home_team_delta, 8.1456, 0.0002);
    CHECK_NEAR(summary.away_team_delta, -8.1456, 0.0002);

    double delta = 0.0;
    double after = 0.0;
    CHECK(rating_player_change(&summary, true, 70.0, 90, 6.0, &delta, &after));
    CHECK_NEAR(delta, 8.15, 0.001);
    CHECK_NEAR(after, 78.15, 0.001);
    return true;
}

static bool test_goal_margin_amplifies_change(void)
{
    double strengths[] = { 70.0, 71.0 };
    RatingMatchSummary close;
    RatingMatchSummary clear;
    char error[256];
    CHECK(rating_match_summary(&close, strengths[0], strengths[1], 1, 0,
                               false, error, sizeof(error)));
    CHECK(rating_match_summary(&clear, strengths[0], strengths[1], 4, 0,
                               false, error, sizeof(error)));
    CHECK(close.margin_multiplier < clear.margin_multiplier);
    CHECK(close.home_team_delta < clear.home_team_delta);
    CHECK_NEAR(clear.margin_multiplier - close.margin_multiplier, 0.30, 1e-12);
    return true;
}

static bool test_performance_adjustment(void)
{
    RatingMatchSummary summary;
    char error[256];
    CHECK(rating_match_summary(&summary, 70.0, 70.0, 1, 0, false,
                               error, sizeof(error)));
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
    CHECK_NEAR(high_delta - neutral_delta, 2.25, 0.02);
    CHECK_NEAR(low_delta - neutral_delta, -3.0, 0.02);
    CHECK_NEAR(high_after, 70.0 + high_delta, 0.001);
    return true;
}

static bool test_minutes_weighting(void)
{
    RatingPlayerInput home[] = { player(70.0, 45, 6.0) };
    RatingPlayerInput away[] = { player(70.0, 90, 6.0) };
    RatingMatchSummary summary;
    char error[256];
    CHECK(rating_compute_match(&summary, home, 1, away, 1, 1, 0, false,
                               error, sizeof(error)));
    /* The line-up strength is unchanged when only one player is present. */
    CHECK_NEAR(summary.home_strength, 70.0, 1e-12);
    double half_delta = 0.0;
    double half_after = 0.0;
    CHECK(rating_player_change(&summary, true, 70.0, 45, 6.0,
                               &half_delta, &half_after));
    CHECK_NEAR(half_delta, summary.home_team_delta * 0.5, 0.011);

    double full_delta = 0.0;
    double full_after = 0.0;
    CHECK(rating_player_change(&summary, true, 70.0, 90, 6.0,
                               &full_delta, &full_after));
    CHECK(fabs(half_delta) < fabs(full_delta));

    double capped_delta = 0.0;
    double capped_after = 0.0;
    CHECK(rating_player_change(&summary, true, 70.0, 120, 6.0,
                               &capped_delta, &capped_after));
    CHECK_NEAR(capped_delta, full_delta, 1e-12);
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
    return true;
}

static bool test_rating_bounds(void)
{
    RatingMatchSummary summary;
    char error[256];
    CHECK(rating_match_summary(&summary, 70.0, 70.0, 3, 0, false,
                               error, sizeof(error)));
    double delta = 0.0;
    double after = 0.0;
    CHECK(rating_player_change(&summary, true, 98.5, 90, 10.0,
                               &delta, &after));
    CHECK_NEAR(after, 99.0, 1e-12);
    CHECK_NEAR(delta, 0.5, 1e-12);

    CHECK(rating_match_summary(&summary, 70.0, 70.0, 0, 3, false,
                               error, sizeof(error)));
    CHECK(rating_player_change(&summary, true, 1.2, 90, 1.0,
                               &delta, &after));
    CHECK_NEAR(after, 1.0, 1e-12);
    CHECK_NEAR(delta, -0.2, 1e-12);
    return true;
}

static bool test_invalid_inputs(void)
{
    RatingPlayerInput bad_ratings[] = { player(0.0, 90, 6.0) };
    RatingPlayerInput good[] = { player(70.0, 90, 6.0) };
    RatingPlayerInput bad_minutes[] = { player(70.0, 0, 6.0) };
    RatingPlayerInput bad_performance[] = { player(70.0, 90, 11.0) };
    RatingMatchSummary summary;
    char error[256];

    CHECK(!rating_compute_match(&summary, bad_ratings, 1, good, 1, 1, 0,
                                false, error, sizeof(error)));
    CHECK(!rating_compute_match(&summary, good, 1, bad_minutes, 1, 1, 0,
                                false, error, sizeof(error)));
    CHECK(!rating_compute_match(&summary, good, 1, bad_performance, 1, 1, 0,
                                false, error, sizeof(error)));
    CHECK(!rating_compute_match(&summary, NULL, 0, good, 1, 1, 0,
                                false, error, sizeof(error)));
    CHECK(!rating_match_summary(&summary, 70.0, 70.0, -1, 0, false,
                                error, sizeof(error)));
    CHECK(!rating_match_summary(&summary, 70.0, 70.0, 0, 100, false,
                                error, sizeof(error)));

    CHECK(rating_match_summary(&summary, 70.0, 70.0, 1, 0, false,
                               error, sizeof(error)));
    double delta = 0.0;
    double after = 0.0;
    CHECK(!rating_player_change(&summary, true, 70.0, 0, 6.0, &delta, &after));
    CHECK(!rating_player_change(&summary, true, 70.0, 90, 0.0, &delta, &after));
    CHECK(!rating_player_change(NULL, true, 70.0, 90, 6.0, &delta, &after));
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
    CHECK_NEAR(normal.home_expected, 0.557305, 0.00001);
    CHECK(neutral.home_team_delta > normal.home_team_delta);
    CHECK_NEAR(neutral.home_team_delta, 9.2, 1e-12);
    CHECK_NEAR(neutral.away_team_delta, -9.2, 1e-12);

    CHECK(rating_match_summary(&neutral, 80.0, 60.0, 1, 1, true,
                               error, sizeof(error)));
    CHECK_NEAR(neutral.home_expected, 1.0 /
                                      (1.0 + pow(10.0, (60.0 - 80.0) / 40.0)),
               1e-12);
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
        CHECK(summary.home_strength >= 1.0 && summary.home_strength <= 99.0);
        CHECK(summary.away_strength >= 1.0 && summary.away_strength <= 99.0);
        CHECK(summary.margin_multiplier >= 1.0 &&
              summary.margin_multiplier <= 1.45);
        for (int i = 0; i < 11; ++i) {
            double delta = 0.0;
            double after = 0.0;
            CHECK(rating_player_change(&summary, true, home[i].rating,
                                       home[i].minutes, home[i].performance,
                                       &delta, &after));
            CHECK(delta >= -RATING_MAX_MATCH_DELTA &&
                  delta <= RATING_MAX_MATCH_DELTA);
            CHECK(after >= RATING_MIN && after <= RATING_MAX);
            CHECK_NEAR(after, home[i].rating + delta, 0.011);
            CHECK(rating_player_change(&summary, false, away[i].rating,
                                       away[i].minutes, away[i].performance,
                                       &delta, &after));
            CHECK(delta >= -RATING_MAX_MATCH_DELTA &&
                  delta <= RATING_MAX_MATCH_DELTA);
            CHECK(after >= RATING_MIN && after <= RATING_MAX);
        }
    }
    return true;
}

const TestCase g_rating_tests[] = {
    TEST_CASE(test_default_rating),
    TEST_CASE(test_expected_score_curve),
    TEST_CASE(test_equal_home_win_values),
    TEST_CASE(test_goal_margin_amplifies_change),
    TEST_CASE(test_performance_adjustment),
    TEST_CASE(test_minutes_weighting),
    TEST_CASE(test_lineup_strength_weighted_by_minutes),
    TEST_CASE(test_rating_bounds),
    TEST_CASE(test_invalid_inputs),
    TEST_CASE(test_underdog_win),
    TEST_CASE(test_neutral_venue_removes_home_advantage),
    TEST_CASE(test_randomized_invariants),
};

const size_t g_rating_test_count =
    sizeof(g_rating_tests) / sizeof(g_rating_tests[0]);
