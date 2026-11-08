# Data Format and Recovery

## Location

Default:

```text
%LOCALAPPDATA%\PitchRate\data.frc
```

A different file can be selected with `--data <path>`. The parent folder is created on demand. Data remains local; PitchRate has no network code.

## Integrity model

- Versioned UTF-8 text records.
- One record per line.
- CRC-32 footer over every byte preceding the footer.
- Reference and range validation after parsing.
- Ratings and match summaries are recomputed from appearance inputs on load.
- Writes go to `data.frc.tmp`, are flushed and committed, then checksummed.
- A valid prior save is validated, copied to `data.frc.bak.tmp`, validated again, then promoted to `data.frc.bak` before the new file is moved into place.
- A corrupt primary does not replace a valid backup.
- Move operations use write-through replacement.
- A primary file that cannot be read, or that uses a different format version, is never replaced automatically.
- On startup, a damaged primary file is preserved as `data.frc.corrupt` and replaced from the last good backup only after the backup has passed parsing and validation. If the damaged file cannot be preserved, recovery stops.
- Before promoting the current primary to backup, the save path revalidates the primary and the backup copy. A corrupt primary cannot overwrite a good backup.
- The newly written temporary file is parsed and validated before it can replace the primary.

## Format v1

```text
PITCHRATE|1
TEAM|<id>|<name>|<short_name>|<city>
PLAYER|<id>|<team_id>|<first_name>|<last_name>|<position>|<starting_rating>|<active>
MATCH|<id>|<yyyy-mm-dd>|<home_team_id>|<away_team_id>|<home_goals>|<away_goals>
APP|<match_id>|<player_id>|<team_id>|<minutes>|<performance>
END
CRC32|<8 uppercase hex digits>
```

Positions: `0` goalkeeper, `1` defender, `2` midfielder, `3` forward. `active` is `0` or `1`. Performance is stored with two-decimal quantization. Appearance rating snapshots are derived and are not stored.

Fields escape backslash, pipe, carriage return and newline as `\\`, `\|`, `\r` and `\n`. Player, team and city values must be valid UTF-8 and cannot contain C0 control characters.

The checksum line has no trailing newline. A UTF-8 byte-order mark is accepted when reading but is not written. File size is capped at 64 MiB.

## Recovery flow

1. Try the primary file.
2. If the primary is missing or corrupt, try `data.frc.bak`.
3. If the primary cannot be read (I/O error) or uses a different format version, stop without touching any file. Do not fall back to a potentially older backup.
4. If the backup is valid, preserve the damaged primary as `data.frc.corrupt`, replace the primary from the good backup, and continue. If preservation fails, stop without modifying the primary.
5. If both files are invalid, stop without overwriting either. This prevents silent data loss.

## Compatibility

The `PITCHRATE|1` header identifies schema version 1. Any other version is rejected with a clear message rather than partially parsed, and neither the primary nor its backup is replaced. Any future format change must:

- increment the version,
- keep old files readable or provide an explicit tested migration,
- add round-trip and damaged-file tests,
- update this document and the release checklist.
