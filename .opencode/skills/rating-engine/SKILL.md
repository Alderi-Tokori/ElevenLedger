---
name: rating-engine
description: Use when changing PitchRate rating formulas, match calculation, chronological replay, player statistics or rating tests. Covers the team-aware model, invariants and required numeric fixtures.
---

# Rating engine workflow

PitchRate's rating engine is pure C in `src/core/rating.c` and `src/core/database.c`. It must stay deterministic, testable without a GUI and consistent with `docs/rating-model.md`.

## Read first

- `src/core/rating.h` for constants and public calculations.
- `src/core/rating.c` for formulas.
- `src/core/database.c` for replay and derived fields.
- `tests/test_rating.c` and `tests/test_database.c` for invariants.
- `docs/rating-model.md` for the user-facing contract.

## Model contract

- Long-term ratings have no fixed lower or upper bound; starting ratings must be finite.
- Match performance is 1.0-10.0 with 6.0 as the default input.
- Actual participation is `minutes / 90` with no 90-minute cap; result participation uses `result_minutes`, normally equal to actual minutes.
- A player sent off in a lost match uses entry-to-final-whistle result minutes. Wins and draws use actual minutes. Line-up strength, performance and statistics always use actual minutes.
- Line-up strength is actual-minutes-weighted.
- Expected result uses a 40-point Elo scale. Normal matches add +4.0 home advantage; neutral-venue matches add 0.0.
- Team movement uses K=16, actual minus expected, times a goal-margin factor. It is one pool for the whole team, not a per-player amount.
- The pool is split by `result_participation / team_result_participation`.
- Performance is `(performance - team_weighted_average_performance) * 0.75 * actual_participation`. It only redistributes points between team-mates.
- There is no per-match movement cap and no rating clamp.
- Every match conserves rating points: all player deltas sum to zero.
- Matches are replayed by date and ID from each player's starting rating.

## Change checklist

1. Update the constant or formula in the core engine only.
2. Add or update deterministic fixture tests with numeric expected values.
3. Extend the randomized invariants in `test_randomized_invariants`.
4. Add replay/ordering tests if database semantics are affected.
5. Update `docs/rating-model.md` and the in-app guide text in `src/ui/page_guide.c`.
6. Run:
   `cmake --build build`
   `ctest --test-dir build --output-on-failure`
   `.\build\pitchrate_tests.exe`
7. Run `scripts/gui-smoke.ps1` if the in-app explanation changed.

## Invariants that must never regress

- Home and away expectations sum to 1.
- Home and away team movements are exact opposites.
- Each team's player deltas sum to its team movement.
- All player deltas in a match sum to zero.
- There is no per-match cap and no final rating clamp.
- `rating_after = rating_before + rating_delta` (within floating-point tolerance).
- Increasing performance never decreases a player's delta.
- For a fixed result weighting, increasing minutes never reduces a player's share of the result pool.
- A sent-off player on a losing team gets at least the result share implied by entry-to-final-whistle minutes; wins and draws keep the actual result share.
- A 120-minute appearance has more participation weight than a 90-minute appearance; participation is not capped at 90.
- Deleting or changing history replays chronological state consistently.
- Derived fields are not persisted as source data.

Do not compensate for modelling concerns in a page or dialog. If the model needs to change, change the engine and its tests.
