# PitchRate Rating Model

## Purpose

The model tracks a long-term player rating. New players start at **50.0** by default, but every player's starting rating is editable and may be any finite number. There is **no fixed upper or lower rating bound**. It also accepts a separate **1-10 match performance** value. The long-term rating represents current form and strength; the match performance represents how well the player performed in that one fixture.

The calculation is deliberately small, deterministic, auditable and **zero-sum**:

- The team result moves rating points from the losing side to the winning side.
- A player's influence on the result grows with time on the pitch relative to team-mates.
- Individual performance reallocates points inside the team instead of creating new ones.
- Every match conserves rating points: the sum of all player deltas is zero.
- The same stored inputs always produce the same history.

## Inputs

For every player who features:

- `rating_before`: current long-term rating before kick-off. Any finite value is accepted.
- `minutes`: whole minutes played, 1-120.
- `performance`: individual match rating, 1.0-10.0; 6.0 is the default input. New entries are quantized to two decimals.

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
| `RATING_DEFAULT` | 50.0 | Default starting rating for a new player |
| `RATING_K_FACTOR` | 16.0 | Base result sensitivity for the team pool |
| `RATING_HOME_ADVANTAGE` | 4.0 | Rating-point home advantage |
| `RATING_PERFORMANCE_WEIGHT` | 0.75 | Weight of the performance input |
| `RATING_NEUTRAL_PERFORMANCE` | 6.0 | Default performance input |
| Full-match minutes | 90 | Participation does not increase beyond 90 |

## 1. Participation factor

```text
participation = min(minutes, 90) / 90
```

A player who plays the whole match has a factor of 1.0. A 45-minute player has 0.5. A 90-minute and a 120-minute appearance have the same factor; the cap keeps cup extra time from giving an outsized share.

## 2. Line-up strength

Each side has a pre-match line-up strength:

```text
strength = sum(rating_before * participation) / sum(participation)
```

This is a minutes-weighted average. A player who features for only a few minutes has less influence on the team's expected result. Every team in a line-up must have at least one player.

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

## 5. Shared team movement (the pool)

```text
home_team_delta = 16 * (actual_home - expected_home) * margin
away_team_delta = -home_team_delta
```

`home_team_delta` is the **entire** result movement for the home team, not a per-player amount. The winning team's players share exactly this pool, and the losing team's players share its opposite.

A draw sets the margin factor to `1.00`, but it does not by itself create a zero pool: the result component is still `16 * (0.5 - expected_home)`, which is non-zero whenever the pre-match expectation is not exactly `0.50`. For example, a draw between two equal line-ups at a normal home venue moves about `0.92` rating points from the home team to the away team. Only a match with `expected_home == 0.50` — such as a 0-0 draw at a neutral venue between equal line-ups — produces exactly zero team movement.

## 6. Team context

For each team, using only its players:

```text
team_participation  = sum(participation)
team_performance    = sum(performance * participation) / team_participation
```

`team_performance` is the minutes-weighted average performance of the team.

## 7. Individual player movement

```text
result_component      = team_delta * participation / team_participation
performance_component = (performance - team_performance) * 0.75 * participation
rating_delta          = result_component + performance_component
rating_after          = rating_before + rating_delta
```

There is no per-match delta cap, no rating clamp and no rounding inside the engine. Ratings are any finite value. Presentation layers may round values for display.

### Why this is zero-sum

Within one team, the performance components sum to zero:

```text
sum((performance - team_performance) * 0.75 * participation)
= 0.75 * (sum(performance * participation) - team_performance * team_participation)
= 0
```

The result components sum to `team_delta`:

```text
sum(team_delta * participation / team_participation) = team_delta
```

Therefore each team's total movement is exactly its team delta, and the two team totals are exact opposites. The sum of every player's rating change in the match is zero.

Performance is therefore relative to the player's team-mates. An above-average performance takes points from team-mates; a below-average performance gives points back. A player alone in a team has no performance adjustment, because there is nobody to exchange points with. The team result still moves that player by the full team pool.

## Worked example

Two equal 70.0 line-ups in a normal home match. The home team wins 2-0. Two home players complete 90 minutes at 7.0 and 6.0. Two away players complete 90 minutes at 6.0.

```text
expected_home          = 0.5573
actual_home            = 1.0
margin                 = 1.30
home_team_delta        = 16 * (1 - 0.5573) * 1.30 = 9.21
away_team_delta        = -9.21
team_participation     = 2.0 on each side
home team_performance  = (7.0 + 6.0) / 2 = 6.50
away team_performance  = 6.00
```

The 7.0 home player:

```text
result_component       = 9.21 * 1.0 / 2.0 = 4.60
performance_component  = (7.0 - 6.5) * 0.75 * 1.0 = 0.375
player delta           = 4.98
rating_after           = 74.98
```

The 6.0 home player receives `4.60 - 0.375 = 4.23`. Each away player receives `-4.60`. In full precision the four changes sum to exactly zero; the displayed two-decimal figures can differ by a rounding cent.

At a neutral venue with the same equal line-ups and a 1-0 home win, the home expectation is `0.50` instead of `0.5573`. The shared team pool becomes `+9.20 / -9.20` rather than `+8.15 / -8.15`; a 0-0 draw produces exactly zero team movement.

## Chronological replay

The database stores each appearance's inputs, not a mutable running total. On load or after any history mutation:

1. Every player's rating resets to their starting rating.
2. Matches are sorted by date, then by ID for same-date ordering.
3. Each match is calculated in order using the ratings available before that match.
4. Appearance snapshots (`rating_before`, `rating_delta`, `rating_after`) are regenerated.
5. Appearances are sorted by match and player for deterministic storage.

This makes editing or deleting a match, changing a starting rating or transferring a player safe and consistent. Changing a player's current club does not rewrite their historical appearances; the appearance retains the team that played the match. When an existing match is edited, players already in its line-up keep the club they represented, even if they have since transferred or been archived. Saving the edit replays all subsequent history.

## Invariants covered by tests

- Home and away expectations sum to 1.
- Shared home and away team movements are exact opposites.
- Stronger teams have higher expectations.
- A larger goal difference never reduces the result movement.
- Each team's player deltas sum to its team movement.
- All player deltas in a match sum to zero, including with uneven line-up sizes and varying minutes.
- Performance only redistributes points within a team.
- More minutes never reduce a player's share of the result pool.
- A higher performance never reduces the player's delta.
- Ratings are not clamped to any lower or upper bound.
- There is no per-match movement cap.
- Full-time and extra-time player participation factors are equal.
- Randomized line-ups satisfy the same invariants over hundreds of cases.

## Calibration notes

The values are intentionally visible and tested. Leagues with different competitive balance can calibrate by changing the constants in `src/core/rating.h` and updating the expected values and documentation. Do not hide calibration changes inside individual pages or storage code; the core engine is the single source of truth.
