---
name: win32-ui
description: Use when adding or changing PitchRate pages, dialogs, custom controls, DPI layout, list views or Win32 resources. Covers native GDI patterns, modal behaviour and visual verification.
---

# Win32 UI workflow

PitchRate uses native Win32 with GDI drawing. There is no web runtime or third-party UI framework. Favour stable, inspectable controls over fragile custom interaction code.

## Read first

- `src/ui/page.c` for the shared page shell, cards and empty states.
- `src/ui/theme.c` for colours, fonts, DPI scaling and double buffering.
- `src/ui/controls.c` for custom buttons, checkbox, edits, combos, list views and modal loop.
- `src/ui/dialogs.c` and `src/ui/dialog_match.c` for modal patterns.
- `src/app.c` for page ownership and save transactions.

## Layout and painting

- Scale all logical measurements with `ui_scale` or the control's DPI-aware helper.
- Paint through `ui_canvas_begin` / `ui_canvas_end` to avoid flicker.
- Keep page headers consistent through `page_draw_shell`.
- Draw cards through `page_draw_card`; use `page_draw_empty_state` for empty data.
- Use theme colours from `src/ui/theme.h`; do not scatter literal RGB values in pages.
- Guard against narrow or short windows. Clamp sizes against the monitor work area.
- Native child controls paint above the GDI canvas; place lists inside card insets.

## Dialogs

- Use `dialog_create_shell`, a per-dialog `...State` struct on the caller stack and `ui_run_modal`.
- Set the window caption explicitly after creation. Custom `WM_NCCREATE` handlers do not reliably retain title text.
- Use control IDs in the dialog's enum range and handle them in `WM_COMMAND`.
- Track modal ownership: `ui_run_modal` disables the owner and re-enables it on exit.
- Support Escape (modal loop) and Tab (IsDialogMessage/WS_EX_CONTROLPARENT).
- Inline cell editors must set the `PitchRate.CellEditor` property so the modal loop dispatches Enter/Escape to them.
- Never mutate the database directly from a dialog. Clone with `app_tx_begin`, mutate, then `app_tx_commit` or `app_tx_abort`.
- Save buttons should keep the dialog open on validation or disk failure.

## Controls

- Use `ui_button_create` styles for primary/secondary/danger/ghost/soft actions.
- Use the custom checkbox for themed consistent checkboxes.
- Use `ui_list_create` and `ui_list_custom_draw` for tables; keep row image height for readable rows.
- Avoid cross-process pointer-based ListView messages in tests; they are unsafe outside the process. Use real clicks or command messages for automation.
- Keep search filters and selected IDs across refresh.
- Enable Edit/Delete/View buttons only when a row is selected.

## Verification

Build and run:

```powershell
cmake --build build
ctest --test-dir build --output-on-failure
pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\gui-smoke.ps1
```

For visual changes, launch with a temporary data file and capture pages/dialogs. Use a DPI-aware capture path or PrintWindow; a PowerShell host without DPI awareness will capture cropped, scaled UI and give false layout results.

## Common pitfalls

- A top-level window can be enumerated before initialisation finishes. Wait for visible + expected title in automation.
- Use explicit `SetWindowTextW` after creating custom-class windows.
- An owner disabled by a modal dialog must be re-enabled even when the dialog is closed from its title bar.
- Theming a native checkbox leaves an opaque button-face rectangle; use the custom control.
- Set column widths after accounting for the vertical scrollbar, otherwise a horizontal scrollbar can appear.
- Do not leave topmost windows or test data behind after screenshots.
