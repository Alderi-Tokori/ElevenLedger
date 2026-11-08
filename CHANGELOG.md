# Changelog

All notable changes to PitchRate are documented here. The project follows a simple versioned schema and local release process.

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
