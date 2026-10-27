// Adapted from BoktaiRecomp's builder (tools/builder/), Copyright (c) 2026 Shy
// (github.com/Shy), licensed under the PolyForm Noncommercial License 1.0.0.

#include "builder_core.h"

#include "sha1.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace pmd::builder {
namespace {

const GameSpec kSpec = {
    /* title       */ "Pokemon Mystery Dungeon: Red Rescue Team",
    /* target      */ "PMDRedRescueTeamRecomp",
#if defined(_WIN32)
    // Short on purpose: the deepest object path is ~125 characters below the
    // build directory, and MAX_PATH is 260 including C:\Users\<name>\AppData\Local.
    /* data_dir    */ "PMDRed",
#else
    /* data_dir    */ "PMDRedRescueTeamRecomp",
#endif
    /* rom_sha1    */ "9f4cfc5b5f4859d17169a485462e977c7aac2b89",
    /* bios_sha1   */ "300c20df6731a33952ded8c436f7f186d25d3492",
    /* game_toml   */ "game.toml",
    /* save_name   */ "pmd_red_rescue_team.sav",
    /* app_version */ "7",
};

// Generated C (~235 MB), the build tree and the linker's peak.
constexpr std::uintmax_t kNeedBytes = 1024ull * 1024 * 1024;

// Never copied into the work tree: build output, anything ROM-derived, and the
// prebuilt recompiler, which is used where it ships. Root-only names are matched
// by depth because gbarecomp has its own src/runtime/generated_bios/.
bool excluded(const fs::path& rel) {
    const std::string name = rel.filename().string();
    static const char* kAnywhere[] = {
        ".git", "build", "flatpak-build", "release-stage", "recomp_cache", "gba_bios.bin",
    };
    for (const char* n : kAnywhere)
        if (name == n) return true;
    if (name.rfind("build-", 0) == 0 || name.rfind("cmake-build-", 0) == 0) return true;
    const std::string ext = rel.extension().string();
    if (ext == ".gba" || ext == ".agb" || ext == ".sav") return true;

    static const char* kAtRoot[] = {
        ".github", ".idea", "bin", "roms", "saves", "generated", "generated_bios",
    };
    if (rel.has_parent_path()) return false;
    for (const char* n : kAtRoot)
        if (name == n) return true;
    return false;
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

// Quotes one argument for the shell popen() and system() hand commands to, or
// for the CRT's command-line parser on Windows.
std::string q(const std::string& s) {
#if defined(_WIN32)
    // Windows paths cannot contain '"', and cmd.exe has no other quoting.
    return "\"" + s + "\"";
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    return out + "'";
#endif
}

// argv[0] is a bare name when started from PATH, which is how Flatpak, desktop
// entries and Steam all start it, so ask the OS instead.
std::string exe_dir(const char* argv0) {
    std::error_code ec;
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return fs::path(std::wstring(buf, n)).parent_path().string();
#else
    fs::path self = fs::read_symlink("/proc/self/exe", ec);
    if (!ec && !self.empty()) return self.parent_path().string();
#endif
    fs::path p = argv0 ? fs::path(argv0) : fs::path();
    if (!p.empty() && p.has_parent_path()) {
        fs::path abs = fs::absolute(p, ec);
        if (!ec) return abs.parent_path().string();
    }
    return fs::current_path(ec).string();
}

// Inside a Flatpak, XDG_DATA_HOME already points at ~/.var/app/<id>/data.
std::string user_data_base() {
#if defined(_WIN32)
    if (const char* v = std::getenv("LOCALAPPDATA"); v && v[0]) return v;
    if (const char* v = std::getenv("USERPROFILE"); v && v[0]) return std::string(v) + "\\AppData\\Local";
#else
    if (const char* v = std::getenv("XDG_DATA_HOME"); v && v[0]) return v;
    if (const char* v = std::getenv("HOME"); v && v[0]) return std::string(v) + "/.local/share";
#endif
    return ".";
}

struct Log {
    std::ofstream out;
    explicit Log(const std::string& path) : out(path, std::ios::app) {}
    void line(const std::string& s) {
        if (out) { out << s << '\n'; out.flush(); }
    }
};

#if defined(_WIN32)
// The builder is a GUI-subsystem program (SDL2 links it with -mwindows), so
// any console child, cmd.exe included, would open a console window of its
// own. CREATE_NO_WINDOW gives the child a hidden console instead, which the
// processes it starts (ninja, gcc) inherit; DETACHED_PROCESS would not, and
// each of those would open a window again.
bool start_hidden(const std::string& cmd, HANDLE out, PROCESS_INFORMATION& pi) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE in = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = in;
    si.hStdOutput = out;
    si.hStdError = out;
    std::vector<char> line(cmd.begin(), cmd.end());
    line.push_back('\0');
    const BOOL ok = CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                   nullptr, nullptr, &si, &pi);
    if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
    return ok != 0;
}

