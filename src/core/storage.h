#ifndef ELEVENLEDGER_CORE_STORAGE_H
#define ELEVENLEDGER_CORE_STORAGE_H

#include "core/database.h"

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef enum {
    STORAGE_OK = 0,
    STORAGE_RECOVERED,
    STORAGE_NOT_FOUND,
    STORAGE_CORRUPT,
    STORAGE_INCOMPATIBLE_VERSION,
    STORAGE_IO_ERROR
} StorageStatus;

/*
 * Loads a checksummed, versioned data file. If the primary file is missing or
 * invalid, the previous atomic save is tried. A primary that cannot be read or
 * uses an unsupported format version stops immediately and is never replaced. On
 * STORAGE_RECOVERED the database was loaded from the backup and can be repaired
 * explicitly via storage_repair_from_backup, which preserves the damaged
 * primary as ".corrupt", or as the next free ".corrupt.N" name when an earlier
 * damaged copy already exists.
 */
StorageStatus storage_load(const wchar_t *path, Database *db,
                           char *error, size_t error_size);
bool storage_save(const wchar_t *path, const Database *db,
                  char *error, size_t error_size);
bool storage_repair_from_backup(const wchar_t *path,
                                char *error, size_t error_size);

/*
 * Copies a legacy PitchRate data file and its backup into the new
 * ElevenLedger location when that location has no primary or backup yet.
 * The old files, including any damaged copy, are left untouched. Returns
 * true when migration is unnecessary or when every active legacy file was
 * copied.
 */
bool storage_migrate_legacy_data(const wchar_t *legacy_path,
                                 const wchar_t *new_path,
                                 char *error, size_t error_size);

#endif
