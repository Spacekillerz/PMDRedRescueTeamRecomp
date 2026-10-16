// The program a player launches: a wrapper first, a builder second.
//
//   launch: if a built game exists and the stamp matches -> exec it
//           else -> find the ROM/BIOS, build with a progress bar, then exec it
//
// So Steam tracks one entry, and Gaming Mode never sees a missing binary.
//
// Adapted from BoktaiRecomp's builder (tools/builder/), Copyright (c) 2026 Shy
// (github.com/Shy), licensed under the PolyForm Noncommercial License 1.0.0.
//
// The window draws with ImGui's built-in font and loads no asset files, so a
// broken install can never block a build. That font is ASCII-only: keep every
// string drawn here ASCII.

#include "asset_scan.h"
#include "builder_core.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <unistd.h>

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_sdl2.h"
#include "launcher_debug.h"
#include "third_party/tinyfiledialogs.h"

using namespace pmd::builder;

namespace {

// Replaces the process so Steam keeps tracking the same pid.
[[noreturn]] void exec_game(const Paths& paths, const Assets& assets,
                            const std::vector<std::string>& passthrough) {
    const std::string binary = paths.game_binary();
    std::vector<std::string> args = game_args(paths, assets, passthrough);
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(binary.c_str()));
    for (auto& s : args) argv.push_back(s.data());
    argv.push_back(nullptr);

    std::error_code ec;
    std::filesystem::create_directories(paths.saves_dir(), ec);
    // The runtime writes its coverage report and self-heal cache to the
    // working directory.
    if (chdir(paths.data_root.c_str()) != 0)
        std::fprintf(stderr, "chdir %s: %s\n", paths.data_root.c_str(), std::strerror(errno));
    execv(binary.c_str(), argv.data());
    std::fprintf(stderr, "could not start %s: %s\n", binary.c_str(), std::strerror(errno));
    std::exit(1);
}

struct Shared {
    std::mutex m;
    Progress progress;
    std::string error;
    std::string remedy;
    Assets scanned;
    std::atomic<bool> scanning{false};
    std::atomic<bool> scan_done{false};
    std::atomic<bool> running{false};
    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};
};

void start_scan(Shared& sh, const Assets& have) {
    sh.scanning = true;
    sh.scan_done = false;
    std::thread([&sh, have] {
        Assets found = have;
        scan_for_assets(default_search_roots(), found);
        {
            std::lock_guard<std::mutex> lk(sh.m);
            sh.scanned = found;
        }
        sh.scan_done = true;
        sh.scanning = false;
    }).detach();
}

void start_build(Shared& sh, const Paths& paths, const Assets& assets) {
    sh.running = true;
    sh.done = false;
    {
        std::lock_guard<std::mutex> lk(sh.m);
        sh.error.clear();
        sh.remedy.clear();
    }
    std::thread([&sh, paths, assets] {
        const Preflight pf = preflight(paths, assets);
        if (!pf.ok) {
            std::lock_guard<std::mutex> lk(sh.m);
            sh.error = pf.error;
            sh.remedy = pf.remedy;
            sh.ok = false;
            sh.done = true;
            sh.running = false;
            return;
        }
        std::string err;
        const bool ok = build(paths, assets, [&sh](const Progress& pr) {
            std::lock_guard<std::mutex> lk(sh.m);
            sh.progress = pr;
        }, err);
        {
            std::lock_guard<std::mutex> lk(sh.m);
            if (!ok) sh.error = err;
        }
        sh.ok = ok;
        sh.done = true;
        sh.running = false;
    }).detach();
}

std::string pick(const char* title, const char* const* patterns, int n, const char* desc) {
    const char* r = tinyfd_openFileDialog(title, "", n, patterns, desc, 0);
    return r ? std::string(r) : std::string();
}

struct AssetStatus {
    bool ok = false;
    std::string line;
};

AssetStatus check_asset(const std::string& path, const char* want, const char* what) {
    AssetStatus s;
    if (path.empty()) {
        s.line = std::string("No ") + what + " found.";
        return s;
    }
    std::string got;
    if (verify_sha1(path, want, got)) {
        s.ok = true;
        s.line = "Verified.";
    } else if (got.empty()) {
        s.line = "Could not read that file.";
    } else {
        s.line = "Wrong file - SHA-1 " + got.substr(0, 16) + "...";
    }
    return s;
}

