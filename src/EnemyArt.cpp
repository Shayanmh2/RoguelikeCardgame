// SDL2 battle scene.
//
// The terminal build drew these sprites as half-block text cells inline in the
// output stream, using moveCursorUp() to redraw the same rows. Here the scene
// is a real region above the console text: sheets load as GPU textures, the
// scene keeps persistent state (poses, tints, auras), and Platform::frame()
// draws it every frame. The print* entry points below set that state and then
// hold it for the same durations the terminal version paused for, so combat
// keeps the exact rhythm it was authored with.
#include "EnemyArt.h"
#include "Audio.h"
#include "Console.h"
#include "Platform.h"
#include "UIHelper.h"

#include <algorithm>
#include <string>
#include <vector>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

namespace EnemyArt {
namespace {

// --- color transforms -------------------------------------------------
// Every tint in the terminal build was "multiply each channel, then add a
// constant". Both halves map onto SDL draw calls exactly: the multiply is a
// color mod on a normal blit, the add is an additive blit of the sprite's
// white silhouette. So these are the original numbers, not approximations.
struct Tint {
    float mulR = 1, mulG = 1, mulB = 1;
    int addR = 0, addG = 0, addB = 0;
    bool identity() const {
        return mulR == 1 && mulG == 1 && mulB == 1 && addR == 0 && addG == 0 && addB == 0;
    }
};

// Damage flash: white, blended rather than flat-added so the sprite's shading
// survives. Red is reserved for buffs. (hitFlash: c*0.45 + 140)
const Tint HIT_FLASH  { 0.45f, 0.45f, 0.45f, 140, 140, 140 };
const Tint TINT_POISON{ 0.45f, 0.45f, 0.45f,  38, 110,  38 };
const Tint TINT_BURN  { 0.45f, 0.45f, 0.45f, 132,  71,  22 };
const Tint TINT_STUN  { 0.45f, 0.45f, 0.45f, 134, 118,  33 };
const Tint TINT_WEAK  { 0.45f, 0.45f, 0.45f,  49,  66, 129 };
const Tint DEATH_DARK { 0.35f, 0.35f, 0.35f,   0,   0,   0 };

const Tint TINT_STRENGTH{ 1.00f, 0.70f, 0.70f, 90,  0,  0 };
const Tint TINT_HEAL    { 0.78f, 1.00f, 0.78f,  0, 85,  0 };

const Tint AURA_STRENGTH{ 1.00f, 0.85f, 0.85f, 55,  0,  0 };
const Tint AURA_WEAK    { 0.85f, 0.85f, 1.00f,  0,  0, 65 };
const Tint AURA_POISON  { 0.85f, 1.00f, 0.85f,  0, 55,  0 };
const Tint AURA_BURN    { 1.00f, 1.00f, 0.75f, 60, 22,  0 };
const Tint AURA_STUN    { 1.00f, 1.00f, 0.80f, 55, 48,  0 };

Tint statusTint(CastGlow glow) {
    switch (glow) {
        case CastGlow::BURN: return TINT_BURN;
        case CastGlow::STUN: return TINT_STUN;
        case CastGlow::WEAK: return TINT_WEAK;
        default:             return TINT_POISON;
    }
}

// --- sheets -----------------------------------------------------------
struct Sheet {
    SDL_Texture* tex = nullptr;        // the artwork
    SDL_Texture* silhouette = nullptr; // same alpha, all-white RGB - carries the additive term
    int frameW = 30, frameH = 32, count = 0;
    // Mean colour of this art's darker half, computed once at load. Backdrops
    // use it to tint the window ground; everything else just ignores it.
    SDL_Color darkAvg{ 13, 13, 15, 255 };
    bool ok() const { return tex != nullptr && count > 0; }
};

Sheet loadSheet(const std::string& path, int frameW) {
    Sheet s;
    s.frameW = frameW;
    int w = 0, h = 0, comp = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (!data) return s;
    if (frameW <= 0 || w < frameW) { stbi_image_free(data); return s; }

    s.frameH = h;
    s.count = w / frameW;

    SDL_Renderer* r = Platform::renderer();
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(data, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (surf) {
        s.tex = SDL_CreateTextureFromSurface(r, surf);
        SDL_FreeSurface(surf);
    }

    // White copy: RGB forced to 255, alpha preserved. Additively blitting this
    // with a color mod adds a flat constant only where the sprite is opaque,
    // which is what the terminal build's "+140" style tints did per pixel.
    std::vector<unsigned char> white((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        white[i * 4 + 0] = 255; white[i * 4 + 1] = 255; white[i * 4 + 2] = 255;
        white[i * 4 + 3] = data[i * 4 + 3];
    }
    SDL_Surface* wsurf = SDL_CreateRGBSurfaceWithFormatFrom(white.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
    if (wsurf) {
        s.silhouette = SDL_CreateTextureFromSurface(r, wsurf);
        SDL_FreeSurface(wsurf);
    }
    // Average the darker half of the opaque pixels. Taking the whole image
    // would wash the tone out with sky and highlights; taking only the very
    // darkest pixels lands on near-black and loses the hue entirely.
    {
        std::vector<unsigned char> lum;
        lum.reserve((size_t)w * h);
        for (size_t i = 0; i < (size_t)w * h; i++)
            if (data[i * 4 + 3] > 8)
                lum.push_back((unsigned char)((data[i*4+0]*77 + data[i*4+1]*150 + data[i*4+2]*29) >> 8));
        if (!lum.empty()) {
            std::vector<unsigned char> sorted = lum;
            std::nth_element(sorted.begin(), sorted.begin() + sorted.size()/2, sorted.end());
            const unsigned char cut = sorted[sorted.size()/2];
            unsigned long long ar = 0, ag = 0, ab = 0, n = 0;
            for (size_t i = 0; i < (size_t)w * h; i++) {
                if (data[i * 4 + 3] <= 8) continue;
                unsigned char l = (unsigned char)((data[i*4+0]*77 + data[i*4+1]*150 + data[i*4+2]*29) >> 8);
                if (l > cut) continue;
                ar += data[i*4+0]; ag += data[i*4+1]; ab += data[i*4+2]; n++;
            }
            if (n) s.darkAvg = SDL_Color{ (Uint8)(ar/n), (Uint8)(ag/n), (Uint8)(ab/n), 255 };
        }
    }

    stbi_image_free(data);

    if (s.tex) {
        SDL_SetTextureScaleMode(s.tex, SDL_ScaleModeNearest);
        SDL_SetTextureBlendMode(s.tex, SDL_BLENDMODE_BLEND);
    }
    if (s.silhouette) {
        SDL_SetTextureScaleMode(s.silhouette, SDL_ScaleModeNearest);
        SDL_SetTextureBlendMode(s.silhouette, SDL_BLENDMODE_ADD);
    }
    return s;
}

// Every enemy ships the same 7-frame sheet:
//   idle A, idle B, attack windup, attack swing, attack impact, hit, death
struct ArtSet {
    Sheet sheet;
    bool loaded = false;
    bool animated = false;
};

enum : int { F_IDLE_A = 0, F_IDLE_B = 1, F_ATK1 = 2, F_ATK2 = 3, F_ATK3 = 4, F_HIT = 5, F_DEATH = 6 };

// Assets resolve relative to the executable, not the working directory, so the
// game runs the same whether it's launched from a shell or a file manager.
std::string basePath() {
    static std::string base = Audio::exeDir();
    return base;
}

ArtSet loadSet(const char* rel) {
    ArtSet s;
    s.sheet = loadSheet(basePath() + rel, 30);
    s.loaded = s.sheet.ok();
    s.animated = s.sheet.count >= 7;
    return s;
}

// Sheets are loaded lazily on first use: they need a live SDL renderer, which
// rules out the terminal build's file-scope static initialization.
struct Library {
    ArtSet MELEE, RANGED, TANK, CASTER, BEAST, UNDEAD;
    ArtSet COLOSSUS, WITCH, WARLORD, HYDRA, DRAGON, SHADOWKNIGHT;
    ArtSet named[45];
    Sheet player, slashFx, castFx;
    Sheet bg[5], tutorialBg, titleBg, bloodMoonBg;

    Library() {
        MELEE  = loadSet("assets/sprites/melee_goblin.png");
        RANGED = loadSet("assets/sprites/ranged_archer.png");
        TANK   = loadSet("assets/sprites/tank_guardian.png");
        CASTER = loadSet("assets/sprites/caster_wizard.png");
        BEAST  = loadSet("assets/sprites/beast.png");
        UNDEAD = loadSet("assets/sprites/undead_skeleton.png");

        COLOSSUS     = loadSet("assets/sprites/boss_colossus.png");
        WITCH        = loadSet("assets/sprites/boss_witch.png");
        WARLORD      = loadSet("assets/sprites/boss_warlord.png");
        HYDRA        = loadSet("assets/sprites/boss_hydra.png");
        DRAGON       = loadSet("assets/sprites/boss_dragon.png");
        SHADOWKNIGHT = loadSet("assets/sprites/boss_shadowknight.png");

        player  = loadSheet(basePath() + "assets/sprites/player.png", 30);
        slashFx = loadSheet(basePath() + "assets/sprites/player_slash_fx.png", 30);
        castFx  = loadSheet(basePath() + "assets/sprites/player_cast_fx.png", 30);
        const char* bgFiles[5] = {
            // TODO: the two dungeon sheets are one layout recoloured, with a torch
            // on a strict 16-column period and no variation across 512px, so the
            // first twenty fights read as the same corridor twice. (An earlier
            // note here blamed a missing depth pass and called zones 3-5 better;
            // that was wrong - the mountain sheet is plainer than either dungeon.)
            "assets/sprites/bg_dungeon.png",        // 1-10
            "assets/sprites/bg_dungeon_purple.png", // 11-20
            "assets/sprites/bg_forest_night.png",   // 21-30
            "assets/sprites/bg_lake_night.png",     // 31-40
            "assets/sprites/bg_mountains_dusk.png", // 41-50
        };
        // Backdrops are 256 columns wide, not 94: painted long enough to span
        // the window so the scene has no bare sides. Character layout still
        // uses the original 94-unit span (backdropW), so the fight stays
        // centre-framed while the art runs edge to edge behind it.
        for (int i = 0; i < 5; i++) bg[i] = loadSheet(basePath() + bgFiles[i], 256);
        tutorialBg = loadSheet(basePath() + "assets/sprites/bg_forest_day.png", 256);
        titleBg    = loadSheet(basePath() + "assets/sprites/bg_title.png", 256);
        bloodMoonBg = loadSheet(basePath() + "assets/sprites/bg_forest_bloodmoon.png", 256);
    }
};

Library& lib() {
    static Library L;
    return L;
}

// Per-name sheets, matched against the enemy's name at encounter start. Same
// table and same fallback rule as the terminal build: a name whose PNG is
// missing falls through to its type's generic sprite.
struct NamedEntry { const char* key; const char* file; };
const NamedEntry NAMED_TABLE[] = {
    {"Bandit","melee_bandit"}, {"Warrior","melee_warrior"}, {"Raider","melee_raider"},
    {"Knight","melee_knight"}, {"Berserker","melee_berserker"}, {"Gladiator","melee_gladiator"},
    {"Enforcer","melee_enforcer"},
    {"Wolf","beast_wolf"}, {"Spider","beast_spider"}, {"Serpent","beast_serpent"},
    {"Wyvern","beast_wyvern"}, {"Basilisk","beast_basilisk"}, {"Manticore","beast_manticore"},
    {"Cockatrice","beast_cockatrice"}, {"Fleshmass","beast_fleshmass"},
    {"Moonstruck","beast_Moonstruck"},
    {"Skeleton","undead_skeleton"}, {"Ghoul","undead_ghoul"}, {"Wraith","undead_wraith"},
    {"Specter","undead_specter"}, {"Banshee","undead_banshee"}, {"Revenant","undead_revenant"},
    {"Lich","undead_lich"},
    {"Barbarian","tank_barbarian"}, {"Sentinel","tank_sentinel"}, {"Warden","tank_warden"},
    {"Paladin","tank_paladin"}, {"Bastion","tank_bastion"}, {"Fortress","tank_fortress"},
    {"Orc","tank_orc"}, {"Slime","tutorial_slime"},
    {"Enchanter","caster_enchanter"}, {"Vampire","caster_vampire"}, {"Sage","caster_sage"},
    {"Sorcerer","caster_sorcerer"}, {"Mystic","caster_mystic"}, {"Archon","caster_archon"},
    {"Spellmaster","caster_spellmaster"},
    {"Falcon","ranged_falcon"}, {"Assassin","ranged_assassin"}, {"Omneye","ranged_omneye"},
    {"Deadeye","ranged_deadeye"},
};
const int NAMED_COUNT = (int)(sizeof(NAMED_TABLE) / sizeof(NAMED_TABLE[0]));

const ArtSet* gNamedVariant = nullptr;
// The summoned add, if the fight has one standing.
const ArtSet* gCompanion = nullptr;

const ArtSet& artSet(EnemyType type, BossType boss) {
    Library& L = lib();
    switch (boss) {
        case BossType::STONE_COLOSSUS: return L.COLOSSUS;
        case BossType::VILE_WITCH:     return L.WITCH;
        case BossType::WARLORD:        return L.WARLORD;
        case BossType::HYDRA:          return L.HYDRA;
        case BossType::DRAGON:         return L.DRAGON;
        case BossType::SHADOW_KNIGHT:  return L.SHADOWKNIGHT;
        default: break;
    }
    if (gNamedVariant && gNamedVariant->loaded) return *gNamedVariant;
    switch (type) {
        case EnemyType::MELEE:  return L.MELEE;
        case EnemyType::RANGED: return L.RANGED;
        case EnemyType::TANK:   return L.TANK;
        case EnemyType::CASTER: return L.CASTER;
        case EnemyType::BEAST:  return L.BEAST;
        case EnemyType::UNDEAD: return L.UNDEAD;
        default:                return L.MELEE;
    }
}

// --- scene state ------------------------------------------------------
// The battle screen needs roughly this many text rows: a 4-line encounter
// header, the ~11-line combat status block (both HP bars, armor, energy),
// any active status-effect lines, and the card menu. The scene only gets the
// rows left over after that. Sizing the sprites first instead pushed the
// status block off the top of the screen on shorter windows - which is why
// the player's HP sometimes wasn't visible during a fight.
constexpr int MIN_TEXT_ROWS = 27;

// Whole screen pixels per sprite pixel - kept an integer so the pixel art
// stays sharp, and fitted to the rows the text isn't using.
int spriteScale() {
    const int cell = std::max(1, Platform::cellH());
    const int totalRows = (Platform::screenH() - 24) / cell;
    const int sceneRowBudget = std::max(5, totalRows - MIN_TEXT_ROWS);

    // -1 for the padding row sceneRowsNeeded() adds under the sprites.
    int byHeight = ((sceneRowBudget - 1) * cell) / 32;
    int byWidth  = Platform::screenW() / 108; // leaves margins beside the 94px backdrop
    return std::max(3, std::min(16, std::min(byHeight, byWidth)));
}
// The backdrop keeps the scene scale; the two characters are drawn one step
// smaller so they sit in the environment rather than filling it. They're
// bottom-aligned to the backdrop's floor line, same as the terminal scene.
int charScale() { return std::max(3, spriteScale() - 1); }

int spriteW() { return 30 * charScale(); }
int spriteH() { return 32 * charScale(); }
int backdropW() { return 94 * spriteScale(); } // same bg:sprite ratio the terminal had
int backdropH() { return 32 * spriteScale(); }

EnemyType gType = EnemyType::MELEE;
BossType  gBoss = BossType::NONE;

int  gEnemyFrame = F_IDLE_A;
int  gPlayerFrame = F_IDLE_A;
Tint gEnemyTint, gPlayerTint;
int  gEnemyNudge = 0;   // lunge toward the player
int  gSlashFrame = -1;  // sword-trail overlay on the player, -1 = none
int  gCastFrame = -1;   // cast-orb overlay on the player
bool gGhost = false;
bool gPortraitOnly = false;
Sheet* gBgSheet = nullptr;

// Rescale the backdrop's own dark tone to a chosen brightness, keeping its hue.
// Sampling alone gives something so near black the tint is invisible, which
// defeats the point; the brightness is set explicitly instead.
SDL_Color groundFromBackdrop(const Sheet* bg, int targetLuma) {
    if (!bg) return SDL_Color{ 13, 13, 15, 255 };
    const SDL_Color& c = bg->darkAvg;
    float cur = std::max(1.0f, (c.r * 77 + c.g * 150 + c.b * 29) / 256.0f);
    float k   = (float)targetLuma / cur;
    auto ch = [&](Uint8 v) { return (Uint8)std::min(255, (int)(v * k + 0.5f)); };
    return SDL_Color{ ch(c.r), ch(c.g), ch(c.b), 255 };
}

void applyGround() { Platform::setGroundColor(groundFromBackdrop(gBgSheet, 26)); }

AuraFlags gAuraKnight, gAuraEnemy;

// Driven by the backdrop, which is the tallest thing in the scene.
int sceneRowsNeeded() {
    int cell = Platform::cellH();
    return cell > 0 ? (backdropH() + cell - 1) / cell + 1 : 18;
}

void showScene() { Console::setSceneRows(sceneRowsNeeded()); }

// Cycles through whichever auras are active, one every 2 seconds, so a side
// carrying several statuses shows each in turn instead of blending to mud.
bool pickAura(const AuraFlags& f, Tint& out) {
    Tint active[5];
    int n = 0;
    if (f.strength) active[n++] = AURA_STRENGTH;
    if (f.weak)     active[n++] = AURA_WEAK;
    if (f.poison)   active[n++] = AURA_POISON;
    if (f.burn)     active[n++] = AURA_BURN;
    if (f.stun)     active[n++] = AURA_STUN;
    if (n == 0) return false;
    out = active[(SDL_GetTicks() / 2000) % (Uint32)n];
    return true;
}

void blit(const Sheet& s, int frame, SDL_Rect dst, const Tint& tint, Uint8 alpha = 255) {
    if (!s.ok() || frame < 0 || frame >= s.count) return;
    SDL_Renderer* r = Platform::renderer();
    SDL_Rect src{ frame * s.frameW, 0, s.frameW, s.frameH };

    Uint8 mr = (Uint8)std::min(255, (int)(tint.mulR * 255.0f + 0.5f));
    Uint8 mg = (Uint8)std::min(255, (int)(tint.mulG * 255.0f + 0.5f));
    Uint8 mb = (Uint8)std::min(255, (int)(tint.mulB * 255.0f + 0.5f));

    SDL_SetTextureColorMod(s.tex, mr, mg, mb);
    SDL_SetTextureAlphaMod(s.tex, alpha);
    SDL_RenderCopy(r, s.tex, &src, &dst);
    SDL_SetTextureColorMod(s.tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(s.tex, 255);

    if (s.silhouette && (tint.addR || tint.addG || tint.addB)) {
        SDL_SetTextureColorMod(s.silhouette, (Uint8)tint.addR, (Uint8)tint.addG, (Uint8)tint.addB);
        SDL_SetTextureAlphaMod(s.silhouette, alpha);
        SDL_RenderCopy(r, s.silhouette, &src, &dst);
        SDL_SetTextureColorMod(s.silhouette, 255, 255, 255);
        SDL_SetTextureAlphaMod(s.silhouette, 255);
    }
}

// --- floating numbers and impact sparks -------------------------------
//
// Drawn in the overlay pass, which runs after Console::render(), so these are
// the first things in the game able to sit on top of the text.
//
// Digits are a hand-built 3x5 bitmap rather than the console font. Scaling
// DejaVu up would give soft, anti-aliased numbers floating over hard pixel
// sprites; a bitmap font scales by the same integer factor as the art and
// reads as part of it.
const unsigned char DIGIT_GLYPH[13][5] = {
    {0b111,0b101,0b101,0b101,0b111}, // 0
    {0b010,0b110,0b010,0b010,0b111}, // 1
    {0b111,0b001,0b111,0b100,0b111}, // 2
    {0b111,0b001,0b111,0b001,0b111}, // 3
    {0b101,0b101,0b111,0b001,0b001}, // 4
    {0b111,0b100,0b111,0b001,0b111}, // 5
    {0b111,0b100,0b111,0b101,0b111}, // 6
    {0b111,0b001,0b001,0b001,0b001}, // 7
    {0b111,0b101,0b111,0b101,0b111}, // 8
    {0b111,0b101,0b111,0b001,0b111}, // 9
    {0b000,0b101,0b010,0b101,0b000}, // 10: x
    {0b000,0b010,0b111,0b010,0b000}, // 11: +
    {0b000,0b000,0b111,0b000,0b000}, // 12: -
};

struct Popup {
    float  x = 0, y = 0;
    std::string text;
    SDL_Color col{255,255,255,255};
    Uint32 born = 0;
    int    ms = 900;
    int    scale = 4;
};
struct Spark {
    float x = 0, y = 0, vx = 0, vy = 0;
    Uint32 born = 0;
    int    ms = 340;
    int    size = 3;
};
std::vector<Popup> gPopups;
std::vector<Spark> gSparks;

// Where the two fighters were drawn this frame. The overlay needs them and
// runs outside drawScene(), so drawScene records them on the way past.
SDL_Rect gPlayerRect{ 0,0,0,0 };
SDL_Rect gEnemyRect { 0,0,0,0 };

void drawGlyphColumnText(SDL_Renderer* r, const std::string& t, int x, int y,
                         int scale, SDL_Color col, Uint8 alpha) {
    auto cell = [&](int cx, int cy, SDL_Color c) {
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, alpha);
        SDL_Rect q{ cx, cy, scale, scale };
        SDL_RenderFillRect(r, &q);
    };
    // dark pass first, offset in four directions: a 1px outline is what keeps
    // a number legible over both a bright sprite and a dark backdrop.
    const SDL_Color dark{ 8, 8, 10, 255 };
    for (int pass = 0; pass < 2; pass++) {
        int px = x;
        for (char ch : t) {
            int g = -1;
            if (ch >= '0' && ch <= '9') g = ch - '0';
            else if (ch == 'x') g = 10;
            else if (ch == '+') g = 11;
            else if (ch == '-') g = 12;
            if (g >= 0) {
                for (int ry = 0; ry < 5; ry++)
                    for (int rx = 0; rx < 3; rx++)
                        if (DIGIT_GLYPH[g][ry] & (1 << (2 - rx))) {
                            if (pass == 0) {
                                cell(px + rx*scale - scale, y + ry*scale, dark);
                                cell(px + rx*scale + scale, y + ry*scale, dark);
                                cell(px + rx*scale, y + ry*scale - scale, dark);
                                cell(px + rx*scale, y + ry*scale + scale, dark);
                            } else {
                                cell(px + rx*scale, y + ry*scale, col);
                            }
                        }
            }
            px += 4 * scale;   // 3 wide + 1 spacing
        }
    }
}

void drawOverlay() {
    SDL_Renderer* r = Platform::renderer();
    const Uint32 now = SDL_GetTicks();

    for (size_t i = 0; i < gSparks.size();) {
        Spark& s = gSparks[i];
        float t = (float)(now - s.born) / (float)s.ms;
        if (t >= 1.0f) { gSparks.erase(gSparks.begin() + i); continue; }
        float px = s.x + s.vx * t;
        float py = s.y + s.vy * t + 34.0f * t * t;   // a little gravity
        Uint8 a = (Uint8)(255 * (1.0f - t));
        int sz = std::max(1, (int)(s.size * (1.0f - t) + 1));
        SDL_SetRenderDrawColor(r, 255, 232, 158, a);
        SDL_Rect q{ (int)px, (int)py, sz, sz };
        SDL_RenderFillRect(r, &q);
        i++;
    }

    for (size_t i = 0; i < gPopups.size();) {
        Popup& p = gPopups[i];
        float t = (float)(now - p.born) / (float)p.ms;
        if (t >= 1.0f) { gPopups.erase(gPopups.begin() + i); continue; }
        // rise fast then ease out, hold opacity for the first half
        float rise = (1.0f - (1.0f - t) * (1.0f - t)) * 9.0f * p.scale;
        Uint8 a = (t < 0.5f) ? 255 : (Uint8)(255 * (1.0f - (t - 0.5f) / 0.5f));
        drawGlyphColumnText(r, p.text, (int)p.x, (int)(p.y - rise), p.scale, p.col, a);
        i++;
    }
}


// Title screen backdrop. Drawn from the SCENE hook, which runs before the
// console text: the earlier version rode the modal hook and therefore painted
// straight over the menu.
bool gTitleMode = false;

void drawTitleScene() {
    const Sheet& s = lib().titleBg;
    if (!s.ok()) return;
    SDL_Renderer* r = Platform::renderer();
    const int W = Platform::screenW(), H = Platform::screenH();
    const int f = (SDL_GetTicks() / 700) % (Uint32)std::max(1, s.count);

    // Cover the window and crop the overflow, rather than stretching to fit.
    // Stretching a 16:9 image onto an arbitrary window is what made this look
    // wrong; cropping keeps the pixels square.
    const float sx = (float)W / (float)s.frameW;
    const float sy = (float)H / (float)s.frameH;
    const float sc = std::max(sx, sy);
    const int drawW = (int)(s.frameW * sc + 0.5f);
    const int drawH = (int)(s.frameH * sc + 0.5f);

    SDL_Rect src{ f * s.frameW, 0, s.frameW, s.frameH };
    SDL_Rect dst{ (W - drawW) / 2, (H - drawH) / 2, drawW, drawH };
    SDL_SetTextureColorMod(s.tex, 255, 255, 255);
    SDL_SetTextureAlphaMod(s.tex, 255);
    SDL_RenderCopy(r, s.tex, &src, &dst);

    // Graded scrim over the top third so the title keeps its contrast. A flat
    // rectangle left a visible horizontal seam across the sky.
    const int band = H / 2;
    for (int y = 0; y < band; y++) {
        float t = 1.0f - (float)y / (float)band;
        SDL_SetRenderDrawColor(r, 6, 6, 10, (Uint8)(150 * t * t));
        SDL_RenderDrawLine(r, 0, y, W, y);
    }
}

// Installed with Platform once, then called every frame.
void drawScene() {
    if (gTitleMode) { drawTitleScene(); return; }
    if (Console::sceneRows() <= 0) return;

    SDL_Renderer* r = Platform::renderer();
    const int scale = charScale();
    const int sprW = spriteW(), sprH = spriteH();
    const int sceneW = backdropW(), sceneH = backdropH();
    // Move with the console's shake so sprites and text never slide apart.
    int shX = 0, shY = 0;
    Platform::shakeOffset(shX, shY);
    const int originX = (Platform::screenW() - sceneW) / 2 + shX;
    const int originY = Console::sceneOriginY() + shY;
    // Characters stand on the backdrop's floor line rather than its top edge.
    const int floorY = originY + sceneH - sprH;

    if (gBgSheet && gBgSheet->ok()) {
        // Two-frame ambient shimmer, same 700ms cadence the terminal used.
        int f = (SDL_GetTicks() / 700) % (Uint32)std::max(1, gBgSheet->count);
        // The backdrop always spans the whole window. Normally there are enough
        // columns to cover it at the scene's own scale, so a centred crop draws
        // 1:1 and the pixels stay square.
        //
        // Short windows are the awkward case: rows are scarce, so a shallow one
        // shrinks spriteScale, and 256 columns at scale 3 covers 768px of a
        // 1600px window - the art became an island with bare sides. Stretch the
        // full width instead. Cropping vertically keeps pixels square but cuts
        // off the top of the frame, where the dungeon torches live.
        const int screenW = Platform::screenW();
        const int fullW   = gBgSheet->frameW * spriteScale();
        int cols, srcX, drawW;
        if (fullW >= screenW) {
            cols  = (screenW + spriteScale() - 1) / spriteScale();
            srcX  = f * gBgSheet->frameW + (gBgSheet->frameW - cols) / 2;
            drawW = cols * spriteScale();
        } else {
            cols  = gBgSheet->frameW;
            srcX  = f * gBgSheet->frameW;
            drawW = screenW;
        }
        SDL_Rect bsrc{ srcX, 0, cols, gBgSheet->frameH };
        SDL_Rect bdst{ (screenW - drawW) / 2 + shX, originY, drawW, sceneH };
        SDL_SetTextureColorMod(gBgSheet->tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(gBgSheet->tex, 255);
        SDL_RenderCopy(r, gBgSheet->tex, &bsrc, &bdst);
    }

    const ArtSet& es = artSet(gType, gBoss);

    if (gPortraitOnly) {
        SDL_Rect dst{ (Platform::screenW() - sprW) / 2, originY, sprW, sprH };
        // Breathe on the same 600ms cadence the battle scene uses. View Enemy
        // was picking one idle frame and holding it, so the portrait sat there
        // as a still image while everything else in the game moved.
        int pframe2 = gEnemyFrame;
        if (es.animated && (pframe2 == F_IDLE_A || pframe2 == F_IDLE_B))
            pframe2 = ((SDL_GetTicks() / 600) % 2) ? F_IDLE_B : F_IDLE_A;
        blit(es.sheet, pframe2, dst, gEnemyTint);
        return;
    }

    // Knight on the left, enemy on the right, both bottom-aligned on the
    // backdrop's floor line - the same composition as the terminal scene.
    Tint pt = gPlayerTint;
    if (pt.identity()) pickAura(gAuraKnight, pt);
    // Pushed apart by an extra 8 units each: the backdrop reaches the window
    // edges now, so the pair no longer has to huddle inside a 94-wide island.
    const int spread = spriteScale() * 8;
    SDL_Rect pdst{ originX + spriteScale() * 6 - spread, floorY, sprW, sprH };
    int pframe = gPlayerFrame;
    if (pframe == F_IDLE_A || pframe == F_IDLE_B)
        pframe = ((SDL_GetTicks() / 600) % 2) ? F_IDLE_B : F_IDLE_A;
    gPlayerRect = pdst;
    blit(lib().player, pframe, pdst, pt);
    if (gSlashFrame >= 0) blit(lib().slashFx, gSlashFrame, pdst, Tint{});
    if (gCastFrame >= 0)  blit(lib().castFx, gCastFrame, pdst, Tint{});

    Tint et = gEnemyTint;
    if (et.identity()) pickAura(gAuraEnemy, et);
    SDL_Rect edst{ originX + sceneW - sprW - spriteScale() * 6 + spread - gEnemyNudge, floorY, sprW, sprH };
    // While idle, breathe between the two idle frames instead of standing on a
    // single one - the terminal build only flipped these on a menu idle tick.
    int eframe = gEnemyFrame;
    if (es.animated && (eframe == F_IDLE_A || eframe == F_IDLE_B))
        eframe = ((SDL_GetTicks() / 600) % 2) ? F_IDLE_B : F_IDLE_A;
    // Ghost/Illusion: faded and spectral while the enemy can't be touched.
    gEnemyRect = edst;
    blit(es.sheet, eframe, edst, et, gGhost ? 110 : 255);

    // The add stands inside the enemy, toward the middle of the field, at one
    // scale step down. Integer scaling only: a fractional step drops the
    // one-pixel features these sprites are mostly made of.
    if (gCompanion && gCompanion->loaded) {
        const int cs = std::max(2, charScale() - 1);
        const int cwid = 30 * cs, chgt = 32 * cs;
        SDL_Rect cdst{ edst.x - cwid + spriteScale() * 2,
                       floorY + (sprH - chgt), cwid, chgt };
        int cframe = F_IDLE_A;
        if (gCompanion->animated)
            cframe = ((SDL_GetTicks() / 600) % 2) ? F_IDLE_B : F_IDLE_A;
        // A shade cooler than the enemy: raised, not native to the fight.
        Tint ct; ct.mulR = 0.78f; ct.mulG = 0.82f; ct.mulB = 0.95f;
        blit(gCompanion->sheet, cframe, cdst, ct);
    }

    (void)r;
}

// Holds the current pose for ms while the window keeps drawing.
void hold(int ms) { Platform::delay(ms); }

// Clears the one-shot pose overrides back to a neutral idle scene.
void resetPose() {
    gPlayerFrame = F_IDLE_A;
    gEnemyFrame = F_IDLE_A;
    gPlayerTint = Tint{};
    gEnemyTint = Tint{};
    gSlashFrame = -1;
    gCastFrame = -1;
    gEnemyNudge = 0;
}

// TODO: the slash sheets are still mostly empty - 0, 7 and 12 opaque pixels
// across the three frames. The swing reads from the knight's pose and the
// impact spark, not from any actual trail. Needs redrawing.
size_t slashVariant(DamageType elem) {
    switch (elem) {
        case DamageType::FIRE:   return 1;
        case DamageType::POISON: return 2;
        case DamageType::WIND:   return 3;
        default:                 return 0;
    }
}

bool gSceneRendererInstalled = false;
void ensureInstalled() {
    if (gSceneRendererInstalled) return;
    Platform::setSceneRenderer(&drawScene);
    Platform::setOverlayRenderer(&drawOverlay);
    gSceneRendererInstalled = true;
    if (!gBgSheet) { gBgSheet = &lib().bg[0]; applyGround(); }
}

} // anonymous namespace

// Switches the scene hook over to the title art. Public because the title
// screen owns the transition, not the battle code.
void setTitleMode(bool on) { ensureInstalled(); gTitleMode = on; }

void popNumber(int amount, bool onEnemy, PopKind kind) {
    ensureInstalled();
    const SDL_Rect& box = onEnemy ? gEnemyRect : gPlayerRect;
    if (box.w <= 0) return;

    std::string txt;
    SDL_Color col;
    switch (kind) {
        case PopKind::HEAL:     txt = "+" + std::to_string(amount); col = SDL_Color{134,209,107,255}; break;
        case PopKind::BLOCKED:  txt = std::to_string(amount);       col = SDL_Color{150,150,168,255}; break;
        case PopKind::WEAK_HIT: txt = std::to_string(amount);       col = SDL_Color{240,180,41,255};  break;
        default:                txt = std::to_string(amount);       col = SDL_Color{255,95,86,255};   break;
    }

    Popup p;
    // A step smaller than the sprites read at - big enough to punch, small
    // enough not to cover the thing it is telling you about.
    p.scale = std::max(2, charScale() - 3);
    int w = (int)txt.size() * 4 * p.scale;
    // jitter so a double hit does not stack two numbers in the same pixels
    p.x = (float)(box.x + box.w/2 - w/2 + (int)(gPopups.size() % 3) * 6 - 6);
    p.y = (float)(box.y + box.h/5);
    p.text = txt; p.col = col; p.born = SDL_GetTicks();
    gPopups.push_back(p);
    if (gPopups.size() > 12) gPopups.erase(gPopups.begin());
}

void popSparks(bool onEnemy) {
    ensureInstalled();
    const SDL_Rect& box = onEnemy ? gEnemyRect : gPlayerRect;
    if (box.w <= 0) return;
    // Contact point: the side the blow arrives from.
    float cx = (float)(onEnemy ? box.x + box.w/4 : box.x + box.w*3/4);
    float cy = (float)(box.y + box.h/2);
    for (int i = 0; i < 11; i++) {
        Spark s;
        float ang = (float)i * 6.2831853f / 11.0f + 0.35f;
        float spd = (float)(charScale() * (5 + (i % 3) * 3));
        s.x = cx; s.y = cy;
        s.vx = std::cos(ang) * spd;
        s.vy = std::sin(ang) * spd * 0.7f;
        s.size = std::max(2, charScale() / 2);
        s.born = SDL_GetTicks();
        gSparks.push_back(s);
    }
}

// --- public interface -------------------------------------------------

const Art& getWalkFrame(EnemyType type, BossType boss) {
    static Art a;
    ensureInstalled();
    const ArtSet& s = artSet(type, boss);
    static int phase = 0;
    phase ^= 1;
    a = Art{ &s, (s.animated && phase) ? F_IDLE_B : F_IDLE_A };
    return a;
}

// Single portrait, used by the View Enemy screen.
void print(const Art& art, int /*indent*/) {
    ensureInstalled();
    const ArtSet* s = static_cast<const ArtSet*>(art.set);
    if (!s) return;
    gPortraitOnly = true;
    gEnemyFrame = art.frame;
    gEnemyTint = Tint{};
    showScene();
}

void printBattle(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    resetPose();
    showScene();
}

void animateBattleIdleAt(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    const ArtSet& s = artSet(type, boss);
    if (s.animated) gEnemyFrame = (gEnemyFrame == F_IDLE_A) ? F_IDLE_B : F_IDLE_A;
    showScene();
}

void printBattleAttack(EnemyType type, BossType boss, bool knightGuard) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    const ArtSet& s = artSet(type, boss);

    // With armor up the knight holds his shield brace instead of standing idle.
    gPlayerFrame = knightGuard ? 6 /*block brace*/ : F_IDLE_A;
    if (s.animated) {
        gEnemyFrame = F_ATK1; hold(90);
        gEnemyFrame = F_ATK2; gEnemyNudge = spriteScale() * 2; hold(90);
        gEnemyFrame = F_ATK3; gEnemyNudge = spriteScale() * 4; hold(150);
    } else {
        // No frames: nudge the enemy toward the knight for a beat.
        gEnemyNudge = spriteScale() * 3; hold(120);
    }
    gEnemyNudge = 0;
    gEnemyFrame = F_IDLE_A;
}

void printBattleHit(EnemyType type, BossType boss, DamageType trailElem, bool connected) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();

    // Knight swings; the enemy flashes with its hit face on the final frame.
    // The sword trail / impact spark tracks the attack's element.
    const int v = (int)slashVariant(trailElem) * 3;
    const ArtSet& s = artSet(type, boss);

    gPlayerFrame = 2; gSlashFrame = v + 0; hold(70);
    gPlayerFrame = 3; gSlashFrame = v + 1; hold(70);
    gPlayerFrame = 4; gSlashFrame = v + 2;
    // Only a landed blow shakes the screen - a whiff already reads as a whiff
    // because the enemy holds its pose.
    if (connected) { Platform::shake(180, 9.0f); popSparks(true); }
    // The swing always plays - the knight committed to it. Only the enemy's
    // reaction is conditional: armor or defense soaking the blow entirely leaves
    // nothing to flinch at, so it holds its pose while the blade goes by.
    if (connected) {
        if (s.animated) gEnemyFrame = F_HIT;
        gEnemyTint = HIT_FLASH;
    }
    hold(150);

    gSlashFrame = -1;
    gPlayerFrame = F_IDLE_A;
    gEnemyFrame = F_IDLE_A;
    gEnemyTint = Tint{};
}

void printBattleBlock(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    gPlayerFrame = 5; hold(90);
    gPlayerFrame = 6; hold(260);
    gPlayerFrame = F_IDLE_A;
}

void printBattleCast(EnemyType type, BossType boss, CastGlow glow) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    int fxIdx = 0; // fx sheet order: poison, burn, stun, weak
    switch (glow) {
        case CastGlow::POISON: fxIdx = 0; break;
        case CastGlow::BURN:   fxIdx = 1; break;
        case CastGlow::STUN:   fxIdx = 2; break;
        case CastGlow::WEAK:   fxIdx = 3; break;
    }
    gPlayerFrame = 7; gCastFrame = fxIdx;
    hold(320);
    gCastFrame = -1;
    gPlayerFrame = F_IDLE_A;
}

void printBattleStatusFlash(EnemyType type, BossType boss, CastGlow glow, bool onEnemy) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    Tint t = statusTint(glow);
    if (onEnemy) gEnemyTint = t; else gPlayerTint = t;
    hold(1000);
    gEnemyTint = Tint{};
    gPlayerTint = Tint{};
}

void printBattleSelfBuff(EnemyType type, BossType boss, SelfGlow glow) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    gPlayerTint = (glow == SelfGlow::STRENGTH) ? TINT_STRENGTH : TINT_HEAL;
    hold(280);
    gPlayerTint = Tint{};
}

