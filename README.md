# ElevenLedger

ElevenLedger is a local-first Windows desktop application for calculating and tracking football player ratings. It pairs a transparent, team-aware rating model with a fast native interface for managing teams, squads and match history.

The application is a dependency-free C17 Win32 program. It does not use a browser runtime, .NET, Python, or an online service. Match data stays on the computer and is saved atomically with a checksummed backup. ElevenLedger makes no network requests; WhoScored imports use page source copied from the user's browser.

## Highlights

- Multiple clubs and squads with availability and starting ratings.
- Player and team lists sortable by any column, with a click to sort ascending and another click to reverse the order.
- Match recording with side-by-side home and away squads, inline player creation for any club (including clubs without players), per-player minutes and sending-off minutes, the final-whistle minute and a 1-10 individual performance input.
- Match import from WhoScored page source copied from the browser. The scraped date, clubs, score, players, minutes, final whistle and sending-off minutes are shown in an editable preview before anything is created. Coloured cues identify new players, existing squad members, players linked to another club, and archived players; duplicate actions can be changed per player.
- A date-aware live rating preview in the match form and WhoScored import preview shows both line-up scores, team performance averages and the home team's change for a one-goal win or defeat as players, minutes and performances change. Existing players use their rating immediately before the fixture date; newly created players start at 50.0.
- Team-result movement shared by the line-up, then adjusted for playing time and individual performance.
- Ratings replayed chronologically after edits or deletions, so history stays consistent.
- Recorded matches can be edited; existing line-up players keep the club they represented even after a transfer or archive.
- Optional neutral venues that remove home advantage for cup finals and neutral-site fixtures.
- Duplicate-name protection when adding players, with an option to add a duplicate or move the existing player to the selected club.
- Match history with full before/change/after player breakdowns.
- Tested data layer with checksummed files, backups and atomic writes.
- Native, DPI-aware interface with dashboard, player, team, match and rating-guide pages.
- Fully static executable linking only Windows system libraries.

## Requirements

- Windows 10 or Windows 11, 64-bit.
- CMake 3.21 or newer.
- A C17 compiler. The project is verified with MinGW-w64 GCC 16.1.
- PowerShell 7 (`pwsh`) only for the optional GUI smoke test.
- At least a 1366 x 768 desktop with an effective logical area of 1366 x 768. On a 1366 x 768 panel, use 100% scaling; 1920 x 1080 or larger is recommended when using 125-150% scaling.

## Build

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The application is produced at `build\elevenledger.exe`. It is built as a GUI subsystem executable with static GCC runtime support.

## Run

```powershell
.\build\elevenledger.exe
```

Optional arguments:

```text
--data <path>   Use a specific data file.
--help          Show command-line help.
--version       Show the version.
```

By default, data is stored at:

```text
%LOCALAPPDATA%\ElevenLedger\data.frc
```

Every save writes and verifies a temporary file on a background worker, validates the previous save before promoting it to `data.frc.bak`, and then atomically replaces the primary file without blocking the window. If the primary file is damaged, ElevenLedger preserves it as `data.frc.corrupt` and restores the last good backup before allowing edits. A file using an unsupported ElevenLedger format version is never overwritten or silently downgraded. Legacy v1-v4 files are loaded safely and upgraded to v5 on the next save. Deleted team, player and match IDs stay reserved across reloads.

### Upgrading from PitchRate

If `%LOCALAPPDATA%\ElevenLedger` has no primary or backup yet, the first launch copies `data.frc` and `data.frc.bak` from `%LOCALAPPDATA%\PitchRate` into the new folder. The old files, including any `data.frc.corrupt`, are left untouched. The data format and `.frc` extension are unchanged, so an existing data file also works directly with `--data <path>`.

## Import a match

Open the WhoScored match in your browser, press **Ctrl+U** to view the page
source, then **Ctrl+A** and **Ctrl+C**. In ElevenLedger, open **Matches** and choose
**Import from WhoScored**, then choose **Paste page source**.

