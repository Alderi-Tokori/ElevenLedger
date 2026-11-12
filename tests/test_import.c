#include "test_framework.h"

#include "core/database.h"
#include "core/import.h"
#include "core/util.h"

#include <string.h>

static const char *HTML_FIXTURE =
    "<html><head><title>Test</title></head><body><script>\n"
    "require.config.params['matchheader'] = {\n"
    "    input: [313,304,'Rennes','Paris Saint-Germain','23/08/2026 20:45:00'],\n"
    "    matchId: 1984066\n"
    "};\n"
    "require.config.params[\"args\"] = {\n"
    "    matchId: 1984066,\n"
    "    matchCentreData: {\n"
    "        \"startDate\":\"2026-08-23T20:45:00\",\n"
    "        \"ftScore\":\"2 : 1\",\n"
    "        \"home\":{\n"
    "            \"name\":\"Rennes\",\n"
    "            \"scores\":{\"fulltime\":2},\n"
    "            \"players\":[\n"
    "                {\"name\":\"Brice Samba\",\"isFirstEleven\":true},\n"
    "                {\"name\":\"Przemyslaw Frankowski\",\"isFirstEleven\":true,\"subbedOutExpandedMinute\":74},\n"
    "                {\"name\":\"Ludovic Blas\",\"isFirstEleven\":false,\"subbedInExpandedMinute\":74},\n"
    "                {\"name\":\"Unused Sub\",\"isFirstEleven\":false}\n"
    "            ]\n"
    "        },\n"
    "        \"away\":{\n"
    "            \"name\":\"PSG\",\n"
    "            \"scores\":{\"fulltime\":1},\n"
    "            \"players\":[\n"
    "                {\"name\":\"Jo\\u00e3o Neves\",\"isFirstEleven\":true},\n"
    "                {\"name\":\"D\\u00e9sir\\u00e9 Dou\\u00e9\",\"isFirstEleven\":true,\"subbedOutExpandedMinute\":90},\n"
    "                {\"name\":\"Vitinha\",\"isFirstEleven\":true},\n"
    "                {\"name\":\"Backup Keeper\",\"isFirstEleven\":false}\n"
    "            ]\n"
    "        }\n"
    "    }\n"
    "};\n"
    "</script></body></html>\n";

static bool test_parse_match_page(void)
{
    ImportedMatch match;
    char error[512];
    CHECK(imported_match_parse_html(HTML_FIXTURE, strlen(HTML_FIXTURE),
                                    &match, error, sizeof(error)));
    CHECK_STRING(match.date, "2026-08-23");
    CHECK_STRING(match.home_team, "Rennes");
    CHECK_STRING(match.away_team, "Paris Saint-Germain");
    CHECK(match.home_goals == 2);
    CHECK(match.away_goals == 1);
    CHECK(match.home_player_count == 3);
    CHECK(match.away_player_count == 3);

    CHECK_STRING(match.home_players[0].first_name, "Brice");
    CHECK_STRING(match.home_players[0].last_name, "Samba");
    CHECK(match.home_players[0].minutes == 90);
    CHECK_STRING(match.home_players[1].first_name, "Przemyslaw");
    CHECK_STRING(match.home_players[1].last_name, "Frankowski");
    CHECK(match.home_players[1].minutes == 74);
    CHECK_STRING(match.home_players[2].first_name, "Ludovic");
    CHECK_STRING(match.home_players[2].last_name, "Blas");
    CHECK(match.home_players[2].minutes == 16);

    CHECK_STRING(match.away_players[0].first_name, "João");
    CHECK_STRING(match.away_players[0].last_name, "Neves");
    CHECK(match.away_players[0].minutes == 90);
    CHECK_STRING(match.away_players[1].first_name, "Désiré");
    CHECK_STRING(match.away_players[1].last_name, "Doué");
    CHECK_STRING(match.away_players[2].first_name, "Vitinha");
    CHECK_STRING(match.away_players[2].last_name, "");
    imported_match_free(&match);
    CHECK(match.home_players == NULL);
    return true;
}

static bool test_parse_score_fallback_and_extra_time(void)
{
    const char *html =
        "<script>matchCentreData: {"
        "\"startDate\":\"2026-05-30T18:00:00\","
        "\"ftScore\":\"\","
        "\"score\":\"0 : 3\","
        "\"etScore\":\"1 : 2\","
        "\"home\":{\"name\":\"A FC\",\"players\":["
        "{\"name\":\"Home One\",\"isFirstEleven\":true}]},"
        "\"away\":{\"name\":\"B FC\",\"players\":["
        "{\"name\":\"Away One\",\"isFirstEleven\":true},"
        "{\"name\":\"Away Two\",\"isFirstEleven\":false,\"subbedInExpandedMinute\":105},"
        "{\"name\":\"Away Three\",\"isFirstEleven\":false,\"subbedInExpandedMinute\":105,\"subbedOutExpandedMinute\":115}]}"
        "}</script>";
    ImportedMatch match;
    char error[512];
    CHECK(imported_match_parse_html(html, strlen(html), &match,
                                    error, sizeof(error)));
    CHECK(match.home_goals == 0);
    CHECK(match.away_goals == 3);
    CHECK(match.home_players[0].minutes == 120);
    CHECK(match.away_players[0].minutes == 120);
    CHECK(match.away_players[1].minutes == 15);
    CHECK(match.away_players[2].minutes == 10);
    imported_match_free(&match);
    return true;
}

