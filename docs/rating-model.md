# ElevenLedger Rating Model

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
- `minutes`: whole minutes actually played, 1-120.
- `sent_off`: the minute the player was removed without replacement (a red card, or another departure the team could not replace), or `0` when the player completed his appearance.
- `performance`: individual match rating, 1.0-10.0; 6.0 is the default input. New entries are quantized to two decimals.

For the match:

- Home and away teams and their line-ups.
- Home and away goals, 0-99.
- Match date.
- Final-whistle minute (`length`), 1-120, including stoppage time.
- Neutral-venue flag. A neutral match removes the home-advantage adjustment; one side is still labelled home for score and line-up entry order.

A player not in the line-up is unaffected by the match.

New matches require at least one player from each team, reject an appearance whose minutes exceed the final whistle, and reject a line-up whose player-minutes exceed 1,320 for either side (11 players x 120 minutes). The save/load validator does not apply these line-up plausibility rules to already-persisted data, so legacy files remain loadable.

## Constants

| Constant | Value | Meaning |
| --- | ---: | --- |
| `RATING_DEFAULT` | 50.0 | Default starting rating for a new player |
| `RATING_K_FACTOR` | 16.0 | Base result sensitivity for the team pool |
| `RATING_HOME_ADVANTAGE` | 4.0 | Rating-point home advantage |
| `RATING_PERFORMANCE_WEIGHT` | 0.75 | Weight of the performance input |
| `RATING_NEUTRAL_PERFORMANCE` | 6.0 | Default performance input |
| Reference minutes | 90 | Participation is `minutes / 90` with no upper cap |

## 1. Participation factors

Actual participation drives line-up strength and the performance component:

```text
participation = minutes / 90
```

Result participation drives only the share of the team result pool:

```text
result_minutes = minutes                         normally
               = length - (sent_off - minutes)   when sent off and the team loses
result_participation = result_minutes / 90
```

The sent-off calculation uses the player's entry minute (`sent_off - minutes`) and counts him from entry to the final whistle. A starter sent off at 30 in a 99-minute loss therefore has 99 result minutes, while his line-up strength still uses his 30 actual minutes. A substitute who enters at 57 and is sent off at 70 in the same match has 42 result minutes. Result minutes are never below the actual minutes. Wins and draws keep the actual minutes, so a red card never increases a player's positive share.

A player who plays the whole match has a factor of 1.0. A 45-minute player has 0.5. A 120-minute appearance has a factor of 1.333, so extra-time minutes carry proportionally more weight than regulation minutes.

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
       = 1.00                                  for a one-goal win or loss
       = 1.20                                  for a two-goal win or loss
       = 1.40                                  for a three-or-more-goal win or loss
```

The margin factor treats a one-goal win as the baseline result. It rises to `1.20` for a two-goal margin and caps at `1.40` from three goals onward. The multiplier depends on the absolute goal difference, so it applies equally to home and away wins.

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
team_participation        = sum(participation)
team_result_participation = sum(result_participation)
team_performance          = sum(performance * participation) / team_participation
```

`team_performance` is the minutes-weighted average performance of the team. `team_participation` uses actual minutes; `team_result_participation` uses the result weighting described above.

## 7. Individual player movement

```text
result_component      = team_delta * result_participation / team_result_participation
performance_component = (performance - team_performance) * 0.75 * participation
rating_delta          = result_component + performance_component
rating_after          = rating_before + rating_delta
```

