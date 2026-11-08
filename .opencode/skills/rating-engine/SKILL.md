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

- Long-term ratings are 1.0-99.0.
- Match performance is 1.0-10.0 with 6.0 neutral.
- Participation is `min(minutes, 90)/90`.
- Line-up strength is minutes-weighted.
- Expected result uses a 40-point Elo scale and +4.0 home advantage.
- Shared team movement uses K=16, actual minus expected, times a goal-margin factor.
- Player movement = `team_delta * participation + (performance - 6) * 0.75 * participation`.
- Player movement is capped at ±12 per match.
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
- Home and away shared movements are exact opposites.
- Player changes stay within ±12 and final ratings within 1-99.
- `rating_after = rating_before + rating_delta` after rounding (within 0.01).
- Increasing performance never decreases a player's delta.
- Increasing minutes never decreases result movement.
- Deleting or changing history replays chronological state consistently.
- Derived fields are not persisted as source data.

Do not compensate for modelling concerns in a page or dialog. If the model needs to change, change the engine and its tests.
