#include "core/rating.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0) {
        snprintf(error, error_size, "%s", message);
    }
}

static double round_to_2(double value)
{
    return round(value * 100.0) / 100.0;
}

static double clamp_double(double value, double low, double high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

double rating_participation(int minutes)
{
    if (minutes <= 0) {
        return 0.0;
    }
    int capped = minutes > 90 ? 90 : minutes;
    return (double)capped / 90.0;
}

double rating_team_strength(const RatingPlayerInput *players, size_t count,
                            bool *valid)
{
    if (valid != NULL) {
        *valid = false;
    }
    if (players == NULL || count == 0) {
        return 0.0;
    }

    double weighted_sum = 0.0;
    double weight_total = 0.0;
    for (size_t i = 0; i < count; ++i) {
        if (!isfinite(players[i].rating) ||
            players[i].rating < RATING_MIN ||
            players[i].rating > RATING_MAX ||
            players[i].minutes < 1 || players[i].minutes > 120 ||
            !isfinite(players[i].performance) ||
            players[i].performance < 1.0 ||
            players[i].performance > 10.0) {
            return 0.0;
        }
        double weight = rating_participation(players[i].minutes);
        weighted_sum += players[i].rating * weight;
        weight_total += weight;
    }
    if (weight_total <= 0.0) {
        return 0.0;
    }
    if (valid != NULL) {
        *valid = true;
    }
    return weighted_sum / weight_total;
}

bool rating_match_summary(RatingMatchSummary *out,
                          double home_strength, double away_strength,
                          int home_goals, int away_goals,
                          char *error, size_t error_size)
{
    if (out == NULL) {
        set_error(error, error_size, "Internal error: missing rating result.");
        return false;
    }
    memset(out, 0, sizeof(*out));

    if (!isfinite(home_strength) || !isfinite(away_strength) ||
        home_strength < RATING_MIN || home_strength > RATING_MAX ||
        away_strength < RATING_MIN || away_strength > RATING_MAX) {
        set_error(error, error_size, "Team ratings must be between 1 and 99.");
        return false;
    }
    if (home_goals < 0 || home_goals > 99 ||
        away_goals < 0 || away_goals > 99) {
        set_error(error, error_size, "Scores must be between 0 and 99.");
        return false;
    }

    double expected_home = 1.0 /
        (1.0 + pow(10.0,
                   (away_strength - home_strength - RATING_HOME_ADVANTAGE) /
                       40.0));
    double actual_home;
    int goal_difference = home_goals - away_goals;
    if (goal_difference > 0) {
        actual_home = 1.0;
    } else if (goal_difference < 0) {
        actual_home = 0.0;
    } else {
        actual_home = 0.5;
    }

    double margin = 1.0;
    if (goal_difference != 0) {
        int magnitude = goal_difference < 0 ? -goal_difference : goal_difference;
        if (magnitude > 3) {
            magnitude = 3;
        }
        margin = 1.0 + (double)magnitude * 0.15;
    }

    double home_delta = RATING_K_FACTOR *
                        (actual_home - expected_home) * margin;

    out->home_strength = home_strength;
    out->away_strength = away_strength;
    out->home_expected = expected_home;
    out->away_expected = 1.0 - expected_home;
    out->home_team_delta = home_delta;
    out->away_team_delta = -home_delta;
    out->margin_multiplier = margin;
    out->home_result = actual_home;
    return true;
}

bool rating_compute_match(RatingMatchSummary *out,
                          const RatingPlayerInput *home, size_t home_count,
                          const RatingPlayerInput *away, size_t away_count,
                          int home_goals, int away_goals,
                          char *error, size_t error_size)
{
    bool home_valid = false;
    bool away_valid = false;
    double home_strength = rating_team_strength(home, home_count, &home_valid);
    double away_strength = rating_team_strength(away, away_count, &away_valid);

    if (!home_valid || !away_valid) {
        set_error(error, error_size,
                  "Each team needs at least one valid player.");
        return false;
    }
    return rating_match_summary(out, home_strength, away_strength,
                                home_goals, away_goals, error, error_size);
}

bool rating_player_change(const RatingMatchSummary *summary, bool home_team,
                          double rating_before, int minutes,
                          double performance, double *rating_delta,
                          double *rating_after)
{
    if (summary == NULL || rating_delta == NULL || rating_after == NULL ||
        !isfinite(rating_before) || rating_before < RATING_MIN ||
        rating_before > RATING_MAX ||
        minutes < 1 || minutes > 120 ||
        !isfinite(performance) || performance < 1.0 || performance > 10.0) {
        return false;
    }

    double participation = rating_participation(minutes);
    double team_delta = home_team ? summary->home_team_delta
                                  : summary->away_team_delta;
    double result_component = team_delta * participation;
    double performance_component =
        (performance - RATING_NEUTRAL_PERFORMANCE) *
        RATING_PERFORMANCE_WEIGHT * participation;

    double proposed_delta = result_component + performance_component;
    proposed_delta = clamp_double(proposed_delta,
                                  -RATING_MAX_MATCH_DELTA,
                                  RATING_MAX_MATCH_DELTA);

    double proposed_rating = clamp_double(rating_before + proposed_delta,
                                          RATING_MIN, RATING_MAX);
    proposed_rating = round_to_2(proposed_rating);
    double delta = round_to_2(proposed_rating - rating_before);

    if (delta == 0.0) {
        delta = 0.0; /* normalize negative zero */
    }
    *rating_delta = delta;
    *rating_after = proposed_rating;
    return true;
}
