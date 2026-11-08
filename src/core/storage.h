#ifndef PITCHRATE_CORE_STORAGE_H
#define PITCHRATE_CORE_STORAGE_H

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
 * uses a different format version stops immediately and is never replaced. On
 * STORAGE_RECOVERED the database was loaded from the backup and can be repaired
 * explicitly via storage_repair_from_backup, which preserves the damaged
 * primary as ".corrupt".
 */
StorageStatus storage_load(const wchar_t *path, Database *db,
                           char *error, size_t error_size);
bool storage_save(const wchar_t *path, const Database *db,
                  char *error, size_t error_size);
bool storage_repair_from_backup(const wchar_t *path,
                                char *error, size_t error_size);

#endif
