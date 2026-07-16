#include "EnemyArt.h"
#include "Colors.h"
#include "UIHelper.h"
#include <iostream>
#include <algorithm>
#include <cstdio>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

namespace EnemyArt {

// PNG sheets in assets/sprites/, frames left to right, sliced by frame
// width. Alpha < 128 = transparent. A missing sheet loads as empty frames
// that render nothing.
static std::vector<Art> loadSheet(const char* path, int frameW) {
    std::vector<Art> frames;
    int w = 0, h = 0, comp = 0;
    unsigned char* data = stbi_load(path, &w, &h, &comp, 4);
    if (!data) return frames;
    if (frameW <= 0 || w < frameW) { stbi_image_free(data); return frames; }
    int count = w / frameW;
    for (int fidx = 0; fidx < count; fidx++) {
        Art art;
        art.trueColorGrid.assign(h, std::vector<RGB>(frameW, RGB{0, 0, 0}));
        art.trueColorOpaque.assign(h, std::vector<bool>(frameW, false));
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < frameW; x++) {
                const unsigned char* p = data + 4 * ((size_t)y * w + (size_t)fidx * frameW + x);
                if (p[3] < 128) continue;
                art.trueColorGrid[y][x] = { p[0], p[1], p[2] };
                art.trueColorOpaque[y][x] = true;
            }
        }
        frames.push_back(std::move(art));
    }
    stbi_image_free(data);
    return frames;
}

static Art frameOr(const std::vector<Art>& sheet, size_t i) {
    return i < sheet.size() ? sheet[i] : Art{};
}

// Every enemy ships the same 7-frame sheet:
//   idle A, idle B, attack windup, attack swing, attack impact, hit, death
struct ArtSet {
    Art idleA, idleB, atk1, atk2, atk3, hit, death;
    bool animated;
};

static ArtSet loadSet(const char* path) {
    std::vector<Art> v = loadSheet(path, 30);
    ArtSet s;
    s.animated = v.size() >= 7;
    s.idleA = frameOr(v, 0);
    s.idleB = frameOr(v, 1);
    s.atk1  = frameOr(v, 2);
    s.atk2  = frameOr(v, 3);
    s.atk3  = frameOr(v, 4);
    s.hit   = frameOr(v, 5);
    s.death = frameOr(v, 6);
    return s;
}

static const ArtSet MELEE_SET    = loadSet("assets/sprites/melee_goblin.png");
static const ArtSet RANGED_SET   = loadSet("assets/sprites/ranged_archer.png");
static const ArtSet TANK_SET     = loadSet("assets/sprites/tank_guardian.png");
static const ArtSet CASTER_SET   = loadSet("assets/sprites/caster_wizard.png");
static const ArtSet BEAST_SET    = loadSet("assets/sprites/beast.png"); // werewolf fallback
static const ArtSet UNDEAD_SET   = loadSet("assets/sprites/undead_skeleton.png");

