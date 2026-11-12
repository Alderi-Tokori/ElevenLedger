#include "test_framework.h"

#include "core/storage.h"
#include "core/util.h"

#include <io.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static unsigned g_temp_counter = 0;

typedef struct {
    wchar_t directory[MAX_PATH];
    wchar_t data_path[MAX_PATH];
    wchar_t backup_path[MAX_PATH];
    wchar_t temp_path[MAX_PATH];
} TestPaths;

static bool make_test_paths(TestPaths *paths)
{
    wchar_t base[MAX_PATH];
    DWORD length = GetTempPathW((DWORD)(sizeof(base) / sizeof(base[0])), base);
    if (length == 0) {
        return false;
    }
    if (swprintf(paths->directory,
                 sizeof(paths->directory) / sizeof(paths->directory[0]),
                 L"%lsElevenLedgerTests-%lu-%u", base,
                 (unsigned long)GetCurrentProcessId(), ++g_temp_counter) < 0) {
        return false;
    }
    if (!CreateDirectoryW(paths->directory, NULL) &&
        GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    if (swprintf(paths->data_path,
                 sizeof(paths->data_path) / sizeof(paths->data_path[0]),
                 L"%ls\\data.frc", paths->directory) < 0 ||
        swprintf(paths->backup_path,
                 sizeof(paths->backup_path) / sizeof(paths->backup_path[0]),
                 L"%ls.bak", paths->data_path) < 0 ||
        swprintf(paths->temp_path,
                 sizeof(paths->temp_path) / sizeof(paths->temp_path[0]),
                 L"%ls.tmp", paths->data_path) < 0) {
        return false;
    }
    return true;
}

static void cleanup_test_paths(TestPaths *paths)
{
    DeleteFileW(paths->data_path);
    DeleteFileW(paths->backup_path);
    DeleteFileW(paths->temp_path);
    wchar_t other[MAX_PATH];
    if (swprintf(other, sizeof(other) / sizeof(other[0]), L"%ls.bak.tmp",
                 paths->data_path) >= 0) {
        DeleteFileW(other);
    }
    if (swprintf(other, sizeof(other) / sizeof(other[0]), L"%ls.corrupt",
                 paths->data_path) >= 0) {
        DeleteFileW(other);
    }
    RemoveDirectoryW(paths->directory);
}

static bool add_two_teams(Database *db, uint64_t *first, uint64_t *second)
{
    char error[256];
    db_init(db);
    CHECK(db_add_team(db, "Northbridge FC", "NBR", "Northbridge",
                      first, error, sizeof(error)));
    CHECK(db_add_team(db, "Riverside Athletic", "RIV", "Riverside",
                      second, error, sizeof(error)));
    return true;
}

static bool populate_sample(Database *db)
{
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(add_two_teams(db, &alpha, &beta));
    char error[256];
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t c = 0;
    uint64_t d = 0;
    CHECK(db_add_player(db, alpha, "Ada", "Stone",
                        80.0, true, &a, error, sizeof(error)));
    CHECK(db_add_player(db, alpha, "Noah", "Bennett",
                        74.0, true, &b, error, sizeof(error)));
    CHECK(db_add_player(db, beta, "Mia", "Clarke",
                        76.0, true, &c, error, sizeof(error)));
    CHECK(db_add_player(db, beta, "Leo", "Foster",
                        68.0, true, &d, error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { a, 90, 7.5, 0 },
        { b, 30, 6.0, 30 },
        { c, 90, 6.0, 0 },
        { d, 74, 5.0, 0 },
    };
    MatchInput match = {
        .date = "2026-09-12",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 2,
        .away_goals = 1,
        .length = 99,
        .players = lineup,
        .player_count = 4,
    };
    CHECK(db_add_match(db, &match, NULL, error, sizeof(error)));
    return true;
}

static bool same_database(const Database *left, const Database *right)
{
    if (left->team_count != right->team_count ||
        left->player_count != right->player_count ||
        left->match_count != right->match_count ||
        left->appearance_count != right->appearance_count) {
        return false;
    }
    for (size_t i = 0; i < left->team_count; ++i) {
        if (left->teams[i].id != right->teams[i].id ||
            strcmp(left->teams[i].name, right->teams[i].name) != 0 ||
            strcmp(left->teams[i].short_name, right->teams[i].short_name) != 0 ||
            strcmp(left->teams[i].city, right->teams[i].city) != 0) {
            return false;
        }
    }
    for (size_t i = 0; i < left->player_count; ++i) {
        if (left->players[i].id != right->players[i].id ||
            strcmp(left->players[i].first_name,
                   right->players[i].first_name) != 0 ||
            strcmp(left->players[i].last_name,
                   right->players[i].last_name) != 0 ||
            left->players[i].team_id != right->players[i].team_id ||
            left->players[i].active != right->players[i].active ||
            fabs(left->players[i].base_rating -
                 right->players[i].base_rating) > 1e-9 ||
            fabs(left->players[i].current_rating -
                 right->players[i].current_rating) > 1e-9 ||
            left->players[i].matches_played !=
                right->players[i].matches_played ||
            left->players[i].minutes_played !=
                right->players[i].minutes_played) {
            return false;
        }
    }
    for (size_t i = 0; i < left->match_count; ++i) {
        if (left->matches[i].id != right->matches[i].id ||
            strcmp(left->matches[i].date, right->matches[i].date) != 0 ||
            left->matches[i].home_team_id !=
                right->matches[i].home_team_id ||
            left->matches[i].away_team_id !=
                right->matches[i].away_team_id ||
            left->matches[i].home_goals != right->matches[i].home_goals ||
            left->matches[i].away_goals != right->matches[i].away_goals ||
            left->matches[i].neutral_venue !=
                right->matches[i].neutral_venue ||
            left->matches[i].length != right->matches[i].length) {
            return false;
        }
    }
    for (size_t i = 0; i < left->appearance_count; ++i) {
        if (left->appearances[i].match_id !=
                right->appearances[i].match_id ||
            left->appearances[i].player_id !=
                right->appearances[i].player_id ||
            left->appearances[i].team_id !=
                right->appearances[i].team_id ||
            left->appearances[i].minutes !=
                right->appearances[i].minutes ||
            left->appearances[i].sent_off !=
                right->appearances[i].sent_off ||
            fabs(left->appearances[i].performance -
                 right->appearances[i].performance) > 1e-9 ||
            fabs(left->appearances[i].rating_before -
                 right->appearances[i].rating_before) > 1e-9 ||
            fabs(left->appearances[i].rating_delta -
                 right->appearances[i].rating_delta) > 1e-9 ||
            fabs(left->appearances[i].rating_after -
                 right->appearances[i].rating_after) > 1e-9) {
            return false;
        }
    }
    return true;
}

static bool test_roundtrip_and_recompute(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database original;
    CHECK(populate_sample(&original));
    char error[768];
    CHECK(storage_save(paths.data_path, &original, error, sizeof(error)));
    CHECK(GetFileAttributesW(paths.backup_path) == INVALID_FILE_ATTRIBUTES);

    Database loaded;
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_OK);
    CHECK(same_database(&original, &loaded));
    CHECK(loaded.team_count == 2);
    CHECK(loaded.player_count == 4);
    CHECK(loaded.match_count == 1);
    CHECK(loaded.matches[0].length == 99);
    bool found_sent_off = false;
    for (size_t i = 0; i < loaded.appearance_count; ++i) {
        if (loaded.appearances[i].minutes == 30 &&
            loaded.appearances[i].sent_off == 30) {
            found_sent_off = true;
        }
    }
    CHECK(found_sent_off);
    CHECK(loaded.players[0].matches_played == 1);
    CHECK(loaded.players[0].minutes_played == 90);
    CHECK_NEAR(loaded.players[0].current_rating,
               original.players[0].current_rating, 1e-9);
    CHECK(loaded.next_team_id == 3);
    CHECK(loaded.next_player_id == 5);
    CHECK(loaded.next_match_id == 2);

    db_free(&original);
    db_free(&loaded);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_neutral_venue_roundtrip(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(add_two_teams(&db, &alpha, &beta));
    char error[768];
    uint64_t home_player = 0;
    uint64_t away_player = 0;
    CHECK(db_add_player(&db, alpha, "Home", "Player",
                        70.0, true, &home_player, error, sizeof(error)));
    CHECK(db_add_player(&db, beta, "Away", "Player",
                        70.0, true, &away_player, error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { home_player, 90, 6.0, 0 },
        { away_player, 90, 6.0, 0 },
    };
    MatchInput input = {
        .date = "2026-06-10",
        .home_team_id = alpha,
        .away_team_id = beta,
        .home_goals = 0,
        .away_goals = 0,
        .neutral_venue = true,
        .length = 90,
        .players = lineup,
        .player_count = 2,
    };
    CHECK(db_add_match(&db, &input, NULL, error, sizeof(error)));
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    db_free(&db);

    FILE *file = _wfopen(paths.data_path, L"rb");
    CHECK(file != NULL);
    char header[16] = { 0 };
    CHECK(fread(header, 1, 11, file) == 11);
    CHECK(fclose(file) == 0);
    CHECK(memcmp(header, "PITCHRATE|5", 11) == 0);

    Database loaded;
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_OK);
    CHECK(loaded.match_count == 1);
    CHECK(loaded.matches[0].neutral_venue);
    CHECK_NEAR(loaded.matches[0].rating.home_expected, 0.5, 1e-12);
    CHECK_NEAR(loaded.matches[0].rating.home_team_delta, 0.0, 1e-12);
    CHECK_NEAR(loaded.players[0].current_rating, 70.0, 1e-12);
    db_free(&loaded);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_unicode_and_escaping(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database db;
    db_init(&db);
    char error[768];
    uint64_t team_id = 0;
    uint64_t player_id = 0;
    CHECK(db_add_team(&db, "Étoile | Nord\\Club", "ÉTO", "Saint-Étienne",
                      &team_id, error, sizeof(error)));
    CHECK(db_add_team(&db, "李 娜 FC", "", "上海", NULL,
                      error, sizeof(error)));
    CHECK(db_add_player(&db, team_id, "Müller", "O'Connor",
                        77.5, true, &player_id,
                        error, sizeof(error)));
    CHECK(db_add_player(&db, team_id, "李", "娜",
                        81.0, true, NULL, error, sizeof(error)));
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    db_free(&db);

    Database loaded;
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_OK);
    CHECK(loaded.team_count == 2);
    CHECK_STRING(loaded.teams[0].name, "Étoile | Nord\\Club");
    CHECK_STRING(loaded.teams[0].city, "Saint-Étienne");
    CHECK_STRING(loaded.teams[1].name, "李 娜 FC");
    CHECK_STRING(loaded.teams[1].city, "上海");
    CHECK(loaded.player_count == 2);
    CHECK_STRING(loaded.players[0].first_name, "Müller");
    CHECK_STRING(loaded.players[0].last_name, "O'Connor");
    CHECK_STRING(loaded.players[1].first_name, "李");
    CHECK_STRING(loaded.players[1].last_name, "娜");
    db_free(&loaded);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_backup_created_and_recovery(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(add_two_teams(&db, &alpha, &beta));
    char error[768];
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    uint64_t gamma = 0;
    CHECK(db_add_team(&db, "Gamma Town", "GAM", "East", &gamma,
                      error, sizeof(error)));
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    CHECK(GetFileAttributesW(paths.backup_path) != INVALID_FILE_ATTRIBUTES);
    db_free(&db);

    Database backup_snapshot;
    StorageStatus backup_status = storage_load(paths.backup_path,
                                               &backup_snapshot,
                                               error, sizeof(error));
    CHECK(backup_status == STORAGE_OK);
    CHECK(backup_snapshot.team_count == 2);
    db_free(&backup_snapshot);

    FILE *file = _wfopen(paths.data_path, L"r+b");
    CHECK(file != NULL);
    CHECK(fseek(file, 38, SEEK_SET) == 0);
    CHECK(fputc('!', file) != EOF);
    CHECK(fclose(file) == 0);

    Database recovered;
    StorageStatus status = storage_load(paths.data_path, &recovered,
                                        error, sizeof(error));
    CHECK(status == STORAGE_RECOVERED);
    CHECK(recovered.team_count == 2);
    CHECK(strstr(error, "backup") != NULL || strstr(error, "Backup") != NULL);

    wchar_t corrupt_path[MAX_PATH];
    CHECK(swprintf(corrupt_path,
                   sizeof(corrupt_path) / sizeof(corrupt_path[0]),
                   L"%ls.corrupt", paths.data_path) >= 0);
    CHECK(CreateDirectoryW(corrupt_path, NULL) ||
          GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK(!storage_repair_from_backup(paths.data_path, error, sizeof(error)));
    CHECK(RemoveDirectoryW(corrupt_path));
    CHECK(storage_repair_from_backup(paths.data_path, error, sizeof(error)));
    CHECK(GetFileAttributesW(corrupt_path) != INVALID_FILE_ATTRIBUTES);
    Database repaired;
    status = storage_load(paths.data_path, &repaired, error, sizeof(error));
    CHECK(status == STORAGE_OK);
    CHECK(repaired.team_count == 2);
    CHECK(db_validate(&repaired, error, sizeof(error)));
    db_free(&repaired);
    db_free(&recovered);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_checksum_detects_damage_without_backup(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database db;
    CHECK(populate_sample(&db));
    char error[768];
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    db_free(&db);
    DeleteFileW(paths.backup_path);

    FILE *file = _wfopen(paths.data_path, L"r+b");
    CHECK(file != NULL);
    CHECK(fseek(file, 25, SEEK_SET) == 0);
    CHECK(fputc('#', file) != EOF);
    CHECK(fclose(file) == 0);

    Database loaded;
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_CORRUPT);
    CHECK(strstr(error, "checksum") != NULL ||
          strstr(error, "Checksum") != NULL);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_truncated_file_rejected(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database db;
    CHECK(populate_sample(&db));
    char error[768];
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    db_free(&db);

    FILE *file = _wfopen(paths.data_path, L"r+b");
    CHECK(file != NULL);
    CHECK(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    CHECK(length > 30);
    CHECK(_chsize_s(_fileno(file), (long long)(length - 15)) == 0);
    CHECK(fclose(file) == 0);

    Database loaded;
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_CORRUPT);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_missing_file(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database loaded;
    char error[768];
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_NOT_FOUND);
    CHECK(loaded.team_count == 0);
    db_free(&loaded);
    cleanup_test_paths(&paths);
    return true;
}

static bool write_raw_data_bytes(const wchar_t *path,
                                 const unsigned char *content,
                                 size_t length)
{
    FILE *file = _wfopen(path, L"wb");
    if (file == NULL) {
        return false;
    }
    bool ok = fwrite(content, 1, length, file) == length;
    ok = fclose(file) == 0 && ok;
    if (!ok) {
        return false;
    }
    uint32_t crc = util_crc32(content, length);
    file = _wfopen(path, L"ab");
    if (file == NULL) {
        return false;
    }
    ok = fprintf(file, "CRC32|%08X", crc) >= 0;
    ok = fclose(file) == 0 && ok;
    return ok;
}

static bool write_raw_data_file(const wchar_t *path, const char *content)
{
    return write_raw_data_bytes(path, (const unsigned char *)content,
                                strlen(content));
}

static bool test_newer_format_rejected(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    CHECK(write_raw_data_file(paths.backup_path,
                              "PITCHRATE|1\nTEAM|1|Alpha||\nEND\n"));
    CHECK(write_raw_data_file(paths.data_path, "PITCHRATE|99\nEND\n"));
    Database loaded;
    char error[768];
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_INCOMPATIBLE_VERSION);
    CHECK(strstr(error, "newer") != NULL);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_older_format_rejected(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    CHECK(write_raw_data_file(paths.backup_path,
                              "PITCHRATE|1\nTEAM|1|Alpha||\nEND\n"));
    CHECK(write_raw_data_file(paths.data_path, "PITCHRATE|0\nEND\n"));
    Database loaded;
    char error[768];
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_INCOMPATIBLE_VERSION);
    CHECK(strstr(error, "older") != NULL);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_save_refuses_newer_primary(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database db;
    CHECK(populate_sample(&db));
    char error[768];
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    const char *newer = "PITCHRATE|99\nEND\n";
    CHECK(write_raw_data_file(paths.data_path, newer));

    uint64_t next_team = 0;
    CHECK(db_add_team(&db, "Gamma Town", "GAM", "East", &next_team,
                      error, sizeof(error)));
    CHECK(!storage_save(paths.data_path, &db, error, sizeof(error)));
    db_free(&db);

    FILE *file = _wfopen(paths.data_path, L"rb");
    CHECK(file != NULL);
    char header[16] = { 0 };
    CHECK(fread(header, 1, 13, file) == 13);
    CHECK(fclose(file) == 0);
    CHECK(memcmp(header, "PITCHRATE|99", 12) == 0);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_locked_primary_is_not_replaced_from_backup(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(add_two_teams(&db, &alpha, &beta));
    char error[768];
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    uint64_t gamma = 0;
    CHECK(db_add_team(&db, "Gamma Town", "GAM", "East", &gamma,
                      error, sizeof(error)));
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    db_free(&db);

    HANDLE lock = CreateFileW(paths.data_path, GENERIC_READ, 0, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(lock != INVALID_HANDLE_VALUE);
    Database loaded;
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_IO_ERROR);
    CloseHandle(lock);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_save_preserves_good_backup_when_primary_is_corrupt(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(add_two_teams(&db, &alpha, &beta));
    char error[768];
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    uint64_t gamma = 0;
    CHECK(db_add_team(&db, "Gamma Town", "GAM", "East", &gamma,
                      error, sizeof(error)));
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));

    FILE *file = _wfopen(paths.data_path, L"r+b");
    CHECK(file != NULL);
    CHECK(fseek(file, 30, SEEK_SET) == 0);
    CHECK(fputc('!', file) != EOF);
    CHECK(fclose(file) == 0);

    uint64_t delta = 0;
    CHECK(db_add_team(&db, "Delta City", "DEL", "West", &delta,
                      error, sizeof(error)));
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    db_free(&db);

    Database backup;
    StorageStatus backup_status = storage_load(paths.backup_path, &backup,
                                               error, sizeof(error));
    CHECK(backup_status == STORAGE_OK);
    CHECK(backup.team_count == 2);
    db_free(&backup);

    Database current;
    StorageStatus current_status = storage_load(paths.data_path, &current,
                                                error, sizeof(error));
    CHECK(current_status == STORAGE_OK);
    CHECK(current.team_count == 4);
    db_free(&current);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_unbounded_starting_rating_roundtrip(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    const char *content =
        "PITCHRATE|3\n"
        "TEAM|1|Alpha||\n"
        "PLAYER|1|1|High|Rating|150.00|1\n"
        "PLAYER|2|1|Low|Rating|-25.00|1\n"
        "END\n";
    CHECK(write_raw_data_file(paths.data_path, content));
    Database loaded;
    char error[768];
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_OK);
    CHECK(loaded.player_count == 2);
    CHECK_NEAR(loaded.players[0].base_rating, 150.0, 1e-12);
    CHECK_NEAR(loaded.players[0].current_rating, 150.0, 1e-12);
    CHECK_NEAR(loaded.players[1].base_rating, -25.0, 1e-12);
    CHECK_NEAR(loaded.players[1].current_rating, -25.0, 1e-12);
    db_free(&loaded);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_bom_and_crlf_are_accepted(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    const char *content =
        "\xEF\xBB\xBFPITCHRATE|1\r\n"
        "TEAM|1|Alpha|ALP|Town\r\n"
        "END\r\n";
    CHECK(write_raw_data_file(paths.data_path, content));
    Database loaded;
    char error[768];
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_OK);
    CHECK(loaded.team_count == 1);
    CHECK_STRING(loaded.teams[0].name, "Alpha");
    db_free(&loaded);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_v2_position_fields_are_ignored(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    const char *content =
        "PITCHRATE|2\n"
        "TEAM|1|Alpha||\n"
        "TEAM|2|Beta||\n"
        "PLAYER|1|1|A|One|0|70.00|1\n"
        "PLAYER|2|2|B|Two|2|72.00|1\n"
        "MATCH|1|2026-07-01|1|2|0|0|1\n"
        "APP|1|1|1|90|6.00\n"
        "APP|1|2|2|90|6.00\n"
        "END\n";
    CHECK(write_raw_data_file(paths.data_path, content));
    Database loaded;
    char error[768];
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_OK);
    CHECK(loaded.team_count == 2);
    CHECK(loaded.player_count == 2);
    CHECK(loaded.match_count == 1);
    CHECK(loaded.matches[0].neutral_venue);
    CHECK(loaded.matches[0].length == 90);
    for (size_t i = 0; i < loaded.appearance_count; ++i) {
        CHECK(loaded.appearances[i].sent_off == 0);
    }
    CHECK_NEAR(loaded.players[0].base_rating, 70.0, 1e-12);
    CHECK_NEAR(loaded.players[1].base_rating, 72.0, 1e-12);
    db_free(&loaded);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_legacy_v1_minutes_and_precision_are_loadable(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    char content[8192];
    size_t length = 0;
#define APPEND_RECORD(...)                                                   \
    do {                                                                     \
        int written = snprintf(content + length, sizeof(content) - length,   \
                               __VA_ARGS__);                                 \
        if (written < 0 || (size_t)written >= sizeof(content) - length) {    \
            return false;                                                    \
        }                                                                    \
        length += (size_t)written;                                           \
    } while (0)

    APPEND_RECORD("PITCHRATE|1\n");
    APPEND_RECORD("TEAM|1|Legacy Home||\n");
    APPEND_RECORD("TEAM|2|Legacy Away||\n");
    for (int i = 0; i < 12; ++i) {
        APPEND_RECORD("PLAYER|%d|1|Home%d|Player|%d|70.00|1\n",
                      i + 1, i, i % 4);
    }
    APPEND_RECORD("PLAYER|13|2|Away|Player|0|70.00|1\n");
    APPEND_RECORD("MATCH|1|2026-04-10|1|2|1|0\n");
    for (int i = 0; i < 12; ++i) {
        APPEND_RECORD("APP|1|%d|1|120|%.2f\n", i + 1,
                      i == 0 ? 7.55 : 6.00);
    }
    APPEND_RECORD("APP|1|13|2|90|6.00\n");
    APPEND_RECORD("END\n");
#undef APPEND_RECORD

    CHECK(write_raw_data_bytes(paths.data_path,
                               (const unsigned char *)content, length));
    Database loaded;
    char error[768];
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_OK);
    CHECK(loaded.team_count == 2);
    CHECK(loaded.player_count == 13);
    CHECK(loaded.match_count == 1);
    CHECK(loaded.appearance_count == 13);
    CHECK_NEAR(loaded.appearances[0].performance, 7.55, 1e-12);
    CHECK(loaded.players[0].minutes_played == 120);
    CHECK(!loaded.matches[0].neutral_venue);
    CHECK(db_validate(&loaded, error, sizeof(error)));
    CHECK(storage_save(paths.data_path, &loaded, error, sizeof(error)));
    db_free(&loaded);

    Database reloaded;
    status = storage_load(paths.data_path, &reloaded, error, sizeof(error));
    CHECK(status == STORAGE_OK);
    CHECK(reloaded.appearance_count == 13);
    CHECK_NEAR(reloaded.appearances[0].performance, 7.55, 1e-12);
    db_free(&reloaded);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_embedded_nul_is_rejected(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    static const unsigned char content[] =
        "PITCHRATE|1\n"
        "TEAM|1|Alpha||\n\0garbage\n"
        "END\n";
    CHECK(write_raw_data_bytes(paths.data_path, content,
                               sizeof(content) - 1));
    Database loaded;
    char error[768];
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_CORRUPT);
    CHECK(strstr(error, "NUL") != NULL);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_semantically_invalid_file_rejected(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    const char *content =
        "PITCHRATE|1\n"
        "TEAM|1|Alpha||\n"
        "TEAM|2|Beta||\n"
        "PLAYER|1|1|A|One|3|75.00|1\n"
        "PLAYER|2|2|B|Two|0|72.00|1\n"
        "MATCH|1|2026-10-01|1|2|1|0\n"
        "APP|1|1|1|90|6.00\n"
        "END\n";
    CHECK(write_raw_data_file(paths.data_path, content));
    Database loaded;
    char error[768];
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_CORRUPT);
    CHECK(strstr(error, "both teams") != NULL ||
          strstr(error, "line-up") != NULL);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_sent_off_file_validation(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    const char *content =
        "PITCHRATE|4\n"
        "TEAM|1|Alpha||\n"
        "TEAM|2|Beta||\n"
        "PLAYER|1|1|A|One|70.00|1\n"
        "PLAYER|2|2|B|Two|70.00|1\n"
        "MATCH|1|2026-10-01|1|2|0|1|0|90\n"
        "APP|1|1|1|30|6.00|95\n"
        "APP|1|2|2|90|6.00|0\n"
        "END\n";
    CHECK(write_raw_data_file(paths.data_path, content));
    Database loaded;
    char error[768];
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_CORRUPT);
    CHECK(strstr(error, "appearance") != NULL);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_v5_id_counters_are_required_and_valid(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database loaded;
    char error[768];

    const char *missing =
        "PITCHRATE|5\n"
        "TEAM|1|Alpha||\n"
        "END\n";
    CHECK(write_raw_data_file(paths.data_path, missing));
    CHECK(storage_load(paths.data_path, &loaded, error, sizeof(error)) ==
          STORAGE_CORRUPT);
    CHECK(strstr(error, "ID counter") != NULL);

    const char *stale =
        "PITCHRATE|5\n"
        "NEXT|1|1|1\n"
        "TEAM|1|Alpha||\n"
        "END\n";
    CHECK(write_raw_data_file(paths.data_path, stale));
    CHECK(storage_load(paths.data_path, &loaded, error, sizeof(error)) ==
          STORAGE_CORRUPT);
    CHECK(strstr(error, "ID counters") != NULL);

    const char *duplicate =
        "PITCHRATE|5\n"
        "NEXT|2|1|1\n"
        "NEXT|2|1|1\n"
        "TEAM|1|Alpha||\n"
        "END\n";
    CHECK(write_raw_data_file(paths.data_path, duplicate));
    CHECK(storage_load(paths.data_path, &loaded, error, sizeof(error)) ==
          STORAGE_CORRUPT);
    CHECK(strstr(error, "Duplicate NEXT") != NULL);

    cleanup_test_paths(&paths);
    return true;
}

static bool test_v4_file_derives_id_counters(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    const char *content =
        "PITCHRATE|4\n"
        "TEAM|2|Alpha||\n"
        "TEAM|5|Beta||\n"
        "PLAYER|3|2|A|One|70.00|1\n"
        "PLAYER|8|5|B|Two|70.00|1\n"
        "MATCH|4|2026-07-01|2|5|0|0|0|90\n"
        "APP|4|3|2|90|6.00|0\n"
        "APP|4|8|5|90|6.00|0\n"
        "END\n";
    CHECK(write_raw_data_file(paths.data_path, content));
    Database loaded;
    char error[768];
    CHECK(storage_load(paths.data_path, &loaded, error, sizeof(error)) ==
          STORAGE_OK);
    CHECK(loaded.next_team_id == 6);
    CHECK(loaded.next_player_id == 9);
    CHECK(loaded.next_match_id == 5);
    db_free(&loaded);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_unknown_record_rejected(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    const char *content =
        "PITCHRATE|1\n"
        "BANANA|1\n"
        "END\n";
    CHECK(write_raw_data_file(paths.data_path, content));
    Database loaded;
    char error[768];
    StorageStatus status = storage_load(paths.data_path, &loaded,
                                        error, sizeof(error));
    CHECK(status == STORAGE_CORRUPT);
    CHECK(strstr(error, "Unknown record") != NULL);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_relative_data_path(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database db;
    CHECK(populate_sample(&db));
    wchar_t original_directory[MAX_PATH];
    CHECK(GetCurrentDirectoryW(
              (DWORD)(sizeof(original_directory) /
                      sizeof(original_directory[0])),
              original_directory) != 0);
    CHECK(SetCurrentDirectoryW(paths.directory));
    char error[768];
    bool saved = storage_save(L"nested\\relative.frc", &db,
                              error, sizeof(error));
    SetCurrentDirectoryW(original_directory);
    CHECK(saved);
    wchar_t relative_file[MAX_PATH];
    CHECK(swprintf(relative_file,
                   sizeof(relative_file) / sizeof(relative_file[0]),
                   L"%ls\\nested\\relative.frc", paths.directory) >= 0);
    CHECK(GetFileAttributesW(relative_file) != INVALID_FILE_ATTRIBUTES);
    DeleteFileW(relative_file);
    wchar_t relative_backup[MAX_PATH];
    if (swprintf(relative_backup,
                 sizeof(relative_backup) / sizeof(relative_backup[0]),
                 L"%ls.bak", relative_file) >= 0) {
        DeleteFileW(relative_backup);
    }
    wchar_t nested_dir[MAX_PATH];
    if (swprintf(nested_dir, sizeof(nested_dir) / sizeof(nested_dir[0]),
                 L"%ls\\nested", paths.directory) >= 0) {
        RemoveDirectoryW(nested_dir);
    }
    db_free(&db);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_nested_directory_created(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    wchar_t nested[MAX_PATH];
    wchar_t nested_file[MAX_PATH];
    CHECK(swprintf(nested, sizeof(nested) / sizeof(nested[0]),
                   L"%ls\\season\\2026", paths.directory) >= 0);
    CHECK(swprintf(nested_file, sizeof(nested_file) / sizeof(nested_file[0]),
                   L"%ls\\data.frc", nested) >= 0);
    Database db;
    CHECK(populate_sample(&db));
    char error[768];
    CHECK(storage_save(nested_file, &db, error, sizeof(error)));
    CHECK(GetFileAttributesW(nested_file) != INVALID_FILE_ATTRIBUTES);
    db_free(&db);
    DeleteFileW(nested_file);
    wchar_t other[MAX_PATH];
    if (swprintf(other, sizeof(other) / sizeof(other[0]), L"%ls.bak",
                 nested_file) >= 0) {
        DeleteFileW(other);
    }
    RemoveDirectoryW(nested);
    if (swprintf(other, sizeof(other) / sizeof(other[0]), L"%ls\\season",
                 paths.directory) >= 0) {
        RemoveDirectoryW(other);
    }
    cleanup_test_paths(&paths);
    return true;
}

static bool test_migrate_legacy_data(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));

    wchar_t legacy_dir[MAX_PATH];
    wchar_t new_dir[MAX_PATH];
    wchar_t legacy_path[MAX_PATH];
    wchar_t new_path[MAX_PATH];
    CHECK(swprintf(legacy_dir, sizeof(legacy_dir) / sizeof(legacy_dir[0]),
                   L"%ls\\legacy", paths.directory) >= 0);
    CHECK(swprintf(new_dir, sizeof(new_dir) / sizeof(new_dir[0]),
                   L"%ls\\new", paths.directory) >= 0);
    CHECK(CreateDirectoryW(legacy_dir, NULL) ||
          GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK(CreateDirectoryW(new_dir, NULL) ||
          GetLastError() == ERROR_ALREADY_EXISTS);
    CHECK(swprintf(legacy_path, sizeof(legacy_path) / sizeof(legacy_path[0]),
                   L"%ls\\data.frc", legacy_dir) >= 0);
    CHECK(swprintf(new_path, sizeof(new_path) / sizeof(new_path[0]),
                   L"%ls\\data.frc", new_dir) >= 0);

    Database legacy_db;
    CHECK(populate_sample(&legacy_db));
    char error[768];
    CHECK(storage_save(legacy_path, &legacy_db, error, sizeof(error)));
    CHECK(storage_save(legacy_path, &legacy_db, error, sizeof(error)));

    wchar_t legacy_backup[MAX_PATH];
    wchar_t legacy_corrupt[MAX_PATH];
    wchar_t new_backup[MAX_PATH];
    wchar_t new_corrupt[MAX_PATH];
    CHECK(swprintf(legacy_backup,
                   sizeof(legacy_backup) / sizeof(legacy_backup[0]),
                   L"%ls.bak", legacy_path) >= 0);
    CHECK(swprintf(legacy_corrupt,
                   sizeof(legacy_corrupt) / sizeof(legacy_corrupt[0]),
                   L"%ls.corrupt", legacy_path) >= 0);
    CHECK(swprintf(new_backup, sizeof(new_backup) / sizeof(new_backup[0]),
                   L"%ls.bak", new_path) >= 0);
    CHECK(swprintf(new_corrupt, sizeof(new_corrupt) / sizeof(new_corrupt[0]),
                   L"%ls.corrupt", new_path) >= 0);
    CHECK(write_raw_data_file(legacy_corrupt, "damaged legacy data"));

    CHECK(storage_migrate_legacy_data(legacy_path, new_path,
                                      error, sizeof(error)));
    CHECK(GetFileAttributesW(new_path) != INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW(new_backup) != INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW(new_corrupt) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW(legacy_corrupt) != INVALID_FILE_ATTRIBUTES);

    Database migrated;
    CHECK(storage_load(new_path, &migrated, error, sizeof(error)) ==
          STORAGE_OK);
    CHECK(same_database(&legacy_db, &migrated));
    db_free(&migrated);

    Database replacement;
    db_init(&replacement);
    uint64_t team = 0;
    CHECK(db_add_team(&replacement, "New Data", "NEW", "New",
                      &team, error, sizeof(error)));
    CHECK(storage_save(new_path, &replacement, error, sizeof(error)));
    CHECK(storage_migrate_legacy_data(legacy_path, new_path,
                                      error, sizeof(error)));

    Database current;
    CHECK(storage_load(new_path, &current, error, sizeof(error)) ==
          STORAGE_OK);
    CHECK(current.team_count == 1);
    CHECK(same_database(&replacement, &current));
    db_free(&current);
    db_free(&replacement);
    db_free(&legacy_db);

    DeleteFileW(new_corrupt);
    DeleteFileW(new_backup);
    DeleteFileW(new_path);
    DeleteFileW(legacy_corrupt);
    DeleteFileW(legacy_backup);
    DeleteFileW(legacy_path);
    RemoveDirectoryW(new_dir);
    RemoveDirectoryW(legacy_dir);
    cleanup_test_paths(&paths);
    return true;
}

static bool test_ids_are_not_reused_after_reload(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database db;
    CHECK(populate_sample(&db));
    char error[768];
    uint64_t alpha = db.teams[0].id;
    uint64_t deleted_team = 0;
    uint64_t deleted_player = 0;
    CHECK(db_add_team(&db, "Gamma Town", "GAM", "East", &deleted_team,
                      error, sizeof(error)));
    CHECK(db_add_player(&db, alpha, "Temp", "Player", 50.0, true,
                        &deleted_player, error, sizeof(error)));
    uint64_t deleted_match = db.matches[0].id;
    CHECK(db_delete_match(&db, deleted_match, error, sizeof(error)));
    CHECK(db_delete_player(&db, deleted_player, error, sizeof(error)));
    CHECK(db_delete_team(&db, deleted_team, error, sizeof(error)));
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    db_free(&db);

    Database loaded;
    CHECK(storage_load(paths.data_path, &loaded, error, sizeof(error)) ==
          STORAGE_OK);
    uint64_t new_team = 0;
    uint64_t new_player = 0;
    uint64_t new_match = 0;
    CHECK(db_add_team(&loaded, "Delta City", "DEL", "West", &new_team,
                      error, sizeof(error)));
    CHECK(db_add_player(&loaded, alpha, "New", "Player", 50.0, true,
                        &new_player, error, sizeof(error)));
    MatchPlayerInput lineup[] = {
        { loaded.players[0].id, 90, 6.0, 0 },
        { loaded.players[2].id, 90, 6.0, 0 },
    };
    MatchInput match = {
        .date = "2026-09-20",
        .home_team_id = loaded.players[0].team_id,
        .away_team_id = loaded.players[2].team_id,
        .home_goals = 1,
        .away_goals = 0,
        .length = 90,
        .players = lineup,
        .player_count = 2,
    };
    CHECK(db_add_match(&loaded, &match, &new_match, error, sizeof(error)));
    bool team_reused = new_team == deleted_team;
    bool player_reused = new_player == deleted_player;
    bool match_reused = new_match == deleted_match;
    db_free(&loaded);
    cleanup_test_paths(&paths);
    if (team_reused || player_reused || match_reused) {
        fprintf(stderr, "  reused after reload: team %s, player %s, match %s\n",
                team_reused ? "yes" : "no", player_reused ? "yes" : "no",
                match_reused ? "yes" : "no");
    }
    CHECK(!team_reused);
    CHECK(!player_reused);
    CHECK(!match_reused);
    return true;
}

static bool overwrite_file_text(const wchar_t *path, const char *content)
{
    FILE *file = _wfopen(path, L"wb");
    if (file == NULL) {
        return false;
    }
    bool ok = fputs(content, file) >= 0;
    return fclose(file) == 0 && ok;
}

/* Reports whether any file in the directory holds exactly this text. */
static bool directory_has_file_with_text(const wchar_t *directory,
                                         const char *content)
{
    wchar_t pattern[MAX_PATH];
    if (swprintf(pattern, sizeof(pattern) / sizeof(pattern[0]), L"%ls\\*",
                 directory) < 0) {
        return false;
    }
    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW(pattern, &entry);
    if (find == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool found = false;
    size_t wanted = strlen(content);
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            continue;
        }
        wchar_t path[MAX_PATH];
        if (swprintf(path, sizeof(path) / sizeof(path[0]), L"%ls\\%ls",
                     directory, entry.cFileName) < 0) {
            continue;
        }
        FILE *file = _wfopen(path, L"rb");
        if (file == NULL) {
            continue;
        }
        char buffer[256];
        size_t read = fread(buffer, 1, sizeof(buffer), file);
        fclose(file);
        if (read == wanted && memcmp(buffer, content, wanted) == 0) {
            found = true;
        }
    } while (!found && FindNextFileW(find, &entry));
    FindClose(find);
    return found;
}

static void delete_directory_files(const wchar_t *directory)
{
    wchar_t pattern[MAX_PATH];
    if (swprintf(pattern, sizeof(pattern) / sizeof(pattern[0]), L"%ls\\*",
                 directory) < 0) {
        return;
    }
    WIN32_FIND_DATAW entry;
    HANDLE find = FindFirstFileW(pattern, &entry);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        wchar_t path[MAX_PATH];
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
            swprintf(path, sizeof(path) / sizeof(path[0]), L"%ls\\%ls",
                     directory, entry.cFileName) >= 0) {
            DeleteFileW(path);
        }
    } while (FindNextFileW(find, &entry));
    FindClose(find);
}

static bool test_second_recovery_keeps_first_damaged_file(void)
{
    TestPaths paths;
    CHECK(make_test_paths(&paths));
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(add_two_teams(&db, &alpha, &beta));
    char error[768];
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    CHECK(storage_save(paths.data_path, &db, error, sizeof(error)));
    db_free(&db);

    const char *first_damage = "first damaged primary";
    const char *second_damage = "second damaged primary";
    CHECK(overwrite_file_text(paths.data_path, first_damage));
    CHECK(storage_repair_from_backup(paths.data_path, error, sizeof(error)));
    CHECK(directory_has_file_with_text(paths.directory, first_damage));

    CHECK(overwrite_file_text(paths.data_path, second_damage));
    bool repaired_again =
        storage_repair_from_backup(paths.data_path, error, sizeof(error));
    /*
     * Refusing the second repair is acceptable. Losing the first preserved
     * file, or the second one after a successful repair, is not.
     */
    bool first_kept =
        directory_has_file_with_text(paths.directory, first_damage);
    bool second_kept =
        directory_has_file_with_text(paths.directory, second_damage);
    delete_directory_files(paths.directory);
    cleanup_test_paths(&paths);
    CHECK(first_kept);
    CHECK(!repaired_again || second_kept);
    return true;
}

const TestCase g_storage_tests[] = {
    TEST_CASE(test_roundtrip_and_recompute),
    TEST_CASE(test_neutral_venue_roundtrip),
    TEST_CASE(test_unicode_and_escaping),
    TEST_CASE(test_backup_created_and_recovery),
    TEST_CASE(test_checksum_detects_damage_without_backup),
    TEST_CASE(test_truncated_file_rejected),
    TEST_CASE(test_missing_file),
    TEST_CASE(test_newer_format_rejected),
    TEST_CASE(test_older_format_rejected),
    TEST_CASE(test_save_refuses_newer_primary),
    TEST_CASE(test_locked_primary_is_not_replaced_from_backup),
    TEST_CASE(test_save_preserves_good_backup_when_primary_is_corrupt),
    TEST_CASE(test_bom_and_crlf_are_accepted),
    TEST_CASE(test_unbounded_starting_rating_roundtrip),
    TEST_CASE(test_v2_position_fields_are_ignored),
    TEST_CASE(test_legacy_v1_minutes_and_precision_are_loadable),
    TEST_CASE(test_embedded_nul_is_rejected),
    TEST_CASE(test_semantically_invalid_file_rejected),
    TEST_CASE(test_sent_off_file_validation),
    TEST_CASE(test_v5_id_counters_are_required_and_valid),
    TEST_CASE(test_v4_file_derives_id_counters),
    TEST_CASE(test_unknown_record_rejected),
    TEST_CASE(test_relative_data_path),
    TEST_CASE(test_nested_directory_created),
    TEST_CASE(test_migrate_legacy_data),
    TEST_CASE(test_ids_are_not_reused_after_reload),
    TEST_CASE(test_second_recovery_keeps_first_damaged_file),
};

const size_t g_storage_test_count =
    sizeof(g_storage_tests) / sizeof(g_storage_tests[0]);