void setBattleAuras(AuraFlags knight, AuraFlags enemy) {
    gAuraKnight = knight;
    gAuraEnemy = enemy;
}

void setEnemyGhost(bool on) { gGhost = on; }

// Force the lazy Library to build now. It decodes roughly twenty PNGs and
// creates two GPU textures for each, and it used to happen on the first
// printBattle call, which put all of it inside the opening beat of the first
// fight. Called from the title screen instead, where a pause is expected.
void preload() { (void)lib(); ensureInstalled(); }



void setBattleBackdrop(int encounterNumber) {
    ensureInstalled();
    int idx = ((encounterNumber - 1) / 10) % 5;
    if (idx < 0) idx = 0;
    gBgSheet = &lib().bg[idx];
    applyGround();
}

// The ??? encounter's own sky. The same forest, wrong light.
void setSecretBackdrop() {
    ensureInstalled();
    if (lib().bloodMoonBg.ok()) { gBgSheet = &lib().bloodMoonBg; applyGround(); }
}

void setTutorialBackdrop() {
    ensureInstalled();
    gBgSheet = &lib().tutorialBg;
    applyGround();
}

// Companion sheets get their own small cache, keyed the same way the named
// variants are, so a repeated summon never reloads the PNG.
void setCompanion(const std::string& spriteKey) {
    ensureInstalled();
    gCompanion = nullptr;
    if (spriteKey.empty()) return;
    static ArtSet cache[NAMED_COUNT];
    static bool tried[NAMED_COUNT] = { false };
    for (int i = 0; i < NAMED_COUNT; i++) {
        if (spriteKey != NAMED_TABLE[i].key) continue;
        if (!tried[i]) {
            cache[i] = loadSet((std::string("assets/sprites/") + NAMED_TABLE[i].file + ".png").c_str());
            tried[i] = true;
        }
        if (cache[i].loaded) gCompanion = &cache[i];
        return;
    }
}