int wait_exit(PROCESS_INFORMATION& pi) {
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(code);
}
#endif

// stderr is folded in: compiler diagnostics and ninja progress both matter.
// Ninja sees a pipe, not a tty, so it prints one line per edge.
int run(const std::string& cmd, Log& log, const std::function<void(const std::string&)>& on_line) {
    log.line("$ " + cmd);
    auto emit = [&](std::string l) {
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        log.line(l);
        if (on_line) on_line(l);
    };
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    PROCESS_INFORMATION pi{};
    if (!CreatePipe(&rd, &wr, &sa, 0) || !SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0) ||
        !start_hidden(cmd, wr, pi)) {
        if (rd) CloseHandle(rd);
        if (wr) CloseHandle(wr);
        log.line("!! could not start the command");
        return -1;
    }
    // Once our copy is closed, ReadFile ends when the last child holding the
    // write end exits.
    CloseHandle(wr);
    std::string pending;
    char chunk[4096];
    DWORD n = 0;
    while (ReadFile(rd, chunk, sizeof(chunk), &n, nullptr) && n > 0) {
        pending.append(chunk, n);
        for (size_t nl; (nl = pending.find('\n')) != std::string::npos; pending.erase(0, nl + 1))
            emit(pending.substr(0, nl));
    }
    if (!pending.empty()) emit(pending);
    CloseHandle(rd);
    const int rc = wait_exit(pi);
#else
    FILE* pipe = popen((cmd + " 2>&1").c_str(), "r");
    if (!pipe) {
        log.line("!! could not start the command");
        return -1;
    }
    char chunk[4096];
    while (std::fgets(chunk, sizeof(chunk), pipe)) emit(chunk);
    const int rc = pclose(pipe);
#endif
    log.line("-- exit " + std::to_string(rc));
    return rc;
}

int quiet_system(const std::string& cmd) {
#if defined(_WIN32)
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    PROCESS_INFORMATION pi{};
    const bool started = start_hidden(cmd, nul, pi);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    return started ? wait_exit(pi) : -1;
#else
    return std::system((cmd + " > /dev/null 2>&1").c_str());
#endif
}

bool have_ninja() {
    static const int rc = quiet_system("ninja --version");
    return rc == 0;
}

std::string generator_flag() { return have_ninja() ? " -G Ninja" : ""; }

}  // namespace

const GameSpec& game_spec() { return kSpec; }

std::string Paths::work_src() const { return (fs::path(data_root) / "src").string(); }
std::string Paths::game_binary() const {
#if defined(_WIN32)
    return (fs::path(work_src()) / "build" / (std::string(kSpec.target) + ".exe")).string();
#else
    return (fs::path(work_src()) / "build" / kSpec.target).string();
#endif
}
std::string Paths::saves_dir() const { return (fs::path(data_root) / "saves").string(); }
std::string Paths::stamp_file() const { return (fs::path(data_root) / "build.stamp").string(); }
std::string Paths::log_file() const { return (fs::path(data_root) / "build.log").string(); }

Paths default_paths(const char* argv0) {
    Paths p;
    p.src_root = exe_dir(argv0);
    {
        std::error_code ec;
        const fs::path tc = fs::path(p.src_root) / "toolchain";
        if (fs::exists(tc / "w64devkit" / "bin", ec)) p.toolchain = tc.string();
    }
    if (const char* over = std::getenv("PMD_BUILDER_SOURCES"); over && over[0]) {
        p.src_root = over;
    } else {
        // Shipped: sources/ beside the executable. Developer: a build directory
        // inside the checkout, so walk up to the project root.
        std::error_code ec;
        fs::path here = fs::path(p.src_root);
        if (fs::exists(here / "sources" / "CMakeLists.txt", ec)) {
            p.src_root = (here / "sources").string();
        } else {
            for (int up = 0; up < 4; ++up) {
                if (fs::exists(here / "CMakeLists.txt", ec) && fs::exists(here / kSpec.game_toml, ec)) {
                    p.src_root = here.string();
                    break;
                }
                if (!here.has_parent_path()) break;
                here = here.parent_path();
            }
        }
    }
    p.data_root = (fs::path(user_data_base()) / kSpec.data_dir).string();
    if (const char* d = std::getenv("PMD_BUILDER_DATA"); d && d[0]) p.data_root = d;
    return p;
}

