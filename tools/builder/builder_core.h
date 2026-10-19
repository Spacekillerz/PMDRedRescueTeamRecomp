// Turns the player's own cartridge dump into a playable native binary.
//
// Adapted from BoktaiRecomp's builder (tools/builder/), Copyright (c) 2026 Shy
// (github.com/Shy), licensed under the PolyForm Noncommercial License 1.0.0.
//
// A recompiled binary is translated cartridge code and is never distributed.
// The builder is: it carries no ROM-derived code, compiles the player's copy on
// first launch, then hands over to it on every later launch.
//
// Two invariants:
//  - The ROM and BIOS are read where they are; only their paths reach the
//    recompiler, so nothing copyrighted is duplicated by us.
//  - The build runs in a writable copy of the sources: a Flatpak's /app is
//    read-only, and the build writes generated code into the source tree.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace pmd::builder {

struct GameSpec {
    const char* title;        // shown to the player
    const char* target;       // CMake target and binary name
    const char* data_dir;     // per-user directory name
    const char* rom_sha1;
    const char* bios_sha1;
    const char* game_toml;    // relative to the source root
    const char* save_name;
    // Bump whenever the sources change in a way a player must recompile to get:
    // the stamp only compares this, the ROM and the BIOS.
    const char* app_version;
};

const GameSpec& game_spec();

struct Paths {
    std::string src_root;   // shipped sources, read-only
    std::string data_root;  // per-user and writable; ~/.var/app/<id>/data/... in a Flatpak
    std::string toolchain;  // bundled compiler (Windows zip); empty when the host provides one

    std::string work_src() const;
    std::string game_binary() const;
    std::string saves_dir() const;
    std::string stamp_file() const;
    std::string log_file() const;
};

Paths default_paths(const char* argv0);

// Puts the bundled toolchain and SDL2 first on PATH. Done once at startup, so
// both the build and the game it execs (whose self-heal tier compiles with the
// same gcc, and which loads SDL2.dll) see them.
void use_bundled_toolchain(const Paths& p);

struct Assets {
    std::string rom;
    std::string bios;
};

enum class Step { CopySources, Bios, Cartridge, Configure, Compile, Done };

struct Progress {
    Step        step = Step::CopySources;
    float       fraction = 0.0f;  // overall, monotonic
    std::string headline;
    std::string detail;
};

using ProgressFn = std::function<void(const Progress&)>;

struct Preflight {
    bool        ok = false;
    std::string error;   // one sentence for the player
    std::string remedy;  // a command to run, or empty
};

// Cheap checks that can fail, run before the multi-minute compile.
Preflight preflight(const Paths& p, const Assets& a);

// `actual_hex` is filled whenever the file could be read, so a mismatch can
// show the real digest.
bool verify_sha1(const std::string& path, const char* want_hex, std::string& actual_hex);

// Incremental and safe to re-run. On failure `err` is one sentence; the full
// transcript is always in p.log_file().
bool build(const Paths& p, const Assets& a, const ProgressFn& on_progress, std::string& err);

void stamp_write(const Paths& p, const Assets& a);
bool stamp_ok(const Paths& p, const Assets& a);
bool stamp_read_assets(const Paths& p, Assets& out);
bool ready_to_play(const Paths& p, const Assets& a);

// The game's command line. Every path is absolute because the working directory
// of a sandboxed app launched from Steam is unspecified.
std::vector<std::string> game_args(const Paths& p, const Assets& a,
                                   const std::vector<std::string>& passthrough);

// "[412/1180] Building CXX object .../recompiled_07.cpp.o" -> 412, 1180, file.
bool parse_ninja_line(const std::string& line, int& done, int& total, std::string& file);

}  // namespace pmd::builder
