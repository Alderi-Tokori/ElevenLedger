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

double rating_participation(int minutes)
{
    if (minutes <= 0) {
        return 0.0;
    }
    int capped = minutes > 90 ? 90 : minutes;
    return (double)capped / 90.0;
}

bool rating_team_profile(const RatingPlayerInput *players, size_t count,
                         double *strength, RatingTeamContext *context)
{
    if (players == NULL || count == 0 || strength == NULL || context == NULL) {
        return false;
    }

    double weighted_sum = 0.0;
    double weight_total = 0.0;
    double performance_total = 0.0;
    for (size_t i = 0; i < count; ++i) {
        if (!isfinite(players[i].rating) ||
            players[i].minutes < 1 || players[i].minutes > 120 ||
            !isfinite(players[i].performance) ||
            players[i].performance < 1.0 ||
            players[i].performance > 10.0) {
            return false;
        }
        double weight = rating_participation(players[i].minutes);
        weighted_sum += players[i].rating * weight;
        weight_total += weight;
        performance_total += players[i].performance * weight;
    }
    if (weight_total <= 0.0 ||
        !isfinite(weighted_sum) || !isfinite(performance_total)) {
        return false;
    }

    *strength = weighted_sum / weight_total;
    context->total_participation = weight_total;
    context->weighted_performance = performance_total;
    return isfinite(*strength);
}

double rating_team_strength(const RatingPlayerInput *players, size_t count,
                            bool *valid)
{
    double strength = 0.0;
    RatingTeamContext context;
    bool ok = rating_team_profile(players, count, &strength, &context);
    if (valid != NULL) {
        *valid = ok;
    }
    return ok ? strength : 0.0;
}

bool rating_match_summary(RatingMatchSummary *out,
                          double home_strength, double away_strength,
                          const RatingTeamContext *home_context,
                          const RatingTeamContext *away_context,
                          int home_goals, int away_goals,
                          bool neutral_venue,
                          char *error, size_t error_size)
{
    if (out == NULL) {
        set_error(error, error_size, "Internal error: missing rating result.");
        return false;
    }
    memset(out, 0, sizeof(*out));

    if (!isfinite(home_strength) || !isfinite(away_strength)) {
        set_error(error, error_size, "Team ratings must be finite numbers.");
        return false;
    }
    if (home_context == NULL || away_context == NULL ||
        !isfinite(home_context->total_participation) ||
        home_context->total_participation <= 0.0 ||
        !isfinite(home_context->weighted_performance) ||
        !isfinite(away_context->total_participation) ||
        away_context->total_participation <= 0.0 ||
        !isfinite(away_context->weighted_performance)) {
        set_error(error, error_size, "Invalid team line-up context.");
        return false;
    }
    if (home_goals < 0 || home_goals > 99 ||
        away_goals < 0 || away_goals > 99) {
        set_error(error, error_size, "Scores must be between 0 and 99.");
        return false;
    }

    double home_advantage =
        neutral_venue ? 0.0 : RATING_HOME_ADVANTAGE;
    double expected_home = 1.0 /
        (1.0 + pow(10.0,
                   (away_strength - home_strength - home_advantage) /
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
    out->home_context = *home_context;
    out->away_context = *away_context;
    return true;
}

bool rating_compute_match(RatingMatchSummary *out,
                          const RatingPlayerInput *home, size_t home_count,
                          const RatingPlayerInput *away, size_t away_count,
                          int home_goals, int away_goals,
                          bool neutral_venue,
                          char *error, size_t error_size)
{
    double home_strength = 0.0;
    double away_strength = 0.0;
    RatingTeamContext home_context;
    RatingTeamContext away_context;

    if (!rating_team_profile(home, home_count, &home_strength,
                             &home_context) ||
        !rating_team_profile(away, away_count, &away_strength,
                             &away_context)) {
        set_error(error, error_size,
                  "Each team needs at least one valid player.");
        return false;
    }
    return rating_match_summary(out, home_strength, away_strength,
                                &home_context, &away_context,
                                home_goals, away_goals, neutral_venue,
                                error, error_size);
}

bool rating_player_change(const RatingMatchSummary *summary, bool home_team,
                          double rating_before, int minutes,
                          double performance, double *rating_delta,
                          double *rating_after)
{
    if (summary == NULL || rating_delta == NULL || rating_after == NULL ||
        !isfinite(rating_before) ||
        minutes < 1 || minutes > 120 ||
        !isfinite(performance) || performance < 1.0 || performance > 10.0) {
        return false;
    }

    const RatingTeamContext *context =
        home_team ? &summary->home_context : &summary->away_context;
    if (!isfinite(context->total_participation) ||
        context->total_participation <= 0.0 ||
        !isfinite(context->weighted_performance)) {
        return false;
    }

    double participation = rating_participation(minutes);
    double team_delta = home_team ? summary->home_team_delta
                                  : summary->away_team_delta;
    double team_average =
        context->weighted_performance / context->total_participation;
    double result_component =
        team_delta * (participation / context->total_participation);
    double performance_component =
        (performance - team_average) *
        RATING_PERFORMANCE_WEIGHT * participation;

    double proposed_delta = result_component + performance_component;
    double proposed_rating = rating_before + proposed_delta;
    if (!isfinite(proposed_delta) || !isfinite(proposed_rating)) {
        return false;
    }
    if (proposed_delta == 0.0) {
        proposed_delta = 0.0; /* normalize negative zero */
    }
    *rating_delta = proposed_delta;
    *rating_after = proposed_rating;
    return true;
}
