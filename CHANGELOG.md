# Changelog

All notable changes to PitchRate are documented here. The project follows a simple versioned schema and local release process.

## 1.12.0 - 2026-11-09

### Changed

- Goal-margin weighting now uses a 1.00 multiplier for a draw or one-goal win, 1.20 for a two-goal win and 1.40 for a three-or-more-goal win. The previous multipliers were 1.00, 1.15, 1.30 and 1.45. Stored ratings and match summaries are recalculated with the new model when history is replayed.

## 1.11.1 - 2026-11-09

### Fixed

- The Record match and Edit match previews, and the line-up Rating before column, now use each player's rating immediately before the fixture's chronological position instead of the player's final current rating. Old-match edits and backdated fixtures therefore preview the same pre-match ratings used by history recalculation; changing the fixture date refreshes the preview.

## 1.11.0 - 2026-11-09

### Added

- The Record match and Edit match dialogs now show a live rating preview. It reports each side's minutes-weighted line-up score and performance average, the expected home result, and the projected home team change for a one-goal win or defeat. The preview updates as players are ticked, team selectors change, minutes and performance are edited, and the neutral-venue option is toggled.

## 1.10.1 - 2026-11-09

### Changed

- The Record match and Edit match dialogs now list clubs alphabetically in the home and away team selectors.

## 1.10.0 - 2026-11-09

### Added

- Player and team lists can be sorted by clicking a column header. The first click sorts ascending and the next click sorts descending; the active column shows a direction arrow. Both lists start sorted by name ascending.

## 1.9.5 - 2026-11-08

### Changed

- The Record match form now lists every club, including clubs without players, so a squad can be created inline before selecting the line-up. Match saving still requires at least one selected player for each side.

## 1.9.4 - 2026-11-08

### Fixed

- The inline Minutes and Performance editor stays visible while the mouse moves over another line-up row. The editor is now raised above the list in the child z-order, and lists clip their painting around overlapping sibling windows.

## 1.9.3 - 2026-11-08

### Fixed

- Double-click editing of Minutes and Performance works again in the match form. The editable columns now use named column constants, so removing the Team column cannot leave the cell editor pointing at the wrong fields.

## 1.9.2 - 2026-11-08

### Fixed

- Closing a match, player, team or match-details dialog no longer exposes another window through the uncovered parent window. Dialogs now hide first, raise and repaint their owner while it is fully exposed, and only then destroy themselves. This also covers nested dialogs such as Add player opened from the match form.

## 1.9.1 - 2026-11-08

### Fixed

- Closing a match creation or edit dialog no longer leaves the parent window presenting a stale black frame. The modal loop repaints the owner and its child windows immediately after re-enabling them.

## 1.9.0 - 2026-11-08

### Changed

- The Record match and Edit match dialogs now show the home and away squads in separate side-by-side lists, making it clear which players belong to each team.
- Each squad list has an Add player button that creates a player for that team without leaving the match dialog. The form keeps the selected team fixed and applies the same validation and duplicate-name warning as the Players page.
- A player created from a squad list is selected automatically for that side, while unsaved line-up selections, minutes and performance values are preserved.

## 1.8.0 - 2026-11-08

### Added

- Existing matches can be edited from the match list or the match details dialog. Date, score, venue, line-up, minutes and performance can change while the match ID and fixture teams stay fixed.
- Existing line-up players remain available while editing and keep the club they represented, even if they have since transferred or been archived. New line-up players must currently belong to one of the two teams.
- Saving an edit replays all matches chronologically and refreshes the match history.

## 1.7.0 - 2026-11-08

### Added

- Adding a player whose name already exists now shows the existing player's club, current rating, starting rating and match count. The warning offers to add a duplicate player, move the existing player to the selected club, or cancel.

## 1.6.2 - 2026-11-08

### Added

- Ctrl+N now opens the same create flow as the current page's primary create button: Add player, Add team or Record match.

## 1.6.1 - 2026-11-08

### Changed

- Rating badge colours are now symmetric around the zero-sum average of 50.0: green from 65.0, blue from 55.0, neutral grey from 45.0, amber from 35.0, and red below 35.0.

## 1.6.0 - 2026-11-08

### Changed

- The rating model is now zero-sum: each team receives one result pool, split across its line-up by minutes, and the winning team's total gain exactly equals the losing team's total loss.
- Individual performance is measured against the team's minutes-weighted average. It redistributes rating points between team-mates instead of creating or removing them.
- Removed the 1.0-99.0 rating bounds, the per-match ±12.0 movement cap and the 1.0-99.0 starting-rating limit. Ratings and starting values accept any finite number.
- Updated the rating guide, worked example, documentation and tests for the zero-sum model.

## 1.5.0 - 2026-11-08

### Changed

- Rating badges now use five bands: green from 80.0, blue from 65.0, neutral grey from 50.0 (average, and the default starting rating), amber from 40.0 and red below 40.0. A rating of exactly 50.0 is no longer shown in red.

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