// Per-name sheets. setEnemyVariant() matches the enemy name at encounter
// start; names without a sheet here fall back to their type's sprite.
// (Chimera intentionally maps to the werewolf BEAST_SET; Skeleton to the
// UNDEAD_SET, whose design it is.)
static const ArtSet MELEE_BANDIT     = loadSet("assets/sprites/melee_bandit.png");
static const ArtSet MELEE_WARRIOR    = loadSet("assets/sprites/melee_warrior.png");
static const ArtSet MELEE_RAIDER     = loadSet("assets/sprites/melee_raider.png");
static const ArtSet MELEE_BARBARIAN  = loadSet("assets/sprites/melee_barbarian.png");
static const ArtSet MELEE_BERSERKER  = loadSet("assets/sprites/melee_berserker.png");
static const ArtSet MELEE_GLADIATOR  = loadSet("assets/sprites/melee_gladiator.png");
static const ArtSet MELEE_ENFORCER   = loadSet("assets/sprites/melee_enforcer.png");
static const ArtSet BEAST_WOLF       = loadSet("assets/sprites/beast_wolf.png");
static const ArtSet BEAST_SPIDER     = loadSet("assets/sprites/beast_spider.png");
static const ArtSet BEAST_SERPENT    = loadSet("assets/sprites/beast_serpent.png");
static const ArtSet BEAST_WYVERN     = loadSet("assets/sprites/beast_wyvern.png");
static const ArtSet BEAST_BASILISK   = loadSet("assets/sprites/beast_basilisk.png");
static const ArtSet BEAST_MANTICORE  = loadSet("assets/sprites/beast_manticore.png");
static const ArtSet BEAST_COCKATRICE = loadSet("assets/sprites/beast_cockatrice.png");
static const ArtSet UNDEAD_GHOUL     = loadSet("assets/sprites/undead_ghoul.png");
static const ArtSet UNDEAD_WRAITH    = loadSet("assets/sprites/undead_wraith.png");
static const ArtSet UNDEAD_SPECTER   = loadSet("assets/sprites/undead_specter.png");
static const ArtSet UNDEAD_BANSHEE   = loadSet("assets/sprites/undead_banshee.png");
static const ArtSet UNDEAD_REVENANT  = loadSet("assets/sprites/undead_revenant.png");
static const ArtSet UNDEAD_LICH      = loadSet("assets/sprites/undead_lich.png");

static const ArtSet* namedVariant = nullptr;

void setEnemyVariant(const std::string& enemyName) {
    static const struct { const char* key; const ArtSet* set; } TABLE[] = {
        {"Bandit",     &MELEE_BANDIT},    {"Warrior",  &MELEE_WARRIOR},
        {"Raider",     &MELEE_RAIDER},    {"Barbarian",&MELEE_BARBARIAN},
        {"Berserker",  &MELEE_BERSERKER}, {"Gladiator",&MELEE_GLADIATOR},
        {"Enforcer",   &MELEE_ENFORCER},
        {"Wolf",       &BEAST_WOLF},      {"Spider",   &BEAST_SPIDER},
        {"Serpent",    &BEAST_SERPENT},   {"Wyvern",   &BEAST_WYVERN},
        {"Basilisk",   &BEAST_BASILISK},  {"Manticore",&BEAST_MANTICORE},
        {"Cockatrice", &BEAST_COCKATRICE},{"Chimera",  &BEAST_SET},
        {"Skeleton",   &UNDEAD_SET},      {"Ghoul",     &UNDEAD_GHOUL},
        {"Wraith",     &UNDEAD_WRAITH},   {"Specter",  &UNDEAD_SPECTER},
        {"Banshee",    &UNDEAD_BANSHEE},  {"Revenant", &UNDEAD_REVENANT},
        {"Lich",       &UNDEAD_LICH},
    };
    namedVariant = nullptr;
    for (const auto& e : TABLE) {
        if (enemyName.find(e.key) != std::string::npos) {
            namedVariant = e.set;
            return;
        }
    }
}
static const ArtSet COLOSSUS_SET     = loadSet("assets/sprites/boss_colossus.png");
static const ArtSet WITCH_SET        = loadSet("assets/sprites/boss_witch.png");
static const ArtSet WARLORD_SET      = loadSet("assets/sprites/boss_warlord.png");
static const ArtSet HYDRA_SET        = loadSet("assets/sprites/boss_hydra.png");
static const ArtSet DRAGON_SET       = loadSet("assets/sprites/boss_dragon.png");
static const ArtSet SHADOWKNIGHT_SET = loadSet("assets/sprites/boss_shadowknight.png");

// The player knight sheet has its own layout:
//   idle A, idle B, attack windup/sweep/thrust, block raise, block brace,
//   cast poison/burn/stun/weak, hit, death
static const std::vector<Art> PLAYER_SHEET = loadSheet("assets/sprites/player.png", 30);

