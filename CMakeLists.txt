cmake_minimum_required(VERSION 3.21)

project(ElevenLedger
    VERSION 2.0.4
    DESCRIPTION "Football player rating calculator for Windows"
    LANGUAGES C RC
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(ELEVENLEDGER_BUILD_TESTS "Build the automated test suite" ON)
option(ELEVENLEDGER_BUILD_TOOLS "Build developer data tools" ON)

if(NOT WIN32)
    message(FATAL_ERROR "ElevenLedger is a Windows desktop application.")
endif()

if(CMAKE_SIZEOF_VOID_P LESS 8)
    message(FATAL_ERROR "ElevenLedger requires a 64-bit build.")
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

configure_file(
    resources/app.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc
    @ONLY
)

add_library(elevenledger_core STATIC
    src/core/database.c
    src/core/import.c
    src/core/import_apply.c
    src/core/rating.c
    src/core/storage.c
    src/core/util.c
)
target_include_directories(elevenledger_core PUBLIC src)
target_link_libraries(elevenledger_core PUBLIC shell32)
target_compile_definitions(elevenledger_core PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)
target_compile_options(elevenledger_core PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wshadow>
)

add_executable(elevenledger WIN32
    src/main.c
    src/app.c
    src/ui/theme.c
    src/ui/controls.c
    src/ui/page.c
    src/ui/page_dashboard.c
    src/ui/page_players.c
    src/ui/page_teams.c
    src/ui/page_matches.c
    src/ui/page_guide.c
    src/ui/dialogs.c
    src/ui/dialog_match.c
    src/ui/dialog_import.c
    src/ui/main_window.c
    ${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc
)
target_include_directories(elevenledger PRIVATE
    src
    resources
)
target_link_libraries(elevenledger PRIVATE
    elevenledger_core
    comctl32
    gdi32
    user32
    shell32
    uxtheme
)
target_compile_definitions(elevenledger PRIVATE
    ELEVENLEDGER_VERSION="${PROJECT_VERSION}"
)
target_compile_options(elevenledger PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wshadow>
)
if(MSVC)
    target_compile_options(elevenledger PRIVATE /W4)
    target_link_options(elevenledger PRIVATE /SUBSYSTEM:WINDOWS)
else()
    target_link_options(elevenledger PRIVATE -static -static-libgcc)
endif()

if(ELEVENLEDGER_BUILD_TOOLS)
    add_executable(elevenledger_demo_seed tools/demo_seed.c)
    target_link_libraries(elevenledger_demo_seed PRIVATE elevenledger_core)
    target_compile_options(elevenledger_demo_seed PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
    )
endif()

if(ELEVENLEDGER_BUILD_TESTS)
    enable_testing()
    add_executable(elevenledger_tests
        tests/test_main.c
        tests/test_util.c
        tests/test_rating.c
        tests/test_database.c
        tests/test_storage.c
        tests/test_import.c
    )
    target_link_libraries(elevenledger_tests PRIVATE elevenledger_core)
    target_compile_options(elevenledger_tests PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wshadow>
    )
    add_test(NAME elevenledger_tests COMMAND elevenledger_tests)
endif()

install(TARGETS elevenledger RUNTIME DESTINATION .)
