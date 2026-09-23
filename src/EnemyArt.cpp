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
#include "ProjectileTable.h"
#include "Audio.h"
#include "Console.h"
#include "Platform.h"
#include "UIHelper.h"

#include <algorithm>
#include <string>
#include <map>
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
const Tint TINT_REND  { 0.45f, 0.45f, 0.45f,  40, 110, 125 };
const Tint DEATH_DARK { 0.35f, 0.35f, 0.35f,   0,   0,   0 };

const Tint TINT_STRENGTH{ 1.00f, 0.70f, 0.70f, 90,  0,  0 };
const Tint TINT_HEAL    { 0.78f, 1.00f, 0.78f,  0, 85,  0 };

const Tint AURA_STRENGTH{ 1.00f, 0.85f, 0.85f, 55,  0,  0 };
const Tint AURA_WEAK    { 0.85f, 0.85f, 1.00f,  0,  0, 65 };
const Tint AURA_POISON  { 0.85f, 1.00f, 0.85f,  0, 55,  0 };
const Tint AURA_BURN    { 1.00f, 1.00f, 0.75f, 60, 22,  0 };
const Tint AURA_STUN    { 1.00f, 1.00f, 0.80f, 55, 48,  0 };
// Wind: green and blue held, red drained. Colour-mod can only pull channels
// down, so the cyan has to come from taking red away rather than adding cyan.
const Tint AURA_REND    { 0.82f, 1.00f, 1.00f,  0, 45, 55 };