// --shot: render the window and save the real framebuffer, the only way to
// test the screen a player sees before anything is built.
struct SelfTest {
    std::string png;
    int frames = 20;
    bool building = false;
};

int run_gui(const Paths& paths, Assets assets, const std::vector<std::string>& passthrough,
            const SelfTest& selftest) {
    const GameSpec& spec = game_spec();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // 1280x800 is the Steam Deck's panel.
    SDL_Window* win = SDL_CreateWindow("Pokemon Mystery Dungeon: Red Rescue Team - setup",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 800,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI |
                                           SDL_WINDOW_RESIZABLE);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    if (!gl) {
        std::fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(1);

    // Gaming Mode has no mouse: every control must be reachable with the pad.
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i)) SDL_GameControllerOpen(i);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    // HiDPI ratio only; the floor keeps text readable on the Deck's 7" panel.
    {
        int ww = 0, dw = 0, wh = 0, dh = 0;
        SDL_GetWindowSize(win, &ww, &wh);
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        const float hidpi = (ww > 0) ? (float)dw / (float)ww : 1.0f;
        io.FontGlobalScale = std::clamp(hidpi, 1.6f, 2.4f);
    }
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 0.0f;
    st.FrameRounding = 4.0f;
    st.FramePadding = ImVec2(10, 7);
    st.ItemSpacing = ImVec2(10, 9);
    st.ScaleAllSizes(io.FontGlobalScale);
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init("#version 330");

    Shared sh;
    bool quit = false, finished = false, selftest_ok = false;
    int frame_count = 0;

    const char* kRomPatterns[] = {"*.gba", "*.agb", "*.bin"};
    const char* kBiosPatterns[] = {"*.bin", "*.rom"};

    AssetStatus rom_st = check_asset(assets.rom, spec.rom_sha1, "ROM");
    AssetStatus bios_st = check_asset(assets.bios, spec.bios_sha1, "BIOS");
    if (!rom_st.ok || !bios_st.ok) {
        Assets keep;
        if (rom_st.ok) keep.rom = assets.rom;
        if (bios_st.ok) keep.bios = assets.bios;
        start_scan(sh, keep);
    } else if (selftest.building) {
        start_build(sh, paths, assets);
    }

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_CONTROLLERDEVICEADDED) SDL_GameControllerOpen(e.cdevice.which);
            if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE &&
                e.window.windowID == SDL_GetWindowID(win))
                quit = true;
        }
        // Quitting mid-build would waste the whole wait for nothing.
        if (quit && sh.running) quit = false;

        if (sh.scan_done.exchange(false)) {
            std::lock_guard<std::mutex> lk(sh.m);
            assets = sh.scanned;
            rom_st = check_asset(assets.rom, spec.rom_sha1, "ROM");
            bios_st = check_asset(assets.bios, spec.bios_sha1, "BIOS");
            if (selftest.building && rom_st.ok && bios_st.ok) start_build(sh, paths, assets);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("builder", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

        Progress pr;
        std::string err, remedy;
        {
            std::lock_guard<std::mutex> lk(sh.m);
            pr = sh.progress;
            err = sh.error;
            remedy = sh.remedy;
        }
        const ImVec4 kGood(0.45f, 0.85f, 0.50f, 1.0f);
        const ImVec4 kBad(0.95f, 0.55f, 0.45f, 1.0f);

        if (sh.done && sh.ok) {
            ImGui::TextUnformatted("The game is built.");
            ImGui::Spacing();
            ImGui::TextWrapped("This only happens once. Next time, the game starts directly.");
            ImGui::Spacing();
            if (ImGui::Button("Play")) finished = true;
            ImGui::SetItemDefaultFocus();
        } else if (sh.running) {
            ImGui::TextUnformatted(pr.headline.c_str());
            ImGui::Spacing();
            ImGui::ProgressBar(pr.fraction, ImVec2(-1, 0));
            ImGui::Spacing();
            ImGui::TextDisabled("%s", pr.detail.c_str());
            ImGui::Spacing();
            ImGui::TextWrapped("Turning a Game Boy Advance game into a native one takes a few "
                               "minutes. You only wait for this once.");
        } else if (sh.scanning) {
            ImGui::TextUnformatted("Looking for your ROM and BIOS...");
            ImGui::Spacing();
            ImGui::TextWrapped("Searching ~/Emulation (EmuDeck), Downloads, your home folder "
                               "and SD cards.");
        } else {
            ImGui::TextUnformatted("Pokemon Mystery Dungeon: Red Rescue Team needs to be built "
                                   "once on this machine.");
            ImGui::Spacing();
            ImGui::TextWrapped("This app contains no game data. It uses your own ROM and Game "
                               "Boy Advance BIOS, reads them where they are, and never copies "
                               "them.");
            ImGui::Separator();
            ImGui::Spacing();

            auto asset_row = [&](const char* label, std::string& path, AssetStatus& status,
                                 const char* const* pats, int npats, const char* desc,
                                 const char* title, const char* want, const char* what) {
                ImGui::TextUnformatted(label);
                ImGui::SameLine();
                ImGui::TextColored(status.ok ? kGood : kBad, "%s", status.line.c_str());
                if (!path.empty()) ImGui::TextDisabled("%s", path.c_str());
                if (ImGui::Button((std::string("Choose file...##") + label).c_str())) {
                    std::string picked = pick(title, pats, npats, desc);
                    if (!picked.empty()) {
                        path = picked;
                        status = check_asset(path, want, what);
                    }
                }
                ImGui::Spacing();
            };

            asset_row("ROM:", assets.rom, rom_st, kRomPatterns, 3, "Game Boy Advance ROM",
                      "Select Pokemon Mystery Dungeon: Red Rescue Team (USA)", spec.rom_sha1, "ROM");
            asset_row("BIOS:", assets.bios, bios_st, kBiosPatterns, 2, "Game Boy Advance BIOS",
                      "Select your Game Boy Advance BIOS (gba_bios.bin)", spec.bios_sha1, "BIOS");

            if (!rom_st.ok || !bios_st.ok) {
                ImGui::TextWrapped("Expected: Pokemon Mystery Dungeon - Red Rescue Team (USA, "
                                   "Australia).gba and gba_bios.bin. With EmuDeck, put them in "
                                   "Emulation/roms/gba and Emulation/bios, then search again.");
                ImGui::Spacing();
            }

            if (!err.empty()) {
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextColored(kBad, "%s", err.c_str());
                if (!remedy.empty()) {
                    ImGui::Spacing();
                    ImGui::TextWrapped("Run this in a terminal, then start this app again:");
                    ImGui::InputText("##remedy", remedy.data(), remedy.size() + 1,
                                     ImGuiInputTextFlags_ReadOnly);
                }
                ImGui::Spacing();
                ImGui::TextDisabled("Full log: %s", paths.log_file().c_str());
            }

            ImGui::Separator();
            ImGui::Spacing();
            const bool can = rom_st.ok && bios_st.ok;
            if (!can) ImGui::BeginDisabled();
            if (ImGui::Button("Build and play")) start_build(sh, paths, assets);
            if (can) ImGui::SetItemDefaultFocus();
            if (!can) ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Search again")) {
                Assets keep;
                if (rom_st.ok) keep.rom = assets.rom;
                if (bios_st.ok) keep.bios = assets.bios;
                start_scan(sh, keep);
            }
            if (!can) ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Quit")) quit = true;
        }

        ImGui::End();

        int w = 0, h = 0;
        SDL_GL_GetDrawableSize(win, &w, &h);
        ImGui::Render();
        glViewport(0, 0, w, h);
        glClearColor(0.07f, 0.07f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Before the swap: the frame just drawn is still the read buffer.
        if (!selftest.png.empty() && ++frame_count >= selftest.frames && !sh.scanning) {
            selftest_ok = launcher_capture_png(selftest.png.c_str(), w, h);
            SDL_GL_SwapWindow(win);
            break;
        }
        SDL_GL_SwapWindow(win);

        if (finished) break;
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();

    if (!selftest.png.empty()) {
        if (!selftest_ok) {
            std::fprintf(stderr, "could not write %s\n", selftest.png.c_str());
            return 1;
        }
        std::printf("wrote %s\n", selftest.png.c_str());
        return 0;
    }

    if (finished) exec_game(paths, assets, passthrough);
    return 0;
}

void print_progress_line(const Progress& pr) {
    static int last = -1;
    static std::string last_headline;
    const int pct = (int)(pr.fraction * 100.0f);
    if (pct == last && pr.detail.empty() && pr.headline == last_headline) return;
    last = pct;
    last_headline = pr.headline;
    std::printf("[%3d%%] %s%s%s\n", pct, pr.headline.c_str(), pr.detail.empty() ? "" : " - ",
                pr.detail.c_str());
    std::fflush(stdout);
}

}  // namespace