static const Art KNIGHT_ART_A           = frameOr(PLAYER_SHEET, 0);
static const Art KNIGHT_ART_B           = frameOr(PLAYER_SHEET, 1);
static const Art KNIGHT_ART_ATK1        = frameOr(PLAYER_SHEET, 2);
static const Art KNIGHT_ART_ATK2        = frameOr(PLAYER_SHEET, 3);
static const Art KNIGHT_ART_STRIKE      = frameOr(PLAYER_SHEET, 4);
static const Art KNIGHT_ART_BLOCK1      = frameOr(PLAYER_SHEET, 5);
static const Art KNIGHT_ART_BLOCK2      = frameOr(PLAYER_SHEET, 6);
static const Art KNIGHT_ART_CAST_POISON = frameOr(PLAYER_SHEET, 7);
static const Art KNIGHT_ART_CAST_BURN   = frameOr(PLAYER_SHEET, 8);
static const Art KNIGHT_ART_CAST_STUN   = frameOr(PLAYER_SHEET, 9);
static const Art KNIGHT_ART_CAST_WEAK   = frameOr(PLAYER_SHEET, 10);
static const Art KNIGHT_ART_HIT         = frameOr(PLAYER_SHEET, 11);
static const Art KNIGHT_ART_DEATH       = frameOr(PLAYER_SHEET, 12);

// Backdrops rotate with progression: one per 10 encounters, cycling.
static const std::vector<Art> BG_SHEETS[] = {
    loadSheet("assets/sprites/bg_dungeon.png", 94),         // 1-10
    loadSheet("assets/sprites/bg_forest_day.png", 94),      // 11-20
    loadSheet("assets/sprites/bg_forest_night.png", 94),    // 21-30
    loadSheet("assets/sprites/bg_lake_night.png", 94),      // 31-40
    loadSheet("assets/sprites/bg_mountains_dusk.png", 94),  // 41-50
};
static const int BG_COUNT = (int)(sizeof(BG_SHEETS) / sizeof(BG_SHEETS[0]));

static const std::vector<Art>* bgSheet = &BG_SHEETS[0];
static bool bgPhase = false;

static const Art& sceneBgArt() {
    static const Art empty;
    const auto& s = *bgSheet;
    if (s.empty()) return empty;
    return s[(bgPhase && s.size() > 1) ? 1 : 0];
}

void setBattleBackdrop(int encounterNumber) {
    int idx = ((encounterNumber > 0 ? encounterNumber - 1 : 0) / 10) % BG_COUNT;
    bgSheet = &BG_SHEETS[idx];
}

static const ArtSet& artSet(EnemyType type, BossType boss) {
    switch (boss) {
        case BossType::STONE_COLOSSUS: return COLOSSUS_SET;
        case BossType::VILE_WITCH:     return WITCH_SET;
        case BossType::WARLORD:        return WARLORD_SET;
        case BossType::HYDRA:          return HYDRA_SET;
        case BossType::DRAGON:         return DRAGON_SET;
        case BossType::SHADOW_KNIGHT:  return SHADOWKNIGHT_SET;
        default: break;
    }
    if (namedVariant) return *namedVariant;
    switch (type) {
        case EnemyType::MELEE:  return MELEE_SET;
        case EnemyType::RANGED: return RANGED_SET;
        case EnemyType::TANK:   return TANK_SET;
        case EnemyType::CASTER: return CASTER_SET;
        case EnemyType::BEAST:  return BEAST_SET;
        case EnemyType::UNDEAD: return UNDEAD_SET;
        default:                return MELEE_SET;
    }
}

const Art& get(EnemyType type, BossType boss) {
    return artSet(type, boss).idleA;
}

const Art& getWalkFrame(EnemyType type, BossType boss) {
    const ArtSet& s = artSet(type, boss);
    if (!s.animated) return s.idleA;
    static int phase = 0;
    phase ^= 1;
    return phase ? s.idleB : s.idleA;
}

const Art& getHitArt(EnemyType type, BossType boss) {
    const ArtSet& s = artSet(type, boss);
    return s.animated ? s.hit : s.idleA;
}

const Art& getDeathArt(EnemyType type, BossType boss) {
    const ArtSet& s = artSet(type, boss);
    return s.animated ? s.death : s.idleA;
}

// --- rendering ---

static const RGB TRANSPARENT_BG = {13, 13, 15}; // matches the game's dark theme

