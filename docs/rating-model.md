# PitchRate Rating Model

## Purpose

The model tracks a long-term player rating on a **1.0 to 99.0** scale. It also accepts a separate **1-10 match performance** value. The long-term rating represents current form and strength; the match performance represents how well the player performed in that one fixture.

The calculation is deliberately small, deterministic and auditable:

- The team result influences every participating player.
- A player's influence on the result grows with time on the pitch.
- Individual performance can improve or reduce the result movement.
- The same stored inputs always produce the same history.

## Inputs

For every player who features:

- `rating_before`: current long-term rating before kick-off, 1.0-99.0.
- `minutes`: whole minutes played, 1-120.
- `performance`: individual match rating, 1.0-10.0; 6.0 is neutral. New entries are quantized to two decimals.

For the match:

- Home and away teams and their line-ups.
- Home and away goals, 0-99.
- Match date.
- Neutral-venue flag. A neutral match removes the home-advantage adjustment; one side is still labelled home for score and line-up entry order.

A player not in the line-up is unaffected by the match.

New matches require at least one player from each team and reject a line-up whose player-minutes exceed 1,320 for either side (11 players x 120 minutes). The save/load validator does not apply this line-up plausibility rule to already-persisted data, so legacy files remain loadable.

## Constants

| Constant | Value | Meaning |
| --- | ---: | --- |
| `RATING_K_FACTOR` | 16.0 | Base result sensitivity |
| `RATING_HOME_ADVANTAGE` | 4.0 | Rating-point home advantage |
| `RATING_PERFORMANCE_WEIGHT` | 0.75 | Weight of the performance input |
| `RATING_NEUTRAL_PERFORMANCE` | 6.0 | Neutral performance |
| `RATING_MAX_MATCH_DELTA` | 12.0 | Maximum movement per match |
| Minimum/maximum rating | 1.0 / 99.0 | Hard rating bounds |
| Full-match minutes | 90 | Impact does not increase beyond 90 |

## 1. Participation factor

```text
participation = min(minutes, 90) / 90
```

A player who plays the whole match has a factor of 1.0. A 45-minute player has 0.5. A 90-minute and a 120-minute appearance have the same player-impact factor; the cap keeps cup extra time from giving an outsized rating swing.

## 2. Line-up strength

Each side has a pre-match line-up strength:

```text
strength = sum(rating_before * participation) / sum(participation)
```

This is a minutes-weighted average. A player who features for only a few minutes has less influence on the team's expected result. Every time in a line-up must have at least one player.

## 3. Expected result

```text
home_advantage = 0.0   for a neutral venue
               = 4.0   otherwise

expected_home = 1 / (1 + 10 ^ ((away_strength - home_strength - home_advantage) / 40.0))
expected_away = 1 - expected_home
```

The 40-point scale and home advantage are expressed in rating points, not win percentages. With two equal line-ups the home side is expected to score about 55.7% of the available result in a normal match, or exactly 50.0% at a neutral venue.

## 4. Actual result and goal margin

```text
actual_home = 1.0  (home win)
            = 0.5  (draw)
            = 0.0  (home loss)

margin = 1.00                                  for a draw
       = 1.00 + 0.15 * min(goal_difference, 3) otherwise
```

The margin factor runs from 1.15 for a one-goal win to 1.45 for a three-or-more-goal win.

## 5. Shared team movement

```text
home_team_delta = 16 * (actual_home - expected_home) * margin
away_team_delta = -home_team_delta
```

This is the team result component. It is symmetric: what the home team gains, the away team loses. It is **not** multiplied by 11 or divided among players; each player receives their own participation-scaled share in the next step.

## 6. Individual player movement

```text
result_component       = team_delta * participation
performance_component  = (performance - 6.0) * 0.75 * participation
proposed_delta          = result_component + performance_component
proposed_delta          = clamp(proposed_delta, -12.0, +12.0)
rating_after            = clamp(rating_before + proposed_delta, 1.0, 99.0)
```

`rating_after` and the final delta are rounded to two decimals for storage. A player with a 6.0 performance receives only the shared result movement. A high individual performance can reduce the damage of a defeat; a low performance reduces the benefit of a win.

## Worked example

Two equal 70.0 line-ups in a normal home match. The home team wins 2-0. One player plays 90 minutes at performance 7.0.

```text
expected_home       = 0.5573
actual_home         = 1.0
margin              = 1.30
home_team_delta     = 16 * (1 - 0.5573) * 1.30 = 9.21
result_component    = 9.21 * 1.0 = 9.21
performance_component = (7.0 - 6.0) * 0.75 * 1.0 = 0.75
player delta        = 9.96
rating_after        = 79.96
```

The full-time 70.0 opponent with a neutral 6.0 performance moves by `-9.21` to `60.79`.

At a neutral venue with the same equal line-ups and a 1-0 home win, the home expectation is `0.50` instead of `0.5573`. The shared team movement becomes `+9.20 / -9.20` rather than `+8.15 / -8.15`; a 0-0 draw produces exactly zero team movement.

## Chronological replay

The database stores each appearance's inputs, not a mutable running total. On load or after any history mutation:

1. Every player's rating resets to their starting rating.
2. Matches are sorted by date, then by ID for same-date ordering.
3. Each match is calculated in order using the ratings available before that match.
4. Appearance snapshots (`rating_before`, `rating_delta`, `rating_after`) are regenerated.
5. Appearances are sorted by match and player for deterministic storage.

This makes deleting a match, changing a starting rating or transferring a player safe and consistent. Changing a player's current club does not rewrite their historical appearances; the appearance retains the team that played the match. Match scores can be corrected by deleting the match and recording it again, which replays the subsequent history.

## Invariants covered by tests

- Home and away expectations sum to 1.
- Shared home and away team movements are exact opposites.
- Stronger teams have higher expectations.
- A larger goal difference never reduces the result movement.
- More minutes never reduce a player's result movement.
- A higher performance never reduces the player's delta.
- Stored rating = before + delta after rounding, within bounds.
- Ratings never leave 1.0-99.0.
- Per-match movement never exceeds 12.0 points.
- Full-time and extra-time player participation factors are equal.
- Randomized line-ups satisfy the same invariants over hundreds of cases.

## Calibration notes

The values are intentionally visible and tested. Leagues with different competitive balance can calibrate by changing the constants in `src/core/rating.h` and updating the expected values and documentation. Do not hide calibration changes inside individual pages or storage code; the core engine is the single source of truth.