static bool test_parse_match_length_with_stoppage_time(void)
{
    const char *html =
        "<script>matchCentreData: {"
        "\"startDate\":\"2026-08-23\","
        "\"score\":\"2 : 2\","
        "\"maxMinute\":94,"
        "\"expandedMaxMinute\":97,"
        "\"periodEndMinutes\":{\"1\":47,\"2\":94},"
        "\"home\":{\"name\":\"Rennes\",\"players\":["
        "{\"name\":\"Full Match\",\"isFirstEleven\":true},"
        "{\"name\":\"Second Half Off\",\"isFirstEleven\":true,\"subbedOutExpandedMinute\":90},"
        "{\"name\":\"First Half Off\",\"isFirstEleven\":true,\"subbedOutExpandedMinute\":45},"
        "{\"name\":\"Second Half Sub\",\"isFirstEleven\":false,\"subbedInExpandedMinute\":74},"
        "{\"name\":\"Stoppage Sub\",\"isFirstEleven\":false,\"subbedInExpandedMinute\":90}"
        "]},"
        "\"away\":{\"name\":\"PSG\",\"players\":["
        "{\"name\":\"Away Full Match\",\"isFirstEleven\":true}]}"
        "}</script>";
    ImportedMatch match;
    char error[512];
    CHECK(imported_match_parse_html(html, strlen(html), &match,
                                    error, sizeof(error)));
    CHECK(match.length == 94);
    CHECK(match.home_player_count == 5);
    CHECK(match.away_player_count == 1);
    CHECK(match.home_players[0].minutes == 94);
    CHECK(match.home_players[1].minutes == 87);
    CHECK(match.home_players[2].minutes == 45);
    CHECK(match.home_players[3].minutes == 23);
    CHECK(match.home_players[4].minutes == 7);
    CHECK(match.away_players[0].minutes == 94);
    imported_match_free(&match);
    return true;
}

static bool test_parse_match_length_falls_back_to_period_end(void)
{
    const char *html =
        "<script>matchCentreData: {"
        "\"startDate\":\"2026-08-23\","
        "\"score\":\"1 : 0\","
        "\"expandedMaxMinute\":96,"
        "\"periodEndMinutes\":{\"1\":46,\"2\":93},"
        "\"home\":{\"name\":\"Rennes\",\"players\":["
        "{\"name\":\"Full Match\",\"isFirstEleven\":true},"
        "{\"name\":\"Late Sub\",\"isFirstEleven\":false,\"subbedInExpandedMinute\":90}"
        "]},"
        "\"away\":{\"name\":\"PSG\",\"players\":["
        "{\"name\":\"Away Full Match\",\"isFirstEleven\":true}]}"
        "}</script>";
    ImportedMatch match;
    char error[512];
    CHECK(imported_match_parse_html(html, strlen(html), &match,
                                    error, sizeof(error)));
    CHECK(match.home_players[0].minutes == 93);
    CHECK(match.home_players[1].minutes == 6);
    CHECK(match.away_players[0].minutes == 93);
    imported_match_free(&match);
    return true;
}

static bool test_parse_sending_off_minutes(void)
{
    const char *html =
        "<script>matchCentreData: {"
        "\"startDate\":\"2026-08-23\","
        "\"score\":\"1 : 2\","
        "\"maxMinute\":94,"
        "\"expandedMaxMinute\":97,"
        "\"periodEndMinutes\":{\"1\":47,\"2\":94},"
        "\"events\":["
        "{\"minute\":34,\"second\":27,\"playerId\":111,\"expandedMinute\":34,"
        "\"type\":{\"value\":17,\"displayName\":\"Card\"},"
        "\"qualifiers\":[{\"type\":{\"value\":33,\"displayName\":\"Red\"}},"
        "{\"type\":{\"value\":55,\"displayName\":\"RelatedEventId\"},\"value\":\"342\"}]},"
        "{\"minute\":20,\"second\":0,\"playerId\":444,\"expandedMinute\":20,"
        "\"type\":{\"value\":17,\"displayName\":\"Card\"},"
        "\"qualifiers\":[{\"type\":{\"value\":31,\"displayName\":\"Yellow\"}}]},"
        "{\"minute\":70,\"second\":0,\"playerId\":222,\"expandedMinute\":73,"
        "\"type\":{\"value\":17,\"displayName\":\"Card\"},"
        "\"qualifiers\":[{\"type\":{\"value\":32,\"displayName\":\"SecondYellow\"}}]}"
        "],"
        "\"home\":{\"name\":\"Rennes\",\"players\":["
        "{\"playerId\":111,\"name\":\"Sent Off\",\"isFirstEleven\":true},"
        "{\"playerId\":444,\"name\":\"Booked Starter\",\"isFirstEleven\":true},"
        "{\"playerId\":555,\"name\":\"Full Match\",\"isFirstEleven\":true}]},"
        "\"away\":{\"name\":\"PSG\",\"players\":["
        "{\"playerId\":222,\"name\":\"Sub Sent Off\",\"isFirstEleven\":false,"
        "\"subbedInExpandedMinute\":60},"
        "{\"playerId\":333,\"name\":\"Away Full\",\"isFirstEleven\":true}]}"
        "}</script>";
    ImportedMatch match;
    char error[512];
    CHECK(imported_match_parse_html(html, strlen(html), &match,
                                    error, sizeof(error)));
    CHECK(match.length == 94);
    CHECK(match.home_players[0].minutes == 34);
    CHECK(match.home_players[0].sent_off == 34);
    CHECK(match.home_players[1].minutes == 94);
    CHECK(match.home_players[1].sent_off == 0);
    CHECK(match.home_players[2].minutes == 94);
    CHECK(match.away_players[0].minutes == 13);
    CHECK(match.away_players[0].sent_off == 70);
    CHECK(match.away_players[1].minutes == 94);
    imported_match_free(&match);
    return true;
}