// Damage flash: white, but blended (not flat-added) so the sprite's shading
// survives instead of washing out into a blob. Red stays reserved for buffs.
static RGB hitFlash(RGB c) {
    return { (unsigned char)(c.r * 0.45 + 140),
             (unsigned char)(c.g * 0.45 + 140),
             (unsigned char)(c.b * 0.45 + 140) };
}

// Ailment flashes: the afflicted sprite blends toward the status color.
static RGB tintPoison(RGB c) {
    return { (unsigned char)(c.r * 0.45 + 38), (unsigned char)(c.g * 0.45 + 110),
             (unsigned char)(c.b * 0.45 + 38) };
}
static RGB tintBurn(RGB c) {
    return { (unsigned char)(c.r * 0.45 + 132), (unsigned char)(c.g * 0.45 + 71),
             (unsigned char)(c.b * 0.45 + 22) };
}
static RGB tintStun(RGB c) {
    return { (unsigned char)(c.r * 0.45 + 134), (unsigned char)(c.g * 0.45 + 118),
             (unsigned char)(c.b * 0.45 + 33) };
}
static RGB tintWeak(RGB c) {
    return { (unsigned char)(c.r * 0.45 + 49), (unsigned char)(c.g * 0.45 + 66),
             (unsigned char)(c.b * 0.45 + 129) };
}

static RGB (*statusTint(CastGlow glow))(RGB) {
    switch (glow) {
        case CastGlow::BURN: return tintBurn;
        case CastGlow::STUN: return tintStun;
        case CastGlow::WEAK: return tintWeak;
        default:             return tintPoison;
    }
}

static RGB darken(RGB c, double factor) {
    return { (unsigned char)(c.r * factor), (unsigned char)(c.g * factor), (unsigned char)(c.b * factor) };
}

// One-shot buff flashes (strong tint).
static RGB tintStrength(RGB c) {
    return { (unsigned char)std::min(255, c.r + 90),
             (unsigned char)(c.g * 0.70), (unsigned char)(c.b * 0.70) };
}

static RGB tintHeal(RGB c) {
    return { (unsigned char)(c.r * 0.78),
             (unsigned char)std::min(255, c.g + 85),
             (unsigned char)(c.b * 0.78) };
}

// Persistent status auras (softer, active while the status lasts).
static RGB auraStrength(RGB c) {
    return { (unsigned char)std::min(255, c.r + 55),
             (unsigned char)(c.g * 0.85), (unsigned char)(c.b * 0.85) };
}

static RGB auraWeak(RGB c) {
    return { (unsigned char)(c.r * 0.85), (unsigned char)(c.g * 0.85),
             (unsigned char)std::min(255, c.b + 65) };
}

static RGB auraPoison(RGB c) {
    return { (unsigned char)(c.r * 0.85), (unsigned char)std::min(255, c.g + 55),
             (unsigned char)(c.b * 0.85) };
}

static RGB auraBurn(RGB c) {
    return { (unsigned char)std::min(255, c.r + 60),
             (unsigned char)(c.g * 0.88), (unsigned char)(c.b * 0.80) };
}

static RGB auraStun(RGB c) {
    return { (unsigned char)std::min(255, c.r + 55),
             (unsigned char)std::min(255, c.g + 48), (unsigned char)(c.b * 0.80) };
}

static AuraFlags auraKnight;
static AuraFlags auraEnemy;

// Advances only on idle redraws (animateBattleIdleAt), each ~450ms apart -
// close enough to wall-clock without pulling in <chrono> for a cosmetic cycle.
static int auraElapsedMs = 0;
static const int AURA_TICK_MS  = 450;
static const int AURA_CYCLE_MS = 2000;

void setBattleAuras(AuraFlags knight, AuraFlags enemy) {
    auraKnight = knight;
    auraEnemy = enemy;
}

