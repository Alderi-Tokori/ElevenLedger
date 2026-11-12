# Changelog

All notable changes to ElevenLedger are documented here. Entries before 2.0.0 use the application's former name, PitchRate. The project follows a simple versioned schema and local release process.

## 2.0.11 - 2026-11-12

### Fixed

- WhoScored imports now cap substitution times reported after the final whistle at the real match length. Such players previously received more minutes than the match lasted, which rejected the whole import during plan validation.

## 2.0.10 - 2026-11-12

### Fixed

- WhoScored imports now match card events for players whose WhoScored ID is larger than one million. Those players keep their sending-off minute and actual minutes instead of being credited with the whole match.

## 2.0.9 - 2026-11-12

### Fixed

- WhoScored imports now keep a substitute who is recorded as entering and leaving in the same minute at the one-minute minimum instead of crediting them with the rest of the match.

## 2.0.8 - 2026-11-12

### Fixed

- WhoScored imports no longer treat a card shown to a player who had already left the pitch as a sending-off. Such cards previously produced an invalid sending-off minute that could reject the whole import.

## 2.0.7 - 2026-11-12

### Fixed

- Editing a match and moving it to a later date no longer replays the edited fixture into its own pre-match rating preview. Other fixtures before the new date still count, matching the ratings recomputed after saving.

## 2.0.6 - 2026-11-12

### Fixed

- Recording or editing a match now rejects an appearance whose minutes exceed the final whistle. WhoScored import plans apply the same check before creating clubs, moving players or recording the fixture.

## 2.0.5 - 2026-11-10

### Changed

- Rating replay is now incremental. Appending a match recalculates only that match; editing or deleting a match recalculates from the first affected fixture onward instead of replaying the whole history.
- The match, player and team lists now update changed rows in place instead of rebuilding the entire list. The match page also reads each fixture's team movement from the sorted appearance range rather than scanning every appearance.

## 2.0.4 - 2026-11-10

### Changed

- Saving now refreshes only the visible page. Hidden player, team and match lists rebuild when they are opened instead of after every save, removing a large UI stall on multi-season databases.

## 2.0.3 - 2026-11-10

### Changed

- File validation before saving no longer replays every rating. The existing primary and the newly written temp file are still parsed and fully checked, while the backup copy is verified by checksum against the validated primary instead of being parsed again.

## 2.0.2 - 2026-11-10

### Changed

- Saves now run on a single background worker. The window remains responsive while the file is validated, written, verified and promoted, and the sidebar shows "Saving changes..." until the save completes. Closing the application waits for the pending save instead of interrupting it.
- Match and player edits no longer repeat a full database validation during rating replay. The changed inputs are validated immediately, while full validation remains on file load and in the background save.

## 2.0.1 - 2026-11-09

### Fixed

- Database validation now uses indexed lookups instead of comparing every match appearance with every other appearance. Saving a multi-season league no longer stalls while the complete history is checked.

## 2.0.0 - 2026-11-09

### Changed

- Renamed the application from PitchRate to ElevenLedger. The executable is now `elevenledger.exe`, and the window, dialog, resource and test names use ElevenLedger.
- The default data folder is now `%LOCALAPPDATA%\ElevenLedger`. On first launch, the existing primary and backup files in `%LOCALAPPDATA%\PitchRate` are copied to the new folder; the old folder, including any `.corrupt` file, is left untouched.
- The data format and `.frc` extension are unchanged. Files with the legacy `PITCHRATE|` header remain readable.

## 1.18.2 - 2026-11-09

### Changed

- PitchRate is now released under the MIT License. Copyright (c) 2026 Vincent Bragard.

## 1.18.1 - 2026-11-09

### Fixed

- Closing a team, player, match, match-details or WhoScored import dialog no longer lets another application flash into the foreground. The dialog now re-enables and activates its owner before it is hidden, so the main window never disappears for a frame.

## 1.18.0 - 2026-11-09

### Added

- The WhoScored import preview now shows the same live rating summary as the Record match and Edit match dialogs: both line-up scores, team performance averages, the expected home result and the home team's projected change for a one-goal win or defeat. Reused players use their rating before the imported fixture, new players start at 50.0, and the summary updates as names, clubs, minutes, performances, player actions and the neutral-venue option change.

