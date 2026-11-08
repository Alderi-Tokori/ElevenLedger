---
name: release-quality
description: Use before committing or packaging a PitchRate change. Covers release builds, tests, GUI smoke verification, static imports, data safety review and local git hygiene.
---

# Release quality checklist

Apply this before every commit that claims production readiness.

## 1. Source review

- Read the complete `git diff`.
- Confirm no generated data, screenshots, temporary test players, build products or secrets are staged.
- Confirm core/UI layering was respected.
- Confirm every data mutation uses the `app_tx_*` transaction helpers and handles failure.
- Confirm no TODO, placeholder option, debug logging or dead code remains.
- Confirm new strings are clear, spelled correctly and consistent with the rating guide.

## 2. Build

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --clean-first
```

The build must be warning-free.

## 3. Tests

```powershell
ctest --test-dir build --output-on-failure
.\build\pitchrate_tests.exe
```

All tests must pass. For UI changes also run:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\scripts\gui-smoke.ps1
```

The smoke test must report:
- main window visible with the expected title,
- every page navigates,
- modal ownership works,
- record and save flow succeeds,
- clean exit code 0.

## 4. Binary

- Confirm `build\pitchrate.exe` exists.
- Confirm it is a GUI subsystem executable.
- Inspect imports and ensure no MinGW runtime DLL is required:
  `objdump -p build\pitchrate.exe` should list only Windows system DLLs/API sets.
- The optional demo seed tool and test binary are not deployment artifacts.

## 5. Data compatibility

- If storage changed, verify v1 files still load.
- Verify the checksum, backup and atomic replacement tests still pass.
- Verify recovery preserves `.corrupt` and does not overwrite a good backup with a corrupt file.
- Verify unreadable and differently versioned primary files are not replaced.
- Verify the user data path and `--data` override still work.

## 6. Git

- Stage only source, docs, resources and tests.
- Commit locally with an imperative subject. No remote, no push.
- After committing, verify `git status` is clean except intentional build output.
