#ifndef ELEVENLEDGER_CORE_RATING_H
#define ELEVENLEDGER_CORE_RATING_H

#include <stdbool.h>
#include <stddef.h>

#define RATING_DEFAULT 50.0
#define RATING_NEUTRAL_PERFORMANCE 6.0
#define RATING_K_FACTOR 16.0
#define RATING_HOME_ADVANTAGE 4.0
#define RATING_PERFORMANCE_WEIGHT 0.75

typedef struct {
    double rating;
    int minutes;
    double performance;
    /*
     * Minutes used only for the result-pool share. Zero means "same as
     * minutes". A sent-off player on the losing team uses the time from entry
     * to the final whistle here while keeping the real minutes for strength,
     * performance and statistics.
     */
    int result_minutes;
} RatingPlayerInput;

/*
 * Team-wide totals used to keep every match zero-sum. Result points are shared
 * in proportion to result participation; performance only reallocates points
 * between team-mates. All fields are validated as finite by the rating
 * functions.
 */
typedef struct {
    double total_participation;
    double total_result_participation;
    double weighted_performance;
} RatingTeamContext;

typedef struct {
    double home_strength;
    double away_strength;
    double home_expected;
    double away_expected;
    double home_team_delta;
    double away_team_delta;
    double margin_multiplier;
    double home_result;
    RatingTeamContext home_context;
    RatingTeamContext away_context;
} RatingMatchSummary;

/*
 * Minutes 1-120, performance 1-10. Ratings can be any finite value. A neutral
 * venue removes the home-advantage adjustment from the expected result.
 */
bool rating_compute_match(RatingMatchSummary *out,
                          const RatingPlayerInput *home, size_t home_count,
                          const RatingPlayerInput *away, size_t away_count,
                          int home_goals, int away_goals,
                          bool neutral_venue,
                          char *error, size_t error_size);

/* Computes the team result from already weighted line-up strengths. */
bool rating_match_summary(RatingMatchSummary *out,
                          double home_strength, double away_strength,
                          const RatingTeamContext *home_context,
                          const RatingTeamContext *away_context,
                          int home_goals, int away_goals,
                          bool neutral_venue,
                          char *error, size_t error_size);

/*
 * Validates a line-up and returns its minutes-weighted strength plus the
 * participation and performance totals needed for zero-sum player movement.
 */
bool rating_team_profile(const RatingPlayerInput *players, size_t count,
                         double *strength, RatingTeamContext *context);

double rating_team_strength(const RatingPlayerInput *players, size_t count,
                            bool *valid);

double rating_participation(int minutes);

/*
 * Applies the shared team result to one player. The result share is scaled by
 * participation within the team, while the performance component is measured
 * against the team's participation-weighted average performance. Consequently
 * the deltas of all players in a match add up to zero.
 */
bool rating_player_change(const RatingMatchSummary *summary, bool home_team,
                          double rating_before, int minutes,
                          double performance, double *rating_delta,
                          double *rating_after);

/*
 * Same as rating_player_change, but distributes the result-pool share using
 * result_minutes instead of the actual minutes. This is used for a sent-off
 * player on the losing team; performance and statistics still use minutes.
 */
bool rating_player_change_with_result_minutes(
    const RatingMatchSummary *summary, bool home_team, double rating_before,
    int minutes, int result_minutes, double performance, double *rating_delta,
    double *rating_after);

#endif
