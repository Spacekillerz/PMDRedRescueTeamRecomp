// Finds the player's ROM and BIOS by content, so a Steam Deck in Gaming Mode
// (no mouse, no file dialog in the sandbox) can build with a single button.

#pragma once

#include <string>
#include <vector>

namespace pmd::builder {

struct Assets;

// Where dumps usually live: EmuDeck's layout, common download folders, and
// removable media. Missing directories are fine.
std::vector<std::string> default_search_roots();

// Fills the empty fields of `found` with the first file under `roots` whose
// size and SHA-1 match. Only exact-size files are hashed, which keeps a scan of
// a large ROM library fast.
void scan_for_assets(const std::vector<std::string>& roots, Assets& found);

}  // namespace pmd::builder