For a sent-off player on the losing team, only the result component uses the larger result participation. His performance input and the team strength still use the actual minutes.

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
sum(team_delta * result_participation / team_result_participation) = team_delta
```

Therefore each team's total movement is exactly its team delta, and the two team totals are exact opposites. The sum of every player's rating change in the match is zero.

Performance is therefore relative to the player's team-mates. An above-average performance takes points from team-mates; a below-average performance gives points back. A player alone in a team has no performance adjustment, because there is nobody to exchange points with. The team result still moves that player by the full team pool.

## Worked example

Two equal 70.0 line-ups in a normal home match. The home team wins 2-0. Two home players complete 90 minutes at 7.0 and 6.0. Two away players complete 90 minutes at 6.0.

```text
expected_home          = 0.5573
actual_home            = 1.0
margin                 = 1.20
home_team_delta        = 16 * (1 - 0.5573) * 1.20 = 8.50
away_team_delta        = -8.50
team_participation     = 2.0 on each side
home team_performance  = (7.0 + 6.0) / 2 = 6.50
away team_performance  = 6.00
```

The 7.0 home player:

```text
result_component       = 8.50 * 1.0 / 2.0 = 4.25
performance_component  = (7.0 - 6.5) * 0.75 * 1.0 = 0.375
player delta           = 4.63
rating_after           = 74.63
```

The 6.0 home player receives `4.25 - 0.375 = 3.88`. Each away player receives `-4.25`. In full precision the four changes sum to exactly zero; the displayed two-decimal figures can differ by a rounding cent.

At a neutral venue with the same equal line-ups and a 1-0 home win, the home expectation is `0.50` instead of `0.5573`. The shared team pool becomes `+8.00 / -8.00` rather than `+7.08 / -7.08`; a 0-0 draw produces exactly zero team movement.

### Sent-off example

A home starter is sent off at minute 30, the final whistle is minute 99, and the home team loses. The player's actual minutes are 30, so his line-up strength and performance use `30 / 90`. His result participation uses the 99 minutes from entry to the final whistle (`99 / 90 = 1.10`), so he takes a larger result share than a player who completed only 90 minutes. His team-mates take correspondingly less of the negative pool. If the same player had been sent off in a win, his result share would use only the 30 actual minutes.

A substitute who enters at 57 and is sent off at 70 in that 99-minute loss uses `99 - 57 = 42` result minutes, not 90 and not the 13 minutes actually played. The team pool itself is unchanged: the sent-off player's larger share is taken from his team-mates' shares.

## Chronological replay

The database stores each match's final-whistle minute and each appearance's actual minutes and sending-off minute, not a mutable running total. On load or after any history mutation:

1. Matches are sorted by date, then by ID for same-date ordering.
2. A full load resets every player's rating to their starting rating.
3. An incremental replay reconstructs each player's rating and statistics at the first affected match from the starting rating and the valid earlier appearance snapshots.
4. Matches from that point are calculated in order using the ratings available before each match.
5. Appearance snapshots (`rating_before`, `rating_delta`, `rating_after`) are regenerated.
6. Appearances are sorted by match and player for deterministic storage.

Incremental replay is an optimization only: its result must be identical to a full replay from the first match. This makes editing or deleting a match, changing a starting rating or transferring a player safe and consistent. Changing a player's current club does not rewrite their historical appearances; the appearance retains the team that played the match. When an existing match is edited, players already in its line-up keep the club they represented, even if they have since transferred or been archived. Saving the edit replays the affected match and all subsequent history.

The Record match, Edit match and WhoScored import previews use the same chronological rule. Their live rating preview shows each existing player's rating immediately before the fixture's position in the replay, including for old-match edits, backdated dates and imported fixtures. The match dialogs also show it in their Rating before column, and new players created by an import start at 50.0. Changing the fixture date recalculates the preview at the new position. An edited fixture is excluded from its own preview even when its stored date is earlier than the new date; other fixtures before the new date still count, so the preview matches the replay after saving.

## Invariants covered by tests

- Home and away expectations sum to 1.
- Shared home and away team movements are exact opposites.
- Stronger teams have higher expectations.
- A larger goal difference never reduces the result movement.
- Each team's player deltas sum to its team movement.
- All player deltas in a match sum to zero, including with uneven line-up sizes and varying minutes.
- Performance only redistributes points within a team.
- More actual minutes never reduce a player's share of the result pool when result weighting is unchanged.
- A higher performance never reduces the player's delta.
- A sent-off player on a losing team receives at least the result share implied by the time from entry to the final whistle; the same appearance on a win or draw keeps its actual result share.
- Ratings are not clamped to any lower or upper bound.
- There is no per-match movement cap.
- A 120-minute appearance has more participation weight than a 90-minute appearance; there is no 90-minute cap.
- Randomized line-ups satisfy the same invariants over hundreds of cases.

## Calibration notes

The values are intentionally visible and tested. Leagues with different competitive balance can calibrate by changing the constants in `src/core/rating.h` and updating the expected values and documentation. Do not hide calibration changes inside individual pages or storage code; the core engine is the single source of truth.