// Collects every tint the side currently carries and rotates through them
// every AURA_CYCLE_MS. A side with one active status just holds that color.
static RGB (*pickAuraTint(const AuraFlags& f))(RGB) {
    RGB (*tints[5])(RGB);
    int n = 0;
    if (f.strength) tints[n++] = auraStrength;
    if (f.weak)     tints[n++] = auraWeak;
    if (f.poison)   tints[n++] = auraPoison;
    if (f.burn)     tints[n++] = auraBurn;
    if (f.stun)     tints[n++] = auraStun;
    if (n == 0) return nullptr;
    int idx = (auraElapsedMs / AURA_CYCLE_MS) % n;
    return tints[idx];
}

static size_t artRows(const Art& a) {
    return a.trueColorGrid.size();
}

static size_t artRowWidth(const Art& a, size_t r) {
    return r < artRows(a) ? a.trueColorGrid[r].size() : 0;
}

static RGB artPixel(const Art& a, size_t r, size_t c) {
    if (r >= artRows(a)) return TRANSPARENT_BG;
    const auto& row = a.trueColorGrid[r];
    const auto& op = a.trueColorOpaque[r];
    if (c >= row.size() || !op[c]) return TRANSPARENT_BG;
    return row[c];
}

static bool artOpaque(const Art& a, size_t r, size_t c) {
    if (r >= artRows(a)) return false;
    const auto& op = a.trueColorOpaque[r];
    return c < op.size() && op[c];
}

// Emits one terminal row from source rows `row`/`row+1`: foreground = top
// pixel, background = bottom, glyph U+2580. Packing two rows per cell
// corrects for terminal cells being ~2x taller than wide. Everything goes
// into one buffer (single write) with color codes coalesced across runs.
static size_t appendRowPair(std::string& out, const Art& art, size_t row, RGB (*transform)(RGB)) {
    size_t width = std::max(artRowWidth(art, row), artRowWidth(art, row + 1));
    char code[32];
    int lastFg = -1, lastBg = -1;
    for (size_t col = 0; col < width; col++) {
        RGB t = artPixel(art, row, col);
        RGB b = artPixel(art, row + 1, col);
        if (transform) { t = transform(t); b = transform(b); }
        int fg = (t.r << 16) | (t.g << 8) | t.b;
        int bg = (b.r << 16) | (b.g << 8) | b.b;
        if (fg != lastFg) {
            snprintf(code, sizeof(code), "\033[38;2;%d;%d;%dm", (int)t.r, (int)t.g, (int)t.b);
            out.append(code);
            lastFg = fg;
        }
        if (bg != lastBg) {
            snprintf(code, sizeof(code), "\033[48;2;%d;%d;%dm", (int)b.r, (int)b.g, (int)b.b);
            out.append(code);
            lastBg = bg;
        }
        out.append("\xE2\x96\x80"); // U+2580 UPPER HALF BLOCK
    }
    return width;
}

static void renderGrid(const Art& art, int indent, RGB (*transform)(RGB)) {
    std::string pad(indent > 0 ? indent : 0, ' ');
    std::string out;
    out.reserve(4096);
    size_t rows = artRows(art);
    for (size_t row = 0; row < rows; row += 2) {
        out.append(pad);
        appendRowPair(out, art, row, transform);
        out.append(Color::RESET);
        out.push_back('\n');
    }
    std::cout.write(out.data(), (std::streamsize)out.size());
}

// --- battle scene ---

static const int BATTLE_LEFT_INDENT = 2;
static const int BATTLE_RIGHT_COL   = 64;

// Alternates the knight's two idle frames.
static const Art& knightFrame() {
    static int phase = 0;
    phase ^= 1;
    return phase ? KNIGHT_ART_B : KNIGHT_ART_A;
}

