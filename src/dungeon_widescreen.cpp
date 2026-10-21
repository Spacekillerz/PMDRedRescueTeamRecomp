#include "dungeon_widescreen.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "gba_bus.h"
#include "gba_ppu.h"
#include "mod_function_hooks.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"

// Dungeon floors keep the whole floor in RAM, so the BG3 terrain past the
// native 240 columns can be rebuilt exactly as the game's own column streamer
// (sub_804A1F0 in pret/pmd-red src/dungeon_map_access.c) would draw it.
// Addresses are for the USA ROM; struct offsets follow pret/pmd-red
// include/structs/str_dungeon.h and include/structs/map.h.

namespace pmd_widescreen {
namespace {

constexpr std::uint32_t kEwramBase = 0x02000000;
constexpr std::uint32_t kEwramSize = 256 * 1024;
constexpr std::uint32_t kRomBase = 0x08000000;

constexpr std::uint32_t kDungeonPtr = 0x0203B418;  // Dungeon *gDungeon
constexpr std::uint32_t kOobTilePtr = 0x0203B430;  // const Tile *gCurTilesetOobTile

constexpr std::uint32_t kStairsKind = 0x3A12;
constexpr std::uint32_t kTilePointers = 0xE8C0;    // Tile *[32][56]
constexpr std::uint32_t kTileKinds = 0x11884;      // u16 [250][9]
constexpr std::uint32_t kSpecialKinds = 0x12A18;   // u16 [29][9]
constexpr std::uint32_t kCameraPixelPos = 0x181F0;
constexpr std::uint32_t kShake = 0x181FC;
constexpr std::uint32_t kBlinded = 0x1820A;
constexpr std::uint32_t kShowInvisible = 0x1820F;
constexpr std::uint32_t kHallucinating = 0x18210;
constexpr std::uint32_t kFloorMapMode = 0x18214;
constexpr std::uint32_t kDungeonReadEnd = 0x18220;

constexpr int kFloorW = 56;
constexpr int kFloorH = 32;

constexpr std::uint16_t kTerrainShop = 1u << 5;
constexpr std::uint16_t kTerrainStairs = 1u << 9;
constexpr std::uint16_t kTerrainUnk1000 = 1u << 12;
constexpr std::uint32_t kEntityTrap = 2;

constexpr int kTerrainBg = 3;
constexpr int kTextBg = 0;
// BG0 tile the game puts in column 0 of every row: a 1-px black left border.
constexpr std::uint16_t kLeftBorderTile = 0x279;
constexpr int kMaxCols = gba::GbaPpu::kMaxRenderWidth / 8 + 2;
constexpr int kMaxRows = 160 / 8 + 2;

const gba::GbaBus* g_bus = nullptr;

std::uint8_t read8(std::uint32_t a) {
    if (a >= kEwramBase && a < kEwramBase + kEwramSize)
        return g_bus->ewram_ptr()[a - kEwramBase];
    if (a >= kRomBase && a - kRomBase < g_bus->rom_size())
        return g_bus->rom_ptr()[a - kRomBase];
    return 0;
}
std::uint16_t read16(std::uint32_t a) {
    return static_cast<std::uint16_t>(read8(a) | (read8(a + 1) << 8));
}
std::uint32_t read32(std::uint32_t a) {
    return read16(a) | (static_cast<std::uint32_t>(read16(a + 2)) << 16);
}

int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
int floor_mod(int a, int b) { return a - floor_div(a, b) * b; }
int sign_extend9(int v) { v &= 0x1FF; return v >= 0x100 ? v - 0x200 : v; }

struct Floor {
    std::uint32_t dungeon = 0;
    std::uint32_t oob_tile = 0;
    bool hallucinating = false;
    bool show_invisible = false;
    int stairs_kind = 0;

    std::uint32_t tile(int x, int y) const {
        if (x >= 0 && y >= 0 && x < kFloorW && y < kFloorH)
            return read32(dungeon + kTilePointers + (y * kFloorW + x) * 4u);
        return oob_tile;
    }

