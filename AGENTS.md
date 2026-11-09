# PitchRate Development Guide

This document is the working contract for anyone modifying PitchRate. It is intentionally concrete. Keep it up to date when architecture, commands or invariants change.

## Mission

PitchRate is a production-grade, local-first Windows desktop application for football player ratings. It must remain stable, understandable, fully tested and deployable as a static GUI executable.

Non-negotiables:

- C17 and CMake.
- Native Win32 GUI. Do not add a browser engine, .NET runtime, Python, or online service.
- No data loss, no silent overwrites, no "temporary" hacks.
- The core rating and storage logic must be testable without a display.
- Every persisted mutation goes through the transactional app layer.
- Unit tests must pass before a commit.
- Git is local only. Never add a remote or push.
- PitchRate must make no network requests. WhoScored imports use page source pasted from the user's browser; never add URL fetching, embedded browsers, or anti-bot bypasses.

Do not inspect or reuse code outside this working directory. Work from this repository and its documentation.

## Build and verification commands

Configure and build:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Release:

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Tests:

```powershell
ctest --test-dir build --output-on-failure
.\build\pitchrate_tests.exe
```

Interactive GUI smoke test:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\gui-smoke.ps1
```

The expected result for every completed change is:

- clean configure and build with no warnings,
- all CTest tests passing,
- GUI smoke test passing when UI behaviour changed,
- no untracked generated data in the repository,
- a reviewed diff.

## Architecture

```text
src/app.c                 Application lifecycle, navigation state, save transactions
src/main.c                WinMain, command line, DPI awareness
src/core/util.c           UTF-8, dates, parsing, CRC-32
src/core/rating.c         Pure rating calculations
src/core/database.c       In-memory domain model, validation, chronological replay
src/core/import.c         WhoScored HTML parsing
src/core/import_apply.c   Transactional club/player/match import engine
src/core/storage.c        Versioned checksummed file format, atomic save, recovery
src/ui/theme.c            DPI-scaled fonts, colours, double-buffered paint helpers
src/ui/controls.c         Custom buttons and checkbox, edits, combos, lists, modal loop
src/ui/page.c             Shared page window and card/header/empty-state painting
src/ui/page_*.c           Five application pages
src/ui/dialogs.c          Shared modal shell and team/player dialogs
src/ui/dialog_match.c     Record/edit-match and match-details dialogs
src/ui/dialog_import.c    Page-source clipboard import and preview cues
src/ui/main_window.c      Shell, sidebar navigation, keyboard accelerators
```

Layering rules:

1. `src/core` must not depend on Win32 UI or the `App` type. The import parser and apply engine must stay testable without a display; only `dialog_import.c` may use the clipboard and the preview controls.
2. UI code must not write the data file directly. Use `app_tx_begin`, `app_tx_commit` and `app_tx_abort`.
3. Derived values (current ratings, appearance before/delta/after, match summaries, player totals) are recomputed. Only source data belongs in the file.
4. The data format is versioned. Format v3 removes player position; v1/v2 files must keep loading with their legacy position discarded and missing neutral values defaulting to false. Add migration tests before changing it again.

## Rating model rules

Source of truth: `src/core/rating.h` and `src/core/rating.c`. Full documentation: `docs/rating-model.md`.

- Ratings have no fixed lower or upper bound; default starting rating 50.0 (`RATING_DEFAULT`), any finite starting value is accepted.
- Players have no stored or input position; do not reintroduce position as an implicit rating factor.
- Performance: 1.0-10.0; default 6.0. New entries are quantized to two decimals.
- Minutes: 1-120; player-impact weight caps at 90 minutes.
- Home advantage: 4.0 rating points for normal matches, 0.0 for neutral-venue matches.
- K factor: 16.0.
- Performance weight: 0.75.
- The team result creates one zero-sum pool: the winning team's total gain equals the losing team's total loss.
- Each team's pool is shared by participation. Performance is measured against the team's minutes-weighted average and only redistributes points within that team.
- Every match is zero-sum: all player deltas add up to zero. Do not add a per-match movement cap or a final rating clamp.
- New match recording rejects a team line-up above 1,320 player-minutes; this validation is intentionally not applied while loading legacy data.
- Matches always replay in date order, then ID order.

Any change to the model requires:

- updated unit tests including randomized invariants,
- updated expected numeric fixtures,
- updated `docs/rating-model.md`,
- updated in-app rating guide text,
- an explicit note in the commit message.

Never patch ratings in a page or dialog. Fix the engine.

## Data safety rules

- Every command that mutates data must clone a backup first (`app_tx_begin`).
- A failed database mutation must call `app_tx_abort`.
- A failed disk save must restore the in-memory database.
- Never truncate or replace a valid file before the replacement has been fully written, flushed and checksummed.
- Before promoting the primary to backup, validate the primary and the backup copy. A corrupt primary must not overwrite a good backup.
- A primary that cannot be read or uses an unsupported format version must never be silently replaced.
- Recovery must preserve a damaged primary as `.corrupt` before restoring the backup.
- No silent reset. If both primary and backup fail validation, stop and report the error.
- All new file validation must reject duplicates, dangling IDs, invalid ranges, invalid UTF-8 and malformed records.
- WhoScored imports must validate the complete plan before mutation, then create clubs, move/create players and add the match inside one app transaction. Cancelling or failing the preview must leave the database unchanged.
- Update `docs/data-format.md` and storage tests for format changes. Import behaviour belongs in `docs/whoscored-import.md`.

## UI rules

- Native Win32 controls and custom GDI drawing; no external GUI libraries.
- All dimensions must go through `ui_scale` or equivalent DPI scaling. Do not hard-code physical pixels in layout decisions.
- Use `ui_canvas_begin` / `ui_canvas_end` for flicker-free page and dialog painting.
- New pages should use the shared `Page` base and `page_draw_shell` / `page_draw_card`.
- Pages with a primary create action must set `Page.create_command` to the create button's command ID so Ctrl+N runs the same flow.
- New toolbars use `ui_button_create` styles rather than native push buttons.
- New confirmations must use `MB_ICONWARNING` and default to the safe choice.
- All modal dialogs must disable their owner, support Tab and Escape, re-enable and repaint the owner before returning, and use `ui_run_modal`. Close dialog windows through `dialog_close` so the owner is exposed and repainted before the dialog is destroyed.
- Inline cell editors must stay above their list in the child z-order after creation so list repaints cannot cover them.
- Custom-class windows should set their window text explicitly after creation. Do not rely on the class's `WM_NCCREATE` path to retain text.
- When adding a player, default the team to the most recently created player's team; use `db_last_added_player` rather than array order guesses.
- Match dialogs show each team's squad in its own list. Add player actions from those lists must use `dialog_add_player_for_team` so the fixed team, validation and duplicate-name flow stay identical to the Players page. Team selectors include every club, even those without players; saving still requires at least one selected player per side.
- Adding a player whose full name matches an existing player must show the existing player's details and offer to add a duplicate, move the existing player to the selected club, or cancel. Use `db_find_player_by_name`; never add the duplicate silently.
- The WhoScored import source dialog must accept page source from the clipboard, keep itself open and show the parser error when the source is invalid, and never fetch a URL. The preview must not mutate data before confirmation. It must show the scraped date, clubs, score, players and minutes, and recompute each player's new/same-club/other-club/archived cue whenever a name or club changes. Per-player duplicate decisions must be explicit; never move an existing player silently.
- Do not add URL fetching, embedded browsers, User-Agent spoofing or anti-bot bypasses. WhoScored's browser-assisted page-source paste is the supported path.
- Pages and dialogs must tolerate DPI settings and available work-area clamping down to the documented minimum display. Do not assume the screen size; clamp all dimensions to non-negative values.
- Text must be valid UTF-8 internally. Convert only at the Win32 boundary.
- Lists must preserve selected IDs across refresh when possible.
- The Players and Teams lists sort by the clicked column: the first click sorts ascending, the next click sorts descending, and the active column shows a direction arrow. Both default to name ascending, and sorting runs after filtering.
- Empty states must explain the next action, not just show an empty box.
- All user-visible labels must be plain language and consistent with the rating guide.

## Testing expectations

Add tests for every bug fix and every new core behaviour. Tests live in `tests/` and are registered in the matching `g_*_tests` array plus `test_main.c` groups.

Minimum coverage areas for changes:

- rating maths and invariants,
- date and UTF-8 validation,
- database CRUD, references, replay, deletion and transfer behaviour,
- WhoScored HTML parsing, minutes and player-name splitting,
- clipboard source validation, transactional import planning, club creation, player creation/moves, archived players and duplicate validation,
- storage round-trip, corruption, recovery and compatibility,
- GUI starts, pages render, modal ownership and complete save flows.

Do not weaken a test to make a failing change pass. Explain and test the intended behaviour instead.

## Coding conventions

- C17, four-space indentation, brace on the same line for functions/control flow.
- Keep functions focused; static helpers in a translation unit when not shared.
- Check every allocation and every Win32 call that can fail.
- Prefer explicit ownership and cleanup over global mutable state.
- Use `bool` from `<stdbool.h>`.
- Use `uint64_t` IDs and never reuse IDs after deletion.
- Error buffers are UTF-8 and sized at least 512 bytes for user-facing domain errors.
- Avoid locale-dependent parsing in persisted formats. The format uses `.` for decimals.
- Build with `-Wall -Wextra -Wpedantic -Wshadow`; do not leave warnings.
- No dead code, debug logging, placeholder labels or silent unimplemented options.

## Git workflow

1. Work locally only. There is no remote.
2. Before committing, inspect `git diff`, review for correctness, data-loss risk and style, and run all required tests.
3. Make focused commits with an imperative subject, for example:
   `Add checksummed backup recovery`.
4. Never commit generated `build/`, data files, temporary screenshots or user data.
5. Update `README.md`, `AGENTS.md`, docs or skills when behaviour or workflow changes.
6. Do not use `git push`.

## Definition of done

A change is complete only when:

- the implementation follows the layering and data rules,
- tests cover the new behaviour and all tests pass,
- the build is warning-free,
- GUI smoke passes for UI changes,
- documentation is current,
- user-visible changes bump `PROJECT_VERSION` and the fallback version strings and add a `CHANGELOG.md` entry,
- the diff has been reviewed,
- there is a local commit.