ElevenLedger parses the source locally and opens an editable preview. Nothing is
written until **Import match** is confirmed. The preview shows each player's
minutes and a live cue for new players, existing squad members, players linked
to another club, and archived players. Existing players can be reused or moved,
or a separate player can be created. It also shows the same rating summary as
the match form, using each reused player's pre-match rating and the default 50.0
for new players.

ElevenLedger makes no network requests and does not try to bypass WhoScored's
Cloudflare protection. Details and limitations are in
[docs/whoscored-import.md](docs/whoscored-import.md).

## Keyboard shortcuts

- `Ctrl+1` to `Ctrl+5`: switch between Dashboard, Players, Teams, Matches and Rating guide.
- `F1`: open the Rating guide.
- `Ctrl+N`: open the current page's create flow (Add player, Add team or Record match). It does nothing on pages without a create action.

To explore the application with a populated example:

```powershell
.\build\elevenledger_demo_seed.exe .\build\demo.frc
.\build\elevenledger.exe --data .\build\demo.frc
```

The seed tool refuses to replace an existing file. Add `--force` if you intentionally want to regenerate the demo at that path.

## Testing

Run the automated C test suite:

```powershell
ctest --test-dir build --output-on-failure
```

Or run the executable directly for a per-test report:

```powershell
.\build\elevenledger_tests.exe
```

The suite covers date and UTF-8 validation, rating invariants, random-model fuzzing, database history replay, minutes and performance behaviour, storage round-trips, checksum failures, backup recovery, legacy data migration, malformed data, relative paths, WhoScored HTML parsing and transactional match import.

An interactive GUI smoke test is also included. It exercises all pages, modal ownership, dialog teardown, side-by-side match line-ups, the final-whistle and sent-off controls, inline minutes/performance/sent-off editing, player creation and duplicate detection from the match dialog, the WhoScored import source dialog, and the complete record-and-save flow against a temporary data file:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\gui-smoke.ps1
```

## Rating model in one minute

Ratings are long-term player ratings with no fixed upper or lower bound. New players start at 50.0 by default, and that baseline can be any finite number. A separate 1-10 match performance input describes the player's individual display.

1. The line-up strength of each team is the minutes-weighted average of its players' ratings before kick-off.
2. An Elo-style expectation is calculated with a 4.0-point home advantage, or no advantage when the match is marked as a neutral venue.
3. A shared team result pool is calculated from the result, expectation and goal margin. The winning team gains exactly what the losing team loses.
4. Each team's pool is shared across its line-up in proportion to result participation. A sent-off player on a losing team is counted from entry to the final whistle for that share; wins and draws use actual minutes. Individual performance is measured against the team's actual-minutes-weighted average, so it moves points between team-mates rather than creating them.
5. Every match is zero-sum: the sum of all player rating changes is zero. There is no per-match movement cap and no final rating clamp. New match recording rejects a team line-up above 1,320 player-minutes and an appearance longer than the final whistle; legacy data remains loadable.

Matches are always replayed in date order from each player's starting rating. Deleting a match or changing a starting rating therefore updates the whole subsequent career, rather than leaving an inconsistent ledger.

Rating badges use five bands centred on the zero-sum average of 50.0: green from 65.0, blue from 55.0, neutral grey from 45.0, amber from 35.0, and red below 35.0.

Full formulas, a worked example and calibration notes are in [docs/rating-model.md](docs/rating-model.md).

## Project layout

```text
src/core/       Rating engine, database model and storage
src/ui/         Native Win32 pages, controls and dialogs
tests/          Automated C test suite
resources/      Manifest, version resources and icon
scripts/        Development verification scripts
tools/          Demo-data and icon generation tools
docs/           Rating and data-format documentation
```

## Engineering documentation

- [AGENTS.md](AGENTS.md): development rules and definition of done.
- [docs/rating-model.md](docs/rating-model.md): rating algorithm and invariants.
- [docs/data-format.md](docs/data-format.md): storage format and recovery behaviour.
- [docs/whoscored-import.md](docs/whoscored-import.md): match page source import, parsed fields and duplicate cues.
- `.opencode/skills/`: focused workflow skills for rating, UI and release work.

## License

ElevenLedger is released under the [MIT License](LICENSE). Copyright (c) 2026 Vincent Bragard.