## 1.17.2 - 2026-11-09

### Fixed

- The WhoScored import preview now gives the Sent off column enough width for its header while trimming the wider Status column.

## 1.17.1 - 2026-11-09

### Fixed

- The Record match, Edit match and WhoScored import preview dialogs are wider and use proportional line-up columns, so the minute, sent-off, performance and rating columns are no longer squeezed together.
- The Record match and Edit match dialogs now allow the sent-off minute to be edited by double-clicking the Sent off cell. Previously only minutes and performance were editable.

## 1.17.0 - 2026-11-09

### Changed

- Removed the 90-minute cap on participation weighting. Minutes above 90 now increase a player's line-up strength weight, result share and performance share proportionally; a 120-minute appearance counts as `120 / 90 = 1.333`.

## 1.16.0 - 2026-11-09

### Added

- Match dialogs now record the final-whistle minute and each player's sending-off minute. WhoScored imports fill both from the page source and show them in the preview.

### Changed

- A player sent off in a lost match now uses the result share from his entry minute to the final whistle, capped at 90 minutes. Wins and draws keep the actual minutes. Line-up strength, performance and career statistics still use the actual minutes.
- The data format is now v4. Older v1-v3 files load with a 90-minute final whistle and no sending-offs, and are upgraded on save.

## 1.15.3 - 2026-11-09

### Fixed

- WhoScored import now stops a player's minutes at a sending-off. Straight red cards and second yellows are read from the match events, so a sent-off starter no longer receives the full match length.

## 1.15.2 - 2026-11-09

### Fixed

- WhoScored import no longer creates a duplicate player when the preview's club field is corrected to the club the player already belongs to. Rows now return to the default action for their recomputed cue, while an explicitly chosen per-player action is kept for the same matched player.
- The import preview now prefers a club's own active player when the same name also exists in another club.

## 1.15.1 - 2026-11-09

### Fixed

- WhoScored import now uses the real elapsed match length from `maxMinute` (falling back to the last `periodEndMinutes` entry) instead of assuming 90 or 120 minutes. Starting players receive stoppage-time minutes, and substitution times are converted from WhoScored's expanded clock back to real match minutes.

## 1.15.0 - 2026-11-09

### Changed

- WhoScored import now accepts only page source pasted from the user's browser. The import dialog shows the source-copy steps and validates/parses the clipboard before opening the existing editable preview.

### Removed

- URL entry, direct HTTP downloading, the WinHTTP worker thread, the Open in browser action and URL validation. WhoScored blocks direct requests through Cloudflare; changing the User-Agent or headers does not fix that. PitchRate now makes no network requests.

## 1.14.1 - 2026-11-09

### Changed

- Clarified WhoScored import failures. A 403 can be an interactive Cloudflare check or a hard network block page. The dialog now explains that a User-Agent change will not solve a blocked network and points to the browser-assisted **Open in browser** and **Paste page source** path when the page is reachable in a browser.

## 1.14.0 - 2026-11-09

### Added

- The WhoScored import dialog now offers **Open in browser** and **Paste page source**. If WhoScored blocks direct downloads with a browser check, the user can pass the check in their normal browser, view the page source with Ctrl+U, copy it and paste it into PitchRate. The pasted source is parsed locally and still requires confirmation in the import preview.
- The 403 message now explains the browser-assisted path instead of only reporting the block.

## 1.13.0 - 2026-11-09

### Added

- Matches can be imported from a WhoScored match URL. PitchRate downloads the page, reads the embedded match data, and opens an editable preview with the fixture date, home and away clubs, score, players and minutes. Cancelling the preview creates nothing.
- The import preview recomputes a coloured cue for every player when names or clubs change: new player, existing squad member, player linked to another club, or archived player. Players linked to another club can be created as a separate record or moved to the imported club per player.
- New clubs are created by name when confirmed, and all club, player and match changes run in one save transaction. Existing players keep their rating and history when moved.
- Match page downloads run on a worker thread. Blocked or malformed pages leave the URL dialog open with a clear HTTP or parsing error.

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