int main(int argc, char** argv) {
    Paths paths = default_paths(argc > 0 ? argv[0] : nullptr);
    Assets assets;
    bool headless = false, check_only = false, force = false, show_paths = false, scan_only = false;
    bool no_gui = false;
    SelfTest selftest;
    std::vector<std::string> passthrough;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string& dst) {
            if (i + 1 < argc) dst = argv[++i];
        };
        if (a == "--build") { headless = true; no_gui = true; }
        else if (a == "--check") { check_only = true; no_gui = true; }
        else if (a == "--scan") { scan_only = true; no_gui = true; }
        else if (a == "--rebuild") { force = true; }
        else if (a == "--paths") { show_paths = true; no_gui = true; }
        else if (a == "--rom") { next(assets.rom); }
        else if (a == "--bios") { next(assets.bios); }
        else if (a == "--shot") { next(selftest.png); force = true; }
        else if (a == "--shot-building") { selftest.building = true; }
        else if (a == "--shot-frames") {
            std::string n;
            next(n);
            if (!n.empty()) selftest.frames = std::max(1, std::atoi(n.c_str()));
        }
        else if (a == "--builder-help" || a == "-h" || a == "--help") {
            std::printf(
                "PMDRedRescueTeamBuilder - builds the game from your own ROM and BIOS.\n\n"
                "  (no arguments)   play; build first if needed\n"
                "  --build          build without a window\n"
                "  --check          run the pre-flight checks only\n"
                "  --scan           search for the ROM and BIOS and print what was found\n"
                "  --rebuild        build even if it is already up to date\n"
                "  --paths          print the resolved locations\n"
                "  --rom PATH       ROM (skips the search)\n"
                "  --bios PATH      GBA BIOS (skips the search)\n"
                "  --shot PATH      render the window to a PNG and exit (self-test)\n"
                "  --shot-frames N  frames to settle before --shot (default 20)\n\n"
                "Any other argument is passed through to the game.\n");
            return 0;
        }
        else passthrough.push_back(a);
    }

    if (show_paths) {
        std::printf("sources : %s\n", paths.src_root.c_str());
        std::printf("data    : %s\n", paths.data_root.c_str());
        std::printf("work    : %s\n", paths.work_src().c_str());
        std::printf("game    : %s\n", paths.game_binary().c_str());
        std::printf("saves   : %s\n", paths.saves_dir().c_str());
        std::printf("stamp   : %s\n", paths.stamp_file().c_str());
        std::printf("log     : %s\n", paths.log_file().c_str());
        return 0;
    }

    if (assets.rom.empty() || assets.bios.empty()) {
        Assets remembered;
        if (stamp_read_assets(paths, remembered)) {
            if (assets.rom.empty()) assets.rom = remembered.rom;
            if (assets.bios.empty()) assets.bios = remembered.bios;
        }
    }

    if (scan_only) {
        scan_for_assets(default_search_roots(), assets);
        std::printf("rom  : %s\n", assets.rom.empty() ? "(not found)" : assets.rom.c_str());
        std::printf("bios : %s\n", assets.bios.empty() ? "(not found)" : assets.bios.c_str());
        return (assets.rom.empty() || assets.bios.empty()) ? 1 : 0;
    }

    if (check_only) {
        const Preflight pf = preflight(paths, assets);
        if (pf.ok) {
            std::printf("pre-flight ok\n");
            return 0;
        }
        std::fprintf(stderr, "%s\n", pf.error.c_str());
        if (!pf.remedy.empty()) std::fprintf(stderr, "  try: %s\n", pf.remedy.c_str());
        return 1;
    }

    if (!force && ready_to_play(paths, assets)) {
        if (no_gui) {
            std::printf("already up to date\n");
            return 0;
        }
        exec_game(paths, assets, passthrough);
    }

    if (headless) {
        if (assets.rom.empty() || assets.bios.empty()) scan_for_assets(default_search_roots(), assets);
        const Preflight pf = preflight(paths, assets);
        if (!pf.ok) {
            std::fprintf(stderr, "%s\n", pf.error.c_str());
            if (!pf.remedy.empty()) std::fprintf(stderr, "  try: %s\n", pf.remedy.c_str());
            return 1;
        }
        std::string err;
        if (!build(paths, assets, print_progress_line, err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        std::printf("built: %s\n", paths.game_binary().c_str());
        return 0;
    }

    return run_gui(paths, assets, passthrough, selftest);
}