namespace {

fs::path sdl2_prefix(const Paths& p) {
    return fs::path(p.toolchain) / "SDL2" / "x86_64-w64-mingw32";
}

}  // namespace

void use_bundled_toolchain(const Paths& p) {
    if (p.toolchain.empty()) return;
    const fs::path tc = p.toolchain;
#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif
    const char* old = std::getenv("PATH");
    const std::string path = (tc / "w64devkit" / "bin").string() + sep +
                             (sdl2_prefix(p) / "bin").string() +
                             (old && old[0] ? std::string(1, sep) + old : std::string());
#if defined(_WIN32)
    SetEnvironmentVariableA("PATH", path.c_str());
    _putenv_s("PATH", path.c_str());
    _putenv_s("CC", "gcc");
    _putenv_s("CXX", "g++");
#else
    setenv("PATH", path.c_str(), 1);
    setenv("CC", "gcc", 1);
    setenv("CXX", "g++", 1);
#endif
}

bool verify_sha1(const std::string& path, const char* want_hex, std::string& actual_hex) {
    actual_hex.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
    if (data.empty()) return false;
    unsigned char digest[20];
    char hex[41];
    recompui_sha1_compute(data.data(), data.size(), digest);
    recompui_sha1_hex(digest, hex);
    actual_hex.assign(hex);
    return want_hex && actual_hex == want_hex;
}

namespace {

// Existence of a compiler binary proves nothing; only a real compile does.
Preflight probe_toolchain(const Paths& p) {
    Preflight r;
    std::error_code ec;
    fs::create_directories(p.data_root, ec);
    const fs::path probe = fs::path(p.data_root) / "toolchain_probe.cpp";
    {
        std::ofstream f(probe);
        if (!f) {
            r.error = "Cannot write to " + p.data_root + ".";
            return r;
        }
        f << "#include <string>\nint main(){ return std::string(\"ok\").size()==2?0:1; }\n";
    }
    const fs::path out = fs::path(p.data_root) / "toolchain_probe.out";
    Log log(p.log_file());
    const char* cxx_env = std::getenv("CXX");
    const std::string cxx = (cxx_env && cxx_env[0]) ? cxx_env : "c++";
    const int rc = run(cxx + " -std=c++20 -x c++ " + q(probe.string()) + " -o " + q(out.string()),
                       log, nullptr);
    fs::remove(probe, ec);
    fs::remove(out, ec);
#if defined(_WIN32)
    // The zip carries its own compiler; nothing to install, only to restore.
    const char* kNoCompiler = "The bundled compiler does not work. Extract the release zip again, "
                              "keeping the toolchain folder next to the program.";
    const char* kCompilerRemedy = "";
    const char* kCMakeRemedy = "";
#else
    const char* kNoCompiler = "No working C++ compiler found.";
    const char* kCompilerRemedy =
        "sudo dnf install gcc-c++ cmake ninja-build   (or: sudo apt install build-essential cmake ninja-build)";
    const char* kCMakeRemedy = "sudo dnf install cmake ninja-build   (or: sudo apt install cmake ninja-build)";
#endif
    if (rc != 0) {
        r.error = kNoCompiler;
        r.remedy = kCompilerRemedy;
        return r;
    }
    if (quiet_system("cmake --version") != 0) {
        r.error = "CMake is not installed.";
        r.remedy = kCMakeRemedy;
        return r;
    }
    r.ok = true;
    return r;
}

}  // namespace