void setEnemyVariant(const std::string& enemyName) {
    ensureInstalled();
    gNamedVariant = nullptr;
    gCompanion = nullptr;   // a new fight never inherits the last one's add
    static ArtSet cache[NAMED_COUNT];
    static bool tried[NAMED_COUNT] = { false };
    for (int i = 0; i < NAMED_COUNT; i++) {
        if (enemyName.find(NAMED_TABLE[i].key) == std::string::npos) continue;
        if (!tried[i]) {
            cache[i] = loadSet((std::string("assets/sprites/") + NAMED_TABLE[i].file + ".png").c_str());
            tried[i] = true;
        }
        // A missing sheet leaves gNamedVariant unset so artSet() falls through
        // to the type's generic sprite instead of drawing nothing.
        if (cache[i].loaded) gNamedVariant = &cache[i];
        return;
    }
}

void printBattleDeath(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    const ArtSet& s = artSet(type, boss);
    if (s.animated) gEnemyFrame = F_DEATH;
    gEnemyTint = DEATH_DARK;
    hold(300);
}

void printBattleKnightHit(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    gPlayerFrame = 8;
    gPlayerTint = HIT_FLASH;
    // Taking a hit shakes harder than landing one - it should feel worse to be
    // on the receiving end.
    Platform::shake(200, 11.0f);
    popSparks(false);
    hold(100);
    gPlayerTint = Tint{};
    gPlayerFrame = F_IDLE_A;
}

void printBattleKnightDeath(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    gPlayerFrame = 9;
    gPlayerTint = DEATH_DARK;
    hold(300);
}

} // namespace EnemyArt
