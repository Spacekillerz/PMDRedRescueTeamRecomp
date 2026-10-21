cmake_minimum_required(VERSION 3.20)
project(PMDRedRescueTeamRecomp C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(GBARECOMP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/gbarecomp" CACHE PATH "gbarecomp checkout")
set(RECOMP_UI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/recomp-ui" CACHE PATH "recomp-ui checkout")
set(GBARECOMP_RUNTIME_UI_ROOT "${RECOMP_UI_ROOT}" CACHE PATH "recomp-ui root used by the runtime")
set(GBARECOMP_GENERATED_BIOS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated_bios"
    CACHE PATH "BIOS recompilation output, kept out of the gbarecomp submodule")

# Option names follow the gbarecomp game repos, so the shared packaging
# templates drive this project unchanged.
option(GBAGAME_RECOMP_UI "Build the recomp-ui launcher and in-game menu" ON)
option(GBAGAME_BUILDER "Build the first-run builder app" ON)
option(GBAGAME_BUILDER_ONLY "Build only the builder: no game target, no ROM needed" OFF)

if(NOT EXISTS "${GBARECOMP_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "gbarecomp not found at ${GBARECOMP_ROOT}; run: git submodule update --init --recursive")
endif()

add_subdirectory("${GBARECOMP_ROOT}" gbarecomp_build EXCLUDE_FROM_ALL)

if(GBAGAME_RECOMP_UI OR GBAGAME_BUILDER)
    find_package(SDL2 REQUIRED)
    set(SDL2_INCLUDE_DIR "${SDL2_INCLUDE_DIRS}")
    include("${RECOMP_UI_ROOT}/recomp_ui.cmake")
endif()

# ---- the game: ROM-derived, never distributed ----------------------------------
if(NOT GBAGAME_BUILDER_ONLY)
    file(GLOB GAME_GENERATED_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/*.cpp")
    if(NOT GAME_GENERATED_SOURCES)
        message(FATAL_ERROR "generated/ is empty: run scripts/regen.sh, or configure "
                            "with -DGBAGAME_BUILDER_ONLY=ON to build only the builder")
    endif()

    set(GAME_TARGET PMDRedRescueTeamRecomp)
    add_executable(${GAME_TARGET} src/main.cpp ${GAME_GENERATED_SOURCES})

    # The recompiled shards are huge machine-written translation units: -O1 keeps
    # compile time and memory bounded. Blocks chain through `fn(); return;`, which
    # must become a jump or a long guest loop overflows the host stack; GCC only
    # enables sibling calls from -O2, hence the explicit flag.
    set(GAME_RECOMPILED_SOURCES ${GAME_GENERATED_SOURCES})
    list(FILTER GAME_RECOMPILED_SOURCES INCLUDE REGEX "/recompiled_[0-9]+\\.cpp$")
    if(GAME_RECOMPILED_SOURCES)
        set_source_files_properties(${GAME_RECOMPILED_SOURCES} PROPERTIES
            COMPILE_OPTIONS "-O1;-foptimize-sibling-calls;-g0;-fno-exceptions;-fno-rtti;-fno-strict-aliasing;-fno-math-errno")
    endif()

    target_include_directories(${GAME_TARGET} PRIVATE
        "${GBARECOMP_ROOT}/src/armv4t"
        "${GBARECOMP_ROOT}/src/gba"
        "${GBARECOMP_ROOT}/src/runtime"
        "${GBARECOMP_ROOT}/src/debug")

    target_link_gbarecomp_runtime_stack(${GAME_TARGET})

    target_compile_definitions(${GAME_TARGET} PRIVATE
        GBARECOMP_DEFAULT_DEBUG_PORT=19895
        GBARECOMP_WINDOW_TITLE="Pokemon Mystery Dungeon: Red Rescue Team"
        GBARECOMP_DEFAULT_GAME_CONFIG="game.toml")

    # The gbarecomp runtime's in-game menu needs the ImGui core this target brings.
    if(GBAGAME_RECOMP_UI)
        add_library(game_launcher_ui STATIC src/game_launcher_boot.cpp)
        target_link_libraries(game_launcher_ui PUBLIC SDL2::SDL2)
        set_target_properties(game_launcher_ui PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_include_directories(game_launcher_ui PUBLIC
            "${GBARECOMP_ROOT}/src/runtime"
            "${GBARECOMP_ROOT}/src/gba"
            src)
        recomp_target_launcher_ui(game_launcher_ui CONSOLE gba)
        target_link_libraries(${GAME_TARGET} PRIVATE game_launcher_ui)
        target_compile_definitions(${GAME_TARGET} PRIVATE PMD_RECOMP_UI)
    endif()

    if(MINGW)
        target_link_options(${GAME_TARGET} PRIVATE -Wl,--stack,16777216)
    elseif(NOT MSVC)
        target_link_options(${GAME_TARGET} PRIVATE -Wl,-z,stack-size=16777216)
    endif()

    # The runtime reads config.ini from the executable's directory, and the
    # launcher rewrites it there: seed it once, never overwrite.
    if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/config.ini")
        configure_file(config/config.ini "${CMAKE_CURRENT_BINARY_DIR}/config.ini" COPYONLY)
    endif()
endif()

# ---- the builder: ROM-free, the published artifact --------------------------------
if(GBAGAME_BUILDER)
    find_package(Threads REQUIRED)

    add_executable(PMDRedRescueTeamBuilder
        tools/builder/builder_main.cpp
        tools/builder/builder_core.cpp
        tools/builder/asset_scan.cpp)
    target_include_directories(PMDRedRescueTeamBuilder PRIVATE tools/builder)
    target_link_libraries(PMDRedRescueTeamBuilder PRIVATE SDL2::SDL2 Threads::Threads)
    # Brings Dear ImGui, tinyfiledialogs, sha1.c and the PNG capture used by --shot.
    recomp_target_launcher_ui(PMDRedRescueTeamBuilder CONSOLE gba)

    add_executable(test_builder_core
        tools/builder/test_builder_core.cpp
        tools/builder/builder_core.cpp
        tools/builder/asset_scan.cpp
        "${RECOMP_UI_ROOT}/src/common/sha1.c")
    target_include_directories(test_builder_core PRIVATE
        tools/builder
        "${RECOMP_UI_ROOT}/src"
        "${RECOMP_UI_ROOT}/src/common")
    target_link_libraries(test_builder_core PRIVATE Threads::Threads)
    enable_testing()
    add_test(NAME builder_core COMMAND test_builder_core)
endif()