// Composites the scene: left sprite over right sprite over backdrop, all
// bottom-aligned to the floor line. Explicit transforms (flashes) override
// the persistent status auras. rightCol can be pulled in for a lunge.
static void renderBattle(const Art& left, const Art& right,
                         RGB (*lt)(RGB), RGB (*rt)(RGB),
                         int rightCol = BATTLE_RIGHT_COL) {
    const Art& bg = sceneBgArt();
    size_t sceneW = std::max(artRowWidth(bg, 0), (size_t)(BATTLE_RIGHT_COL + 30));
    size_t rows = std::max(std::max(artRows(left), artRows(right)), artRows(bg));
    size_t loff = rows - artRows(left);
    size_t roff = rows - artRows(right);
    size_t boff = rows - artRows(bg);

    RGB (*leftFx)(RGB)  = lt ? lt : pickAuraTint(auraKnight);
    RGB (*rightFx)(RGB) = rt ? rt : pickAuraTint(auraEnemy);

    auto pixAt = [&](size_t r, size_t c) -> RGB {
        if (c >= (size_t)BATTLE_LEFT_INDENT && r >= loff) {
            size_t lc = c - (size_t)BATTLE_LEFT_INDENT;
            if (artOpaque(left, r - loff, lc)) {
                RGB p = artPixel(left, r - loff, lc);
                return leftFx ? leftFx(p) : p;
            }
        }
        if (c >= (size_t)rightCol && r >= roff) {
            size_t rc = c - (size_t)rightCol;
            if (artOpaque(right, r - roff, rc)) {
                RGB p = artPixel(right, r - roff, rc);
                return rightFx ? rightFx(p) : p;
            }
        }
        if (r >= boff && artOpaque(bg, r - boff, c)) return artPixel(bg, r - boff, c);
        return TRANSPARENT_BG;
    };

    std::string out;
    out.reserve(16384);
    char code[32];
    for (size_t row = 0; row < rows; row += 2) {
        int lastFg = -1, lastBg = -1;
        for (size_t col = 0; col < sceneW; col++) {
            RGB t = pixAt(row, col);
            RGB b = pixAt(row + 1, col);
            int fg = (t.r << 16) | (t.g << 8) | t.b;
            int bgc = (b.r << 16) | (b.g << 8) | b.b;
            if (fg != lastFg) {
                snprintf(code, sizeof(code), "\033[38;2;%d;%d;%dm", (int)t.r, (int)t.g, (int)t.b);
                out.append(code);
                lastFg = fg;
            }
            if (bgc != lastBg) {
                snprintf(code, sizeof(code), "\033[48;2;%d;%d;%dm", (int)b.r, (int)b.g, (int)b.b);
                out.append(code);
                lastBg = bgc;
            }
            out.append("\xE2\x96\x80");
        }
        out.append(Color::RESET);
        out.push_back('\n');
    }
    std::cout.write(out.data(), (std::streamsize)out.size());
}

static size_t battlePairRows(const Art& right) {
    size_t rows = std::max(std::max(artRows(KNIGHT_ART_A), artRows(right)), artRows(sceneBgArt()));
    return (rows + 1) / 2;
}

void print(const Art& art, int indent) {
    renderGrid(art, indent, nullptr);
    std::cout << "\n";
}

// Moves the cursor back over the just-printed block so the next frame
// overwrites it in place.
static void moveCursorUp(size_t n) {
    if (n > 0) std::cout << "\033[" << n << "A";
}

void printBattle(EnemyType type, BossType boss) {
    renderBattle(knightFrame(), getWalkFrame(type, boss), nullptr, nullptr);
    std::cout << "\n";
}

void animateBattleIdleAt(EnemyType type, BossType boss, int startRow) {
    bgPhase = !bgPhase; // ambient flicker (torches, fireflies, shimmer)
    auraElapsedMs += AURA_TICK_MS;
    // SetConsoleCursorPosition acts immediately - flush so buffered output
    // lands before each jump.
    std::cout.flush();
    int cur = UIHelper::getCursorRow();
    UIHelper::setCursorRow(startRow);
    renderBattle(knightFrame(), getWalkFrame(type, boss), nullptr, nullptr);
    std::cout.flush();
    UIHelper::setCursorRow(cur);
}