    // Base of the 3x3 tilemap entries the game draws for floor tile (x, y).
    std::uint32_t kind(int x, int y) const {
        const std::uint32_t t = tile(x, y);
        const std::uint16_t flags = read16(t);
        const std::uint32_t own = dungeon + kTileKinds + read8(t + 8) * 18u;
        if (flags & kTerrainShop) return dungeon + kSpecialKinds + 25 * 18u;
        if (flags & kTerrainUnk1000) return dungeon + kSpecialKinds + 26 * 18u;
        if (hallucinating) return own;
        if (const std::uint32_t object = read32(t + 0x14)) {
            if (read32(object) == kEntityTrap && (read8(object + 0x20) || show_invisible))
                return dungeon + kSpecialKinds + read8(read32(object + 0x70)) * 18u;
            return own;
        }
        if (flags & kTerrainStairs) return dungeon + kSpecialKinds + stairs_kind * 18u;
        return own;
    }

    std::uint16_t entry(int tx, int ty) const {
        const int sub = floor_mod(tx, 3) + floor_mod(ty, 3) * 3;
        return read16(kind(floor_div(tx, 3), floor_div(ty, 3)) + sub * 2u);
    }
};

// Terrain for every 8x8 cell on screen this frame, margins included.
std::uint16_t g_cells[kMaxRows][kMaxCols];
int g_first_tx = 0, g_first_ty = 0, g_cols = 0, g_rows = 0;
int g_world_x0 = 0, g_world_y0 = 0;  // world pixel shown at native (0, 0)

int provide_margin_tile(int bg, int hx, int y, std::uint16_t* out) {
    if (bg != kTerrainBg) return gba::kWsTilemapUnavailable;
    const int col = floor_div(g_world_x0 + hx, 8) - g_first_tx;
    const int row = floor_div(g_world_y0 + y, 8) - g_first_ty;
    if (col < 0 || row < 0 || col >= g_cols || row >= g_rows)
        return gba::kWsTilemapUnavailable;
    *out = g_cells[row][col];
    return gba::kWsTilemapReplace;
}

std::uint16_t io16(const gbarecomp::ExtendedViewFrameInfo* f, std::size_t off) {
    return static_cast<std::uint16_t>(f->io[off] | (f->io[off + 1] << 8));
}

// Sprite engine state (src/sprite.c), USA addresses.
constexpr std::uint32_t kAddAxSprite = 0x08004EA8;
constexpr const char* kMarginSpritesHook = "pmd.widescreen.margin-sprites";
constexpr std::uint32_t kSpriteCount = 0x020266A8;      // s32 sSpriteCount
constexpr std::uint32_t kSpriteBuffer = 0x020262A8;     // SpriteOAM [128]
constexpr std::uint32_t kSpriteLinks = 0x02025EA8;      // UnkSpriteLink [128]
constexpr std::uint32_t kSpriteList = 0x020256A0;       // UnkSpriteLink [256], one chain per depth
constexpr std::uint32_t kPoseTileNums = 0x02025672;     // s16 [8]
constexpr std::uint32_t kPosePalettes = 0x02025682;     // s16 [9]
constexpr std::uint32_t kPartQueueCursor = 0x0203B074;  // unkStruct_20266B0 *
// The part queue ends where sCharMemCursor starts: one address, two roles.
constexpr std::uint32_t kPartQueueEnd = 0x02026E30;
constexpr std::uint32_t kCharMemCursor = 0x02026E30;    // void *sCharMemCursor

int g_margin_right = 0;

std::int16_t guest_s16(std::uint32_t a) { return static_cast<std::int16_t>(bus_read_u16(a)); }

void register_sprite_parts(std::uint32_t part) {
    for (; bus_read_u32(part + 4) != 0; part += 8) {
        const std::uint32_t entry = bus_read_u32(kPartQueueCursor);
        if (entry >= kPartQueueEnd) return;
        const std::uint32_t bytes = bus_read_u32(part + 4);
        const std::uint32_t dest = bus_read_u32(kCharMemCursor);
        bus_write_u32(entry, bytes);
        bus_write_u32(entry + 4, bus_read_u32(part));
        bus_write_u32(entry + 8, dest);
        bus_write_u32(kCharMemCursor, dest + bytes);
        bus_write_u32(kPartQueueCursor, entry + 12);
    }
}

// Tail of AddSprite/AddAxSprite: the next buffer slot, chained at `depth`.
void append_sprite(const std::uint16_t attr[4], int depth) {
    const std::uint32_t count = bus_read_u32(kSpriteCount);
    const std::uint32_t slot = kSpriteBuffer + count * 8;
    for (int i = 0; i < 4; ++i) bus_write_u16(slot + 2 * i, attr[i]);
    const std::uint32_t link = kSpriteLinks + count * 8, head = kSpriteList + depth * 8u;
    bus_write_u32(link, bus_read_u32(head));
    bus_write_u32(head, link);
    bus_write_u32(kSpriteCount, count + 1);
}

// AddAxSprite(axPose, axData, axSprite, spriteMasks), ported from the ROM code
// at 0x08004EA8, for the parts its `x >= 240` test would drop although they
// land in the right margin. Every other call declines and runs the original.
int add_margin_ax_sprite(std::uint32_t, int, ArmCpuState* cpu) {
    const std::uint32_t pose = cpu->R[0], data = cpu->R[1], parts = cpu->R[2], masks = cpu->R[3];
    const std::uint32_t count = bus_read_u32(kSpriteCount);
    if (count >= 128) return 0;

    const std::uint16_t flags1 = bus_read_u16(pose + 4), flags2 = bus_read_u16(pose + 6);
    std::uint16_t attr[4] = {
        static_cast<std::uint16_t>(flags1 & 0xFCFF),
        static_cast<std::uint16_t>(flags2 & 0xF1FF),
        bus_read_u16(pose + 8),
        static_cast<std::uint16_t>(((flags2 & 0x0E00) >> 9) | ((flags1 & 0x03FF) << 4)),
    };
    if (masks)
        for (int i = 0; i < 3; ++i)
            attr[i] = static_cast<std::uint16_t>((attr[i] & bus_read_u16(masks + 2 * i)) |
                                                 bus_read_u16(masks + 6 + 2 * i));

    const int x = (attr[1] & 0x1FF) - 0x100 + guest_s16(data);
    const int y = (attr[3] >> 4) - 0x200 + guest_s16(data + 2);
    if (x < 240 || x >= 240 + g_margin_right || y < -64 || y >= 160) return 0;

    int depth = guest_s16(data + 0x16) + static_cast<std::int8_t>(bus_read_u8(pose + 3));
    depth = depth < 0 ? 0 : depth > 255 ? 255 : depth;

    const std::uint8_t pose_kind = bus_read_u8(pose + 2);
    const int pose_tile = pose_kind ? guest_s16(kPoseTileNums + pose_kind * 2u) : 0;
    const int tile = pose_tile ? pose_tile & 0x3FF : ((attr[2] & 0x3FF) + guest_s16(data + 0x14)) & 0x3FF;
    attr[2] = static_cast<std::uint16_t>((attr[2] & 0xFC00) | tile);
    attr[1] = static_cast<std::uint16_t>((attr[1] & 0xFE00) | (x & 0x1FF));
    attr[0] = static_cast<std::uint16_t>((attr[0] & 0xFF00) | (y & 0xFF));
    if (!(attr[3] & 2))
        attr[2] = static_cast<std::uint16_t>((attr[2] & 0x0FFF) | ((bus_read_u8(data + 0x1E) & 0xF) << 12));
    if (pose_kind)
        attr[2] = static_cast<std::uint16_t>((attr[2] & 0x0FFF) |
                                             ((guest_s16(kPosePalettes + pose_kind * 2u) & 0xF) << 12));

    if (parts) register_sprite_parts(parts);
    append_sprite(attr, depth);
    cpu->R[15] = cpu->R[14] & ~1u;
    return 1;
}

// Status icons (sleep "Z", poison...): DrawStatusSprite(species, status, pos,
// posOffset, posScreen, priority, unused) in src/dungeon_pokemon_sprites.c,
// ported for the icons its `posX < 240` test would drop into the right margin.
constexpr std::uint32_t kDrawStatusSprite = 0x0800F364;
constexpr const char* kMarginStatusHook = "pmd.widescreen.margin-status";
constexpr std::uint32_t kStatusSpritesInfo = 0x080D3474;  // {u32 gfxIndex, u32 palIndex} []
constexpr std::uint32_t kStatusGfxInfo = 0x080CE79C;      // StatusGraphicsInfo [] (0x18 bytes)
constexpr std::uint32_t kStatusVramBase = 0x32B;
constexpr std::uint32_t kStatusOam = 0x0203B0DC;          // SpriteOAM sStatusSpriteOAM

struct StatusShape { std::uint8_t shape, size, dx, at_pos_y; };
// Indexed by STATUS_GFX_*; gfx 0 (none) is never drawn.
constexpr StatusShape kStatusShapes[17] = {
    {0, 0, 0, 0},
    {0, 0, 0, 0},  // sleepless
    {0, 1, 0, 0},  // burned
    {0, 1, 0, 0},  // poisoned
    {0, 2, 0, 1},  // frozen
    {1, 2, 0, 0},  // confused
    {0, 1, 0, 0},  // whiffer
    {0, 1, 0, 0},  // taunted
    {2, 0, 0, 0},  // low HP
    {0, 1, 0, 0},  // shield
    {0, 1, 0, 0},  // stat down
    {0, 1, 0, 0},  // sword
    {0, 0, 0, 0},  // blinker
    {0, 1, 0, 0},  // cross-eyed
    {0, 0, 0, 0},  // eyedrops
    {0, 1, 0, 0},  // muzzled
    {0, 1, 8, 0},  // sleep
};

int draw_margin_status_sprite(std::uint32_t, int, ArmCpuState* cpu) {
    const std::uint32_t pos = cpu->R[2], offset = cpu->R[3];
    const std::uint32_t screen = bus_read_u32(cpu->R[13]), priority = bus_read_u32(cpu->R[13] + 4);
    const std::uint32_t info = kStatusSpritesInfo + cpu->R[1] * 8;
    const std::uint32_t gfx = bus_read_u32(info), palette = bus_read_u32(info + 4);
    if (gfx < 1 || gfx > 16) return 0;
    const std::uint32_t gfx_info = kStatusGfxInfo + gfx * 0x18;
    const int x = guest_s16(pos) - static_cast<int>(bus_read_u32(gfx_info)) * 4 - guest_s16(screen) + guest_s16(offset);
    const int y = guest_s16(pos + 2) - static_cast<int>(bus_read_u32(gfx_info + 4)) * 4 -
                  guest_s16(screen + 2) + guest_s16(offset + 2);
    if (x < 240 || x >= 240 + g_margin_right || y < -32 || y >= 160) return 0;

    const StatusShape& s = kStatusShapes[gfx];
    std::uint16_t attr[4];
    for (int i = 0; i < 4; ++i) attr[i] = bus_read_u16(kStatusOam + 2 * i);
    attr[0] = static_cast<std::uint16_t>((attr[0] & 0x3FFF) | (s.shape << 14));
    attr[1] = static_cast<std::uint16_t>((attr[1] & 0x3FFF & 0xFE00) | (s.size << 14) | ((x + s.dx) & 0x1FF));
    attr[2] = static_cast<std::uint16_t>(((bus_read_u32(gfx_info + 8) + kStatusVramBase) & 0x3FF) |
                                         ((priority & 3) << 10) | ((palette & 0xF) << 12));
    attr[3] = static_cast<std::uint16_t>((attr[3] & 0xF) | (((s.at_pos_y ? y : y - 16) & 0xFFF) << 4));
    for (int i = 0; i < 4; ++i) bus_write_u16(kStatusOam + 2 * i, attr[i]);

    // AddSprite(&sStatusSpriteOAM, depth, NULL, NULL), 0x080050B0.
    if (bus_read_u32(kSpriteCount) < 128) {
        int depth = (guest_s16(pos + 2) - guest_s16(screen + 2)) / 2 + 1;
        depth = depth < 0 ? 0 : depth > 255 ? 255 : depth;
        attr[0] = static_cast<std::uint16_t>((attr[0] & 0xFF00) | ((attr[3] >> 4) & 0xFF));
        append_sprite(attr, depth);
    }
    cpu->R[15] = cpu->R[14] & ~1u;
    return 1;
}

// The game shows wild Pokémon and floor items only within 6 tiles of the
// leader horizontally (sub_803F428). The leader's tile moves at the start of a
// step while the camera is still scrolling, so with margins the 7th tile is on
// screen. The two draw calls below see 7 tiles; every other caller (messages,
// AI, move effects) keeps the game's own answer.
constexpr std::uint32_t kMonsterOnScreen = 0x08042768;      // sub_8042768
constexpr std::uint32_t kMonsterDrawReturn = 0x0806C882;    // its call in the Pokémon renderer
constexpr std::uint32_t kShouldDisplayEntity = 0x08045888;
constexpr std::uint32_t kItemDrawReturn = 0x0804630E;       // its call in the floor item renderer
constexpr const char* kMonsterRangeHook = "pmd.widescreen.monster-range";
constexpr const char* kItemRangeHook = "pmd.widescreen.item-range";

constexpr std::uint32_t kCameraPos = 0x181E8;     // DungeonPos, leader's tile
constexpr std::uint32_t kCameraTarget = 0x181F8;  // Entity *
constexpr std::uint32_t kVisibility = 0x18209;
constexpr std::uint32_t kAllTilesRevealed = 0x1820B;
constexpr std::uint32_t kUnk1820C = 0x1820C;
constexpr std::uint32_t kShowWildMonsters = 0x1820D;
constexpr std::uint32_t kShowAllFloorItems = 0x1820E;
constexpr std::uint32_t kRoomData = 0x104C4;      // RoomData [] (0x1C bytes)
constexpr std::uint32_t kEntityMonster = 1, kEntityItem = 3;
constexpr int kWideRange = 7;

// IsPositionActuallyInSight(&cameraPos, target), 0x08083294.
bool in_sight(std::uint32_t d, int tx, int ty) {
    const int ox = guest_s16(d + kCameraPos), oy = guest_s16(d + kCameraPos + 2);
    int range = bus_read_u8(d + kVisibility);
    if (!range) range = 2;
    const std::uint32_t tile = (ox >= 0 && oy >= 0 && ox < kFloorW && oy < kFloorH)
        ? bus_read_u32(d + kTilePointers + (oy * kFloorW + ox) * 4u) : bus_read_u32(kOobTilePtr);
    const std::uint8_t room = bus_read_u8(tile + 9);
    if (room == 0xFF) return std::abs(ox - tx) <= range && std::abs(oy - ty) <= range;
    const std::uint32_t r = d + kRoomData + room * 0x1Cu;
    return guest_s16(r + 2) - 1 <= tx && guest_s16(r + 4) - 1 <= ty &&
           guest_s16(r + 6) + 1 > tx && guest_s16(r + 8) + 1 > ty;
}

int g_margin_left = 0;

int entity_screen_x(std::uint32_t d, std::uint32_t ent) {
    return static_cast<std::int32_t>(bus_read_u32(ent + 0xC)) / 256 - guest_s16(d + kCameraPixelPos);
}

// ShouldDisplayEntity (0x08045888) for a monster or an item, with a
// kWideRange-tile window and the left pixel bound moved from -32 to `left`.
bool wide_display(std::uint32_t ent, std::uint32_t type, int left) {
    const std::uint32_t d = bus_read_u32(kDungeonPtr);
    if (!bus_read_u8(ent + 0x20)) return false;
    const int px = entity_screen_x(d, ent);
    const int py = static_cast<std::int32_t>(bus_read_u32(ent + 0x10)) / 256 - guest_s16(d + kCameraPixelPos + 2);
    if (px < left || py < -32 || px > 272 || py > 192) return false;
    if (type == kEntityMonster) {
        const std::uint32_t info = bus_read_u32(ent + 0x70);
        if (!bus_read_u8(info + 6)) return true;
        if (!bus_read_u8(d + kShowInvisible) && bus_read_u8(info + 0xE4) == 1) return false;
        if (bus_read_u8(d + kShowWildMonsters)) return true;
    } else if (bus_read_u8(d + kShowAllFloorItems)) {
        return true;
    }
    const int tx = guest_s16(ent + 4), ty = guest_s16(ent + 6);
    if (std::abs(guest_s16(d + kCameraPos) - tx) > kWideRange || std::abs(guest_s16(d + kCameraPos + 2) - ty) > 5)
        return false;
    if (bus_read_u8(d + kAllTilesRevealed) || bus_read_u8(d + kUnk1820C) || !bus_read_u32(d + kCameraTarget))
        return true;
    return in_sight(d, tx, ty);
}

// Answers a call to ShouldDisplayEntity or sub_8042768 from a renderer, unless
// the entity is within the game's own window and bounds.
int answer_wide(ArmCpuState* cpu, std::uint32_t type, int left) {
    const std::uint32_t ent = cpu->R[0], d = bus_read_u32(kDungeonPtr);
    if (!ent || bus_read_u32(ent) != type) return 0;
    const int dx = std::abs(guest_s16(d + kCameraPos) - guest_s16(ent + 4));
    if (dx < kWideRange && entity_screen_x(d, ent) >= -32) return 0;
    cpu->R[0] = wide_display(ent, type, left) ? 1 : 0;
    cpu->R[15] = cpu->R[14] & ~1u;
    return 1;
}

// Pokémon parts reach 32 px either side of their position; floor item icons
// are 16x16 from theirs.
int monster_left_bound() { return -g_margin_left - 32; }
int item_left_bound() { return -g_margin_left - 15; }

// sub_8042768 = isVisible && (blinded ? leader : ShouldDisplayEntity).
int monster_wide_range(std::uint32_t, int, ArmCpuState* cpu) {
    if ((cpu->R[14] & ~1u) != kMonsterDrawReturn) return 0;
    if (bus_read_u8(bus_read_u32(kDungeonPtr) + kBlinded)) return 0;
    return answer_wide(cpu, kEntityMonster, monster_left_bound());
}

int item_wide_range(std::uint32_t, int, ArmCpuState* cpu) {
    if ((cpu->R[14] & ~1u) != kItemDrawReturn) return 0;
    return answer_wide(cpu, kEntityItem, item_left_bound());
}

// sub_80462AC(entity, hallucinating, a2, a3, a4), the floor item renderer,
// ported from 0x080462AC for icons its own `x >= -32` test would drop
// although they reach into the left margin.
constexpr std::uint32_t kDrawFloorItem = 0x080462AC;
constexpr const char* kMarginItemHook = "pmd.widescreen.margin-items";
constexpr std::uint32_t kItemOam = 0x0202EDC0;          // SpriteOAM gUnknown_202EDC0
constexpr std::uint32_t kItemTileNums = 0x0203B420;     // u8 sUnkItemTileNums[]
constexpr std::uint32_t kItemMatrixNums = 0x0203B428;   // u8 sUnkItemMatrixNums[]
constexpr std::uint32_t kItemParameters = 0x020389A4;   // ItemDataEntry *, 0x20 bytes each

int draw_margin_floor_item(std::uint32_t, int, ArmCpuState* cpu) {
    const std::uint32_t ent = cpu->R[0];
    const bool hallucinating = (cpu->R[1] & 0xFF) != 0, check_display = (cpu->R[2] & 0xFF) != 0;
    const std::uint32_t a3 = cpu->R[3] & 0xFF;
    const bool depth_from_y = (bus_read_u32(cpu->R[13]) & 0xFF) != 0;
    if (!ent || !bus_read_u32(ent)) return 0;

    const std::uint32_t d = bus_read_u32(kDungeonPtr);
    const int cam_x = guest_s16(d + kCameraPixelPos), cam_y = guest_s16(d + kCameraPixelPos + 2);
    const std::int32_t pixel_y = static_cast<std::int32_t>(bus_read_u32(ent + 0x10));
    const int x = static_cast<std::int32_t>(bus_read_u32(ent + 0xC)) / 256 - cam_x;
    const int y = (pixel_y - static_cast<std::int32_t>(bus_read_u32(ent + 0x1C))) / 256 - cam_y;
    if (x >= -32 || x < item_left_bound() || y < -32 || y > 192) return 0;

    std::uint32_t obj_mode = 0;
    if (check_display) {
        const int tx = guest_s16(ent + 4), ty = guest_s16(ent + 6);
        const std::uint32_t tile = (tx >= 0 && ty >= 0 && tx < kFloorW && ty < kFloorH)
            ? bus_read_u32(d + kTilePointers + (ty * kFloorW + tx) * 4u) : bus_read_u32(kOobTilePtr);
        const std::uint32_t terrain = bus_read_u16(tile) & 3;
        if (terrain == 0 || !wide_display(ent, kEntityItem, item_left_bound())) return 0;
        if (terrain == 2) obj_mode = 1;
    }

    std::uint16_t attr[4];
    for (int i = 0; i < 4; ++i) attr[i] = bus_read_u16(kItemOam + 2 * i);
    attr[1] &= 0xC1FF;
    std::uint32_t icon;
    if (hallucinating) {
        icon = 0x17;
        attr[2] = static_cast<std::uint16_t>((attr[2] & 0x0FFF) | 0xA000);
    } else {
        const std::uint32_t item = bus_read_u32(ent + 0x70);
        const std::uint32_t params = bus_read_u32(kItemParameters) + bus_read_u8(item + 2) * 0x20u;
        icon = bus_read_u8(params + 0xD);
        if (a3 != 0xFF && icon == 0) {
            icon = bus_read_u8(kItemTileNums + a3);
            attr[1] = static_cast<std::uint16_t>((attr[1] & 0xC1FF) | (((bus_read_u8(kItemMatrixNums + a3) << 3) & 0x1F) << 9));
        }
        attr[2] = static_cast<std::uint16_t>((attr[2] & 0x0FFF) | ((bus_read_u8(params + 0x1D) & 0xF) << 12));
    }
    attr[3] = static_cast<std::uint16_t>((attr[3] & 0xF) | ((y & 0xFFF) << 4));
    attr[1] = static_cast<std::uint16_t>((attr[1] & 0xFE00) | (x & 0x1FF));
    attr[0] = static_cast<std::uint16_t>((attr[0] & 0xF3FF) | (obj_mode << 10));
    attr[2] = static_cast<std::uint16_t>((attr[2] & 0xF3FF & 0xFC00) | 0x0C00 | ((icon * 4 + 0x1A0) & 0x3FF));
    for (int i = 0; i < 4; ++i) bus_write_u16(kItemOam + 2 * i, attr[i]);

    // AddSprite(&gUnknown_202EDC0, y2, NULL, NULL), 0x080050B0.
    if (bus_read_u32(kSpriteCount) < 128) {
        int depth = depth_from_y ? (pixel_y / 256 - cam_y + 8) / 2 : 1;
        depth = depth < 0 ? 0 : depth > 255 ? 255 : depth;
        attr[0] = static_cast<std::uint16_t>((attr[0] & 0xFF00) | ((attr[3] >> 4) & 0xFF));
        append_sprite(attr, depth);
    }
    cpu->R[0] = 1;
    cpu->R[15] = cpu->R[14] & ~1u;
    return 1;
}

struct MarginHook {
    const char* id;
    std::uint32_t addr;
    GBAModFunctionEntryCallback callback;
};
constexpr MarginHook kMarginHooks[] = {
    {kMarginSpritesHook, kAddAxSprite, add_margin_ax_sprite},
    {kMarginStatusHook, kDrawStatusSprite, draw_margin_status_sprite},
    {kMonsterRangeHook, kMonsterOnScreen, monster_wide_range},
    {kItemRangeHook, kShouldDisplayEntity, item_wide_range},
    {kMarginItemHook, kDrawFloorItem, draw_margin_floor_item},
};

// The game culls sprites left of x = -64, so 9-bit OAM X values 0x100..0x1BF
// can only be right-margin positions past 255.
int decode_margin_obj_x(int raw_x, int* out_x) {
    if (raw_x < 0x100 || raw_x >= 0x1C0) return 0;
    *out_x = raw_x;
    return 1;
}

std::uint16_t g_text_cnt = 0, g_text_hofs = 0, g_text_vofs = 0;

// Once the margins carry terrain, the native left border would cut through the
// middle of the picture.
int hide_left_border(int bg, int x, int y, int*, int*) {
    if (bg != kTextBg || x < 0 || x >= 8) return 0;
    const std::uint32_t tx = ((x + g_text_hofs) >> 3) & 31u, ty = ((y + g_text_vofs) >> 3) & 31u;
    const std::uint8_t* e = g_bus->vram_ptr() + ((g_text_cnt >> 8) & 0x1Fu) * 0x800u + (ty * 32 + tx) * 2;
    return ((e[0] | (e[1] << 8)) & 0x3FF) == kLeftBorderTile ? -1 : 0;
}

// Builds this frame's cells; false when the screen is not a dungeon floor.
bool build_floor_cells(const gbarecomp::ExtendedViewFrameInfo* f) {
    if (!f->io || f->io_size < 0x20 || !(io16(f, 0x00) & 0x0800u)) return false;

    Floor floor;
    floor.dungeon = read32(kDungeonPtr);
    if (floor.dungeon < kEwramBase || floor.dungeon + kDungeonReadEnd > kEwramBase + kEwramSize)
        return false;
    const std::uint32_t d = floor.dungeon;
    if (read8(d + kBlinded) || read8(d + kFloorMapMode)) return false;
    floor.oob_tile = read32(kOobTilePtr);
    floor.hallucinating = read8(d + kHallucinating) != 0;
    floor.show_invisible = read8(d + kShowInvisible) != 0;
    floor.stairs_kind = static_cast<std::int16_t>(read16(d + kStairsKind));

    // The game scrolls BG3 to the camera minus a shake offset; following the
    // live registers keeps the margins in step with the native picture.
    const int cam_x = static_cast<std::int16_t>(read16(d + kCameraPixelPos));
    const int cam_y = static_cast<std::int16_t>(read16(d + kCameraPixelPos + 2));
    const int hofs = io16(f, 0x1C), vofs = io16(f, 0x1E);
    g_world_x0 = cam_x + sign_extend9(hofs - cam_x);
    g_world_y0 = cam_y + sign_extend9(vofs - cam_y);

    const int left = static_cast<int>(f->extra_left), right = static_cast<int>(f->extra_right);
    g_first_tx = floor_div(g_world_x0 - left, 8);
    g_first_ty = floor_div(g_world_y0, 8);
    g_cols = floor_div(g_world_x0 + 239 + right, 8) - g_first_tx + 1;
    g_rows = floor_div(g_world_y0 + 159, 8) - g_first_ty + 1;
    if (g_cols > kMaxCols || g_rows > kMaxRows) return false;
    for (int r = 0; r < g_rows; ++r)
        for (int c = 0; c < g_cols; ++c)
            g_cells[r][c] = floor.entry(g_first_tx + c, g_first_ty + r);

    // BG3 is only terrain while the native picture agrees with the floor
    // data; title cards, menus and transitions reuse the layer.
    const std::uint16_t bg3cnt = io16(f, 0x0E);
    const std::uint8_t* screen = g_bus->vram_ptr() + ((bg3cnt >> 8) & 0x1Fu) * 0x800u;
    int match = 0, total = 0;
    for (int sy = 0; sy < 160; sy += 8) {
        for (int sx = 0; sx < 240; sx += 8) {
            const int rx = ((hofs + sx) >> 3) & 31, ry = ((vofs + sy) >> 3) & 31;
            const std::uint16_t hw = static_cast<std::uint16_t>(
                screen[(ry * 32 + rx) * 2] | (screen[(ry * 32 + rx) * 2 + 1] << 8));
            const int c = floor_div(g_world_x0 + sx, 8) - g_first_tx;
            const int r = floor_div(g_world_y0 + sy, 8) - g_first_ty;
            match += g_cells[r][c] == hw;
            ++total;
        }
    }
    return match * 10 >= total * 9;
}

}  // namespace

void extended_view_frame(const gbarecomp::ExtendedViewFrameInfo* frame) {
    g_bus = gbarecomp::active_bus();
    const bool floor = g_bus && build_floor_cells(frame);
    if (floor) {
        g_text_cnt = io16(frame, 0x08);
        g_text_hofs = io16(frame, 0x10);
        g_text_vofs = io16(frame, 0x12);
    }
    g_margin_right = floor ? static_cast<int>(frame->extra_right) : 0;
    g_margin_left = floor ? static_cast<int>(frame->extra_left) : 0;
    for (const MarginHook& hook : kMarginHooks) {
        gba_mod_register_function_entry_plugin(hook.id, hook.addr, 1, hook.callback);
        gba_mod_set_function_hook_enabled(hook.id, floor && g_margin_right > 0);
    }
    gba::g_ws_obj_x_provider = floor ? decode_margin_obj_x : nullptr;
    gba::g_ws_tilemap_provider = floor ? provide_margin_tile : nullptr;
    gba::g_ws_bg_xy_provider = floor ? hide_left_border : nullptr;
    gba::g_ws_bg_xy_provider_layers = 1u << kTextBg;
    gba::g_ws_authored_margin_layers = floor ? 1 : 0;
    gba::g_ws_pillarbox = floor ? 0 : 1;
}

}  // namespace pmd_widescreen
