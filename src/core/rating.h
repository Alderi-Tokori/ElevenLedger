#ifndef PITCHRATE_CORE_RATING_H
#define PITCHRATE_CORE_RATING_H

#include <stdbool.h>
#include <stddef.h>

#define RATING_MIN 1.0
#define RATING_MAX 99.0
#define RATING_DEFAULT 50.0
#define RATING_NEUTRAL_PERFORMANCE 6.0
#define RATING_K_FACTOR 16.0
#define RATING_HOME_ADVANTAGE 4.0
#define RATING_PERFORMANCE_WEIGHT 0.75
#define RATING_MAX_MATCH_DELTA 12.0

typedef struct {
    double rating;
    int minutes;
    double performance;
} RatingPlayerInput;

typedef struct {
    double home_strength;
    double away_strength;
    double home_expected;
    double away_expected;
    double home_team_delta;
    double away_team_delta;
    double margin_multiplier;
    double home_result;
} RatingMatchSummary;

/*
 * Minutes 1-120, performance 1-10, ratings 1-99. A neutral venue removes the
 * home-advantage adjustment from the expected result.
 */
bool rating_compute_match(RatingMatchSummary *out,
                          const RatingPlayerInput *home, size_t home_count,
                          const RatingPlayerInput *away, size_t away_count,
                          int home_goals, int away_goals,
                          bool neutral_venue,
                          char *error, size_t error_size);

/* Computes only the team-level part from already weighted line-up strengths. */
bool rating_match_summary(RatingMatchSummary *out,
                          double home_strength, double away_strength,
                          int home_goals, int away_goals,
                          bool neutral_venue,
                          char *error, size_t error_size);

double rating_team_strength(const RatingPlayerInput *players, size_t count,
                            bool *valid);

double rating_participation(int minutes);

/*
 * Applies the shared team result to one player and adds the individual
 * performance adjustment. All adjustments are weighted by minutes played.
 */
bool rating_player_change(const RatingMatchSummary *summary, bool home_team,
                          double rating_before, int minutes,
                          double performance, double *rating_delta,
                          double *rating_after);

#endif
