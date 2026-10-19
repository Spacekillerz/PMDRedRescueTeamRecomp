// The builder's pure pieces. Each fails quietly when wrong: a bad ninja parse
// freezes the progress bar (a hang, on a handheld), a lax SHA-1 gate lets a bad
// dump through to a crash minutes later, and a relative game argument breaks
// only when Steam launches the app from an unexpected directory.
//
// Adapted from BoktaiRecomp's builder (tools/builder/), Copyright (c) 2026 Shy
// (github.com/Shy), licensed under the PolyForm Noncommercial License 1.0.0.

#include "asset_scan.h"
#include "builder_core.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace pmd::builder;
namespace fs = std::filesystem;

namespace {

int failures = 0;

void check(bool cond, const std::string& what) {
    std::printf("  %-58s %s\n", what.c_str(), cond ? "ok" : "FAIL");
    if (!cond) ++failures;
}

void expect_ninja(const std::string& line, int want_done, int want_total, const std::string& want_file) {
    int d = -1, t = -1;
    std::string f = "unset";
    const bool got = parse_ninja_line(line, d, t, f);
    check(got && d == want_done && t == want_total && f == want_file, "\"" + line.substr(0, 42) + "\"");
    if (got && (d != want_done || t != want_total || f != want_file))
        std::printf("      got %d/%d file=\"%s\"\n", d, t, f.c_str());
}

void expect_not_ninja(const std::string& line) {
    int d = 0, t = 0;
    std::string f;
    check(!parse_ninja_line(line, d, t, f), "rejects \"" + line.substr(0, 40) + "\"");
}

void write_file(const fs::path& p, std::size_t size) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    for (std::size_t i = 0; i < size; ++i) f.put(static_cast<char>(i * 31));
}

}  // namespace

int main() {
    std::printf("parse_ninja_line - accepts real ninja output\n");
    expect_ninja("[412/1180] Building CXX object CMakeFiles/PMDRedRescueTeamRecomp.dir/"
                 "generated/recompiled_07.cpp.o",
                 412, 1180, "recompiled_07.cpp.o");
    expect_ninja("[1/1] Linking CXX executable PMDRedRescueTeamRecomp", 1, 1, "");
    expect_ninja("[3/4] ", 3, 4, "");

    std::printf("\nparse_ninja_line - rejects everything else\n");
    expect_not_ninja("");
    expect_not_ninja("ninja: no work to do.");
    expect_not_ninja("-- Configuring done");
    expect_not_ninja("[abc/def] nonsense");
    expect_not_ninja("[0/0] zero total would divide by zero");
    expect_not_ninja("[5] no slash");
    expect_not_ninja("no bracket at all 1/2");
    expect_not_ninja("[-Wunused-variable] warning here");

    std::printf("\nverify_sha1 - the pre-build asset gate\n");
    const fs::path tmp = fs::temp_directory_path() / "pmd_builder_test";
    fs::remove_all(tmp);
    {
        const fs::path probe = tmp / "abc";
        fs::create_directories(tmp);
        { std::ofstream f(probe, std::ios::binary); f << "abc"; }
        std::string got;
        const char* kAbc = "a9993e364706816aba3e25717850c26c9cd0d89d";
        check(verify_sha1(probe.string(), kAbc, got), "matches a known digest");
        check(got == kAbc, "reports the digest it computed");
        check(!verify_sha1(probe.string(), "0000000000000000000000000000000000000000", got),
              "rejects a mismatch");
        check(got == kAbc, "still reports the real digest on mismatch");
        fs::remove(probe);
        check(!verify_sha1(probe.string(), kAbc, got), "rejects a missing file");
        check(got.empty(), "reports no digest for a missing file");
    }

    std::printf("\nscan_for_assets - only exact dumps are picked\n");
    {
        write_file(tmp / "lib" / "gba" / "decoy.gba", 32u * 1024 * 1024);
        write_file(tmp / "lib" / "bios" / "gba_bios.bin", 16u * 1024);
        write_file(tmp / "lib" / "small.gba", 1024);
        Assets found;
        scan_for_assets({(tmp / "lib").string(), (tmp / "missing").string()}, found);
        check(found.rom.empty(), "ignores a ROM-sized file with another hash");
        check(found.bios.empty(), "ignores a BIOS-sized file with another hash");

        Assets kept{"/given/rom.gba", "/given/bios.bin"};
        scan_for_assets({(tmp / "lib").string()}, kept);
        check(kept.rom == "/given/rom.gba" && kept.bios == "/given/bios.bin",
              "keeps assets that were already chosen");
    }

    std::printf("\ngame_args - absolute paths, config last\n");
    {
        const fs::path base = fs::absolute(tmp);
        Paths p;
        p.src_root = (base / "sources").string();
        p.data_root = (base / "data").string();
        const auto args = game_args(p, {(base / "pmd.gba").string(), (base / "gba_bios.bin").string()},
                                    {"--fullscreen"});
        bool all_abs = true;
        for (std::size_t i = 1; i + 2 < args.size(); i += 2)
            if (!fs::path(args[i]).is_absolute()) all_abs = false;
        check(all_abs, "every path argument is absolute");
        check(!args.empty() && fs::path(args.back()) == fs::path(p.work_src()) / "game.toml",
              "game.toml comes last");
        check(args.size() >= 2 && args[args.size() - 2] == "--fullscreen", "passes extra arguments through");
        check(fs::path(args[5]) == fs::path(p.data_root) / "saves" / "pmd_red_rescue_team.sav",
              "saves live in the data directory");
    }

    fs::remove_all(tmp);
    std::printf("\n%s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}