static bool test_parse_escaped_names(void)
{
    const char *html =
        "<script>matchCentreData: {"
        "\"startDate\":\"2026-08-23\","
        "\"score\":\"1 : 0\","
        "\"home\":{\"name\":\"A \\\"Quoted\\\" FC\",\"players\":["
        "{\"name\":\"A \\\"B\\\" C\",\"isFirstEleven\":true}]},"
        "\"away\":{\"name\":\"B FC\",\"players\":["
        "{\"name\":\"Emoji \\ud83d\\ude80 Player\",\"isFirstEleven\":true}]}"
        "}</script>";
    ImportedMatch match;
    char error[512];
    CHECK(imported_match_parse_html(html, strlen(html), &match,
                                    error, sizeof(error)));
    CHECK_STRING(match.home_team, "A \"Quoted\" FC");
    CHECK_STRING(match.home_players[0].first_name, "A");
    CHECK_STRING(match.home_players[0].last_name, "\"B\" C");
    CHECK_STRING(match.away_players[0].first_name, "Emoji");
    CHECK_STRING(match.away_players[0].last_name,
                 "\xF0\x9F\x9A\x80 Player");
    imported_match_free(&match);
    return true;
}

static bool test_parse_rejects_bad_pages(void)
{
    ImportedMatch match;
    char error[512];

    const char *no_data = "<html><body>Nothing here</body></html>";
    CHECK(!imported_match_parse_html(no_data, strlen(no_data), &match,
                                     error, sizeof(error)));
    CHECK(strstr(error, "match data") != NULL);

    const char *bad_json =
        "<script>matchCentreData: {\"startDate\":";
    CHECK(!imported_match_parse_html(bad_json, strlen(bad_json), &match,
                                     error, sizeof(error)));

    const char *no_team =
        "<script>matchCentreData: {\"startDate\":\"2026-08-23\","
        "\"score\":\"1 : 0\",\"home\":{\"name\":\"A FC\",\"players\":["
        "{\"name\":\"One Player\",\"isFirstEleven\":true}]}}</script>";
    CHECK(!imported_match_parse_html(no_team, strlen(no_team), &match,
                                     error, sizeof(error)));
    CHECK(strstr(error, "both teams") != NULL);

    const char *no_score =
        "<script>matchCentreData: {\"startDate\":\"2026-08-23\","
        "\"home\":{\"name\":\"A FC\",\"players\":["
        "{\"name\":\"One Player\",\"isFirstEleven\":true}]},"
        "\"away\":{\"name\":\"B FC\",\"players\":["
        "{\"name\":\"Two Player\",\"isFirstEleven\":true}]}}</script>";
    CHECK(!imported_match_parse_html(no_score, strlen(no_score), &match,
                                     error, sizeof(error)));
    CHECK(strstr(error, "score") != NULL);

    const char *bad_date =
        "<script>matchCentreData: {\"startDate\":\"23/08/2026\","
        "\"score\":\"1 : 0\","
        "\"home\":{\"name\":\"A FC\",\"players\":["
        "{\"name\":\"One Player\",\"isFirstEleven\":true}]},"
        "\"away\":{\"name\":\"B FC\",\"players\":["
        "{\"name\":\"Two Player\",\"isFirstEleven\":true}]}}</script>";
    CHECK(!imported_match_parse_html(bad_date, strlen(bad_date), &match,
                                     error, sizeof(error)));
    CHECK(strstr(error, "date") != NULL);

    const char *no_players =
        "<script>matchCentreData: {\"startDate\":\"2026-08-23\","
        "\"score\":\"1 : 0\","
        "\"home\":{\"name\":\"A FC\",\"players\":[]},"
        "\"away\":{\"name\":\"B FC\",\"players\":["
        "{\"name\":\"Two Player\",\"isFirstEleven\":true}]}}</script>";
    CHECK(!imported_match_parse_html(no_players, strlen(no_players), &match,
                                     error, sizeof(error)));
    CHECK(strstr(error, "players") != NULL);

    const char *control_name =
        "<script>matchCentreData: {\"startDate\":\"2026-08-23\","
        "\"score\":\"1 : 0\","
        "\"home\":{\"name\":\"A FC\",\"players\":["
        "{\"name\":\"Bad\\u0001Name\",\"isFirstEleven\":true}]},"
        "\"away\":{\"name\":\"B FC\",\"players\":["
        "{\"name\":\"Two Player\",\"isFirstEleven\":true}]}}</script>";
    CHECK(!imported_match_parse_html(control_name, strlen(control_name),
                                     &match, error, sizeof(error)));

    CHECK(!imported_match_parse_html(NULL, 0, &match, error,
                                     sizeof(error)));
    const char invalid_utf8[] = { (char)0xFF, '\0' };
    CHECK(!imported_match_parse_html(invalid_utf8, 1, &match,
                                     error, sizeof(error)));
    return true;
}