Preflight preflight(const Paths& p, const Assets& a) {
    Preflight r;
    std::error_code ec;

    if (!fs::exists(fs::path(p.src_root) / "CMakeLists.txt", ec)) {
        r.error = "No sources found at " + p.src_root + ".";
        r.remedy = "PMD_BUILDER_SOURCES=/path/to/PMDRedRescueTeamRecomp";
        return r;
    }

    std::string got;
    if (a.rom.empty() || !fs::exists(a.rom, ec)) {
        r.error = "Select your Pokemon Mystery Dungeon: Red Rescue Team (USA) ROM.";
        return r;
    }
    if (!verify_sha1(a.rom, kSpec.rom_sha1, got)) {
        r.error = got.empty() ? "Could not read the ROM."
                              : "That is not Pokemon Mystery Dungeon: Red Rescue Team (USA). Its SHA-1 is " + got + ".";
        return r;
    }
    if (a.bios.empty() || !fs::exists(a.bios, ec)) {
        r.error = "Select your Game Boy Advance BIOS (gba_bios.bin).";
        return r;
    }
    if (!verify_sha1(a.bios, kSpec.bios_sha1, got)) {
        r.error = got.empty() ? "Could not read the BIOS."
                              : "That is not the Game Boy Advance BIOS. Its SHA-1 is " + got + ".";
        return r;
    }

    fs::create_directories(p.data_root, ec);
    const fs::space_info space = fs::space(p.data_root, ec);
    if (!ec && space.available < kNeedBytes) {
        r.error = "Not enough free disk space: the build needs about " +
                  std::to_string(kNeedBytes / (1024 * 1024)) + " MB.";
        return r;
    }

    return probe_toolchain(p);
}

bool parse_ninja_line(const std::string& line, int& done, int& total, std::string& file) {
    if (line.size() < 5 || line[0] != '[') return false;
    const auto slash = line.find('/');
    const auto close = line.find(']');
    if (slash == std::string::npos || close == std::string::npos || slash > close) return false;
    const std::string a = line.substr(1, slash - 1);
    const std::string b = line.substr(slash + 1, close - slash - 1);
    if (a.empty() || b.empty()) return false;
    for (char c : a) if (!std::isdigit((unsigned char)c)) return false;
    for (char c : b) if (!std::isdigit((unsigned char)c)) return false;
    done = std::atoi(a.c_str());
    total = std::atoi(b.c_str());
    if (total <= 0) return false;

    file.clear();
    std::istringstream ss(line.substr(close + 1));
    std::string tok;
    while (ss >> tok) {
        const auto cut = tok.find_last_of('/');
        if (cut != std::string::npos) file = tok.substr(cut + 1);
    }
    return true;
}

namespace {

struct Reporter {
    const ProgressFn& fn;
    Progress cur;
    float lo = 0.0f, hi = 1.0f;

    void phase(Step s, float a, float b, std::string headline) {
        lo = a;
        hi = b;
        cur.step = s;
        cur.fraction = a;
        cur.headline = std::move(headline);
        cur.detail.clear();
        emit();
    }
    void within(float t, std::string detail) {
        cur.fraction = lo + (hi - lo) * std::clamp(t, 0.0f, 1.0f);
        cur.detail = std::move(detail);
        emit();
    }
    void emit() const { if (fn) fn(cur); }
};

bool same_contents(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    const auto size = fs::file_size(a, ec);
    if (ec || fs::file_size(b, ec) != size || ec) return false;
    std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
    if (!fa || !fb) return false;
    char ba[1 << 16], bb[1 << 16];
    while (fa && fb) {
        fa.read(ba, sizeof ba);
        fb.read(bb, sizeof bb);
        if (fa.gcount() != fb.gcount() ||
            !std::equal(ba, ba + fa.gcount(), bb)) return false;
    }
    return true;
}

// Copies over the top rather than wiping: the build tree lives inside the copy,
// and keeping it makes a post-update rebuild incremental. Files are compared by
// content, not date: Flatpak stamps everything under /app with the epoch, so a
// newer bundled source never looks newer than the copy.
bool copy_sources(const Paths& p, Reporter& rep, Log& log, std::string& err) {
    std::error_code ec;
    const fs::path from = p.src_root, to = p.work_src();
    fs::create_directories(to, ec);

    std::size_t total = 0, n = 0;
    for (int pass = 0; pass < 2; ++pass) {
        fs::recursive_directory_iterator it(from, fs::directory_options::skip_permission_denied, ec);
        if (ec) { err = "Cannot read the bundled sources."; return false; }
        for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            const fs::path& src = it->path();
            const fs::path rel = fs::relative(src, from, ec);
            if (ec) { ec.clear(); continue; }
            if (excluded(rel)) {
                if (it->is_directory(ec)) it.disable_recursion_pending();
                continue;
            }
            if (it->is_directory(ec)) continue;
            if (pass == 0) { ++total; continue; }

            const fs::path dst = to / rel;
            fs::create_directories(dst.parent_path(), ec);
            if (!same_contents(src, dst))
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                log.line("!! copy failed: " + src.string() + " -> " + ec.message());
                ec.clear();
            }
            if (++n % 64 == 0 && total)
                rep.within((float)n / (float)total, std::to_string(n) + "/" + std::to_string(total) + " files");
        }
        ec.clear();
    }
    log.line("-- copied " + std::to_string(n) + " source files to " + to.string());
    return true;
}

