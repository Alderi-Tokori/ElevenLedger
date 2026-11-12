#include "core/storage.h"

#include "core/util.h"

#include <errno.h>
#include <inttypes.h>
#include <io.h>
#include <math.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define STORAGE_FORMAT_VERSION 5
#define STORAGE_MIN_FORMAT_VERSION 1
#define STORAGE_MAX_FILE_BYTES (64u * 1024u * 1024u)
#define STORAGE_MAX_FIELDS 12
#define STORAGE_READ_BUFFER 8192

/*
 * The header keeps the original product token so files written before the
 * ElevenLedger rename remain readable. Changing it requires a format-version
 * migration.
 */
#define STORAGE_MAGIC "PITCHRATE|"
#define STORAGE_MAGIC_LENGTH 10

static StorageStatus load_file(const wchar_t *path, Database *db,
                               bool recompute, uint32_t *out_content_crc,
                               char *error, size_t error_size);
static StorageStatus validate_data_file(const wchar_t *path,
                                        uint32_t *out_content_crc,
                                        char *error, size_t error_size);
static StorageStatus validate_data_file_checksum(const wchar_t *path,
                                                 uint32_t expected_crc,
                                                 char *error,
                                                 size_t error_size);

static void set_error(char *error, size_t error_size, const char *format, ...)
{
    if (error == NULL || error_size == 0) {
        return;
    }
    va_list args;
    va_start(args, format);
    vsnprintf(error, error_size, format, args);
    va_end(args);
}

static void set_windows_error(char *error, size_t error_size,
                              const wchar_t *action, DWORD code)
{
    if (error == NULL || error_size == 0) {
        return;
    }
    wchar_t wide[512];
    wide[0] = L'\0';
    DWORD length = FormatMessageW(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, code, 0, wide, (DWORD)(sizeof(wide) / sizeof(wide[0])), NULL);
    if (length == 0) {
        snprintf(error, error_size, "%ls failed (Windows error %lu).",
                 action, (unsigned long)code);
        return;
    }
    while (length > 0 &&
           (wide[length - 1] == L'\r' || wide[length - 1] == L'\n' ||
            wide[length - 1] == L' ')) {
        wide[--length] = L'\0';
    }
    char utf8[768];
    int converted = WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8,
                                        (int)sizeof(utf8), NULL, NULL);
    if (converted == 0) {
        snprintf(error, error_size, "%ls failed (Windows error %lu).",
                 action, (unsigned long)code);
        return;
    }
    snprintf(error, error_size, "%ls: %s", action, utf8);
}

static wchar_t *build_sibling_path(const wchar_t *path, const wchar_t *suffix,
                                   char *error, size_t error_size)
{
    if (path == NULL || suffix == NULL) {
        set_error(error, error_size, "Invalid data path.");
        return NULL;
    }
    size_t path_length = wcslen(path);
    size_t suffix_length = wcslen(suffix);
    if (path_length > SIZE_MAX - suffix_length - 1) {
        set_error(error, error_size, "Data path is too long.");
        return NULL;
    }
    wchar_t *result = malloc((path_length + suffix_length + 1) *
                             sizeof(wchar_t));
    if (result == NULL) {
        set_error(error, error_size, "Not enough memory for a data path.");
        return NULL;
    }
    memcpy(result, path, path_length * sizeof(wchar_t));
    memcpy(result + path_length, suffix,
           (suffix_length + 1) * sizeof(wchar_t));
    return result;
}

static bool ensure_parent_directory(const wchar_t *path,
                                    char *error, size_t error_size)
{
    size_t length = wcslen(path);
    if (length == 0) {
        set_error(error, error_size, "The data path is empty.");
        return false;
    }
    wchar_t *parent = malloc((length + 1) * sizeof(wchar_t));
    if (parent == NULL) {
        set_error(error, error_size, "Not enough memory for the data path.");
        return false;
    }
    memcpy(parent, path, (length + 1) * sizeof(wchar_t));

    wchar_t *last_separator = NULL;
    for (wchar_t *cursor = parent; *cursor != L'\0'; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') {
            last_separator = cursor;
        }
    }
    if (last_separator == NULL) {
        free(parent);
        return true;
    }
    *last_separator = L'\0';
    if (parent[0] == L'\0' ||
        (parent[0] != L'\0' && parent[1] == L':' && parent[2] == L'\0')) {
        free(parent);
        return true;
    }

    DWORD required = GetFullPathNameW(parent, 0, NULL, NULL);
    if (required == 0) {
        set_windows_error(error, error_size, L"Resolving the data folder",
                          GetLastError());
        free(parent);
        return false;
    }
    wchar_t *absolute = malloc((size_t)required * sizeof(wchar_t));
    if (absolute == NULL) {
        free(parent);
        set_error(error, error_size,
                  "Not enough memory for the data folder path.");
        return false;
    }
    if (GetFullPathNameW(parent, required, absolute, NULL) == 0) {
        set_windows_error(error, error_size, L"Resolving the data folder",
                          GetLastError());
        free(absolute);
        free(parent);
        return false;
    }
    free(parent);

    int result = SHCreateDirectoryExW(NULL, absolute, NULL);
    free(absolute);
    if (result != ERROR_SUCCESS && result != ERROR_ALREADY_EXISTS &&
        result != ERROR_FILE_EXISTS) {
        set_windows_error(error, error_size, L"Creating the data folder",
                          (DWORD)result);
        return false;
    }
    return true;
}

