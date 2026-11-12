# Data Format and Recovery

## Location

Default:

```text
%LOCALAPPDATA%\ElevenLedger\data.frc
```

A different file can be selected with `--data <path>`. The parent folder is created on demand. Data remains local. ElevenLedger makes no network requests; WhoScored imports use page source pasted from the browser; see [whoscored-import.md](whoscored-import.md).

## Migration from PitchRate

On first launch, when `%LOCALAPPDATA%\ElevenLedger` contains no primary or backup, the application copies `data.frc` and `data.frc.bak` from `%LOCALAPPDATA%\PitchRate` into the new folder. The legacy files, including any `data.frc.corrupt`, are left untouched. If the new folder already contains a primary or backup, or the legacy folder has no active data, migration does nothing; an existing damaged copy in the new folder is left in place. A path selected with `--data` is never migrated.

## Integrity model

- Versioned UTF-8 text records.
- One record per line.
- CRC-32 footer over every byte preceding the footer.
- Reference and range validation after parsing.
- Ratings and match summaries are recomputed from the stored match and appearance inputs when a database is loaded for use.
- Validation-only file checks parse records and verify references and ranges without recomputing ratings.
- Writes go to `data.frc.tmp`, are flushed and committed, then checksummed. The write and verification run on one background save worker so the window remains responsive.
- A valid prior save is validated, copied to `data.frc.bak.tmp`, checksum-verified against the validated primary, then promoted to `data.frc.bak` before the new file is moved into place.
- A corrupt primary does not replace a valid backup.
- Move operations use write-through replacement.
- A primary file that cannot be read, or that uses an unsupported format version, is never replaced automatically.
- On startup, a damaged primary file is preserved as `data.frc.corrupt` and replaced from the last good backup only after the backup has passed parsing and validation. If `data.frc.corrupt` already exists, the damaged copy is preserved as `data.frc.corrupt.1`, then `.2`, and so on, so earlier damaged files are never overwritten. If preservation fails, recovery stops.
- Before promoting the current primary to backup, the save path revalidates the primary and the backup copy. A corrupt primary cannot overwrite a good backup.
- The newly written temporary file is parsed and validated before it can replace the primary.
- The next data mutation waits for any pending background save, and closing the application waits for the pending save to finish.

## Format v5

```text
PITCHRATE|5
NEXT|<next_team_id>|<next_player_id>|<next_match_id>
TEAM|<id>|<name>|<short_name>|<city>
PLAYER|<id>|<team_id>|<first_name>|<last_name>|<starting_rating>|<active>
MATCH|<id>|<yyyy-mm-dd>|<home_team_id>|<away_team_id>|<home_goals>|<away_goals>|<neutral_venue>|<length>
APP|<match_id>|<player_id>|<team_id>|<minutes>|<performance>|<sent_off>
END
CRC32|<8 uppercase hex digits>
```

The `PITCHRATE|` token is retained from the application's former name so existing files remain readable. It identifies the format, not the current product name.

`NEXT` stores the counters used for the next team, player and match IDs. Deleted IDs stay reserved, so after deletions the counters can be greater than the highest stored ID plus one. Each counter must be greater than every stored ID of its kind. The record is required in v5 files; a missing or duplicate `NEXT` record is rejected.

`neutral_venue` is `0` or `1`. `active` is `0` or `1`. `length` is the final-whistle minute, 1-120. `sent_off` is `0` when there was no removal without replacement, otherwise the sending-off minute; a non-zero value must be between that appearance's `minutes` and `length`. Performance is stored with two-decimal quantization. `starting_rating` accepts any finite number, including negative values or values above `99.0`, and is stored with two-decimal quantization. Appearance rating snapshots are derived and are not stored. Actual `minutes` are stored; the sent-off result weighting is derived during replay.

### Format v1-v4 compatibility

- **v1** used an eight-field `PLAYER` record with a position between last name and starting rating, and a seven-field `MATCH` record without a neutral-venue flag.
- **v2** used the same eight-field `PLAYER` record and added the neutral-venue flag to `MATCH`.
- **v3** removes player position. The reader still validates and discards the legacy position field from v1/v2 records.
- **v4** added the final-whistle minute and each appearance's sending-off minute.
- V1 matches load as non-neutral. V1-v3 matches load with a final whistle of 90 and no sending-offs. Files before v5 do not store ID counters, so each counter is derived from the highest stored ID plus one. Saving any loaded older database upgrades it to v5.

The legacy position values were `0` goalkeeper, `1` defender, `2` midfielder and `3` forward. They are not retained after loading.

Fields escape backslash, pipe, carriage return and newline as `\\`, `\|`, `\r` and `\n`. Player, team and city values must be valid UTF-8 and cannot contain C0 control characters.

The checksum line has no trailing newline. A UTF-8 byte-order mark is accepted when reading but is not written. File size is capped at 64 MiB.

## Recovery flow

1. Try the primary file.
2. If the primary is missing or corrupt, try `data.frc.bak`.
3. If the primary cannot be read (I/O error) or uses an unsupported format version, stop without touching any file. Do not fall back to a potentially older backup.
4. If the backup is valid, preserve the damaged primary as `data.frc.corrupt` (or the next free `data.frc.corrupt.N` when that name is taken), replace the primary from the good backup, and continue. If preservation fails, stop without modifying the primary.
5. If both files are invalid, stop without overwriting either. This prevents silent data loss.

## Compatibility

The `PITCHRATE|5` header identifies schema version 5 and keeps the legacy product token for compatibility. Versions 1-4 are still accepted for loading; any other version is rejected with a clear message rather than partially parsed, and neither the primary nor its backup is replaced. Any future format change must:

- increment the version,
- keep old files readable or provide an explicit tested migration,
- add round-trip and damaged-file tests,
- update this document and the release checklist.
