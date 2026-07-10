#include "EnemyArt.h"
#include "Colors.h"
#include "UIHelper.h"
#include <iostream>
#include <algorithm>

namespace EnemyArt {

// Goblin grunt bust: pointed ears, red eyes, grinning teeth, dark leather straps.
// This one is hand-tuned as the style test; the rest below are placeholder blobs
// (solid color + outline) until this technique is confirmed to read well.
static const Art MELEE_ART = {
    {
        ".K..........K.",
        ".KKGGGGGGGGKK.",
        "KGGGGGGGGGGGGK",
        "KGGGGGGGGGGGGK",
        "KGGHGGGGGGHGGK",
        "KGGGRRGGRRGGGK",
        "KGGGrRGGRrGGGK",
        "KGGGGGGGGGGGGK",
        "KGGGGggggGGGGK",
        "KGGGGggggGGGGK",
        "KGGGWWKKWWGGGK",
        "KGGGKKKKKKGGGK",
        ".KGGGGGGGGGGK.",
        "..KggggggggK..",
        "KooooooooooooK",
        "KboooooooooobK",
        "KooGGGGGGGGooK",
        ".KKKKKKKKKKKK.",
    },
    {
        {'K', {18, 18, 20}},
        {'G', {90, 150, 60}},
        {'g', {50, 90, 35}},
        {'H', {130, 190, 90}},
        {'R', {220, 40, 40}},
        {'r', {130, 20, 20}},
        {'W', {235, 235, 225}},
        {'o', {110, 70, 40}},
        {'b', {70, 45, 25}},
    }
};

// --- Placeholder blobs (solid color + outline) - to be replaced with real sprites ---
static Art placeholderBlob(RGB body) {
    Art art;
    art.grid = {
        ".KKKKKKKK.",
        "KBBBBBBBBK",
        "KBBBBBBBBK",
        "KBBBBBBBBK",
        "KBBBBBBBBK",
        "KBBBBBBBBK",
        "KBBBBBBBBK",
        ".KKKKKKKK.",
    };
    art.palette = { {'K', {18, 18, 20}}, {'B', body} };
    return art;
}

static const Art RANGED_ART          = placeholderBlob({190, 170, 60});
static const Art TANK_ART            = placeholderBlob({90, 110, 160});
static const Art CASTER_ART          = placeholderBlob({150, 60, 180});
static const Art BEAST_ART           = placeholderBlob({190, 110, 40});
static const Art UNDEAD_ART          = placeholderBlob({170, 175, 180});
static const Art STONE_COLOSSUS_ART  = placeholderBlob({140, 140, 145});
static const Art VILE_WITCH_ART      = placeholderBlob({140, 60, 170});
static const Art THUNDER_BEAST_ART   = placeholderBlob({210, 190, 50});
static const Art HYDRA_ART           = placeholderBlob({60, 150, 80});
static const Art DRAGON_ART          = placeholderBlob({200, 100, 30});

const Art& get(EnemyType type, BossType boss) {
    switch (boss) {
        case BossType::STONE_COLOSSUS: return STONE_COLOSSUS_ART;
        case BossType::VILE_WITCH:     return VILE_WITCH_ART;
        case BossType::WARLORD:        return THUNDER_BEAST_ART;
        case BossType::HYDRA:          return HYDRA_ART;
        case BossType::DRAGON:         return DRAGON_ART;
        default: break;
    }
    switch (type) {
        case EnemyType::MELEE:  return MELEE_ART;
        case EnemyType::RANGED: return RANGED_ART;
        case EnemyType::TANK:   return TANK_ART;
        case EnemyType::CASTER: return CASTER_ART;
        case EnemyType::BEAST:  return BEAST_ART;
        case EnemyType::UNDEAD: return UNDEAD_ART;
        default:                return MELEE_ART;
    }
}

static const RGB TRANSPARENT_BG = {13, 13, 15}; // near-black, matches the game's dark theme

static RGB colorOf(const Art& art, char c) {
    if (c == '.' || c == ' ') return TRANSPARENT_BG;
    auto it = art.palette.find(c);
    return it != art.palette.end() ? it->second : TRANSPARENT_BG;
}

static RGB brighten(RGB c, int amt) {
    return { (unsigned char)std::min(255, c.r + amt),
             (unsigned char)std::min(255, c.g + amt),
             (unsigned char)std::min(255, c.b + amt) };
}

static RGB darken(RGB c, double factor) {
    return { (unsigned char)(c.r * factor), (unsigned char)(c.g * factor), (unsigned char)(c.b * factor) };
}

// Packs two grid rows into one printed terminal row: foreground = top pixel,
// background = bottom pixel, glyph = upper-half-block (U+2580).
static void renderGrid(const std::vector<std::string>& grid, const Art& art, int indent,
                        RGB (*transform)(RGB)) {
    std::string pad(indent > 0 ? indent : 0, ' ');
    for (size_t row = 0; row < grid.size(); row += 2) {
        std::cout << pad;
        const std::string& top = grid[row];
        const std::string  bot = (row + 1 < grid.size()) ? grid[row + 1] : std::string(top.size(), '.');
        size_t width = std::max(top.size(), bot.size());
        for (size_t col = 0; col < width; col++) {
            char tc = col < top.size() ? top[col] : '.';
            char bc = col < bot.size() ? bot[col] : '.';
            RGB t = colorOf(art, tc);
            RGB b = colorOf(art, bc);
            if (transform) { t = transform(t); b = transform(b); }
            std::cout << "\033[38;2;" << (int)t.r << ";" << (int)t.g << ";" << (int)t.b << "m"
                       << "\033[48;2;" << (int)b.r << ";" << (int)b.g << ";" << (int)b.b << "m"
                       << "\xE2\x96\x80"; // UTF-8 for U+2580 UPPER HALF BLOCK
        }
        std::cout << Color::RESET << "\n";
    }
}

void print(const Art& art, int indent) {
    renderGrid(art.grid, art, indent, nullptr);
    std::cout << "\n";
}

void printLunge(const Art& art) {
    renderGrid(art.grid, art, 0, nullptr); // shifted to the left edge - reads as "closing in"
    UIHelper::pause(120);
    std::cout << "\n";
}

void printHitFlash(const Art& art) {
    renderGrid(art.grid, art, 2, [](RGB c) { return brighten(c, 120); });
    UIHelper::pause(100);
    std::cout << "\n";
}

void printDeath(const Art& art) {
    renderGrid(art.grid, art, 2, [](RGB c) { return darken(c, 0.35); });
    UIHelper::pause(300);
    std::cout << "\n";
}

}
