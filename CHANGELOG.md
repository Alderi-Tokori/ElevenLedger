# Changelog

All notable changes to PitchRate are documented here. The project follows a simple versioned schema and local release process.

## 1.4.0 - 2026-11-08

### Changed

- Add Player now pre-selects the team of the most recently added player, falling back to the first team when no players exist.
- Added a database helper and automated coverage for the most recently created player.

## 1.3.0 - 2026-11-08

### Changed

- Centralized the default starting rating in `RATING_DEFAULT` and changed it from 70.0 to 50.0.
- The Add Player form now reads the default from the shared rating constant instead of hardcoding a duplicate value.

## 1.2.0 - 2026-11-08

### Changed

- Removed player position from the data model, player editor and player table. Positions were not used by the rating engine and players commonly cover multiple roles.
- Match line-ups are now ordered by player name instead of position.
- Data format v3 removes the stored position field. V1 and V2 files remain loadable, with legacy position values validated and discarded.

## 1.1.0 - 2026-11-08

### Added

- Per-match neutral-venue support. Neutral matches remove the home-advantage adjustment from the expected result, and the venue is shown in match history and details.
- Data format v2 with backward-compatible loading of v1 files. V1 matches load with `neutral_venue = false`.
- Unit and storage round-trip coverage for neutral and legacy-format matches.

## 1.0.0 - 2026-11-08

Initial production release.

### Added

- Native Win32 dashboard, players, teams, matches and rating-guide pages.
- Team-aware football rating model with minutes and performance weighting.
- Match recording with selectable line-ups, partial minutes and per-player performance.
- Chronological rating replay for edits, deletions and starting-rating changes.
- Match history with player before/change/after breakdowns.
- Versioned, checksummed, atomically saved data files with validated backups and recovery.
- Player and team management with archive, validation and dependency protection.
- Automated C test suite for rating, database, storage and utility logic.
- Interactive GUI smoke test and demo data seeding tool.
- Reproducible application icon and resource tooling.