void printBattleAttack(EnemyType type, BossType boss) {
    const Art& knight = KNIGHT_ART_A;
    const ArtSet& s = artSet(type, boss);
    if (s.animated) {
        renderBattle(knight, s.atk1, nullptr, nullptr);
        UIHelper::pause(90);
        moveCursorUp(battlePairRows(s.atk1));
        renderBattle(knight, s.atk2, nullptr, nullptr);
        UIHelper::pause(90);
        moveCursorUp(battlePairRows(s.atk2));
        renderBattle(knight, s.atk3, nullptr, nullptr);
        UIHelper::pause(150);
    } else {
        // No frames: nudge the enemy toward the knight for a beat.
        renderBattle(knight, s.idleA, nullptr, nullptr, BATTLE_RIGHT_COL - 3);
        UIHelper::pause(120);
        moveCursorUp(battlePairRows(s.idleA));
        renderBattle(knight, s.idleA, nullptr, nullptr);
    }
    std::cout << "\n";
}

void printBattleHit(EnemyType type, BossType boss) {
    // Knight swings; the enemy flashes with its hit face on the final frame.
    const Art& idleEnemy = get(type, boss);
    renderBattle(KNIGHT_ART_ATK1, idleEnemy, nullptr, nullptr);
    UIHelper::pause(70);
    moveCursorUp(battlePairRows(idleEnemy));
    renderBattle(KNIGHT_ART_ATK2, idleEnemy, nullptr, nullptr);
    UIHelper::pause(70);
    moveCursorUp(battlePairRows(idleEnemy));
    renderBattle(KNIGHT_ART_STRIKE, getHitArt(type, boss),
                 nullptr, hitFlash);
    UIHelper::pause(150);
    std::cout << "\n";
}

void printBattleBlock(EnemyType type, BossType boss) {
    const Art& idleEnemy = get(type, boss);
    renderBattle(KNIGHT_ART_BLOCK1, idleEnemy, nullptr, nullptr);
    UIHelper::pause(90);
    moveCursorUp(battlePairRows(idleEnemy));
    renderBattle(KNIGHT_ART_BLOCK2, idleEnemy, nullptr, nullptr);
    UIHelper::pause(260);
    std::cout << "\n";
}

void printBattleCast(EnemyType type, BossType boss, CastGlow glow) {
    const Art* cast = &KNIGHT_ART_CAST_POISON;
    switch (glow) {
        case CastGlow::POISON: cast = &KNIGHT_ART_CAST_POISON; break;
        case CastGlow::BURN:   cast = &KNIGHT_ART_CAST_BURN;   break;
        case CastGlow::STUN:   cast = &KNIGHT_ART_CAST_STUN;   break;
        case CastGlow::WEAK:   cast = &KNIGHT_ART_CAST_WEAK;   break;
    }
    renderBattle(*cast, get(type, boss), nullptr, nullptr);
    UIHelper::pause(320);
    std::cout << "\n";
}

void printBattleStatusFlash(EnemyType type, BossType boss, CastGlow glow, bool onEnemy) {
    RGB (*tint)(RGB) = statusTint(glow);
    renderBattle(KNIGHT_ART_A, get(type, boss),
                 onEnemy ? nullptr : tint, onEnemy ? tint : nullptr);
    UIHelper::pause(1000);
    std::cout << "\n";
}

void printBattleDeath(EnemyType type, BossType boss) {
    renderBattle(KNIGHT_ART_A, getDeathArt(type, boss),
                 nullptr, [](RGB c) { return darken(c, 0.35); });
    UIHelper::pause(300);
    std::cout << "\n";
}

void printBattleKnightHit(EnemyType type, BossType boss) {
    renderBattle(KNIGHT_ART_HIT, get(type, boss),
                 hitFlash, nullptr);
    UIHelper::pause(100);
    std::cout << "\n";
}

void printBattleKnightDeath(EnemyType type, BossType boss) {
    renderBattle(KNIGHT_ART_DEATH, get(type, boss),
                 [](RGB c) { return darken(c, 0.35); }, nullptr);
    UIHelper::pause(400);
    std::cout << "\n";
}

void printBattleSelfBuff(EnemyType type, BossType boss, SelfGlow glow) {
    RGB (*tint)(RGB) = (glow == SelfGlow::STRENGTH) ? tintStrength : tintHeal;
    renderBattle(KNIGHT_ART_A, get(type, boss), tint, nullptr);
    UIHelper::pause(280);
    std::cout << "\n";
}

}
