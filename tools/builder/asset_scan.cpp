#include "asset_scan.h"

#include "builder_core.h"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace pmd::builder {
namespace {

constexpr std::uintmax_t kRomSize = 32u * 1024 * 1024;
constexpr std::uintmax_t kBiosSize = 16u * 1024;
constexpr int kMaxDepth = 5;
// Bounds a scan of a huge home directory; the likely roots come first anyway.
constexpr std::size_t kMaxEntries = 200000;

void add_if_dir(std::vector<std::string>& out, const fs::path& p) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) out.push_back(p.string());
}

}  // namespace

std::vector<std::string> default_search_roots() {
    std::vector<std::string> roots;
    const char* home_env = std::getenv("HOME");
    const fs::path home = (home_env && home_env[0]) ? fs::path(home_env) : fs::path();

    if (!home.empty()) {
        add_if_dir(roots, home / "Emulation" / "roms" / "gba");
        add_if_dir(roots, home / "Emulation" / "bios");
    }
    // EmuDeck on an SD card: /run/media/<card>/Emulation or /run/media/<user>/<card>/Emulation.
    std::error_code ec;
    for (const auto& lvl1 : fs::directory_iterator("/run/media", ec)) {
        add_if_dir(roots, lvl1.path() / "Emulation" / "roms" / "gba");
        add_if_dir(roots, lvl1.path() / "Emulation" / "bios");
        std::error_code ec2;
        for (const auto& lvl2 : fs::directory_iterator(lvl1.path(), ec2)) {
            add_if_dir(roots, lvl2.path() / "Emulation" / "roms" / "gba");
            add_if_dir(roots, lvl2.path() / "Emulation" / "bios");
        }
    }
    if (!home.empty()) {
        add_if_dir(roots, home / "Downloads");
        add_if_dir(roots, home);
    }
    add_if_dir(roots, "/run/media");
    return roots;
}

void scan_for_assets(const std::vector<std::string>& roots, Assets& found) {
    const GameSpec& spec = game_spec();
    std::size_t visited = 0;
    for (const auto& root : roots) {
        if (!found.rom.empty() && !found.bios.empty()) return;
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (++visited > kMaxEntries) return;
            const std::string name = it->path().filename().string();
            std::error_code fe;
            if (it->is_directory(fe)) {
                // Hidden trees (.cache, .var, .steam...) are large and never hold dumps.
                if (it.depth() >= kMaxDepth || (!name.empty() && name[0] == '.'))
                    it.disable_recursion_pending();
                continue;
            }
            if (!it->is_regular_file(fe)) continue;
            const std::uintmax_t size = it->file_size(fe);
            if (fe) continue;
            std::string got;
            if (found.rom.empty() && size == kRomSize &&
                verify_sha1(it->path().string(), spec.rom_sha1, got)) {
                found.rom = it->path().string();
            } else if (found.bios.empty() && size == kBiosSize &&
                       verify_sha1(it->path().string(), spec.bios_sha1, got)) {
                found.bios = it->path().string();
            }
            if (!found.rom.empty() && !found.bios.empty()) return;
        }
    }
}

}  // namespace pmd::builder