static bool test_find_team_by_name(void)
{
    Database db;
    db_init(&db);
    char error[256];
    uint64_t team_id = 0;
    CHECK(db_add_team(&db, "Northbridge FC", "NBR", "Northbridge",
                      &team_id, error, sizeof(error)));
    const Team *team = db_find_team_by_name(&db, "  northbridge fc  ");
    CHECK(team != NULL);
    CHECK(team->id == team_id);
    CHECK(db_find_team_by_name(&db, "Northbridge") == NULL);
    CHECK(db_find_team_by_name(&db, "") == NULL);
    CHECK(db_find_team_by_name(&db, NULL) == NULL);
    CHECK(db_find_team_by_name(NULL, "Northbridge FC") == NULL);
    db_free(&db);
    return true;
}

static bool setup_import_db(Database *db, uint64_t *alpha, uint64_t *beta)
{
    char error[256];
    db_init(db);
    CHECK(db_add_team(db, "Alpha FC", "ALP", "Northbridge",
                      alpha, error, sizeof(error)));
    CHECK(db_add_team(db, "Beta United", "BET", "Riverside",
                      beta, error, sizeof(error)));
    return true;
}

static const Appearance *find_import_appearance(const Database *db,
                                                uint64_t match_id,
                                                uint64_t player_id)
{
    for (size_t i = 0; i < db->appearance_count; ++i) {
        if (db->appearances[i].match_id == match_id &&
            db->appearances[i].player_id == player_id) {
            return &db->appearances[i];
        }
    }
    return NULL;
}