Tint statusTint(CastGlow glow) {
    switch (glow) {
        case CastGlow::BURN: return TINT_BURN;
        case CastGlow::STUN: return TINT_STUN;
        case CastGlow::WEAK: return TINT_WEAK;
        case CastGlow::REND: return TINT_REND;
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
    // Where a projectile should leave this sprite, as a percentage of frame
    // height. Taken from the vertical centroid of the attack frame's opaque
    // pixels, so a bolt leaves a tall caster's hands and a low crawling beast's
    // mouth instead of every enemy firing from the same spot near the top.
    int muzzlePct = 50;
    // The knight casts with his arm up, so the body centroid sits well below
    // where the spell actually leaves him. This is the centroid of the upper
    // half of the cast frame, which lands on the hand.
    int castMuzzlePct = 40;
    // Horizontal companion to muzzlePct, and the colour of the attack itself.
    // Both come from the pixels that differ between the idle and attack frames,
    // i.e. the part of the sprite that actually moves to make the attack.
    int muzzleXPct = 50;

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

    // Muzzle: vertical centroid of the opaque pixels in the attack frame (frame 2
    // where the sheet has one, else the first frame).
    {
        const int fw = s.frameW > 0 ? s.frameW : w;
        const int f  = (s.count > 2) ? 2 : 0;
        unsigned long long sy = 0, n = 0;
        for (int y = 0; y < h; y++)
            for (int x = f * fw; x < (f + 1) * fw && x < w; x++)
                if (data[((size_t)y * w + x) * 4 + 3] > 8) { sy += (unsigned)y; n++; }
        if (n && h > 0) s.muzzlePct = (int)((sy / n) * 100 / (unsigned)h);
        // Where the attack comes FROM, and what colour it is. Comparing the attack
        // frame against the idle frame isolates the arm, hand or jaws - the part
        // that moved - and everything static (torso, legs, robe) drops out.
        if (s.count > 4) {
            const int fi = 0, fa = 4;   // idle A vs the furthest attack frame
            unsigned long long mx = 0, my = 0, mn = 0;
            for (int y = 0; y < h; y++) {
                for (int x = 0; x < fw; x++) {
                    const size_t ia = ((size_t)y * w + (size_t)(fi * fw + x)) * 4;
                    const size_t ib = ((size_t)y * w + (size_t)(fa * fw + x)) * 4;
                    if (fa * fw + x >= w) continue;
                    const int d = std::abs((int)data[ia+0] - (int)data[ib+0])
                                + std::abs((int)data[ia+1] - (int)data[ib+1])
                                + std::abs((int)data[ia+2] - (int)data[ib+2])
                                + std::abs((int)data[ia+3] - (int)data[ib+3]);
                    if (d < 60) continue;                  // unchanged
                    if (data[ib+3] < 100) continue;        // changed to nothing
                    mx += (unsigned)x; my += (unsigned)y; mn++;
                }
            }
            if (mn) {
                s.muzzleXPct = (int)((mx / mn) * 100 / (unsigned)fw);
                s.muzzlePct  = (int)((my / mn) * 100 / (unsigned)h);
            }
        }

        // Cast muzzle: frame 7 is the knight's cast pose. Only the top half of its
        // opaque pixels count, so the raised arm wins over the legs.
        if (s.count > 7) {
            unsigned long long cy = 0, cn = 0;
            for (int y = 0; y < h / 2; y++)
                for (int x = 7 * fw; x < 8 * fw && x < w; x++)
                    if (data[((size_t)y * w + x) * 4 + 3] > 8) { cy += (unsigned)y; cn++; }
            if (cn && h > 0) s.castMuzzlePct = (int)((cy / cn) * 100 / (unsigned)h);
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
// Past the seven every sheet ships, and only one sheet has them: the true
// form's end, which is the only animated death in the game. Guard and cast
// poses were tried here and dropped - they were the player's frames
// recoloured, and the player faces the other way, so the knights braced and
// cast backwards.
enum : int { F_CRACK1 = 7, F_CRACK2 = 8, F_CRACK3 = 9, F_BURST = 10 };

// Assets resolve relative to the executable, not the working directory, so the
// game runs the same whether it's launched from a shell or a file manager.
std::string basePath() {
    static std::string base = Audio::dataDir();
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
    ArtSet TRUEKNIGHT;   // the Shadow Knight's true form: the moon itself
    ArtSet named[45];
    Sheet player, slashFx, castFx;
    // The knight's gear, one row of his frames per tier: the armour he wears
    // and the sword he carries, drawn over the same poses.
    Sheet playerArmor, playerWeapon;
    // Real projectiles: arrow, dagger, arcane bolt, fang. The cast-orb sheet
    // was standing in for all of them, so an archer's shot read as a spell.
    Sheet projFx;   // see include/ProjectileTable.h for what each frame is
    Sheet bg[5], tutorialBg, titleBg, bloodMoonBg;
    // One crimson sky per area for the Moonstruck, the forest's being the
    // original blood moon. Item icons for the gear screens, and the seal.
    Sheet crimsonBg[5], items, seal;

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
        TRUEKNIGHT   = loadSet("assets/sprites/moon_shadowknight.png");

        player  = loadSheet(basePath() + "assets/sprites/player.png", 30);
        playerArmor  = loadSheet(basePath() + "assets/sprites/player_armor.png", 30);
        playerWeapon = loadSheet(basePath() + "assets/sprites/player_weapon.png", 30);
        slashFx = loadSheet(basePath() + "assets/sprites/player_slash_fx.png", 30);
        castFx  = loadSheet(basePath() + "assets/sprites/player_cast_fx.png", 30);
        projFx  = loadSheet(basePath() + "assets/sprites/projectiles.png", 30);
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
        const char* crimsonFiles[5] = {
            "assets/sprites/bg_dungeon_crimson.png",
            "assets/sprites/bg_dungeon_purple_crimson.png",
            "assets/sprites/bg_forest_bloodmoon.png",
            "assets/sprites/bg_lake_crimson.png",
            "assets/sprites/bg_mountains_crimson.png",
        };
        for (int i = 0; i < 5; i++) crimsonBg[i] = loadSheet(basePath() + crimsonFiles[i], 256);
        items = loadSheet(basePath() + "assets/sprites/items.png", 24);
        seal  = loadSheet(basePath() + "assets/sprites/seal.png", 32);
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
    {"Moonstruck Weaver","moon_weaver"}, {"Moonstruck Beguiler","moon_beguiler"},
    {"Moonstruck Gorgon","moon_gorgon"}, {"Moonstruck Templar","moon_templar"},
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
// Set by name, like the named variants: bosses do not go through the name
// table (the plain Shadow Knight would match the regular Knight).
bool gTrueForm = false;
// The summoned add, if the fight has one standing.
const ArtSet* gCompanion = nullptr;
// The add swings and flinches on its own, rather than its master reacting for it.
int  gCompanionNudge = 0;
int  gCompanionFrame = -1;
Tint gCompanionTint;

const ArtSet& artSet(EnemyType type, BossType boss) {
    Library& L = lib();
    switch (boss) {
        case BossType::STONE_COLOSSUS: return L.COLOSSUS;
        case BossType::VILE_WITCH:     return L.WITCH;
        case BossType::WARLORD:        return L.WARLORD;
        case BossType::HYDRA:          return L.HYDRA;
        case BossType::DRAGON:         return L.DRAGON;
        case BossType::SHADOW_KNIGHT:  return (gTrueForm && L.TRUEKNIGHT.loaded) ? L.TRUEKNIGHT : L.SHADOWKNIGHT;
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
// Clear space between the two fighters at rest, derived from the same layout
// drawScene() builds: the knight sits spread-left of centre, the enemy
// spread-right, and both are spriteW() wide. It works out around 150% of a
// sprite width, which is why a lunge measured against the SPRITE looked like
// nothing - it closed barely a fifth of the distance and the blade still fell
// well short. A lunge is a fraction of this instead.
int restingGap() { return std::max(0, backdropW() - 2 * spriteW() + 4 * spriteScale()); }

EnemyType gType = EnemyType::MELEE;
BossType  gBoss = BossType::NONE;

int  gEnemyFrame = F_IDLE_A;
int  gPlayerFrame = F_IDLE_A;
Tint gEnemyTint, gPlayerTint;
int  gEnemyNudge = 0;   // lunge toward the player
int  gPlayerNudge = 0;  // and the knight stepping in to meet them
// A bolt in flight between the two sprites. gProjT runs 0 (at the caster) to
// 1 (at the target); -1 on the frame means nothing is in the air.
int   gProjFrame = -1;
float gProjT = 0.0f;
Tint  gProjTint;   // lets a physical shot read differently from a spell
bool  gProjReverse = false;  // true = travelling from the knight to the enemy
int   gProjSrcPct = 50, gProjDstPct = 50;  // muzzle heights, % of sprite height
int   gProjSrcXPct = 50, gProjDstXPct = 50; // and horizontally, % of sprite width
bool  gProjIsCast = false;  // true = the status-orb sheet, false = a real projectile
bool  gProjFall = false;    // true = drops under gravity onto the target (the knight's spells)
int   gProjScalePct = 30;   // size of the thing in flight, % of a sprite
// A beam is anchored at the shooter and grows toward the target instead of
// travelling, so it needs its own state rather than another flavour of gProj*.
int   gBeamFrame = -1;
float gBeamT = 0.0f;
int   gBeamSrcXPct = 50, gBeamSrcYPct = 50;
int   gBeamAlpha = 255;
Tint  gBeamTint;
// Pillars of flame standing on the floor between the fighters, climbing as
// gFlameT runs 0 to 1.
int   gFlameFrame = -1;
float gFlameT = 0.0f;
int  gSlashFrame = -1;  // sword-trail overlay on the player, -1 = none
int  gCastFrame = -1;   // cast-orb overlay on the player
bool gGhost = false;
// How many weapon / armour drops the knight has taken. Picks which tier
// frame of the gear sheets he is drawn wearing.
int gWeaponTiers = 0, gArmorTiers = 0;
// Second-wave enemies, tinted gilded-amber. Colour-mod can only pull
// channels down, so this is blue drained hard and green a little: red
// stays full and the sprite reads as gold-touched.
//
// Violet and crimson were both tried first and flattened distinct enemies
// toward one hue - the goblin lost its green entirely and stopped looking
// like a goblin. Amber keeps every sprite recognisable.
// 0 the first fifty, 1 Hard, 2 Extreme. Hard reddens every enemy; Extreme
// drains them to a cold violet, so which road you are on is visible at a
// glance rather than only in the numbers.
int gShadeTier = 0;
bool gPortraitOnly = false;
bool gPortraitKnight = false;   // portrait mode, but of the player
Sheet* gBgSheet = nullptr;
// The backdrop being crossfaded out, and how far through that is. Only the
// transformation uses these: every other backdrop change is a cut, because
// every other one happens on a screen nobody is looking at.
Sheet* gBgPrev = nullptr;
float  gBgFade = 1.0f;

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
    Tint active[6];
    int n = 0;
    if (f.strength) active[n++] = AURA_STRENGTH;
    if (f.weak)     active[n++] = AURA_WEAK;
    if (f.poison)   active[n++] = AURA_POISON;
    if (f.burn)     active[n++] = AURA_BURN;
    if (f.rend)     active[n++] = AURA_REND;
    if (f.stun)     active[n++] = AURA_STUN;
    if (n == 0) return false;
    out = active[(SDL_GetTicks() / 2000) % (Uint32)n];
    return true;
}

// Same as blit(), with an optional horizontal flip. Projectile art is drawn
// pointing right; a shot travelling the other way has to be mirrored or the
// arrowhead trails the shaft.
void blitFlipped(const Sheet& s, int frame, SDL_Rect dst, const Tint& tint, bool flip,
                 double angle = 0.0) {
    if (!s.ok() || frame < 0 || frame >= s.count) return;
    SDL_Renderer* r = Platform::renderer();
    SDL_Rect src{ frame * s.frameW, 0, s.frameW, s.frameH };
    Uint8 mr = (Uint8)std::min(255, (int)(tint.mulR * 255.0f + 0.5f));
    Uint8 mg = (Uint8)std::min(255, (int)(tint.mulG * 255.0f + 0.5f));
    Uint8 mb = (Uint8)std::min(255, (int)(tint.mulB * 255.0f + 0.5f));
    SDL_SetTextureColorMod(s.tex, mr, mg, mb);
    // Clockwise degrees with y pointing down, so atan2(dy, dx) of the flight path
    // can be passed straight through.
    SDL_RenderCopyEx(r, s.tex, &src, &dst, angle, nullptr,
                     flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
    SDL_SetTextureColorMod(s.tex, 255, 255, 255);
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
SDL_Rect gCompanionRect{ 0,0,0,0 };
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

// The seal after a boss: -1 hidden, 0 whole, 1-3 cracking, 4 broken.
int gSealFrame = -1;

void drawOverlay() {
    SDL_Renderer* r = Platform::renderer();
    const Uint32 now = SDL_GetTicks();

    // The seal, drawn over the text so the screen that offers it can stay
    // undimmed. Whole multiples of the 32px art so it stays crisp.
    if (gSealFrame >= 0 && lib().seal.ok()) {
        const int W = Platform::screenW(), H = Platform::screenH();
        int shX = 0, shY = 0;
        Platform::shakeOffset(shX, shY);
        int size = std::max(96, std::min(W, H) * 30 / 100);
        size -= size % 32;
        const SDL_Rect d{ (W - size) / 2 + shX, H * 40 / 100 - size / 2 + shY, size, size };
        if (gSealFrame >= 1) {
            // Light through the cracks: a red bloom that grows as it breaks.
            const int cx = d.x + size / 2, cy = d.y + size / 2;
            SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
            for (int ring = 4; ring >= 1; ring--) {
                const int rad = size / 2 + ring * size * std::min(gSealFrame, 3) / 40;
                SDL_SetRenderDrawColor(r, 200, 24, 30, (Uint8)(12 + gSealFrame * 6));
                for (int yy = -rad; yy <= rad; yy += 2) {
                    const int half = (int)std::sqrt((double)rad * rad - (double)yy * yy);
                    SDL_Rect row{ cx - half, cy + yy, half * 2, 2 };
                    SDL_RenderFillRect(r, &row);
                }
            }
        }
        blit(lib().seal, std::min(gSealFrame, lib().seal.count - 1), d, Tint{});
    }

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
// The knight in the gear he is carrying: his armour tier drawn in the pose,
// then his blade over it. Both sheets hold one row of poses per tier. If either
// is missing the plain sprite is drawn instead, so the game still runs on an
// install without them.
void drawKnight(int frame, const SDL_Rect& dst, const Tint& tint, Uint8 alpha = 255) {
    const Sheet& armour = lib().playerArmor;
    const Sheet& weapon = lib().playerWeapon;
    const int poses = lib().player.count;
    if (!armour.ok() || poses <= 0) {
        blit(lib().player, frame, dst, tint, alpha);
        return;
    }
    const int armTier = std::max(0, std::min(gArmorTiers, armour.count / poses - 1));
    blit(armour, armTier * poses + frame, dst, tint, alpha);
    if (weapon.ok()) {
        const int wepTier = std::max(0, std::min(gWeaponTiers, weapon.count / poses - 1));
        blit(weapon, wepTier * poses + frame, dst, tint, alpha);
    }
}

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

    // Drawn through a lambda so a crossfade can ask for the same work twice,
    // once for what is going and once for what is arriving.
    auto drawBackdrop = [&](Sheet* bg, Uint8 alpha) {
        if (!bg || !bg->ok() || alpha == 0) return;
        int f = (SDL_GetTicks() / 700) % (Uint32)std::max(1, bg->count);
        const int screenW = Platform::screenW();
        const int fullW   = bg->frameW * spriteScale();
        int cols, srcX, drawW;
        if (fullW >= screenW) {
            cols  = (screenW + spriteScale() - 1) / spriteScale();
            srcX  = f * bg->frameW + (bg->frameW - cols) / 2;
            drawW = cols * spriteScale();
        } else {
            cols  = bg->frameW;
            srcX  = f * bg->frameW;
            drawW = screenW;
        }
        SDL_Rect bsrc{ srcX, 0, cols, bg->frameH };
        SDL_Rect bdst{ (screenW - drawW) / 2 + shX, originY, drawW, sceneH };
        SDL_SetTextureColorMod(bg->tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(bg->tex, alpha);
        SDL_RenderCopy(r, bg->tex, &bsrc, &bdst);
        SDL_SetTextureAlphaMod(bg->tex, 255);
    };
    if (gBgPrev && gBgFade < 1.0f) {
        drawBackdrop(gBgPrev, 255);
        drawBackdrop(gBgSheet, (Uint8)(gBgFade * 255.0f + 0.5f));
    } else if (gBgSheet && gBgSheet->ok()) {
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

    if (gPortraitOnly && gPortraitKnight) {
        // View Player's portrait: the knight in the gear he is actually
        // wearing, breathing on the same cadence as everything else.
        SDL_Rect dst{ (Platform::screenW() - sprW) / 2, originY, sprW, sprH };
        const int kframe = ((SDL_GetTicks() / 600) % 2) ? F_IDLE_B : F_IDLE_A;
        drawKnight(kframe, dst, Tint{});
        return;
    }

    if (gPortraitOnly) {
        SDL_Rect dst{ (Platform::screenW() - sprW) / 2, originY, sprW, sprH };
        // Breathe on the same 600ms cadence the battle scene uses. View Enemy
        // was picking one idle frame and holding it, so the portrait sat there
        // as a still image while everything else in the game moved.
        int pframe2 = gEnemyFrame;
        if (es.animated && (pframe2 == F_IDLE_A || pframe2 == F_IDLE_B))
            pframe2 = ((SDL_GetTicks() / 600) % 2) ? F_IDLE_B : F_IDLE_A;
        Tint pt2 = gEnemyTint;
        if (gShadeTier == 1) { pt2.mulG *= 0.80f; pt2.mulB *= 0.55f; }
        else if (gShadeTier >= 2) { pt2.mulR *= 0.88f; pt2.mulG *= 0.72f; pt2.mulB *= 1.20f; pt2.addB += 18; }
        blit(es.sheet, pframe2, dst, pt2);
        return;
    }

    // Knight on the left, enemy on the right, both bottom-aligned on the
    // backdrop's floor line - the same composition as the terminal scene.
    Tint pt = gPlayerTint;
    if (pt.identity()) pickAura(gAuraKnight, pt);
    // Pushed apart by an extra 8 units each: the backdrop reaches the window
    // edges now, so the pair no longer has to huddle inside a 94-wide island.
    const int spread = spriteScale() * 8;
    SDL_Rect pdst{ originX + spriteScale() * 6 - spread + gPlayerNudge, floorY, sprW, sprH };
    int pframe = gPlayerFrame;
    if (pframe == F_IDLE_A || pframe == F_IDLE_B)
        pframe = ((SDL_GetTicks() / 600) % 2) ? F_IDLE_B : F_IDLE_A;
    gPlayerRect = pdst;
    drawKnight(pframe, pdst, pt);
    if (gSlashFrame >= 0) blit(lib().slashFx, gSlashFrame, pdst, Tint{});
    if (gCastFrame >= 0)  blit(lib().castFx, gCastFrame, pdst, Tint{});

    Tint et = gEnemyTint;
    if (et.identity()) pickAura(gAuraEnemy, et);
    if (gShadeTier == 1) { et.mulG *= 0.80f; et.mulB *= 0.55f; }
    else if (gShadeTier >= 2) { et.mulR *= 0.88f; et.mulG *= 0.72f; et.mulB *= 1.20f; et.addB += 18; }
    SDL_Rect edst{ originX + sceneW - sprW - spriteScale() * 6 + spread - gEnemyNudge, floorY, sprW, sprH };
    // While idle, breathe between the two idle frames instead of standing on a
    // single one - the terminal build only flipped these on a menu idle tick.
    int eframe = gEnemyFrame;
    if (es.animated && (eframe == F_IDLE_A || eframe == F_IDLE_B))
        eframe = ((SDL_GetTicks() / 600) % 2) ? F_IDLE_B : F_IDLE_A;
    // Ghost/Illusion: faded and spectral while the enemy can't be touched.
    gEnemyRect = edst;
    blit(es.sheet, eframe, edst, et, gGhost ? 110 : 255);

    // The bolt, drawn last so it passes in front of both fighters.
    if (gProjFrame >= 0) {
        const float t = gProjT < 0.0f ? 0.0f : (gProjT > 1.0f ? 1.0f : gProjT);
        const SDL_Rect& from = gProjReverse ? pdst : edst;
        const SDL_Rect& to   = gProjReverse ? edst : pdst;
        // Drawn at a fraction of the sprite, not stretched across the whole rect -
        // a full-rect blit put the bolt wherever the orb happened to sit inside
        // its 30x32 frame, which read as "always near the top" for every enemy.
        SDL_Rect r;
        // A cast orb is a small mark inside its frame, so it flies at full sprite
        // size and matches the orb in the hand. Projectile art fills its frame, so
        // it flies at a fraction of that or it dwarfs the caster.
        const int scalePct = gProjIsCast ? 100 : gProjScalePct;
        r.w = std::max(8, from.w * scalePct / 100);
        r.h = std::max(8, from.h * scalePct / 100);
        const int x0 = from.x + from.w * gProjSrcXPct / 100;
        const int x1 = to.x   + to.w   * gProjDstXPct / 100;
        const int y0 = from.y + from.h * gProjSrcPct  / 100;
        const int y1 = to.y   + to.h   * gProjDstPct  / 100;
        // A falling spell keeps a steady horizontal pace but drops as t*t, so it
        // leaves the hand level and comes down onto the target. A straight line
        // from a raised hand read as flying over the enemy's head.
        const float ty = gProjFall ? t * t : t;
        r.x = x0 + (int)((x1 - x0) * t)  - r.w / 2;
        r.y = y0 + (int)((y1 - y0) * ty) - r.h / 2;
        double angle = 0.0;
        if (gProjFall) {
            const double dx = (double)(x1 - x0);
            const double dy = 2.0 * (double)(y1 - y0) * t;   // slope of the t*t drop
            angle = std::atan2(dy, dx) * 57.29577951;
        }
        // Cast orbs are symmetric; projectile art is drawn pointing right, so
        // anything travelling leftward is mirrored or the arrowhead trails.
        const Sheet& ps = gProjIsCast ? lib().castFx : lib().projFx;
        blitFlipped(ps, gProjFrame, r, gProjTint, !gProjIsCast && !gProjReverse, angle);
    }

    // One end stays on the eye, the other advances: the frame is stretched to
    // the length reached so far, so the ray stays joined to its source.
    if (gBeamFrame >= 0) {
        const Sheet& ps = lib().projFx;
        if (ps.ok()) {
            const int bx0 = edst.x + edst.w * gBeamSrcXPct / 100;
            const int bx1 = pdst.x + pdst.w / 2;
            const float bt = gBeamT < 0.0f ? 0.0f : (gBeamT > 1.0f ? 1.0f : gBeamT);
            const int len = (int)((bx0 - bx1) * bt);
            // Drawn over the full sprite box. The ray sits at its own height inside
            // the frame, so it comes out at the size and position the sheet paints
            // it rather than at a thickness picked here.
            if (len > 2) {
                SDL_Rect bdst{ bx0 - len, edst.y, len, sprH };
                SDL_Rect bsrc{ gBeamFrame * ps.frameW, 0, ps.frameW, ps.frameH };
                SDL_SetTextureAlphaMod(ps.tex, (Uint8)gBeamAlpha);
                SDL_SetTextureColorMod(ps.tex,
                                       (Uint8)std::min(255, (int)(gBeamTint.mulR * 255.0f + 0.5f)),
                                       (Uint8)std::min(255, (int)(gBeamTint.mulG * 255.0f + 0.5f)),
                                       (Uint8)std::min(255, (int)(gBeamTint.mulB * 255.0f + 0.5f)));
                SDL_RenderCopyEx(r, ps.tex, &bsrc, &bdst, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
                SDL_SetTextureColorMod(ps.tex, 255, 255, 255);
                SDL_SetTextureAlphaMod(ps.tex, 255);
            }
        }
    }

    // The pillar art is bottom-anchored in its frame, so cropping the top away
    // and matching the destination height reveals it from the ground up.
    if (gFlameFrame >= 0) {
        const Sheet& ps = lib().projFx;
        if (ps.ok()) {
            const int cols = 7;
            const int groundY = floorY + sprH;
            const int fx0 = pdst.x + pdst.w / 2, fx1 = edst.x + edst.w / 2;
            // Full sprite width: the flame is a narrow strip inside its frame, so
            // drawing the frame at sprite size puts the column at the same
            // thickness the Archon raises at its own feet.
            const int fw = sprW;
            for (int i = 0; i < cols; i++) {
                // The near pillar leads and the far one lags, so the fire rolls.
                const float lead = 0.10f * (float)(cols - 1 - i);
                float t = (gFlameT - lead) / std::max(0.20f, 1.0f - lead);
                if (t <= 0.0f) continue;
                if (t > 1.0f) t = 1.0f;
                const int fh = (int)(sprH * 1.05f * t);
                if (fh < 4) continue;
                const int cx = fx0 + (fx1 - fx0) * i / (cols - 1);
                SDL_Rect fdst{ cx - fw / 2, groundY - fh, fw, fh };
                const int srcH = std::max(1, (int)(ps.frameH * t));
                SDL_Rect fsrc{ gFlameFrame * ps.frameW, ps.frameH - srcH, ps.frameW, srcH };
                SDL_RenderCopy(r, ps.tex, &fsrc, &fdst);
            }
        }
    }

    // The add stands inside the enemy, toward the middle of the field, at one
    // scale step down. Integer scaling only: a fractional step drops the
    // one-pixel features these sprites are mostly made of.
    if (gCompanion && gCompanion->loaded) {
        const int cs = std::max(2, charScale() - 1);
        const int cwid = 30 * cs, chgt = 32 * cs;
        // Anchored to where the enemy STANDS, not to its current lunge, so the add
        // keeps its ground while its master swings.
        SDL_Rect cdst{ edst.x + gEnemyNudge - cwid + spriteScale() * 2 - gCompanionNudge,
                       floorY + (sprH - chgt), cwid, chgt };
        int cframe = F_IDLE_A;
        if (gCompanion->animated && gCompanionFrame >= 0) cframe = gCompanionFrame;
        else if (gCompanion->animated)
            cframe = ((SDL_GetTicks() / 600) % 2) ? F_IDLE_B : F_IDLE_A;
        // A shade cooler than the enemy: raised, not native to the fight.
        Tint ct = gCompanionTint;
        if (ct.identity()) { ct.mulR = 0.78f; ct.mulG = 0.82f; ct.mulB = 0.95f; }
        gCompanionRect = cdst;
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
    gPlayerNudge = 0;
    gProjFrame = -1;
    gProjT = 0.0f;
    gProjTint = Tint{};
    gProjReverse = false;
    gProjScalePct = 30;
    gBeamFrame = -1;
    gFlameFrame = -1;
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

void setSealFrame(int frame) { ensureInstalled(); gSealFrame = frame; }

void drawItemIcon(SDL_Renderer* r, int index, const SDL_Rect& dst) {
    (void)r;
    ensureInstalled();
    blit(lib().items, index, dst, Tint{});
}

// Loaded the first time something asks for it, and kept. These are not in
// the Library because they are wanted before a fight loads any of that, and
// a missing one has to be a normal answer rather than a failure.
const Sheet& wordmarkSheet(const std::string& name) {
    static std::map<std::string, Sheet> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    Sheet s;
    int w = 0, h = 0, comp = 0;
    const std::string path = basePath() + "assets/sprites/" + name + ".png";
    unsigned char* probe = stbi_load(path.c_str(), &w, &h, &comp, 4);
    if (probe) {
        stbi_image_free(probe);
        s = loadSheet(path, w);   // frameW = the whole image: one frame, not a strip
    }
    return cache.emplace(name, s).first->second;
}

bool wordmarkSize(const std::string& name, int& w, int& h) {
    const Sheet& s = wordmarkSheet(name);
    if (!s.ok()) return false;
    w = s.frameW; h = s.frameH;
    return true;
}

void drawWordmark(const std::string& name, const SDL_Rect& dst) {
    const Sheet& s = wordmarkSheet(name);
    if (!s.ok()) return;
    blit(s, 0, dst, Tint{});
}

static void popIn(const SDL_Rect& box, int amount, PopKind kind) {
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

void popNumber(int amount, bool onEnemy, PopKind kind) {
    ensureInstalled();
    popIn(onEnemy ? gEnemyRect : gPlayerRect, amount, kind);
}

void popNumberAdd(int amount, PopKind kind) {
    ensureInstalled();
    popIn(gCompanionRect, amount, kind);
}

void popSparksIn(const SDL_Rect& box, bool onEnemy) {
    ensureInstalled();
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

void popSparks(bool onEnemy) { popSparksIn(onEnemy ? gEnemyRect : gPlayerRect, onEnemy); }

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
    gPortraitKnight = false;
    gEnemyFrame = art.frame;
    gEnemyTint = Tint{};
    showScene();
}

// The knight alone, for View Player. Mirrors print() on the enemy side.
void printPlayerPortrait() {
    ensureInstalled();
    gPortraitOnly = true;
    gPortraitKnight = true;
    gPlayerTint = Tint{};
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

// Sends a bolt between the two fighters; reverse=true sends it the other way,
// for the knight's own spells. muzzleX/muzzleY are percentages of the shooter's
// sprite, -1 meaning "use the value derived from the sheet"; ProjectileTable.h
// supplies them per enemy, since the derived answer misses a raised weapon.
void flyProjectile(int frame, const Tint& tint, int msPerStep, bool reverse = false,
                   bool isCast = true, int muzzleX = -1, int muzzleY = -1,
                   int dstX = -1, int dstY = -1, bool fall = false) {
    // Leaves the attacker at its own muzzle height and arrives at the target's.
    // The knight casts with his arm raised, so his own spells leave from a
    // separate cast muzzle rather than from his body centre.
    const Sheet& shooter = reverse ? lib().player : artSet(gType, gBoss).sheet;
    const Sheet& target  = reverse ? artSet(gType, gBoss).sheet : lib().player;
    gProjSrcPct  = muzzleY >= 0 ? muzzleY
                 : (reverse ? shooter.castMuzzlePct : shooter.muzzlePct);
    gProjSrcXPct = muzzleX >= 0 ? muzzleX
                 : (reverse ? 62 : shooter.muzzleXPct);
    gProjDstPct  = dstY >= 0 ? dstY : target.muzzlePct;
    gProjDstXPct = dstX >= 0 ? dstX : 50;
    gProjIsCast  = isCast;
    gProjFrame = frame;
    gProjTint  = tint;
    gProjReverse = reverse;
    gProjFall = fall;
    // A curve needs more positions than a straight line to read as a curve.
    const int steps = fall ? 8 : 4;
    for (int i = 0; i <= steps; i++) { gProjT = (float)i / steps; hold(msPerStep); }
    gProjFrame = -1;
    gProjT = 0.0f;
    gProjTint = Tint{};
    gProjReverse = false;
    gProjIsCast = true;
    gProjFall = false;
}

void printBattleAttack(EnemyType type, BossType boss, bool knightGuard, bool ranged,
                       bool useAttackFrames, int projectile, int muzzleX, int muzzleY,
                       bool closeIn) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    const ArtSet& s = artSet(type, boss);

    // With armor up the knight holds his shield brace instead of standing idle.
    gPlayerFrame = knightGuard ? 6 /*block brace*/ : F_IDLE_A;
    // A ranged attacker holds its ground: the shot travels, not the shooter.
    // Without this an archer lunged into the knight to fire point blank, which
    // made it read identically to a brawler.
    // Fractions of the distance between the two, so the blow actually arrives.
    // A diving flyer is ranged for every rule but this one: it does cross.
    const bool holdsGround = ranged && !closeIn;
    const int gap   = restingGap();
    const int step2 = holdsGround ? 0 : gap * 35 / 100;
    const int step3 = holdsGround ? 0 : gap * 85 / 100;
    // A shot we draw is released on atk3 and let go almost at once. Most firing
    // sheets paint their own projectile into that frame, so holding it for the
    // full beat and only then launching ours put two on screen together.
    const bool launches = ranged && projectile >= 0;
    if (s.animated && useAttackFrames) {
        gEnemyFrame = F_ATK1; hold(90);
        gEnemyFrame = F_ATK2; gEnemyNudge = step2; hold(90);
        gEnemyFrame = F_ATK3; gEnemyNudge = step3; hold(launches ? 70 : 150);
    } else {
        // Either the sheet has no attack frames, or the caller asked us not to use
        // them. Both cases still close the distance if the blow is a melee one.
        gEnemyNudge = holdsGround ? 0 : gap * 35 / 100; hold(110);
        if (!holdsGround) { gEnemyNudge = gap * 85 / 100; hold(130); }
    }

    // A ranged attack crosses the gap the same way a cast does. The caller passes
    // the frame from the generated table; anything negative means nothing crosses,
    // as for the Wyvern and the Falcon. Not an early return - the resets below
    // still have to run, or a flyer stays frozen mid-lunge.
    if (ranged && projectile >= 0) {
        // Drop to idle as it leaves: the painted shot disappears from the hand
        // on the same frame ours appears at that spot, so it reads as one
        // object leaving rather than a copy.
        if (s.animated && useAttackFrames) gEnemyFrame = F_IDLE_A;
        flyProjectile(projectile, Tint{}, 40, /*reverse*/false, /*isCast*/false,
                      muzzleX, muzzleY);
    }

    gEnemyNudge = 0;
    gEnemyFrame = F_IDLE_A;
}

void printBattleHit(EnemyType type, BossType boss, DamageType trailElem, bool connected,
                    bool onCompanion) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();

    // Knight swings; the enemy flashes with its hit face on the final frame.
    // The sword trail / impact spark tracks the attack's element.
    const int v = (int)slashVariant(trailElem) * 3;
    const ArtSet& s = artSet(type, boss);

    // Step in on the wind-up, furthest forward on the swing itself. The knight
    // used to swing from his idle spot, so a melee exchange looked like two
    // figures hitting the air between them.
    // Same fractions as the enemy lunge: a step in, then the blade arrives.
    const int gap = restingGap();
    gPlayerFrame = 2; gSlashFrame = v + 0; gPlayerNudge = gap * 35 / 100; hold(70);
    gPlayerFrame = 3; gSlashFrame = v + 1; gPlayerNudge = gap * 70 / 100; hold(70);
    gPlayerFrame = 4; gSlashFrame = v + 2; gPlayerNudge = gap * 85 / 100;
    // Only a landed blow shakes the screen - a whiff already reads as a whiff
    // because the enemy holds its pose.
    if (connected) { Platform::shake(180, 9.0f);
                     popSparksIn(onCompanion ? gCompanionRect : gEnemyRect, true); }
    // The swing always plays - the knight committed to it. Only the enemy's
    // reaction is conditional: armor or defense soaking the blow entirely leaves
    // nothing to flinch at, so it holds its pose while the blade goes by.
    if (connected) {
        if (onCompanion) {
            gCompanionFrame = F_HIT;
            gCompanionTint = HIT_FLASH;
        } else {
            if (s.animated) gEnemyFrame = F_HIT;
            gEnemyTint = HIT_FLASH;
        }
    }
    hold(150);

    gSlashFrame = -1;
    gPlayerNudge = 0;
    gPlayerFrame = F_IDLE_A;
    gEnemyFrame = F_IDLE_A;
    gEnemyTint = Tint{};
    gCompanionFrame = -1;
    gCompanionTint = Tint{};
}

// The add's own swing. It stands between the two fighters, so it has a shorter
// way to go than its master.
void printCompanionAttack(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    if (!gCompanion || !gCompanion->loaded) return;
    const int gap = restingGap();
    const bool anim = gCompanion->animated;
    if (anim) gCompanionFrame = F_ATK1;
    gCompanionNudge = gap * 25 / 100; hold(90);
    if (anim) gCompanionFrame = F_ATK2;
    gCompanionNudge = gap * 45 / 100; hold(90);
    if (anim) gCompanionFrame = F_ATK3;
    gCompanionNudge = gap * 55 / 100; hold(120);
    gCompanionNudge = 0;
    gCompanionFrame = -1;
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

void printBattleShieldBash(EnemyType type, BossType boss, bool connected) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    const ArtSet& s = artSet(type, boss);
    // Shield up on the step in, braced on the drive, and arriving at the same
    // 85% of the gap a sword swing reaches.
    const int gap = restingGap();
    gPlayerFrame = 5; gPlayerNudge = gap * 35 / 100; hold(80);
    gPlayerFrame = 6; gPlayerNudge = gap * 70 / 100; hold(70);
    gPlayerNudge = gap * 85 / 100;
    if (connected) {
        Platform::shake(140, 6.0f);
        if (s.animated) gEnemyFrame = F_HIT;
        gEnemyTint = HIT_FLASH;
    }
    hold(150);
    gPlayerNudge = 0;
    gPlayerFrame = F_IDLE_A;
    gEnemyFrame = F_IDLE_A;
    gEnemyTint = Tint{};
}

void printEnemyCast(EnemyType type, BossType boss, CastGlow glow, int projectile,
                    int muzzleX, int muzzleY, int scalePct) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    const ArtSet& s = artSet(type, boss);

    int fxIdx = 0; // fx sheet order: poison, burn, stun, weak, rend
    switch (glow) {
        case CastGlow::POISON: fxIdx = 0; break;
        case CastGlow::BURN:   fxIdx = 1; break;
        case CastGlow::STUN:   fxIdx = 2; break;
        case CastGlow::WEAK:   fxIdx = 3; break;
        case CastGlow::REND:   fxIdx = 4; break;
    }

    // Wind-up on the enemy's own frames. atk3 is where most sheets paint the
    // spell: a move that throws nothing holds it, anything that flies shows it
    // for a beat and lets go.
    const bool noFlight = (projectile == Proj::NONE);
    if (s.animated) {
        gEnemyFrame = F_ATK1; hold(80);
        gEnemyFrame = F_ATK2; hold(80);
        gEnemyFrame = F_ATK3; hold(noFlight ? 260 : 70);
        if (!noFlight) gEnemyFrame = F_IDLE_A;
    } else {
        hold(90);
    }

    // Then the bolt crosses the field. Five steps is enough to read as travel
    // without holding up the turn.
    // A named move with its own art flies that; a generic ailment keeps the orb.
    if (noFlight) {
        // Nothing crosses; the flash below is the whole hit.
    } else if (projectile >= 0) {
        gProjScalePct = scalePct;
        flyProjectile(projectile, Tint{}, 45, /*reverse*/false, /*isCast*/false,
                      muzzleX, muzzleY);
        gProjScalePct = 30;
    } else {
        flyProjectile(fxIdx, Tint{}, 45, /*reverse*/false, /*isCast*/true,
                      muzzleX, muzzleY);
    }

    // Land it on the knight: the same flash the player's own casts use.
    gPlayerTint = statusTint(glow);
    hold(120);
    gPlayerTint = Tint{};
    gEnemyFrame = F_IDLE_A;
}

// A beam extends rather than travels: it stays joined to the eye, reaches the
// knight, holds, and fades.
void printEnemyBeam(EnemyType type, BossType boss, int projectile,
                    int muzzleX, int muzzleY, bool weakGlow) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    const ArtSet& s = artSet(type, boss);
    if (s.animated) {
        gEnemyFrame = F_ATK1; hold(90);
        gEnemyFrame = F_ATK2; hold(90);
        gEnemyFrame = F_ATK3;
    }
    gBeamFrame   = projectile;
    gBeamSrcXPct = muzzleX >= 0 ? muzzleX : s.sheet.muzzleXPct;
    gBeamSrcYPct = muzzleY >= 0 ? muzzleY : s.sheet.muzzlePct;
    gBeamAlpha   = 255;
    gBeamTint    = weakGlow ? statusTint(CastGlow::WEAK) : Tint{};
    // Reaches across in about a third of a second, then burns for a beat.
    for (int i = 0; i <= 8; i++) { gBeamT = (float)i / 8.0f; hold(26); }
    hold(150);
    // Fades rather than vanishing: a beam that blinks out looks like a dropped frame.
    for (int a = 255; a > 40; a -= 55) { gBeamAlpha = a; hold(28); }
    gBeamFrame = -1;
    gBeamT = 0.0f;
    gBeamAlpha = 255;
    gBeamTint = Tint{};
    gEnemyFrame = F_IDLE_A;
}

// The Archon's own frames show fire climbing out of the floor at its feet.
// Hellfire is that, spread across the arena.
void printGroundFlames(EnemyType type, BossType boss, int projectile) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    const ArtSet& s = artSet(type, boss);
    // The fire rises WITH the cast: the sprite's own frames raise flame at its
    // feet, and the arena follows them up rather than lighting afterwards.
    gFlameFrame = projectile;
    gFlameT = 0.0f;
    if (s.animated) gEnemyFrame = F_ATK1;
    for (int i = 0; i <= 12; i++) {
        if (s.animated && i == 4) gEnemyFrame = F_ATK2;
        if (s.animated && i == 8) gEnemyFrame = F_ATK3;
        gFlameT = (float)i / 12.0f;
        hold(34);
    }
    hold(220);
    // Sink back the way they came up.
    for (int i = 10; i >= 0; i -= 2) { gFlameT = (float)i / 12.0f; hold(26); }
    gFlameFrame = -1;
    gFlameT = 0.0f;
    gEnemyFrame = F_IDLE_A;
}

void printBattleCast(EnemyType type, BossType boss, CastGlow glow) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    int fxIdx = 0; // fx sheet order: poison, burn, stun, weak, rend
    switch (glow) {
        case CastGlow::POISON: fxIdx = 0; break;
        case CastGlow::BURN:   fxIdx = 1; break;
        case CastGlow::STUN:   fxIdx = 2; break;
        case CastGlow::WEAK:   fxIdx = 3; break;
        case CastGlow::REND:   fxIdx = 4; break;
    }
    // Wind-up in his hand, then the spell actually travels. It used to glow on
    // the knight and take effect on the enemy with nothing crossing between.
    gPlayerFrame = 7; gCastFrame = fxIdx;
    hold(200);
    gCastFrame = -1;
    // The orb in his hand (every cast-fx frame puts it at x83-93%, y3-15%) leaves
    // from exactly there as the enemy orb shape in the same colour, and drops
    // onto the middle of the enemy. It used to fly the whole cast-fx frame from
    // (70, 46): the orb sits at the top-right of that frame, so the visible orb
    // travelled high and landed over the enemy's head.
    int orb = ProjectileTable::PC_POISON;
    switch (glow) {
        case CastGlow::POISON: orb = ProjectileTable::PC_POISON; break;
        case CastGlow::BURN:   orb = ProjectileTable::PC_BURN;   break;
        case CastGlow::STUN:   orb = ProjectileTable::PC_STUN;   break;
        case CastGlow::WEAK:   orb = ProjectileTable::PC_WEAK;   break;
        case CastGlow::REND:   orb = ProjectileTable::PC_REND;   break;
    }
    flyProjectile(orb, Tint{}, 28, /*reverse*/true, /*isCast*/false, 88, 9, 50, 50, /*fall*/true);
    gEnemyTint = statusTint(glow);
    hold(120);
    gEnemyTint = Tint{};
    gPlayerFrame = F_IDLE_A;
}

void printBattleStatusFlash(EnemyType type, BossType boss, CastGlow glow, bool onEnemy) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    Tint t = statusTint(glow);
    if (onEnemy) gEnemyTint = t; else gPlayerTint = t;
    // Long enough to read as a hit of colour, short enough that a move made only
    // of a status does not stall the turn.
    hold(360);
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

void setGearTiers(int weaponTiers, int armorTiers) {
    ensureInstalled();
    gWeaponTiers = weaponTiers;
    gArmorTiers  = armorTiers;
}

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
    gBgPrev = nullptr; gBgFade = 1.0f;
    applyGround();
}

// The Moonstruck's sky: whichever area it catches you in, under the wrong
// light. Falls back to the blood-moon forest if an area's sheet is missing.
void setSecretBackdrop(int zone) {
    ensureInstalled();
    const int z = ((zone % 5) + 5) % 5;
    Sheet* s = lib().crimsonBg[z].ok() ? &lib().crimsonBg[z] : &lib().bloodMoonBg;
    if (s->ok()) { gBgSheet = s; gBgPrev = nullptr; gBgFade = 1.0f; applyGround(); }
}

// The knight burns white, the moon comes up inside it, and the peak turns
// into the last arena behind it. One continuous shot: the fight never cuts
// away, because it is still the same fight and still the same body.
void transformToTrueForm(int zone, const std::string& trueName) {
    ensureInstalled();
    gPortraitOnly = false;
    showScene();

    const int STEPS = 22, MS = 42;

    // Up into the white. The multiplier comes down as the add goes up, so it
    // bleaches rather than simply glowing brighter.
    for (int i = 1; i <= STEPS; ++i) {
        const float k = (float)i / STEPS;
        Tint t;
        t.mulR = t.mulG = t.mulB = 1.0f - 0.55f * k;
        t.addR = (int)(205 * k); t.addG = (int)(205 * k); t.addB = (int)(210 * k);
        gEnemyTint = t;
        hold(MS);
    }

    // At the top of the white, where nothing can be made out, the art is
    // swapped and the backdrop starts changing underneath.
    const int z = ((zone % 5) + 5) % 5;
    Sheet* to = lib().crimsonBg[z].ok() ? &lib().crimsonBg[z] : &lib().bloodMoonBg;
    const SDL_Color groundFrom = groundFromBackdrop(gBgSheet, 26);
    // Where the ground ends up: exactly what this backdrop would have been
    // given on any other Moonstruck fight. An earlier version pushed it
    // further towards red on top of that, which made the last arena brighter
    // than the four the moon had already dragged him through.
    const SDL_Color groundTo = groundFromBackdrop(to, 26);
    if (to->ok()) {
        gBgPrev = gBgSheet;
        gBgSheet = to;
        gBgFade = 0.0f;
    }
    setEnemyVariant(trueName);
    gEnemyFrame = F_IDLE_A;
    hold(260);

    // And back down, with the new sky coming through as the glare leaves.
    for (int i = STEPS; i >= 0; --i) {
        const float k = (float)i / STEPS;
        Tint t;
        t.mulR = t.mulG = t.mulB = 1.0f - 0.55f * k;
        t.addR = (int)(205 * k); t.addG = (int)(205 * k); t.addB = (int)(210 * k);
        gEnemyTint = t;
        gBgFade = 1.0f - k;
        // The ground travels with the sky, so the whole frame turns together.
        const float g2 = 1.0f - k;
        Platform::setGroundColor(SDL_Color{
            (Uint8)(groundFrom.r + (groundTo.r - groundFrom.r) * g2),
            (Uint8)(groundFrom.g + (groundTo.g - groundFrom.g) * g2),
            (Uint8)(groundFrom.b + (groundTo.b - groundFrom.b) * g2), 255 });
        hold(MS);
    }
    gEnemyTint = Tint{};
    gBgPrev = nullptr;
    gBgFade = 1.0f;
    Platform::setGroundColor(groundTo);
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
    // Run.cpp prefixes second-wave enemies with "Greater" and bosses with
    // "Ancient", so the name is enough to know which pass this is.
    gShadeTier = (enemyName.rfind("Eternal ", 0) == 0) ? 2
               : (enemyName.rfind("Greater ", 0) == 0 || enemyName.rfind("Ancient ", 0) == 0) ? 1 : 0;
    gTrueForm = enemyName.find("Moonstruck Shadow Knight") != std::string::npos;
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

    // The true form is the one thing in the game that gets a death rather
    // than a frame. It never goes down: it turns away with its sword lowered,
    // the pose every enemy dies in, and then stays on its feet shaking harder
    // as the cracks open through it until it is not there any more.
    if (s.sheet.count > F_BURST) {
        gEnemyTint = Tint{};
        gEnemyFrame = F_DEATH;  Platform::shake(360, 1.2f); hold(420);
        gEnemyFrame = F_CRACK1; Platform::shake(340, 1.8f); hold(300);
        gEnemyFrame = F_CRACK2; Platform::shake(320, 2.6f); hold(280);
        gEnemyFrame = F_CRACK3; Platform::shake(300, 3.4f); hold(260);
        // The light gets out before the shell does.
        Tint flare; flare.addR += 46; flare.addG += 42; flare.addB += 34;
        gEnemyTint = flare;     hold(150);
        gEnemyTint = Tint{};
        gEnemyFrame = F_BURST;  Platform::shake(420, 4.5f); hold(440);
        gEnemyTint = DEATH_DARK;
        hold(200);
        return;
    }

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