std::string find_recompiler(const Paths& p) {
    std::error_code ec;
    for (const fs::path cand : {fs::path(p.src_root) / "bin" / "gba_recompile",
                                fs::path(p.src_root) / "bin" / "gba_recompile.exe",
                                fs::path(p.work_src()) / "gbarecomp" / "build" / "gba_recompile",
                                fs::path(p.work_src()) / "gbarecomp" / "build" / "gba_recompile.exe"}) {
        if (fs::exists(cand, ec)) return cand.string();
    }
    return {};
}

}  // namespace

bool build(const Paths& p, const Assets& a, const ProgressFn& on_progress, std::string& err) {
    std::error_code ec;
    fs::create_directories(p.data_root, ec);
    { std::ofstream fresh(p.log_file(), std::ios::trunc); }
    Log log(p.log_file());
    log.line(std::string("== PMDRedRescueTeamRecomp builder, app version ") + kSpec.app_version);
    log.line("-- sources: " + p.src_root);
    log.line("-- work:    " + p.work_src());
    log.line(std::string("-- ninja:   ") + (have_ninja() ? "yes" : "no"));
    log.line("-- rom:     " + a.rom);
    log.line("-- bios:    " + a.bios);

    Reporter rep{on_progress, {}, 0.0f, 1.0f};

    rep.phase(Step::CopySources, 0.00f, 0.05f, "Preparing a build directory");
    if (!copy_sources(p, rep, log, err)) return false;

    const fs::path work = p.work_src();
    const fs::path eng = work / "gbarecomp";

    // A shipped builder brings a prebuilt recompiler; a developer checkout
    // without one builds it first.
    std::string recompiler = find_recompiler(p);
    if (recompiler.empty()) {
        rep.phase(Step::Bios, 0.05f, 0.08f, "Building the recompiler");
        const std::string bdir = (eng / "build").string();
        if (run("cmake -B " + q(bdir) + " -S " + q(eng.string()) + generator_flag() +
                    " -DCMAKE_BUILD_TYPE=Release", log, nullptr) != 0 ||
            run("cmake --build " + q(bdir) + " --target gba_recompile --parallel", log, nullptr) != 0) {
            err = "Could not build the recompiler.";
            return false;
        }
        recompiler = find_recompiler(p);
        if (recompiler.empty()) {
            err = "The recompiler was not produced.";
            return false;
        }
    }

    rep.phase(Step::Bios, 0.08f, 0.10f, "Recompiling the BIOS");
    if (run(q(recompiler) + " --bios " + q(a.bios) +
                " --config " + q((eng / "bios" / "gba_bios.toml").string()) +
                " --out " + q((work / "generated_bios").string()), log, nullptr) != 0) {
        err = "Could not recompile the BIOS.";
        return false;
    }

    rep.phase(Step::Cartridge, 0.10f, 0.14f, "Recompiling the game");
    const fs::path gen = work / "generated";
    if (run(q(recompiler) + " --rom " + q(a.rom) +
                " --config " + q((work / kSpec.game_toml).string()) +
                " --out " + q(gen.string()), log,
            // Only the recompiler's "==>" markers are meaningful to a player.
            [&](const std::string& l) {
                const std::string t = trim(l);
                if (t.rfind("==>", 0) != 0) return;
                std::string d = trim(t.substr(3));
                if (const auto sp = d.find(" /"); sp != std::string::npos) d = d.substr(0, sp);
                if (const auto par = d.find(" ("); par != std::string::npos) d = d.substr(0, par);
                rep.within(0.5f, trim(d).substr(0, 72));
            }) != 0) {
        err = "Could not recompile the game.";
        return false;
    }
    if (!fs::exists(gen, ec) || fs::is_empty(gen, ec)) {
        err = "The recompiler produced no output.";
        return false;
    }

    rep.phase(Step::Configure, 0.14f, 0.16f, "Configuring the build");
    const std::string bdir = (work / "build").string();
    std::string platform_flags;
    if (!p.toolchain.empty()) {
        // SDL2 comes from the zip; -static folds libstdc++, libgcc and
        // winpthread into the game so only SDL2.dll travels beside it.
        platform_flags = " -DCMAKE_PREFIX_PATH=" + q(sdl2_prefix(p).string()) +
                         " -DCMAKE_EXE_LINKER_FLAGS=-static";
    }
    if (run("cmake -B " + q(bdir) + " -S " + q(work.string()) + generator_flag() +
                " -DCMAKE_BUILD_TYPE=Release -DGBAGAME_RECOMP_UI=ON -DGBAGAME_BUILDER=OFF" +
                platform_flags, log, nullptr) != 0) {
        err = "Could not configure the build.";
        return false;
    }

    rep.phase(Step::Compile, 0.16f, 0.99f, "Compiling the game");
    if (run("cmake --build " + q(bdir) + " --target " + kSpec.target + " --parallel", log,
            [&](const std::string& l) {
                int d = 0, t = 0;
                std::string f;
                if (parse_ninja_line(l, d, t, f))
                    rep.within((float)d / (float)t,
                               std::to_string(d) + "/" + std::to_string(t) + (f.empty() ? "" : "   " + f));
            }) != 0) {
        err = "The build failed. The full log is at " + p.log_file() + ".";
        return false;
    }

    if (!fs::exists(p.game_binary(), ec)) {
        err = "The build reported success but produced no game binary.";
        return false;
    }
    if (!p.toolchain.empty()) {
        const fs::path dll = sdl2_prefix(p) / "bin" / "SDL2.dll";
        if (fs::exists(dll, ec))
            fs::copy_file(dll, fs::path(p.game_binary()).parent_path() / "SDL2.dll",
                          fs::copy_options::update_existing, ec);
    }

    stamp_write(p, a);
    rep.phase(Step::Done, 1.00f, 1.00f, "Ready to play");
    return true;
}