static bool test_import_creates_players_and_match(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_import_db(&db, &alpha, &beta));
    char error[512];
    uint64_t mia_id = 0;
    CHECK(db_add_player(&db, beta, "Mia", "Clarke", 75.0, true, &mia_id,
                        error, sizeof(error)));

    ImportPlayerPlan home[] = {
        { .first_name = "Ava", .last_name = "Stone", .minutes = 90,
          .performance = 6.5, .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlayerPlan away[] = {
        { .first_name = "Mia", .last_name = "Clarke", .minutes = 90,
          .performance = 7.0, .action = IMPORT_PLAYER_USE_EXISTING,
          .existing_player_id = mia_id },
    };
    ImportPlan plan = {
        .date = "2026-08-23",
        .home_team_name = "Alpha FC",
        .away_team_name = "Beta United",
        .home_goals = 2,
        .away_goals = 1,
        .neutral_venue = false,
        .length = 90,
        .home_players = home,
        .home_player_count = 1,
        .away_players = away,
        .away_player_count = 1,
    };
    uint64_t match_id = 0;
    CHECK(db_import_match(&db, &plan, &match_id, error, sizeof(error)));
    CHECK(match_id != 0);
    CHECK(db.match_count == 1);
    CHECK(db.appearance_count == 2);
    const Match *match = db_find_match(&db, match_id);
    CHECK(match != NULL);
    CHECK(match->home_team_id == alpha);
    CHECK(match->away_team_id == beta);
    CHECK(match->home_goals == 2);
    CHECK(match->away_goals == 1);

    const Player *ava = db_find_player_by_name(&db, "Ava", "Stone", NULL);
    CHECK(ava != NULL);
    CHECK(ava->team_id == alpha);
    CHECK_NEAR(ava->base_rating, RATING_DEFAULT, 1e-12);
    const Player *mia = db_find_player(&db, mia_id);
    CHECK(mia != NULL);
    CHECK(mia->team_id == beta);

    const Appearance *home_appearance =
        find_import_appearance(&db, match_id, ava->id);
    const Appearance *away_appearance =
        find_import_appearance(&db, match_id, mia_id);
    CHECK(home_appearance != NULL);
    CHECK(away_appearance != NULL);
    CHECK_NEAR(home_appearance->rating_delta + away_appearance->rating_delta,
               0.0, 1e-9);
    db_free(&db);
    return true;
}

static bool test_import_preserves_sending_off(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_import_db(&db, &alpha, &beta));
    char error[512];
    ImportPlayerPlan home[] = {
        { .first_name = "Sent", .last_name = "Off", .minutes = 30,
          .sent_off = 30, .performance = 6.0,
          .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlayerPlan away[] = {
        { .first_name = "Away", .last_name = "One", .minutes = 90,
          .sent_off = 0, .performance = 6.0,
          .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlan plan = {
        .date = "2026-08-23",
        .home_team_name = "Alpha FC",
        .away_team_name = "Beta United",
        .home_goals = 0,
        .away_goals = 1,
        .length = 99,
        .home_players = home,
        .home_player_count = 1,
        .away_players = away,
        .away_player_count = 1,
    };
    uint64_t match_id = 0;
    CHECK(db_import_match(&db, &plan, &match_id, error, sizeof(error)));
    const Match *match = db_find_match(&db, match_id);
    CHECK(match != NULL);
    CHECK(match->length == 99);
    bool found = false;
    for (size_t i = 0; i < db.appearance_count; ++i) {
        if (db.appearances[i].match_id == match_id &&
            db.appearances[i].minutes == 30) {
            CHECK(db.appearances[i].sent_off == 30);
            found = true;
        }
    }
    CHECK(found);
    db_free(&db);
    return true;
}

static bool test_import_creates_missing_teams(void)
{
    Database db;
    db_init(&db);
    char error[512];
    ImportPlayerPlan home[] = {
        { .first_name = "Home", .last_name = "One", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlayerPlan away[] = {
        { .first_name = "Away", .last_name = "One", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlan plan = {
        .date = "2026-08-23",
        .home_team_name = "Gamma FC",
        .away_team_name = "Delta United",
        .home_goals = 0,
        .away_goals = 0,
        .neutral_venue = true,
        .length = 90,
        .home_players = home,
        .home_player_count = 1,
        .away_players = away,
        .away_player_count = 1,
    };
    uint64_t match_id = 0;
    CHECK(db_import_match(&db, &plan, &match_id, error, sizeof(error)));
    CHECK(db.team_count == 2);
    CHECK(db.player_count == 2);
    const Team *gamma = db_find_team_by_name(&db, "gamma fc");
    const Team *delta = db_find_team_by_name(&db, "delta united");
    CHECK(gamma != NULL);
    CHECK(delta != NULL);
    const Match *match = db_find_match(&db, match_id);
    CHECK(match != NULL);
    CHECK(match->home_team_id == gamma->id);
    CHECK(match->away_team_id == delta->id);
    CHECK(match->neutral_venue);
    db_free(&db);
    return true;
}

static bool test_import_can_move_existing_player(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_import_db(&db, &alpha, &beta));
    char error[512];
    uint64_t mia_id = 0;
    CHECK(db_add_player(&db, beta, "Mia", "Clarke", 75.0, true, &mia_id,
                        error, sizeof(error)));
    size_t players_before = db.player_count;

    ImportPlayerPlan home[] = {
        { .first_name = "Mia", .last_name = "Clarke", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_USE_EXISTING,
          .existing_player_id = mia_id },
    };
    ImportPlayerPlan away[] = {
        { .first_name = "Away", .last_name = "One", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlan plan = {
        .date = "2026-08-23",
        .home_team_name = "Alpha FC",
        .away_team_name = "Beta United",
        .home_goals = 1,
        .away_goals = 0,
        .length = 90,
        .home_players = home,
        .home_player_count = 1,
        .away_players = away,
        .away_player_count = 1,
    };
    uint64_t match_id = 0;
    CHECK(db_import_match(&db, &plan, &match_id, error, sizeof(error)));
    CHECK(db.player_count == players_before + 1);
    const Player *mia = db_find_player(&db, mia_id);
    CHECK(mia != NULL);
    CHECK(mia->team_id == alpha);
    const Appearance *appearance =
        find_import_appearance(&db, match_id, mia_id);
    CHECK(appearance != NULL);
    CHECK(appearance->team_id == alpha);
    db_free(&db);
    return true;
}

static bool test_import_can_create_duplicate_player(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_import_db(&db, &alpha, &beta));
    char error[512];
    uint64_t mia_id = 0;
    CHECK(db_add_player(&db, beta, "Mia", "Clarke", 75.0, true, &mia_id,
                        error, sizeof(error)));
    size_t players_before = db.player_count;

    ImportPlayerPlan home[] = {
        { .first_name = "Mia", .last_name = "Clarke", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlayerPlan away[] = {
        { .first_name = "Away", .last_name = "One", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlan plan = {
        .date = "2026-08-23",
        .home_team_name = "Alpha FC",
        .away_team_name = "Beta United",
        .home_goals = 1,
        .away_goals = 1,
        .length = 90,
        .home_players = home,
        .home_player_count = 1,
        .away_players = away,
        .away_player_count = 1,
    };
    uint64_t match_id = 0;
    CHECK(db_import_match(&db, &plan, &match_id, error, sizeof(error)));
    CHECK(db.player_count == players_before + 2);
    size_t mia_count = 0;
    const Player *duplicate =
        db_find_player_by_name(&db, "mia", "clarke", &mia_count);
    CHECK(duplicate != NULL);
    CHECK(mia_count == 2);
    const Player *existing = db_find_player(&db, mia_id);
    CHECK(existing != NULL);
    CHECK(existing->team_id == beta);
    bool new_in_alpha = false;
    for (size_t i = 0; i < db.player_count; ++i) {
        const Player *player = &db.players[i];
        if (player->id != mia_id &&
            util_ascii_iequal(player->first_name, "Mia") &&
            util_ascii_iequal(player->last_name, "Clarke") &&
            player->team_id == alpha) {
            new_in_alpha = true;
            const Appearance *appearance =
                find_import_appearance(&db, match_id, player->id);
            CHECK(appearance != NULL);
            CHECK(appearance->team_id == alpha);
        }
    }
    CHECK(new_in_alpha);
    db_free(&db);
    return true;
}

static bool test_import_moves_existing_to_new_team(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_import_db(&db, &alpha, &beta));
    char error[512];
    uint64_t mia_id = 0;
    CHECK(db_add_player(&db, beta, "Mia", "Clarke", 75.0, true, &mia_id,
                        error, sizeof(error)));

    ImportPlayerPlan home[] = {
        { .first_name = "Mia", .last_name = "Clarke", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_USE_EXISTING,
          .existing_player_id = mia_id },
    };
    ImportPlayerPlan away[] = {
        { .first_name = "Away", .last_name = "One", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlan plan = {
        .date = "2026-08-23",
        .home_team_name = "Gamma FC",
        .away_team_name = "Delta United",
        .home_goals = 1,
        .away_goals = 0,
        .length = 90,
        .home_players = home,
        .home_player_count = 1,
        .away_players = away,
        .away_player_count = 1,
    };
    uint64_t match_id = 0;
    CHECK(db_import_match(&db, &plan, &match_id, error, sizeof(error)));
    const Team *gamma = db_find_team_by_name(&db, "Gamma FC");
    const Team *delta = db_find_team_by_name(&db, "Delta United");
    CHECK(gamma != NULL);
    CHECK(delta != NULL);
    const Player *mia = db_find_player(&db, mia_id);
    CHECK(mia != NULL);
    CHECK(mia->team_id == gamma->id);
    const Match *match = db_find_match(&db, match_id);
    CHECK(match != NULL);
    CHECK(match->home_team_id == gamma->id);
    CHECK(match->away_team_id == delta->id);
    const Appearance *appearance =
        find_import_appearance(&db, match_id, mia_id);
    CHECK(appearance != NULL);
    CHECK(appearance->team_id == gamma->id);
    db_free(&db);
    return true;
}

static bool test_import_rejects_archived_existing_player(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_import_db(&db, &alpha, &beta));
    char error[512];
    uint64_t old_id = 0;
    CHECK(db_add_player(&db, beta, "Old", "Timer", 70.0, true, &old_id,
                        error, sizeof(error)));
    CHECK(db_set_player_active(&db, old_id, false, error, sizeof(error)));
    size_t matches_before = db.match_count;
    size_t players_before = db.player_count;
    size_t teams_before = db.team_count;

    ImportPlayerPlan home[] = {
        { .first_name = "Old", .last_name = "Timer", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_USE_EXISTING,
          .existing_player_id = old_id },
    };
    ImportPlayerPlan away[] = {
        { .first_name = "Away", .last_name = "One", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlan plan = {
        .date = "2026-08-23",
        .home_team_name = "Alpha FC",
        .away_team_name = "Beta United",
        .home_goals = 1,
        .away_goals = 0,
        .length = 90,
        .home_players = home,
        .home_player_count = 1,
        .away_players = away,
        .away_player_count = 1,
    };
    uint64_t match_id = 0;
    CHECK(!db_import_match(&db, &plan, &match_id, error, sizeof(error)));
    CHECK(strstr(error, "archived") != NULL);
    CHECK(db.match_count == matches_before);
    CHECK(db.player_count == players_before);
    CHECK(db.team_count == teams_before);
    CHECK(match_id == 0);
    db_free(&db);
    return true;
}

static bool test_import_rejects_reused_existing_player(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_import_db(&db, &alpha, &beta));
    char error[512];
    uint64_t mia_id = 0;
    CHECK(db_add_player(&db, beta, "Mia", "Clarke", 75.0, true, &mia_id,
                        error, sizeof(error)));

    ImportPlayerPlan home[] = {
        { .first_name = "Mia", .last_name = "Clarke", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_USE_EXISTING,
          .existing_player_id = mia_id },
        { .first_name = "Mia", .last_name = "Clarke", .minutes = 10,
          .performance = 6.0, .action = IMPORT_PLAYER_USE_EXISTING,
          .existing_player_id = mia_id },
    };
    ImportPlayerPlan away[] = {
        { .first_name = "Away", .last_name = "One", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlan plan = {
        .date = "2026-08-23",
        .home_team_name = "Alpha FC",
        .away_team_name = "Beta United",
        .home_goals = 1,
        .away_goals = 0,
        .length = 90,
        .home_players = home,
        .home_player_count = 2,
        .away_players = away,
        .away_player_count = 1,
    };
    uint64_t match_id = 0;
    CHECK(!db_import_match(&db, &plan, &match_id, error, sizeof(error)));
    CHECK(strstr(error, "more than once") != NULL);
    CHECK(db.match_count == 0);
    CHECK(db.player_count == 1);
    CHECK(mia_id != 0);
    db_free(&db);
    return true;
}

static bool test_import_validates_before_mutation(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_import_db(&db, &alpha, &beta));
    char error[512];
    size_t teams_before = db.team_count;
    size_t players_before = db.player_count;
    size_t matches_before = db.match_count;

    ImportPlayerPlan home[] = {
        { .first_name = "Home", .last_name = "One", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlayerPlan away[] = {
        { .first_name = "Away", .last_name = "One", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlan plan = {
        .date = "2026-08-23",
        .home_team_name = "New Home FC",
        .away_team_name = "New Away FC",
        .home_goals = 100,
        .away_goals = 0,
        .length = 90,
        .home_players = home,
        .home_player_count = 1,
        .away_players = away,
        .away_player_count = 1,
    };
    uint64_t match_id = 0;
    CHECK(!db_import_match(&db, &plan, &match_id, error, sizeof(error)));
    CHECK(strstr(error, "Scores") != NULL);
    CHECK(db.team_count == teams_before);
    CHECK(db.player_count == players_before);
    CHECK(db.match_count == matches_before);

    plan.home_goals = 1;
    plan.home_team_name = "Alpha FC";
    plan.away_team_name = "Beta United";
    home[0].minutes = 0;
    CHECK(!db_import_match(&db, &plan, &match_id, error, sizeof(error)));
    CHECK(strstr(error, "Minutes") != NULL);
    CHECK(db.team_count == teams_before);
    CHECK(db.player_count == players_before);
    CHECK(db.match_count == matches_before);

    home[0].minutes = 90;
    plan.length = 45;
    CHECK(!db_import_match(&db, &plan, &match_id, error, sizeof(error)));
    CHECK(strstr(error, "final whistle") != NULL);
    CHECK(db.team_count == teams_before);
    CHECK(db.player_count == players_before);
    CHECK(db.match_count == matches_before);
    db_free(&db);
    return true;
}

static bool test_import_rejects_mismatched_existing_name(void)
{
    Database db;
    uint64_t alpha = 0;
    uint64_t beta = 0;
    CHECK(setup_import_db(&db, &alpha, &beta));
    char error[512];
    uint64_t mia_id = 0;
    CHECK(db_add_player(&db, beta, "Mia", "Clarke", 75.0, true, &mia_id,
                        error, sizeof(error)));

    ImportPlayerPlan home[] = {
        { .first_name = "Wrong", .last_name = "Name", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_USE_EXISTING,
          .existing_player_id = mia_id },
    };
    ImportPlayerPlan away[] = {
        { .first_name = "Away", .last_name = "One", .minutes = 90,
          .performance = 6.0, .action = IMPORT_PLAYER_CREATE },
    };
    ImportPlan plan = {
        .date = "2026-08-23",
        .home_team_name = "Alpha FC",
        .away_team_name = "Beta United",
        .home_goals = 1,
        .away_goals = 0,
        .length = 90,
        .home_players = home,
        .home_player_count = 1,
        .away_players = away,
        .away_player_count = 1,
    };
    uint64_t match_id = 0;
    CHECK(!db_import_match(&db, &plan, &match_id, error, sizeof(error)));
    CHECK(strstr(error, "matches") != NULL);
    CHECK(db.match_count == 0);
    CHECK(db.player_count == 1);
    db_free(&db);
    return true;
}

static bool test_parse_card_after_substitution_is_not_sending_off(void)
{
    const char *html =
        "<script>matchCentreData: {"
        "\"startDate\":\"2026-08-23\","
        "\"score\":\"0 : 1\","
        "\"maxMinute\":94,"
        "\"expandedMaxMinute\":97,"
        "\"periodEndMinutes\":{\"1\":48,\"2\":94},"
        "\"events\":["
        "{\"type\":{\"displayName\":\"Card\"},\"playerId\":10,\"minute\":85,"
        "\"qualifiers\":[{\"type\":{\"displayName\":\"Red\",\"value\":33}}]},"
        "{\"type\":{\"displayName\":\"Card\"},\"playerId\":11,\"minute\":85,"
        "\"qualifiers\":[{\"type\":{\"displayName\":\"Red\",\"value\":33}}]}],"
        "\"home\":{\"name\":\"Rennes\",\"players\":["
        "{\"name\":\"Starter Replaced\",\"playerId\":10,\"isFirstEleven\":true,\"subbedOutExpandedMinute\":63},"
        "{\"name\":\"Sub Replaced\",\"playerId\":11,\"isFirstEleven\":false,\"subbedInExpandedMinute\":63,\"subbedOutExpandedMinute\":83}"
        "]},"
        "\"away\":{\"name\":\"PSG\",\"players\":["
        "{\"name\":\"Away Full Match\",\"isFirstEleven\":true}]}"
        "}</script>";
    ImportedMatch match;
    char error[512];
    CHECK(imported_match_parse_html(html, strlen(html), &match,
                                    error, sizeof(error)));
    CHECK(match.length == 94);
    CHECK(match.home_player_count == 2);
    /* Both players had already been replaced when they were shown the card. */
    CHECK(match.home_players[0].minutes == 60);
    CHECK(match.home_players[0].sent_off == 0);
    CHECK(match.home_players[1].minutes == 20);
    CHECK(match.home_players[1].sent_off == 0);
    imported_match_free(&match);
    return true;
}

static bool test_parse_same_minute_substitution(void)
{
    const char *html =
        "<script>matchCentreData: {"
        "\"startDate\":\"2026-08-23\","
        "\"score\":\"0 : 1\","
        "\"maxMinute\":94,"
        "\"expandedMaxMinute\":97,"
        "\"periodEndMinutes\":{\"1\":48,\"2\":94},"
        "\"home\":{\"name\":\"Rennes\",\"players\":["
        "{\"name\":\"Quick Sub\",\"isFirstEleven\":false,\"subbedInExpandedMinute\":73,\"subbedOutExpandedMinute\":73}"
        "]},"
        "\"away\":{\"name\":\"PSG\",\"players\":["
        "{\"name\":\"Away Full Match\",\"isFirstEleven\":true}]}"
        "}</script>";
    ImportedMatch match;
    char error[512];
    CHECK(imported_match_parse_html(html, strlen(html), &match,
                                    error, sizeof(error)));
    CHECK(match.home_player_count == 1);
    CHECK(match.home_players[0].minutes == 1);
    imported_match_free(&match);
    return true;
}

static bool test_parse_large_player_id_keeps_sending_off(void)
{
    const char *html =
        "<script>matchCentreData: {"
        "\"startDate\":\"2026-08-23\","
        "\"score\":\"0 : 1\","
        "\"maxMinute\":94,"
        "\"expandedMaxMinute\":97,"
        "\"periodEndMinutes\":{\"1\":48,\"2\":94},"
        "\"events\":["
        "{\"type\":{\"displayName\":\"Card\"},\"playerId\":1000001,\"minute\":30,"
        "\"qualifiers\":[{\"type\":{\"displayName\":\"Red\",\"value\":33}}]}],"
        "\"home\":{\"name\":\"Rennes\",\"players\":["
        "{\"name\":\"Big Id\",\"playerId\":1000001,\"isFirstEleven\":true}"
        "]},"
        "\"away\":{\"name\":\"PSG\",\"players\":["
        "{\"name\":\"Away Full Match\",\"isFirstEleven\":true}]}"
        "}</script>";
    ImportedMatch match;
    char error[512];
    CHECK(imported_match_parse_html(html, strlen(html), &match,
                                    error, sizeof(error)));
    CHECK(match.home_player_count == 1);
    CHECK(match.home_players[0].minutes == 30);
    CHECK(match.home_players[0].sent_off == 30);
    imported_match_free(&match);
    return true;
}

const TestCase g_import_tests[] = {
    TEST_CASE(test_parse_match_page),
    TEST_CASE(test_parse_score_fallback_and_extra_time),
    TEST_CASE(test_parse_match_length_with_stoppage_time),
    TEST_CASE(test_parse_match_length_falls_back_to_period_end),
    TEST_CASE(test_parse_sending_off_minutes),
    TEST_CASE(test_parse_escaped_names),
    TEST_CASE(test_parse_rejects_bad_pages),
    TEST_CASE(test_find_team_by_name),
    TEST_CASE(test_import_creates_players_and_match),
    TEST_CASE(test_import_preserves_sending_off),
    TEST_CASE(test_import_creates_missing_teams),
    TEST_CASE(test_import_can_move_existing_player),
    TEST_CASE(test_import_moves_existing_to_new_team),
    TEST_CASE(test_import_can_create_duplicate_player),
    TEST_CASE(test_import_rejects_archived_existing_player),
    TEST_CASE(test_import_rejects_reused_existing_player),
    TEST_CASE(test_import_validates_before_mutation),
    TEST_CASE(test_import_rejects_mismatched_existing_name),
    TEST_CASE(test_parse_card_after_substitution_is_not_sending_off),
    TEST_CASE(test_parse_same_minute_substitution),
    TEST_CASE(test_parse_large_player_id_keeps_sending_off)
};

const size_t g_import_test_count =
    sizeof(g_import_tests) / sizeof(g_import_tests[0]);