static bool local_file_exists(const wchar_t *path)
{
    return path != NULL &&
           GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static bool copy_legacy_file(const wchar_t *source,
                             const wchar_t *destination, bool *copied,
                             char *error, size_t error_size)
{
    *copied = false;
    if (!local_file_exists(source) || local_file_exists(destination)) {
        return true;
    }
    if (!CopyFileW(source, destination, TRUE)) {
        DWORD code = GetLastError();
        if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) {
            return true;
        }
        set_windows_error(error, error_size,
                          L"Copying the existing data file", code);
        DeleteFileW(destination);
        return false;
    }
    *copied = true;
    return true;
}

bool storage_migrate_legacy_data(const wchar_t *legacy_path,
                                 const wchar_t *new_path,
                                 char *error, size_t error_size)
{
    if (legacy_path == NULL || new_path == NULL) {
        set_error(error, error_size, "Invalid data migration path.");
        return false;
    }
    if (_wcsicmp(legacy_path, new_path) == 0) {
        return true;
    }

    wchar_t *legacy_backup = build_sibling_path(legacy_path, L".bak",
                                                error, error_size);
    wchar_t *new_backup = build_sibling_path(new_path, L".bak",
                                             error, error_size);
    if (legacy_backup == NULL || new_backup == NULL) {
        free(legacy_backup);
        free(new_backup);
        return false;
    }

    bool destination_has_data = local_file_exists(new_path) ||
                                local_file_exists(new_backup);
    bool source_has_data = local_file_exists(legacy_path) ||
                           local_file_exists(legacy_backup);
    bool ok = true;
    if (!destination_has_data && source_has_data) {
        ok = ensure_parent_directory(new_path, error, error_size);
        bool copied_primary = false;
        bool copied_backup = false;
        if (ok) {
            ok = copy_legacy_file(legacy_path, new_path, &copied_primary,
                                  error, error_size);
        }
        if (ok) {
            ok = copy_legacy_file(legacy_backup, new_backup, &copied_backup,
                                  error, error_size);
        }
        if (!ok) {
            if (copied_backup) {
                DeleteFileW(new_backup);
            }
            if (copied_primary) {
                DeleteFileW(new_path);
            }
        }
    }

    free(legacy_backup);
    free(new_backup);
    return ok;
}

static bool write_field(FILE *file, const char *value)
{
    if (fputc('|', file) == EOF) {
        return false;
    }
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor != 0; ++cursor) {
        switch (*cursor) {
        case '\\':
            if (fputs("\\\\", file) == EOF) {
                return false;
            }
            break;
        case '|':
            if (fputs("\\|", file) == EOF) {
                return false;
            }
            break;
        case '\n':
            if (fputs("\\n", file) == EOF) {
                return false;
            }
            break;
        case '\r':
            if (fputs("\\r", file) == EOF) {
                return false;
            }
            break;
        default:
            if (fputc(*cursor, file) == EOF) {
                return false;
            }
            break;
        }
    }
    return true;
}

static bool write_team(FILE *file, const Team *team)
{
    if (fprintf(file, "TEAM|%" PRIu64, team->id) < 0 ||
        !write_field(file, team->name) ||
        !write_field(file, team->short_name) ||
        !write_field(file, team->city)) {
        return false;
    }
    return fputc('\n', file) != EOF;
}

static bool write_player(FILE *file, const Player *player)
{
    if (fprintf(file, "PLAYER|%" PRIu64 "|%" PRIu64,
                player->id, player->team_id) < 0 ||
        !write_field(file, player->first_name) ||
        !write_field(file, player->last_name) ||
        fprintf(file, "|%.2f|%d",
                player->base_rating, player->active ? 1 : 0) < 0) {
        return false;
    }
    return fputc('\n', file) != EOF;
}

static bool write_match(FILE *file, const Match *match)
{
    if (fprintf(file, "MATCH|%" PRIu64, match->id) < 0 ||
        !write_field(file, match->date) ||
        fprintf(file, "|%" PRIu64 "|%" PRIu64 "|%d|%d|%d|%d",
                match->home_team_id, match->away_team_id,
                match->home_goals, match->away_goals,
                match->neutral_venue ? 1 : 0, match->length) < 0) {
        return false;
    }
    return fputc('\n', file) != EOF;
}

static bool write_appearance(FILE *file, const Appearance *appearance)
{
    if (fprintf(file, "APP|%" PRIu64 "|%" PRIu64 "|%" PRIu64
                      "|%d|%.2f|%d",
                appearance->match_id, appearance->player_id,
                appearance->team_id, appearance->minutes,
                appearance->performance, appearance->sent_off) < 0) {
        return false;
    }
    return fputc('\n', file) != EOF;
}

static bool write_records(FILE *file, const Database *db)
{
    if (fprintf(file, STORAGE_MAGIC "%d\n", STORAGE_FORMAT_VERSION) < 0 ||
        fprintf(file, "NEXT|%" PRIu64 "|%" PRIu64 "|%" PRIu64 "\n",
                db->next_team_id, db->next_player_id,
                db->next_match_id) < 0) {
        return false;
    }
    for (size_t i = 0; i < db->team_count; ++i) {
        if (!write_team(file, &db->teams[i])) {
            return false;
        }
    }
    for (size_t i = 0; i < db->player_count; ++i) {
        if (!write_player(file, &db->players[i])) {
            return false;
        }
    }
    for (size_t i = 0; i < db->match_count; ++i) {
        if (!write_match(file, &db->matches[i])) {
            return false;
        }
    }
    for (size_t i = 0; i < db->appearance_count; ++i) {
        if (!write_appearance(file, &db->appearances[i])) {
            return false;
        }
    }
    return fputs("END\n", file) != EOF;
}