namespace {

std::string stamp_expected(const Assets& a) {
    std::string rom_hex, bios_hex;
    verify_sha1(a.rom, nullptr, rom_hex);
    verify_sha1(a.bios, nullptr, bios_hex);
    return std::string("version=") + kSpec.app_version + "\nrom=" + rom_hex + "\nbios=" + bios_hex + "\n";
}

}  // namespace

// Records the asset paths, never their contents, so a returning player is not
// asked for them again.
void stamp_write(const Paths& p, const Assets& a) {
    std::error_code ec;
    fs::create_directories(p.data_root, ec);
    std::ofstream f(p.stamp_file(), std::ios::trunc);
    if (!f) return;
    f << stamp_expected(a) << "rom_path=" << a.rom << "\nbios_path=" << a.bios << '\n';
}

bool stamp_ok(const Paths& p, const Assets& a) {
    std::ifstream f(p.stamp_file());
    if (!f) return false;
    std::string got((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return got.rfind(stamp_expected(a), 0) == 0;
}

bool stamp_read_assets(const Paths& p, Assets& out) {
    std::ifstream f(p.stamp_file());
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = trim(line.substr(eq + 1));
        if (key == "rom_path") out.rom = val;
        if (key == "bios_path") out.bios = val;
    }
    std::error_code ec;
    return !out.rom.empty() && !out.bios.empty() && fs::exists(out.rom, ec) && fs::exists(out.bios, ec);
}

bool ready_to_play(const Paths& p, const Assets& a) {
    std::error_code ec;
    return fs::exists(p.game_binary(), ec) && stamp_ok(p, a);
}

std::vector<std::string> game_args(const Paths& p, const Assets& a,
                                   const std::vector<std::string>& passthrough) {
    std::vector<std::string> args = {
        "--bios", a.bios,
        "--rom", a.rom,
        "--save-path", (fs::path(p.saves_dir()) / kSpec.save_name).string(),
        // States default to sitting beside the ROM, which the sandbox mounts read-only.
        "--state-dir", p.saves_dir(),
    };
    args.insert(args.end(), passthrough.begin(), passthrough.end());
    args.push_back((fs::path(p.work_src()) / kSpec.game_toml).string());
    return args;
}

}  // namespace pmd::builder
