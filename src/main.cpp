#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "runtime.h"

#if defined(PMD_RECOMP_UI)
#include "game_launcher_boot.h"
#endif

namespace {

constexpr const char* kPmdRedUsaSha1 = "9f4cfc5b5f4859d17169a485462e977c7aac2b89";

}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf(
                "PMDRedRescueTeamRecomp --bios <gba_bios.bin> --rom <pmd_red.gba> [options] [game.toml]\n\n"
                "  --no-launcher   skip the launcher once\n"
                "  --fullscreen    start in fullscreen\n"
                "  --no-window     headless run (with --frames N)\n");
            return 0;
        }
    }

    gbarecomp::RunOptions opts;
    opts.builtin_game_name = "Pokemon Mystery Dungeon: Red Rescue Team";
    opts.builtin_rom_sha1 = kPmdRedUsaSha1;
    opts.launcher_region = "USA / Australia";
    opts.launcher_game_config = "game.toml";
    opts.launcher_save_path = "saves/pmd_red_rescue_team.sav";
    opts.freely_resizable_window = true;

    std::vector<std::string> args(argv, argv + argc);

#if defined(PMD_RECOMP_UI)
    if (game_launcher_preboot(args, opts)) return 0;
#endif

    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& arg : args) av.push_back(arg.data());
    return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
}