static bool close_written_file(FILE *file, const wchar_t *path,
                               char *error, size_t error_size)
{
    bool ok = true;
    if (fflush(file) != 0) {
        ok = false;
    }
    if (ok && _commit(_fileno(file)) != 0) {
        ok = false;
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    if (!ok) {
        set_error(error, error_size,
                  "Could not completely write the data file (error %d).",
                  errno);
        DeleteFileW(path);
        return false;
    }
    return true;
}

static bool compute_file_crc(const wchar_t *path, uint32_t *out,
                             char *error, size_t error_size)
{
    FILE *file = _wfopen(path, L"rb");
    if (file == NULL) {
        set_error(error, error_size,
                  "Could not read back the data file (error %d).", errno);
        return false;
    }
    unsigned char buffer[STORAGE_READ_BUFFER];
    uint32_t state = util_crc32_begin();
    bool ok = true;
    for (;;) {
        size_t read = fread(buffer, 1, sizeof(buffer), file);
        if (read > 0) {
            state = util_crc32_extend(state, buffer, read);
        }
        if (read < sizeof(buffer)) {
            if (ferror(file)) {
                ok = false;
            }
            break;
        }
    }
    fclose(file);
    if (!ok) {
        set_error(error, error_size, "Could not checksum the data file.");
        return false;
    }
    *out = util_crc32_finish(state);
    return true;
}

static bool append_checksum(const wchar_t *path,
                            char *error, size_t error_size)
{
    uint32_t crc = 0;
    if (!compute_file_crc(path, &crc, error, error_size)) {
        return false;
    }
    FILE *file = _wfopen(path, L"ab");
    if (file == NULL) {
        set_error(error, error_size,
                  "Could not finalize the data file (error %d).", errno);
        return false;
    }
    if (fprintf(file, "CRC32|%08" PRIX32, crc) < 0) {
        set_error(error, error_size, "Could not write the data checksum.");
        fclose(file);
        return false;
    }
    return close_written_file(file, path, error, error_size);
}

bool storage_save(const wchar_t *path, const Database *db,
                  char *error, size_t error_size)
{
    if (path == NULL || db == NULL) {
        set_error(error, error_size, "Invalid data path or database.");
        return false;
    }
    if (!db_validate(db, error, error_size)) {
        return false;
    }
    if (!ensure_parent_directory(path, error, error_size)) {
        return false;
    }

    wchar_t *temp_path = build_sibling_path(path, L".tmp", error, error_size);
    wchar_t *backup_path = build_sibling_path(path, L".bak", error, error_size);
    wchar_t *backup_temp = build_sibling_path(path, L".bak.tmp",
                                               error, error_size);
    if (temp_path == NULL || backup_path == NULL || backup_temp == NULL) {
        free(temp_path);
        free(backup_path);
        free(backup_temp);
        return false;
    }

    char current_error[768] = "";
    uint32_t current_crc = 0;
    StorageStatus current_status = validate_data_file(
        path, &current_crc, current_error, sizeof(current_error));
    bool update_backup = false;
    if (current_status == STORAGE_OK) {
        update_backup = true;
    } else if (current_status != STORAGE_NOT_FOUND &&
               current_status != STORAGE_CORRUPT) {
        if (current_error[0] != '\0') {
            set_error(error, error_size,
                      "Refusing to replace the existing data file. %s",
                      current_error);
        } else {
            set_error(error, error_size,
                      "Refusing to replace the existing data file.");
        }
        free(temp_path);
        free(backup_path);
        free(backup_temp);
        return false;
    }

    DeleteFileW(temp_path);
    DeleteFileW(backup_temp);
    FILE *file = _wfopen(temp_path, L"wb");
    if (file == NULL) {
        set_error(error, error_size,
                  "Could not create the data file (error %d).", errno);
        free(temp_path);
        free(backup_path);
        free(backup_temp);
        return false;
    }

    bool wrote = write_records(file, db);
    if (!wrote) {
        set_error(error, error_size, "Could not write the data file.");
    }
    if (!close_written_file(file, temp_path, error, error_size) || !wrote) {
        DeleteFileW(temp_path);
        free(temp_path);
        free(backup_path);
        free(backup_temp);
        return false;
    }
    if (!append_checksum(temp_path, error, error_size)) {
        DeleteFileW(temp_path);
        free(temp_path);
        free(backup_path);
        free(backup_temp);
        return false;
    }
    char temp_error[768] = "";
    if (validate_data_file(temp_path, NULL, temp_error,
                           sizeof(temp_error)) != STORAGE_OK) {
        set_error(error, error_size,
                  "The newly written data file did not pass validation. %s",
                  temp_error);
        DeleteFileW(temp_path);
        free(temp_path);
        free(backup_path);
        free(backup_temp);
        return false;
    }

    if (update_backup) {
        if (!CopyFileW(path, backup_temp, FALSE)) {
            set_windows_error(error, error_size, L"Backing up the data file",
                              GetLastError());
            DeleteFileW(temp_path);
            free(temp_path);
            free(backup_path);
            free(backup_temp);
            return false;
        }
        char backup_error[768] = "";
        if (validate_data_file_checksum(backup_temp, current_crc,
                                        backup_error,
                                        sizeof(backup_error)) != STORAGE_OK) {
            set_error(error, error_size,
                      "The backup copy could not be verified. %s",
                      backup_error);
            DeleteFileW(temp_path);
            DeleteFileW(backup_temp);
            free(temp_path);
            free(backup_path);
            free(backup_temp);
            return false;
        }
        if (!MoveFileExW(backup_temp, backup_path,
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            set_windows_error(error, error_size, L"Updating the backup file",
                              GetLastError());
            DeleteFileW(temp_path);
            DeleteFileW(backup_temp);
            free(temp_path);
            free(backup_path);
            free(backup_temp);
            return false;
        }
    }

    if (!MoveFileExW(temp_path, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        set_windows_error(error, error_size, L"Saving the data file",
                          GetLastError());
        DeleteFileW(temp_path);
        free(temp_path);
        free(backup_path);
        free(backup_temp);
        return false;
    }

    free(temp_path);
    free(backup_path);
    free(backup_temp);
    return true;
}

bool storage_repair_from_backup(const wchar_t *path,
                                char *error, size_t error_size)
{
    if (path == NULL) {
        set_error(error, error_size, "Invalid data path.");
        return false;
    }
    wchar_t *backup_path = build_sibling_path(path, L".bak", error, error_size);
    if (backup_path == NULL) {
        return false;
    }
    char backup_error[768] = "";
    if (validate_data_file(backup_path, NULL, backup_error,
                           sizeof(backup_error)) != STORAGE_OK) {
        set_error(error, error_size, "The backup file is not valid. %s",
                  backup_error);
        free(backup_path);
        return false;
    }

    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        wchar_t *corrupt_path = build_sibling_path(path, L".corrupt",
                                                    error, error_size);
        if (corrupt_path == NULL) {
            free(backup_path);
            return false;
        }
        if (!CopyFileW(path, corrupt_path, FALSE)) {
            DWORD code = GetLastError();
            set_windows_error(error, error_size,
                              L"Preserving the damaged data file", code);
            free(corrupt_path);
            free(backup_path);
            return false;
        }
        free(corrupt_path);
    }
    if (!CopyFileW(backup_path, path, FALSE)) {
        DWORD code = GetLastError();
        set_windows_error(error, error_size, L"Restoring the backup file", code);
        free(backup_path);
        return false;
    }
    free(backup_path);
    return true;
}

static bool reserve_array(void **array, size_t element_size, size_t *capacity,
                          size_t needed, char *error, size_t error_size)
{
    if (needed <= *capacity) {
        return true;
    }
    size_t new_capacity = *capacity == 0 ? 8 : *capacity;
    while (new_capacity < needed) {
        if (new_capacity > SIZE_MAX / 2) {
            new_capacity = needed;
            break;
        }
        new_capacity *= 2;
    }
    if (element_size != 0 && new_capacity > SIZE_MAX / element_size) {
        set_error(error, error_size, "Data file is too large.");
        return false;
    }
    void *resized = realloc(*array, new_capacity * element_size);
    if (resized == NULL) {
        set_error(error, error_size, "Not enough memory to load the data file.");
        return false;
    }
    *array = resized;
    *capacity = new_capacity;
    return true;
}

static bool parse_u64(const char *text, uint64_t *out)
{
    if (text == NULL || *text == '\0' || *text == '-') {
        return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0 ||
        value > UINT64_MAX) {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static bool split_fields(char *line, char **fields, size_t field_capacity,
                         size_t *count, char *error, size_t error_size)
{
    size_t used = 0;
    fields[used++] = line;
    for (char *cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\') {
            if (cursor[1] == '\0') {
                set_error(error, error_size, "Data file ends with an escape.");
                return false;
            }
            ++cursor;
        } else if (*cursor == '|') {
            if (used >= field_capacity) {
                set_error(error, error_size, "Data record has too many fields.");
                return false;
            }
            *cursor = '\0';
            fields[used++] = cursor + 1;
        }
    }
    *count = used;
    return true;
}

static bool unescape_field(const char *source, char *out, size_t out_size,
                           char *error, size_t error_size)
{
    size_t written = 0;
    for (const char *cursor = source; *cursor != '\0'; ++cursor) {
        char value = *cursor;
        if (value == '\\') {
            ++cursor;
            switch (*cursor) {
            case '\\':
                value = '\\';
                break;
            case '|':
                value = '|';
                break;
            case 'n':
                value = '\n';
                break;
            case 'r':
                value = '\r';
                break;
            default:
                set_error(error, error_size,
                          "Data file contains an invalid escape sequence.");
                return false;
            }
        }
        if (written + 1 >= out_size) {
            set_error(error, error_size, "Data field is too long.");
            return false;
        }
        out[written++] = value;
    }
    out[written] = '\0';
    if (!util_utf8_valid(out)) {
        set_error(error, error_size, "Data file contains invalid UTF-8.");
        return false;
    }
    return true;
}

static bool parse_team_record(Database *db, char **fields, size_t count,
                              char *error, size_t error_size)
{
    if (count != 5) {
        set_error(error, error_size, "Invalid TEAM record.");
        return false;
    }
    uint64_t id = 0;
    if (!parse_u64(fields[1], &id)) {
        set_error(error, error_size, "Invalid team ID.");
        return false;
    }
    if (!reserve_array((void **)&db->teams, sizeof(Team), &db->team_capacity,
                       db->team_count + 1, error, error_size)) {
        return false;
    }
    Team *team = &db->teams[db->team_count];
    memset(team, 0, sizeof(*team));
    team->id = id;
    if (!unescape_field(fields[2], team->name, sizeof(team->name),
                        error, error_size) ||
        !unescape_field(fields[3], team->short_name, sizeof(team->short_name),
                        error, error_size) ||
        !unescape_field(fields[4], team->city, sizeof(team->city),
                        error, error_size)) {
        return false;
    }
    db->team_count++;
    return true;
}

static bool parse_player_record(Database *db, char **fields, size_t count,
                                int file_version,
                                char *error, size_t error_size)
{
    size_t expected_count = file_version <= 2 ? 8 : 7;
    if (count != expected_count) {
        set_error(error, error_size, "Invalid PLAYER record.");
        return false;
    }
    uint64_t id = 0;
    uint64_t team_id = 0;
    int position = 0;
    int active = 0;
    double base_rating = 0.0;
    bool values_ok = parse_u64(fields[1], &id) &&
                     parse_u64(fields[2], &team_id);
    if (values_ok && file_version <= 2) {
        /* Legacy position is validated but intentionally discarded. */
        values_ok = util_parse_int(fields[5], 0, 3, &position) &&
                    util_parse_finite_double(fields[6], &base_rating) &&
                    util_parse_int(fields[7], 0, 1, &active);
    } else if (values_ok) {
        values_ok = util_parse_finite_double(fields[5], &base_rating) &&
                    util_parse_int(fields[6], 0, 1, &active);
    }
    if (!values_ok) {
        set_error(error, error_size, "Invalid player record values.");
        return false;
    }
    if (!reserve_array((void **)&db->players, sizeof(Player),
                       &db->player_capacity, db->player_count + 1,
                       error, error_size)) {
        return false;
    }
    Player *player = &db->players[db->player_count];
    memset(player, 0, sizeof(*player));
    player->id = id;
    player->team_id = team_id;
    if (!unescape_field(fields[3], player->first_name,
                        sizeof(player->first_name), error, error_size) ||
        !unescape_field(fields[4], player->last_name,
                        sizeof(player->last_name), error, error_size)) {
        return false;
    }
    double rounded_base = round(base_rating * 100.0) / 100.0;
    if (!isfinite(rounded_base)) {
        set_error(error, error_size, "Invalid player record values.");
        return false;
    }
    player->base_rating = rounded_base;
    player->current_rating = player->base_rating;
    player->active = active != 0;
    db->player_count++;
    return true;
}

static bool parse_match_record(Database *db, char **fields, size_t count,
                               int file_version,
                               char *error, size_t error_size)
{
    bool neutral_venue = false;
    int length = 90;
    size_t expected_count = file_version == 1 ? 7
                            : file_version < 4 ? 8
                                               : 9;
    if (count != expected_count) {
        set_error(error, error_size, "Invalid MATCH record.");
        return false;
    }
    uint64_t id = 0;
    uint64_t home_id = 0;
    uint64_t away_id = 0;
    int home_goals = 0;
    int away_goals = 0;
    int neutral = 0;
    if (!parse_u64(fields[1], &id) || !util_date_valid(fields[2]) ||
        !parse_u64(fields[3], &home_id) || !parse_u64(fields[4], &away_id) ||
        !util_parse_int(fields[5], 0, 99, &home_goals) ||
        !util_parse_int(fields[6], 0, 99, &away_goals)) {
        set_error(error, error_size, "Invalid match record values.");
        return false;
    }
    if (file_version >= 2) {
        if (!util_parse_int(fields[7], 0, 1, &neutral)) {
            set_error(error, error_size, "Invalid neutral-venue value.");
            return false;
        }
        neutral_venue = neutral != 0;
    }
    if (file_version >= 4 &&
        !util_parse_int(fields[8], 1, 120, &length)) {
        set_error(error, error_size, "Invalid final-whistle value.");
        return false;
    }
    if (!reserve_array((void **)&db->matches, sizeof(Match),
                       &db->match_capacity, db->match_count + 1,
                       error, error_size)) {
        return false;
    }
    Match *match = &db->matches[db->match_count];
    memset(match, 0, sizeof(*match));
    match->id = id;
    util_str_copy(match->date, sizeof(match->date), fields[2]);
    match->home_team_id = home_id;
    match->away_team_id = away_id;
    match->home_goals = home_goals;
    match->away_goals = away_goals;
    match->neutral_venue = neutral_venue;
    match->length = length;
    db->match_count++;
    return true;
}

static bool parse_appearance_record(Database *db, char **fields, size_t count,
                                    int file_version,
                                    char *error, size_t error_size)
{
    size_t expected_count = file_version >= 4 ? 7 : 6;
    if (count != expected_count) {
        set_error(error, error_size, "Invalid APP record.");
        return false;
    }
    uint64_t match_id = 0;
    uint64_t player_id = 0;
    uint64_t team_id = 0;
    int minutes = 0;
    int sent_off = 0;
    double performance = 0.0;
    if (!parse_u64(fields[1], &match_id) ||
        !parse_u64(fields[2], &player_id) ||
        !parse_u64(fields[3], &team_id) ||
        !util_parse_int(fields[4], 1, 120, &minutes) ||
        !util_parse_double(fields[5], 1.0, 10.0, &performance) ||
        (file_version >= 4 &&
         !util_parse_int(fields[6], 0, 120, &sent_off))) {
        set_error(error, error_size, "Invalid appearance record values.");
        return false;
    }
    if (!reserve_array((void **)&db->appearances, sizeof(Appearance),
                       &db->appearance_capacity, db->appearance_count + 1,
                       error, error_size)) {
        return false;
    }
    Appearance *appearance = &db->appearances[db->appearance_count];
    memset(appearance, 0, sizeof(*appearance));
    appearance->match_id = match_id;
    appearance->player_id = player_id;
    appearance->team_id = team_id;
    appearance->minutes = minutes;
    appearance->performance = round(performance * 100.0) / 100.0;
    appearance->sent_off = sent_off;
    db->appearance_count++;
    return true;
}

static bool parse_next_record(Database *db, char **fields, size_t count,
                              bool *seen, char *error, size_t error_size)
{
    if (count != 4) {
        set_error(error, error_size, "Invalid NEXT record.");
        return false;
    }
    if (*seen) {
        set_error(error, error_size, "Duplicate NEXT record.");
        return false;
    }
    uint64_t next_team = 0;
    uint64_t next_player = 0;
    uint64_t next_match = 0;
    if (!parse_u64(fields[1], &next_team) ||
        !parse_u64(fields[2], &next_player) ||
        !parse_u64(fields[3], &next_match)) {
        set_error(error, error_size, "Invalid ID counter record values.");
        return false;
    }
    db->next_team_id = next_team;
    db->next_player_id = next_player;
    db->next_match_id = next_match;
    *seen = true;
    return true;
}

static bool parse_records(char *data, size_t offset, size_t content_end,
                          Database *db, bool *incompatible_version,
                          bool *out_next_ids_stored,
                          char *error, size_t error_size)
{
    if (incompatible_version != NULL) {
        *incompatible_version = false;
    }
    if (out_next_ids_stored != NULL) {
        *out_next_ids_stored = false;
    }
    bool next_ids_stored = false;
    char *cursor = data + offset;
    char *end = data + content_end;
    size_t line_number = 0;

    char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
    if (newline == NULL) {
        set_error(error, error_size, "Data file has no complete header.");
        return false;
    }
    size_t line_length = (size_t)(newline - cursor);
    if (line_length > 0 && cursor[line_length - 1] == '\r') {
        line_length--;
    }
    cursor[line_length] = '\0';
    line_number++;
    if (strncmp(cursor, STORAGE_MAGIC, STORAGE_MAGIC_LENGTH) != 0) {
        set_error(error, error_size, "This is not an ElevenLedger data file.");
        return false;
    }
    int parsed_file_version = 0;
    if (!util_parse_int(cursor + STORAGE_MAGIC_LENGTH, 0, 9999,
                        &parsed_file_version)) {
        set_error(error, error_size, "Data file has an invalid format version.");
        return false;
    }
    if (parsed_file_version < STORAGE_MIN_FORMAT_VERSION ||
        parsed_file_version > STORAGE_FORMAT_VERSION) {
        if (incompatible_version != NULL) {
            *incompatible_version = true;
        }
        if (parsed_file_version > STORAGE_FORMAT_VERSION) {
            set_error(error, error_size,
                      "This data file was created by a newer ElevenLedger version.");
        } else {
            set_error(error, error_size,
                      "This data file uses an unsupported older ElevenLedger format.");
        }
        return false;
    }
    cursor = newline + 1;

    bool ended = false;
    while (cursor < end) {
        line_number++;
        newline = memchr(cursor, '\n', (size_t)(end - cursor));
        if (newline == NULL) {
            set_error(error, error_size,
                      "Data file is incomplete near line %zu.", line_number);
            return false;
        }
        line_length = (size_t)(newline - cursor);
        if (line_length > 0 && cursor[line_length - 1] == '\r') {
            line_length--;
        }
        cursor[line_length] = '\0';
        if (ended) {
            set_error(error, error_size,
                      "Unexpected record after END on line %zu.", line_number);
            return false;
        }
        if (line_length == 0) {
            set_error(error, error_size,
                      "Empty record on line %zu.", line_number);
            return false;
        }
        if (strcmp(cursor, "END") == 0) {
            ended = true;
            cursor = newline + 1;
            continue;
        }

        char *fields[STORAGE_MAX_FIELDS];
        size_t field_count = 0;
        if (!split_fields(cursor, fields, STORAGE_MAX_FIELDS, &field_count,
                          error, error_size)) {
            return false;
        }
        bool parsed;
        if (strcmp(fields[0], "TEAM") == 0) {
            parsed = parse_team_record(db, fields, field_count,
                                       error, error_size);
        } else if (strcmp(fields[0], "NEXT") == 0) {
            if (parsed_file_version < 5) {
                set_error(error, error_size,
                          "NEXT record requires data format version 5.");
                parsed = false;
            } else {
                parsed = parse_next_record(db, fields, field_count,
                                           &next_ids_stored,
                                           error, error_size);
            }
        } else if (strcmp(fields[0], "PLAYER") == 0) {
            parsed = parse_player_record(db, fields, field_count,
                                         parsed_file_version,
                                         error, error_size);
        } else if (strcmp(fields[0], "MATCH") == 0) {
            parsed = parse_match_record(db, fields, field_count,
                                        parsed_file_version,
                                        error, error_size);
        } else if (strcmp(fields[0], "APP") == 0) {
            parsed = parse_appearance_record(db, fields, field_count,
                                             parsed_file_version,
                                             error, error_size);
        } else {
            set_error(error, error_size,
                      "Unknown record type on line %zu.", line_number);
            parsed = false;
        }
        if (!parsed) {
            return false;
        }
        cursor = newline + 1;
    }
    if (!ended) {
        set_error(error, error_size, "Data file is missing its END marker.");
        return false;
    }
    if (parsed_file_version >= 5 && !next_ids_stored) {
        set_error(error, error_size,
                  "Data file is missing its ID counter record.");
        return false;
    }
    if (out_next_ids_stored != NULL) {
        *out_next_ids_stored = next_ids_stored;
    }
    return true;
}

static bool finish_loaded_database(Database *db, bool next_ids_stored,
                                   bool recompute,
                                   char *error, size_t error_size)
{
    uint64_t max_team = 0;
    uint64_t max_player = 0;
    uint64_t max_match = 0;
    for (size_t i = 0; i < db->team_count; ++i) {
        if (db->teams[i].id > max_team) {
            max_team = db->teams[i].id;
        }
    }
    for (size_t i = 0; i < db->player_count; ++i) {
        if (db->players[i].id > max_player) {
            max_player = db->players[i].id;
        }
    }
    for (size_t i = 0; i < db->match_count; ++i) {
        if (db->matches[i].id > max_match) {
            max_match = db->matches[i].id;
        }
    }
    if (next_ids_stored) {
        /*
         * Stored counters may be far above the highest live ID because
         * deleted records keep their ID reserved. They may never collide
         * with a live record.
         */
        if (db->next_team_id <= max_team ||
            db->next_player_id <= max_player ||
            db->next_match_id <= max_match) {
            set_error(error, error_size,
                      "Data file contains invalid ID counters.");
            return false;
        }
    } else {
        /* Versions 1-4 did not store counters, so derive them. */
        if (max_team == UINT64_MAX || max_player == UINT64_MAX ||
            max_match == UINT64_MAX) {
            set_error(error, error_size,
                      "Data file has exhausted its ID range.");
            return false;
        }
        db->next_team_id = max_team + 1;
        db->next_player_id = max_player + 1;
        db->next_match_id = max_match + 1;
    }

    if (recompute) {
        return db_recompute_ratings(db, error, error_size);
    }
    return db_validate(db, error, error_size);
}

static StorageStatus read_checked_file(const wchar_t *path, char **out_buffer,
                                       size_t *out_length,
                                       size_t *out_footer_start,
                                       uint32_t *out_content_crc,
                                       char *error, size_t error_size)
{
    if (out_buffer == NULL) {
        set_error(error, error_size, "Invalid data file buffer.");
        return STORAGE_IO_ERROR;
    }
    *out_buffer = NULL;
    if (out_length != NULL) {
        *out_length = 0;
    }
    if (out_footer_start != NULL) {
        *out_footer_start = 0;
    }
    if (out_content_crc != NULL) {
        *out_content_crc = 0;
    }

    DWORD attributes = GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
            return STORAGE_NOT_FOUND;
        }
        set_windows_error(error, error_size, L"Opening the data file", code);
        return STORAGE_IO_ERROR;
    }

    FILE *file = _wfopen(path, L"rb");
    if (file == NULL) {
        set_error(error, error_size,
                  "Could not open the data file (error %d).", errno);
        return STORAGE_IO_ERROR;
    }

    size_t capacity = 16384;
    size_t length = 0;
    char *buffer = malloc(capacity + 1);
    if (buffer == NULL) {
        fclose(file);
        set_error(error, error_size, "Not enough memory to load the data file.");
        return STORAGE_IO_ERROR;
    }
    bool read_ok = true;
    const size_t read_limit = STORAGE_MAX_FILE_BYTES + 1u;
    for (;;) {
        if (length == capacity) {
            if (capacity >= read_limit) {
                read_ok = false;
                set_error(error, error_size, "Data file is unexpectedly large.");
                break;
            }
            size_t new_capacity = capacity * 2;
            if (new_capacity > read_limit) {
                new_capacity = read_limit;
            }
            char *resized = realloc(buffer, new_capacity + 1);
            if (resized == NULL) {
                read_ok = false;
                set_error(error, error_size,
                          "Not enough memory to load the data file.");
                break;
            }
            buffer = resized;
            capacity = new_capacity;
        }
        size_t read = fread(buffer + length, 1, capacity - length, file);
        length += read;
        if (read == 0) {
            if (ferror(file)) {
                read_ok = false;
                set_error(error, error_size, "Could not read the data file.");
            }
            break;
        }
    }
    fclose(file);
    if (!read_ok) {
        free(buffer);
        return STORAGE_IO_ERROR;
    }
    if (length > STORAGE_MAX_FILE_BYTES) {
        set_error(error, error_size, "Data file is unexpectedly large.");
        free(buffer);
        return STORAGE_CORRUPT;
    }
    if (memchr(buffer, '\0', length) != NULL) {
        set_error(error, error_size, "Data file contains an embedded NUL byte.");
        free(buffer);
        return STORAGE_CORRUPT;
    }

    buffer[length] = '\0';
    size_t parsed_length = length;
    while (parsed_length > 0 &&
           (buffer[parsed_length - 1] == '\n' ||
            buffer[parsed_length - 1] == '\r')) {
        parsed_length--;
    }
    if (parsed_length == 0) {
        set_error(error, error_size, "Data file is empty.");
        free(buffer);
        return STORAGE_CORRUPT;
    }
    size_t footer_start = parsed_length;
    while (footer_start > 0 && buffer[footer_start - 1] != '\n') {
        footer_start--;
    }
    if (footer_start == 0) {
        set_error(error, error_size, "Data file is missing its checksum.");
        free(buffer);
        return STORAGE_CORRUPT;
    }
    size_t footer_length = parsed_length - footer_start;
    if (footer_length != 14 || memcmp(buffer + footer_start, "CRC32|", 6) != 0) {
        set_error(error, error_size, "Data file checksum footer is invalid.");
        free(buffer);
        return STORAGE_CORRUPT;
    }
    char checksum_text[9];
    memcpy(checksum_text, buffer + footer_start + 6, 8);
    checksum_text[8] = '\0';
    char *checksum_end = NULL;
    errno = 0;
    unsigned long expected = strtoul(checksum_text, &checksum_end, 16);
    if (errno != 0 || checksum_end == checksum_text || *checksum_end != '\0') {
        set_error(error, error_size, "Data file checksum footer is invalid.");
        free(buffer);
        return STORAGE_CORRUPT;
    }
    uint32_t expected_crc = (uint32_t)expected;
    uint32_t actual_crc = util_crc32(buffer, footer_start);
    if (expected_crc != actual_crc) {
        set_error(error, error_size,
                  "Data file checksum mismatch. The file may be damaged.");
        free(buffer);
        return STORAGE_CORRUPT;
    }

    *out_buffer = buffer;
    if (out_length != NULL) {
        *out_length = length;
    }
    if (out_footer_start != NULL) {
        *out_footer_start = footer_start;
    }
    if (out_content_crc != NULL) {
        *out_content_crc = actual_crc;
    }
    return STORAGE_OK;
}

static StorageStatus load_file(const wchar_t *path, Database *db,
                               bool recompute, uint32_t *out_content_crc,
                               char *error, size_t error_size)
{
    char *buffer = NULL;
    size_t length = 0;
    size_t footer_start = 0;
    uint32_t content_crc = 0;
    StorageStatus status = read_checked_file(path, &buffer, &length,
                                             &footer_start, &content_crc,
                                             error, error_size);
    if (status != STORAGE_OK) {
        return status;
    }
    if (out_content_crc != NULL) {
        *out_content_crc = content_crc;
    }

    size_t offset = 0;
    if (length >= 3 &&
        (unsigned char)buffer[0] == 0xEF &&
        (unsigned char)buffer[1] == 0xBB &&
        (unsigned char)buffer[2] == 0xBF) {
        offset = 3;
    }
    if (offset >= footer_start) {
        set_error(error, error_size, "Data file has no records.");
        free(buffer);
        return STORAGE_CORRUPT;
    }

    db_init(db);
    bool incompatible_version = false;
    bool next_ids_stored = false;
    bool parsed = parse_records(buffer, offset, footer_start, db,
                                &incompatible_version, &next_ids_stored,
                                error, error_size);
    if (parsed) {
        parsed = finish_loaded_database(db, next_ids_stored, recompute,
                                        error, error_size);
    }
    free(buffer);
    if (!parsed) {
        db_free(db);
        return incompatible_version ? STORAGE_INCOMPATIBLE_VERSION
                                    : STORAGE_CORRUPT;
    }
    return STORAGE_OK;
}

static StorageStatus validate_data_file(const wchar_t *path,
                                        uint32_t *out_content_crc,
                                        char *error, size_t error_size)
{
    Database db;
    db_init(&db);
    StorageStatus status = load_file(path, &db, false, out_content_crc,
                                     error, error_size);
    db_free(&db);
    return status;
}

static StorageStatus validate_data_file_checksum(const wchar_t *path,
                                                 uint32_t expected_crc,
                                                 char *error,
                                                 size_t error_size)
{
    char *buffer = NULL;
    uint32_t content_crc = 0;
    StorageStatus status = read_checked_file(path, &buffer, NULL, NULL,
                                             &content_crc, error, error_size);
    free(buffer);
    if (status != STORAGE_OK) {
        return status;
    }
    if (content_crc != expected_crc) {
        set_error(error, error_size,
                  "The backup copy did not match the validated primary.");
        return STORAGE_CORRUPT;
    }
    return STORAGE_OK;
}

StorageStatus storage_load(const wchar_t *path, Database *db,
                           char *error, size_t error_size)
{
    if (path == NULL || db == NULL) {
        set_error(error, error_size, "Invalid data path or database.");
        return STORAGE_IO_ERROR;
    }
    db_init(db);

    char main_error[768] = "";
    StorageStatus main_status = load_file(path, db, true, NULL, main_error,
                                          sizeof(main_error));
    if (main_status == STORAGE_OK) {
        return STORAGE_OK;
    }
    if (main_status == STORAGE_IO_ERROR ||
        main_status == STORAGE_INCOMPATIBLE_VERSION) {
        set_error(error, error_size, "%s", main_error);
        return main_status;
    }

    wchar_t *backup_path = build_sibling_path(path, L".bak", error, error_size);
    if (backup_path == NULL) {
        return STORAGE_IO_ERROR;
    }
    char backup_error[768] = "";
    StorageStatus backup_status = load_file(backup_path, db, true, NULL,
                                            backup_error,
                                            sizeof(backup_error));
    free(backup_path);

    if (backup_status == STORAGE_OK) {
        if (main_status == STORAGE_NOT_FOUND) {
            set_error(error, error_size,
                      "The primary data file was missing; the last backup was loaded.");
        } else {
            set_error(error, error_size,
                      "The primary data file could not be read; the last backup was loaded. %s",
                      main_error);
        }
        return STORAGE_RECOVERED;
    }

    if (main_status == STORAGE_NOT_FOUND && backup_status == STORAGE_NOT_FOUND) {
        db_init(db);
        return STORAGE_NOT_FOUND;
    }

    if (main_status == STORAGE_NOT_FOUND) {
        set_error(error, error_size, "%s", backup_error);
        return backup_status;
    }
    if (backup_status == STORAGE_NOT_FOUND) {
        set_error(error, error_size, "%s", main_error);
        return main_status;
    }
    set_error(error, error_size, "%s Backup: %s", main_error, backup_error);
    db_init(db);
    if (backup_status == STORAGE_INCOMPATIBLE_VERSION) {
        return STORAGE_INCOMPATIBLE_VERSION;
    }
    return (main_status == STORAGE_IO_ERROR ||
            backup_status == STORAGE_IO_ERROR)
               ? STORAGE_IO_ERROR
               : STORAGE_CORRUPT;
}
