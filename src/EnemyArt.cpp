// SDL2 battle scene: a region above the console text. The print* calls set
// persistent state (poses, tints, auras) and hold it for the authored
// durations; Platform::frame() draws it every frame.
#include "EnemyArt.h"
#include "ProjectileTable.h"
#include "IntroTable.h"
#include "EndingTable.h"
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
// A tint multiplies each channel, then adds a constant: a colour-mod blit,
// then an additive blit of the sprite's white silhouette.
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
// Moonstruck: a deeper, darker red than Strength's, the moon's own.
const Tint AURA_MOONSTRUCK{ 0.92f, 0.62f, 0.66f, 58,  0,  8 };

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
    // Where a projectile leaves this sprite, as a percentage of frame height:
    // the vertical centroid of the attack frame's opaque pixels.
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
    // with a color mod adds a flat constant only where the sprite is opaque.
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
    // Where the standard 30x32 box sits inside a bigger frame, in art pixels.
    // Only the true form uses it, so its sword has room to swing.
    int boxX = 0, boxY = 0;
};

enum : int { F_IDLE_A = 0, F_IDLE_B = 1, F_ATK1 = 2, F_ATK2 = 3, F_ATK3 = 4, F_HIT = 5, F_DEATH = 6 };
// Past the seven every sheet ships, and only one sheet has them: the true
// form's end, which is the only animated death in the game.
enum : int { F_CRACK1 = 7, F_CRACK2 = 8, F_CRACK3 = 9, F_BURST = 10 };
// And three more after those, played at the top of the white when the knight
// turns into it: the knight bending, the shape between, the new one forming.
enum : int { F_MORPH1 = 11, F_MORPH2 = 12, F_MORPH3 = 13 };
// And its escape, in the run the church opens: the hit pose with the sword
// let go, the sword left standing alone, and the body turned away to run.
enum : int { F_LETGO = 14, F_LEFT_SWORD = 15, F_TURNED = 16 };
// The False Moon's own twelfth frame: its hands held up with the moon's fire
// in them, shown while it casts.
enum : int { F_SPELL = 11 };
// The true form going as the armour goes in the intro (a sheet with them
// gets this escape): the dark over it, all dark, sunk partway, sunk to the
// shoulders, a pool, and the pool running off. And the False Moon's arrival:
// the moon's eye where it will stand, shut into a dark moon, the ring, and
// it climbing out.
enum : int { F_SINK1 = 17, F_POOL = 21, F_POOL_SMALL = 22 };
enum : int { F_ARRIVE1 = 12, F_RISING = 18, F_ARRIVE8 = 19 };

// The true form's frame: 56x36, with the standard box 16 in from the left
// and 4 down. tools/make_trueform.py draws to these numbers.
const int TRUE_FORM_FRAME_W = 56;
const int TRUE_FORM_BOX_X   = 16;
const int TRUE_FORM_BOX_Y   = 4;

// Assets resolve relative to the executable, not the working directory, so the
// game runs the same whether it's launched from a shell or a file manager.
std::string basePath() {
    static std::string base = Audio::dataDir();
    return base;
}

ArtSet loadSet(const char* rel, int frameW = 30, int boxX = 0, int boxY = 0) {
    ArtSet s;
    s.sheet = loadSheet(basePath() + rel, frameW);
    s.loaded = s.sheet.ok();
    s.animated = s.sheet.count >= 7;
    s.boxX = boxX;
    s.boxY = boxY;
    return s;
}

// Sheets are loaded lazily on first use: they need a live SDL renderer, so
// they cannot be built during static initialization.
struct Library {
    ArtSet MELEE, RANGED, TANK, CASTER, BEAST, UNDEAD;
    ArtSet COLOSSUS, WITCH, WARLORD, HYDRA, DRAGON, SHADOWKNIGHT;
    ArtSet TRUEKNIGHT;   // the Shadow Knight's true form: the moon itself
    // The False Moon, the church's boss, drawn in the true form's frame. If
    // its sheet is missing, the true form stands in for it.
    ArtSet FALSEMOON;
    ArtSet named[45];
    Sheet player, slashFx, castFx;
    // The knight's gear, one row of his frames per tier: the armour he wears
    // and the sword he carries, drawn over the same poses.
    Sheet playerArmor, playerWeapon;
    // Real projectiles: arrow, dagger, arcane bolt, fang. The cast-orb sheet
    // was standing in for all of them, so an archer's shot read as a spell.
    Sheet projFx;   // see include/ProjectileTable.h for what each frame is
    Sheet bg[5], tutorialBg, titleBg, bloodMoonBg;
    // The ruined church past the peak, the same ruin under the eclipsed moon
    // for the False Moon's fight, and the ruin with the moon gone, which the
    // eclipse fades into when the False Moon dies.
    Sheet churchBg, churchEclipseBg, churchGoneBg;
    // The church's moon waking before the False Moon's fight: its eye opens,
    // looks down at the knight and shuts into the eclipse, a frame a step.
    Sheet churchWakeBg;
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
        TRUEKNIGHT   = loadSet("assets/sprites/moon_shadowknight.png",
                               TRUE_FORM_FRAME_W, TRUE_FORM_BOX_X, TRUE_FORM_BOX_Y);
        FALSEMOON    = loadSet("assets/sprites/boss_false_moon.png",
                               TRUE_FORM_FRAME_W, TRUE_FORM_BOX_X, TRUE_FORM_BOX_Y);

        player  = loadSheet(basePath() + "assets/sprites/player.png", 30);
        playerArmor  = loadSheet(basePath() + "assets/sprites/player_armor.png", 30);
        playerWeapon = loadSheet(basePath() + "assets/sprites/player_weapon.png", 30);
        slashFx = loadSheet(basePath() + "assets/sprites/player_slash_fx.png", 30);
        castFx  = loadSheet(basePath() + "assets/sprites/player_cast_fx.png", 30);
        projFx  = loadSheet(basePath() + "assets/sprites/projectiles.png", 30);
        const char* bgFiles[5] = {
            // TODO: the two dungeon sheets are one layout recoloured, with a torch on a
            // strict 16-column period and no variation across 512px, so the first
            // twenty fights read as the same corridor twice.
            "assets/sprites/bg_dungeon.png",        // 1-10
            "assets/sprites/bg_dungeon_purple.png", // 11-20
            "assets/sprites/bg_forest_night.png",   // 21-30
            "assets/sprites/bg_lake_night.png",     // 31-40
            "assets/sprites/bg_mountains_dusk.png", // 41-50
        };
        // Backdrops are 256 wide so they span the window; the fighters still lay
        // out on the 94-unit span (backdropW), centred.
        for (int i = 0; i < 5; i++) bg[i] = loadSheet(basePath() + bgFiles[i], 256);
        tutorialBg = loadSheet(basePath() + "assets/sprites/bg_forest_day.png", 256);
        titleBg    = loadSheet(basePath() + "assets/sprites/bg_title.png", 256);
        bloodMoonBg = loadSheet(basePath() + "assets/sprites/bg_forest_bloodmoon.png", 256);
        churchBg        = loadSheet(basePath() + "assets/sprites/bg_church.png", 256);
        churchEclipseBg = loadSheet(basePath() + "assets/sprites/bg_church_eclipse.png", 256);
        churchGoneBg    = loadSheet(basePath() + "assets/sprites/bg_church_moonless.png", 256);
        churchWakeBg    = loadSheet(basePath() + "assets/sprites/bg_church_wake.png", 256);
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

// Per-name sheets, matched against the enemy's name at encounter start. A
// name whose PNG is missing falls through to its type's generic sprite. A
// sheet drawn bigger than the standard 30x32 says its frame width and where
// the standard box sits in it, as the true form's does (loadNamed).
struct NamedEntry { const char* key; const char* file; int frameW = 30, boxX = 0, boxY = 0; };
const NamedEntry NAMED_TABLE[] = {
    {"Bandit","melee_bandit"}, {"Warrior","melee_warrior"}, {"Raider","melee_raider"},
    {"Knight","melee_knight"}, {"Berserker","melee_berserker"}, {"Gladiator","melee_gladiator"},
    {"Enforcer","melee_enforcer"},
    {"Wolf","beast_wolf"}, {"Spider","beast_spider"}, {"Serpent","beast_serpent"},
    {"Wyvern","beast_wyvern"}, {"Basilisk","beast_basilisk"}, {"Manticore","beast_manticore"},
    {"Cockatrice","beast_cockatrice"}, {"Fleshmass","beast_fleshmass", 46, 8, 4},
    {"Moon Shade Weaver","moon_weaver"}, {"Moon Shade Beguiler","moon_beguiler"},
    {"Moon Shade Gorgon","moon_gorgon"}, {"Moon Shade Templar","moon_templar"},
    {"Moon Shade","moon_werewolf"},
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
    // The ruined church's nine (tools/church_art).
    {"Exhumed Saint","undead_saint"}, {"Sexton","melee_sexton"}, {"Mirror Nun","caster_mirror_nun"},
    {"Gargoyle","beast_gargoyle"}, {"Bellringer","tank_bellringer"}, {"Inquisitor","ranged_inquisitor"},
    {"Confessor","caster_confessor"}, {"Glass Templar","tank_glass_templar"}, {"Unchosen","melee_unchosen"},
};
const int NAMED_COUNT = (int)(sizeof(NAMED_TABLE) / sizeof(NAMED_TABLE[0]));

// The sheets a summon is drawn from, the Lich's add or your own raised dead,
// cached by key so a repeated summon never reloads the PNG.
// A named sheet, in its own frame size; one still the standard 32 tall loads
// the standard way, so an old sheet keeps working until its bigger one is in.
ArtSet loadNamed(const NamedEntry& e) {
    const std::string path = std::string("assets/sprites/") + e.file + ".png";
    ArtSet s = loadSet(path.c_str(), e.frameW, e.boxX, e.boxY);
    if (e.frameW != 30 && s.loaded && s.sheet.frameH == 32) s = loadSet(path.c_str());
    return s;
}

const ArtSet* summonSheet(const std::string& key) {
    // The Dragon is a boss, drawn from the library like the others.
    if (key == "Dragon") return lib().DRAGON.loaded ? &lib().DRAGON : nullptr;
    static ArtSet cache[NAMED_COUNT];
    static bool tried[NAMED_COUNT] = { false };
    for (int i = 0; i < NAMED_COUNT; i++) {
        if (key != NAMED_TABLE[i].key) continue;
        if (!tried[i]) {
            cache[i] = loadNamed(NAMED_TABLE[i]);
            tried[i] = true;
        }
        return cache[i].loaded ? &cache[i] : nullptr;
    }
    return nullptr;
}

const ArtSet* gNamedVariant = nullptr;
// Set by name, like the named variants: bosses do not go through the name
// table (the plain Shadow Knight would match the regular Knight).
bool gTrueForm = false;
// The fights where a raised undead can stand on each side - the Lich's, and
// the Shadow Knight's in both its phases - stand the fighters further apart
// for the whole fight, so nothing jumps when a summon arrives.
bool gWideField = false;
// The summoned add, if the fight has one standing.
const ArtSet* gCompanion = nullptr;
// The add swings and flinches on its own, rather than its master reacting for it.
int  gCompanionNudge = 0;
int  gCompanionFrame = -1;
Tint gCompanionTint;
// Your own raised dead, the add's mirror on the knight's side.
const ArtSet* gAlly = nullptr;
bool gBlowsAtAlly = false;   // the blow in flight is theirs to take
int  gAllyNudge = 0;
int  gAllyFrame = -1;
Tint gAllyTint;

// Whether this enemy has a frame of its own for casting.
static bool castsWithHands(const ArtSet& s, BossType boss) {
    return boss == BossType::FALSE_MOON && s.sheet.count > F_SPELL;
}

const ArtSet& artSet(EnemyType type, BossType boss) {
    Library& L = lib();
    switch (boss) {
        case BossType::STONE_COLOSSUS: return L.COLOSSUS;
        case BossType::VILE_WITCH:     return L.WITCH;
        case BossType::WARLORD:        return L.WARLORD;
        case BossType::HYDRA:          return L.HYDRA;
        case BossType::DRAGON:         return L.DRAGON;
        case BossType::SHADOW_KNIGHT:  return (gTrueForm && L.TRUEKNIGHT.loaded) ? L.TRUEKNIGHT : L.SHADOWKNIGHT;
        case BossType::FALSE_MOON:
            return L.FALSEMOON.loaded ? L.FALSEMOON : L.TRUEKNIGHT.loaded ? L.TRUEKNIGHT : L.SHADOWKNIGHT;
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
// Text rows the battle screen needs (header, status block, effects, menu).
// The scene gets what is left, so a short window never pushes the status
// block off the top.
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
// The raised dead, on either side: two thirds of the fighters, rounded, so
// they read as followers at every window size. One step down would be 14
// against 15 in a full window, as tall as the knight. Whole steps only, like
// the rest.
int summonScale() { return std::max(2, (charScale() * 2 + 1) / 3); }
// How much further a summon steps to land a blow than it did at one step
// down: its body is narrower, so its front edge starts further back.
int summonReachExtra() { return std::max(0, 30 * (charScale() - 1 - summonScale())); }

int spriteW() { return 30 * charScale(); }
int spriteH() { return 32 * charScale(); }
int backdropW() { return 94 * spriteScale(); } // same bg:sprite ratio the terminal had
int backdropH() { return 32 * spriteScale(); }
// Clear space between the two fighters at rest, from drawScene()'s layout
// (about 1.5 sprite widths). Lunges are fractions of this, not of a sprite.
// How much further apart each fighter stands on a wide field.
int fieldExtra() { return gWideField ? spriteScale() * 8 : 0; }
int restingGap() { return std::max(0, backdropW() - 2 * spriteW() + 4 * spriteScale() + 2 * fieldExtra()); }

EnemyType gType = EnemyType::MELEE;
BossType  gBoss = BossType::NONE;

int  gEnemyFrame = F_IDLE_A;
int  gPlayerFrame = F_IDLE_A;
Tint gEnemyTint, gPlayerTint;
int  gEnemyNudge = 0;   // lunge toward the player
int  gEnemyAlpha = 255; // the enemy fading out of the scene (the true form running)
int  gEnemyLeft = -1;   // a frame of its own left standing where it was (the true form's sword)
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
bool gStone = false;   // the Gargoyle, turned to stone for your turn
// How many weapon / armour drops the knight has taken. Picks which tier
// frame of the gear sheets he is drawn wearing.
int gWeaponTiers = 0, gArmorTiers = 0;
// 1 on Hard: every enemy is tinted amber so the mode shows at a glance.
// Colour-mod only pulls channels down, so blue drops hard and green a little.
int gShadeTier = 0;
bool gPortraitOnly = false;
bool gPortraitKnight = false;   // portrait mode, but of the player
int  gPortraitCap = -1;         // setPortraitRows(): -1 for the scene's own height
Sheet* gBgSheet = nullptr;
// The backdrop being crossfaded out, and how far through that is. Only the
// transformation uses these: every other backdrop change is a cut, because
// every other one happens on a screen nobody is looking at.
Sheet* gBgPrev = nullptr;
float  gBgFade = 1.0f;
// A backdrop frame held still instead of the shimmer (-1: shimmer), for
// each of the two: the church's moon waking is played a frame a step.
int gBgFrame = -1, gBgPrevFrame = -1;
// The False Moon is not there yet: it comes out of the eclipse once the
// fight is on screen (prepareArrival), so the first draw must not show it.
bool gArrivalPending = false;

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

void showScene() {
    int rows = sceneRowsNeeded();
    if (gPortraitOnly && gPortraitCap >= 0) rows = std::min(rows, gPortraitCap);
    Console::setSceneRows(rows);
}

// Cycles through whichever auras are active, one every 2 seconds, so a side
// carrying several statuses shows each in turn instead of blending to mud.
bool pickAura(const AuraFlags& f, Tint& out) {
    Tint active[7];
    int n = 0;
    if (f.moonstruck) active[n++] = AURA_MOONSTRUCK;
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

void blit(const Sheet& s, int frame, SDL_Rect dst, const Tint& tint, Uint8 alpha = 255);

// blit() turned to face the other way, hit flash and all: your raised dead are
// the enemies' own sprites, turned round to face what they fight.
void blitMirrored(const Sheet& s, int frame, SDL_Rect dst, const Tint& tint) {
    if (!s.ok() || frame < 0 || frame >= s.count) return;
    SDL_Renderer* r = Platform::renderer();
    SDL_Rect src{ frame * s.frameW, 0, s.frameW, s.frameH };
    SDL_SetTextureColorMod(s.tex, (Uint8)std::min(255, (int)(tint.mulR * 255.0f + 0.5f)),
                                  (Uint8)std::min(255, (int)(tint.mulG * 255.0f + 0.5f)),
                                  (Uint8)std::min(255, (int)(tint.mulB * 255.0f + 0.5f)));
    SDL_RenderCopyEx(r, s.tex, &src, &dst, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
    SDL_SetTextureColorMod(s.tex, 255, 255, 255);
    if (s.silhouette && (tint.addR || tint.addG || tint.addB)) {
        SDL_SetTextureColorMod(s.silhouette, (Uint8)tint.addR, (Uint8)tint.addG, (Uint8)tint.addB);
        SDL_RenderCopyEx(r, s.silhouette, &src, &dst, 0.0, nullptr, SDL_FLIP_HORIZONTAL);
        SDL_SetTextureColorMod(s.silhouette, 255, 255, 255);
    }
}

// An enemy frame, given the box it stands in. Most sheets are exactly the box.
// A bigger frame is laid out around it and reaches out of it, and whatever
// reaches above the scene is cut off there rather than drawn over the HUD.
void blitInBox(const ArtSet& a, int frame, const SDL_Rect& box, const Tint& tint, Uint8 alpha,
               int scale, int sceneTop, int sceneH) {
    const bool bigger = a.boxX || a.boxY || a.sheet.frameW != 30 || a.sheet.frameH != 32;
    if (!bigger) { blit(a.sheet, frame, box, tint, alpha); return; }
    const SDL_Rect dst{ box.x - a.boxX * scale, box.y - a.boxY * scale,
                        a.sheet.frameW * scale, a.sheet.frameH * scale };
    SDL_Renderer* r = Platform::renderer();
    const SDL_Rect clip{ 0, sceneTop, Platform::screenW(), sceneH };
    SDL_RenderSetClipRect(r, &clip);
    blit(a.sheet, frame, dst, tint, alpha);
    SDL_RenderSetClipRect(r, nullptr);
}

void blit(const Sheet& s, int frame, SDL_Rect dst, const Tint& tint, Uint8 alpha) {
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
// Drawn in the overlay pass, after Console::render(), so they sit over the
// text. The digits are a 3x5 bitmap so they scale by whole pixels like the art.
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

// The moon's new vessel (printBattleVessel): the knight's frames as it takes
// him, made then from whatever he is wearing; while gVesselShown is set they
// are drawn in place of him (its last two are him standing as the vessel,
// breathing). gVesselMote is the last piece of him on its way up into the
// moon, 0 to 1, and gVesselPulse the moon's ring flaring as it takes it; -1
// when there is none. gVesselChestX/Y: where in his frame the dark comes in
// (% of the frame), which is where the piece leaves and the wisp arrives.
std::vector<SDL_Texture*> gVesselTex;
int   gVesselShown = -1;
float gVesselMote = -1.0f;
float gVesselPulse = -1.0f;
float gVesselChestX = 50.0f, gVesselChestY = 45.0f;

// The church's moon on screen: the eclipse's disc in bg_church_eclipse.png,
// where bg_church.png has its moon too (centre measured in backdrop pixels),
// placed the way drawScene places the backdrop, a centred crop at the
// scene's scale.
SDL_Point churchMoonOnScreen() {
    const float MOON_X = 141.5f, MOON_Y = 7.0f;
    const int sc = spriteScale(), screenW = Platform::screenW();
    const int frameW = lib().churchBg.ok() ? lib().churchBg.frameW : 256;
    float x;
    if (frameW * sc >= screenW) {
        const int cols = (screenW + sc - 1) / sc;
        x = (float)((screenW - cols * sc) / 2) + (MOON_X - (float)((frameW - cols) / 2)) * sc;
    } else {
        x = MOON_X * screenW / frameW;
    }
    return SDL_Point{ (int)(x + 0.5f), Console::sceneOriginY() + (int)(MOON_Y * sc + 0.5f) };
}

// Where the two fighters were drawn this frame. The overlay needs them and
// runs outside drawScene(), so drawScene records them on the way past.
SDL_Rect gPlayerRect{ 0,0,0,0 };
SDL_Rect gCompanionRect{ 0,0,0,0 };
SDL_Rect gAllyRect{ 0,0,0,0 };
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
// The knight at his fire for "Sit a while": the armour tier he is wearing, or
// -1 when the scene is not up.
int gRestTier = -1;

// The cutscenes: their sheets and timelines, which one is up and which shot
// (-1: none), when the shot began, and the last step whose sound has played.
struct SceneDef {
    const char* file;
    const CutsceneStep* steps;
    const CutsceneShot* shots;
    int shotCount, frameW, frameH, columns;
};
const SceneDef SCENES[] = {
    { "assets/sprites/intro_scene.png", IntroTable::STEPS, IntroTable::SHOT, IntroTable::SHOTS,
      IntroTable::FRAME_W, IntroTable::FRAME_H, IntroTable::COLUMNS },
    { "assets/sprites/ending_scene.png", EndingTable::STEPS, EndingTable::SHOT, EndingTable::SHOTS,
      EndingTable::FRAME_W, EndingTable::FRAME_H, EndingTable::COLUMNS },
};
int gScene = 0, gSceneShot = -1;
Uint32 gSceneStart = 0;
int gSceneSounded = -1;
// The church's run ends with no moon in the sky: the False Moon went out in
// the nave, so the morning has nothing to watch go down. The same frames,
// numbered as ending_scene.png's, so EndingTable serves both.
const char* ENDING_MOONLESS = "assets/sprites/ending_scene_moonless.png";
bool gEndingMoonless = false;

// A whole image as one texture, loaded on first use and kept: each is only
// wanted at the start or the end of a run. Null when the file is missing.
SDL_Texture* wholeTexture(const std::string& rel) {
    static std::map<std::string, SDL_Texture*> cache;
    auto it = cache.find(rel);
    if (it != cache.end()) return it->second;
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0, comp = 0;
    const std::string path = basePath() + rel;
    if (unsigned char* data = stbi_load(path.c_str(), &w, &h, &comp, 4)) {
        if (SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(data, w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32)) {
            tex = SDL_CreateTextureFromSurface(Platform::renderer(), surf);
            SDL_FreeSurface(surf);
        }
        stbi_image_free(data);
    }
    cache[rel] = tex;
    return tex;
}

const SceneDef& sceneDef() { return SCENES[gScene]; }

// The sheet a scene is drawn from: the ending's moonless one when the run
// came through the church and the file is there, else its own.
const char* sceneFile() {
    if (gScene == 1 && gEndingMoonless && wholeTexture(ENDING_MOONLESS)) return ENDING_MOONLESS;
    return sceneDef().file;
}
const CutsceneShot& shotDef() { return sceneDef().shots[gSceneShot]; }

int shotIntroMs() {
    const CutsceneShot& s = shotDef();
    int ms = 0;
    for (int i = 0; i < s.intro; i++) ms += sceneDef().steps[s.first + i].ms;
    return ms;
}

// The step showing `elapsed` ms into the shot: its own steps once, then its
// loop round and round.
int sceneStepAt(Uint32 elapsed) {
    const CutsceneStep* steps = sceneDef().steps;
    const CutsceneShot& s = shotDef();
    Uint32 t = elapsed;
    for (int i = 0; i < s.intro; i++) {
        const Uint32 ms = (Uint32)steps[s.first + i].ms;
        if (t < ms) return s.first + i;
        t -= ms;
    }
    if (s.loop <= 0) return s.first + std::max(0, s.intro - 1);
    Uint32 cycle = 0;
    for (int i = 0; i < s.loop; i++) cycle += (Uint32)steps[s.first + s.intro + i].ms;
    t %= std::max<Uint32>(1, cycle);
    for (int i = 0; i < s.loop; i++) {
        const Uint32 ms = (Uint32)steps[s.first + s.intro + i].ms;
        if (t < ms) return s.first + s.intro + i;
        t -= ms;
    }
    return s.first + s.intro;
}

// Whole multiples of the art, so it stays crisp: as big as fits in the top
// of the window with room for the words under it.
SDL_Rect sceneRect() {
    const SceneDef& d = sceneDef();
    const int W = Platform::screenW(), H = Platform::screenH();
    const int scale = std::max(2, std::min(W * 80 / 100 / d.frameW, H * 64 / 100 / d.frameH));
    return SDL_Rect{ (W - d.frameW * scale) / 2, H * 4 / 100, d.frameW * scale, d.frameH * scale };
}

// The ending's knight, in the gear he finished the run wearing, lit for the
// step: his armour, then his weapon over it, from ending_knight.png.
void drawEndingKnight(const SDL_Rect& scene, int knight, int scale) {
    SDL_Texture* tex = wholeTexture("assets/sprites/ending_knight.png");
    if (!tex || knight < 0) return;
    using namespace EndingTable;
    const int light = std::min(LIGHTS - 1, knight / POSES), pose = knight % POSES;
    const int armour = std::max(0, std::min(TIERS - 1, gArmorTiers));
    const int weapon = std::max(0, std::min(TIERS - 1, gWeaponTiers));
    const int layers[2] = { armour * POSES + pose, TIERS * POSES + weapon * POSES + pose };
    const SDL_Rect to{ scene.x + KNIGHT_X * scale, scene.y + KNIGHT_Y * scale, KNIGHT_W * scale, KNIGHT_H * scale };
    for (int c : layers) {
        const SDL_Rect from{ c * KNIGHT_W, light * KNIGHT_H, KNIGHT_W, KNIGHT_H };
        SDL_RenderCopy(Platform::renderer(), tex, &from, &to);
    }
}

void drawCutscene(Uint32 now) {
    const SceneDef& d = sceneDef();
    SDL_Texture* tex = wholeTexture(sceneFile());
    if (!tex) return;
    const int step = sceneStepAt(now - gSceneStart);
    // Sounds belong to the steps that play once; each is heard as its step
    // comes up, and never again for the same showing.
    const CutsceneShot& s = shotDef();
    for (int i = std::max(gSceneSounded + 1, (int)s.first); i <= step && i < s.first + s.intro; i++)
        if (d.steps[i].sfx) Audio::playSFX(d.steps[i].sfx);
    gSceneSounded = std::max(gSceneSounded, std::min(step, s.first + s.intro - 1));

    const int frame = d.steps[step].frame;
    const SDL_Rect src{ (frame % d.columns) * d.frameW, (frame / d.columns) * d.frameH, d.frameW, d.frameH };
    const SDL_Rect dst = sceneRect();
    SDL_RenderCopy(Platform::renderer(), tex, &src, &dst);
    if (d.steps[step].knight >= 0) drawEndingKnight(dst, d.steps[step].knight, dst.w / d.frameW);
}


void drawOverlay() {
    SDL_Renderer* r = Platform::renderer();
    const Uint32 now = SDL_GetTicks();

    if (gSceneShot >= 0) drawCutscene(now);

    // The fire scene sits in the top of the screen and the words go under it.
    // One strip, three flame frames per armour, baked by
    // tools/make_campfire_scene.py with the knight and the light already in.
    if (gRestTier >= 0) {
        static bool tried = false;
        static Sheet scene;
        if (!tried) {
            tried = true;
            scene = loadSheet(basePath() + "assets/sprites/campfire_scene.png", 80);
        }
        if (scene.ok()) {
            const int W = Platform::screenW(), H = Platform::screenH();
            const int tiers = scene.count / 3;
            const int tier = std::max(0, std::min(tiers - 1, gRestTier));
            const int scale = std::max(2, std::min(W * 70 / 100 / scene.frameW,
                                                   H * 40 / 100 / scene.frameH));
            const SDL_Rect d{ (W - scene.frameW * scale) / 2, H * 4 / 100,
                              scene.frameW * scale, scene.frameH * scale };
            blit(scene, tier * 3 + (int)((now / 180) % 3), d, Tint{});
        }
    }

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

    // The last piece of him going up into the moon: a white point with a soft
    // cross of glow, out of his chest and over into the eclipse, the points it
    // has just left fading behind it, slow to leave him and slow to go in,
    // smaller as it nears and gone into its dark. Then the moon's ring flares
    // once, a pixel ring of its red going out from the disc and fading.
    if ((gVesselMote >= 0.0f || gVesselPulse >= 0.0f) && gPlayerRect.w > 0) {
        const int sz = std::max(1, charScale());
        const SDL_Point moon = churchMoonOnScreen();
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        auto dot = [&](float px, float py, int w, int h, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca) {
            SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
            SDL_Rect q{ (int)px - w / 2, (int)py - h / 2, w, h };
            SDL_RenderFillRect(r, &q);
        };
        if (gVesselMote >= 0.0f) {
            const float x0 = gPlayerRect.x + gPlayerRect.w * gVesselChestX / 100.0f;
            const float y0 = gPlayerRect.y + gPlayerRect.h * gVesselChestY / 100.0f;
            // over the top: the curve's middle point above both ends, nearer him
            const float cx = x0 + (moon.x - x0) * 0.3f;
            const float cy = std::min(y0, (float)moon.y) - sz * 9;
            auto at = [&](float u, float& px, float& py) {
                const float e = u * u * (3.0f - 2.0f * u);
                const float a = 1.0f - e;
                px = a * a * x0 + 2 * a * e * cx + e * e * moon.x;
                py = a * a * y0 + 2 * a * e * cy + e * e * moon.y;
            };
            const float t = gVesselMote;
            const float near = t < 0.7f ? 1.0f : std::max(0.0f, (1.0f - t) / 0.3f);
            const float fade = t < 0.9f ? 1.0f : std::max(0.0f, (1.0f - t) / 0.1f);
            const int core = sz + (int)(sz * near + 0.5f);          // two pixels across, one by the moon
            for (int k = 4; k >= 1; --k) {                          // where it has just been
                float px, py;
                at(std::max(0.0f, t - k * 0.03f), px, py);
                dot(px, py, sz, sz, 150, 160, 220, (Uint8)(255 * fade * (0.5f - k * 0.1f)));
            }
            float px, py;
            at(t, px, py);
            dot(px, py, core + sz * 2, core, 150, 160, 220, (Uint8)(130 * fade));
            dot(px, py, core, core + sz * 2, 150, 160, 220, (Uint8)(130 * fade));
            dot(px, py, core, core, 238, 242, 255, (Uint8)(255 * fade));
        }
        if (gVesselPulse >= 0.0f) {
            const float p = gVesselPulse;
            const int sp = spriteScale();
            const float R = 4.5f + 2.5f * p;
            SDL_SetRenderDrawColor(r, 214, 46, 44, (Uint8)(190 * (1.0f - p)));
            const int n = (int)R + 2;
            for (int dy = -n; dy <= n; ++dy)
                for (int dx = -n; dx <= n; ++dx) {
                    if (std::fabs(std::hypot((float)dx, (float)dy) - R) >= 0.5f) continue;
                    SDL_Rect q{ moon.x + dx * sp - sp / 2, moon.y + dy * sp - sp / 2, sp, sp };
                    SDL_RenderFillRect(r, &q);
                }
        }
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
// console text, so it never paints over the menu.
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

// The knight in his gear: the armour tier's pose, then his blade over it.
// Without those sheets the plain sprite is drawn instead.
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
    // A portrait in a band shorter than the scene (setPortraitRows): the
    // backdrop is cropped to the band from the floor up, and the figure is
    // scaled to stand in it.
    const int bandH = Console::sceneRows() * std::max(1, Platform::cellH());
    const bool capped = gPortraitOnly && bandH < sceneH;
    const int bgY = capped ? originY + bandH - sceneH : originY;
    if (capped) {
        const SDL_Rect band{ 0, originY, Platform::screenW(), bandH };
        SDL_RenderSetClipRect(r, &band);
    }

    // Drawn through a lambda so a crossfade can ask for the same work twice,
    // once for what is going and once for what is arriving.
    auto drawBackdrop = [&](Sheet* bg, Uint8 alpha, int pin) {
        if (!bg || !bg->ok() || alpha == 0) return;
        int f = pin >= 0 ? pin % std::max(1, bg->count)
                         : (int)((SDL_GetTicks() / 700) % (Uint32)std::max(1, bg->count));
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
        SDL_Rect bdst{ (screenW - drawW) / 2 + shX, bgY, drawW, sceneH };
        SDL_SetTextureColorMod(bg->tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(bg->tex, alpha);
        SDL_RenderCopy(r, bg->tex, &bsrc, &bdst);
        SDL_SetTextureAlphaMod(bg->tex, 255);
    };
    if (gBgPrev && gBgFade < 1.0f) {
        drawBackdrop(gBgPrev, 255, gBgPrevFrame);
        drawBackdrop(gBgSheet, (Uint8)(gBgFade * 255.0f + 0.5f), gBgFrame);
    } else if (gBgSheet && gBgSheet->ok()) {
        // Two-frame ambient shimmer, same 700ms cadence the terminal used.
        int f = gBgFrame >= 0 ? gBgFrame % std::max(1, gBgSheet->count)
                              : (int)((SDL_GetTicks() / 700) % (Uint32)std::max(1, gBgSheet->count));
        // The backdrop always spans the window: a centred 1:1 crop when it is wide
        // enough at the scene's scale, stretched to full width on a short window
        // (a vertical crop would cut off the dungeon torches).
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
        SDL_Rect bdst{ (screenW - drawW) / 2 + shX, bgY, drawW, sceneH };
        SDL_SetTextureColorMod(gBgSheet->tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(gBgSheet->tex, 255);
        SDL_RenderCopy(r, gBgSheet->tex, &bsrc, &bdst);
    }

    const ArtSet& es = artSet(gType, gBoss);

    // In a capped band the figure is as big as the band allows, standing on
    // its floor; otherwise where it has always been.
    const int pscale = capped ? std::max(2, std::min(scale, (bandH - 4) / 32)) : scale;
    const SDL_Rect portraitDst = capped
        ? SDL_Rect{ (Platform::screenW() - 30 * pscale) / 2, originY + bandH - 32 * pscale - 2, 30 * pscale, 32 * pscale }
        : SDL_Rect{ (Platform::screenW() - sprW) / 2, originY, sprW, sprH };

    if (gPortraitOnly && gPortraitKnight) {
        // View Player's portrait: the knight in the gear he is actually
        // wearing, breathing on the same cadence as everything else.
        const int kframe = ((SDL_GetTicks() / 600) % 2) ? F_IDLE_B : F_IDLE_A;
        drawKnight(kframe, portraitDst, Tint{});
        if (capped) SDL_RenderSetClipRect(r, nullptr);
        return;
    }

    if (gPortraitOnly) {
        const SDL_Rect dst = portraitDst;
        // Breathe on the same 600ms cadence the battle scene uses. View Enemy
        // was picking one idle frame and holding it, so the portrait sat there
        // as a still image while everything else in the game moved.
        int pframe2 = gEnemyFrame;
        if (es.animated && (pframe2 == F_IDLE_A || pframe2 == F_IDLE_B))
            pframe2 = ((SDL_GetTicks() / 600) % 2) ? F_IDLE_B : F_IDLE_A;
        Tint pt2 = gEnemyTint;
        if (gShadeTier == 1) { pt2.mulG *= 0.80f; pt2.mulB *= 0.55f; }
        blitInBox(es, pframe2, dst, pt2, 255, pscale, originY, capped ? bandH : sceneH);
        if (capped) SDL_RenderSetClipRect(r, nullptr);
        return;
    }

    // Knight on the left, enemy on the right, both bottom-aligned on the
    // backdrop's floor line - the same composition as the terminal scene.
    Tint pt = gPlayerTint;
    if (pt.identity()) pickAura(gAuraKnight, pt);
    // Pushed apart by an extra 8 units each: the backdrop reaches the window
    // edges, so the pair has room to stand apart.
    const int spread = spriteScale() * 8 + fieldExtra();
    SDL_Rect pdst{ originX + spriteScale() * 6 - spread + gPlayerNudge, floorY, sprW, sprH };
    // Your raised dead stand just in front of you, toward the middle, at
    // summon size. Drawn first, so your own lunge passes in front of them, and
    // anchored to where you stand rather than to that lunge.
    if (gAlly && gAlly->loaded) {
        const int as = summonScale();
        const int awid = 30 * as, ahgt = 32 * as;
        SDL_Rect adst{ pdst.x - gPlayerNudge + sprW - spriteScale() * 2 + gAllyNudge,
                       floorY + (sprH - ahgt), awid, ahgt };
        int aframe = F_IDLE_A;
        if (gAlly->animated && gAllyFrame >= 0) aframe = gAllyFrame;
        else if (gAlly->animated)
            aframe = ((SDL_GetTicks() / 600 + 1) % 2) ? F_IDLE_B : F_IDLE_A;
        // The Lich's adds wear the same shade: up out of the ground, not
        // native to the fight.
        Tint at = gAllyTint;
        if (at.identity()) { at.mulR = 0.78f; at.mulG = 0.82f; at.mulB = 0.95f; }
        gAllyRect = adst;
        blitMirrored(gAlly->sheet, aframe, adst, at);
    } else {
        gAllyRect = SDL_Rect{ 0, 0, 0, 0 };
    }
    int pframe = gPlayerFrame;
    if (pframe == F_IDLE_A || pframe == F_IDLE_B)
        pframe = ((SDL_GetTicks() / 600) % 2) ? F_IDLE_B : F_IDLE_A;
    gPlayerRect = pdst;
    if (gVesselShown >= 0 && gVesselShown < (int)gVesselTex.size()) {
        // the moon's vessel: the frames it took him in, then him as it wears him
        int v = gVesselShown;
        if (v >= (int)gVesselTex.size() - 2)
            v = (int)gVesselTex.size() - 2 + (int)((SDL_GetTicks() / 600) % 2);
        if (gVesselTex[v]) SDL_RenderCopy(r, gVesselTex[v], nullptr, &pdst);
    } else {
        drawKnight(pframe, pdst, pt);
    }
    if (gSlashFrame >= 0) blit(lib().slashFx, gSlashFrame, pdst, Tint{});
    if (gCastFrame >= 0)  blit(lib().castFx, gCastFrame, pdst, Tint{});

    Tint et = gEnemyTint;
    if (et.identity()) pickAura(gAuraEnemy, et);
    if (gShadeTier == 1) { et.mulG *= 0.80f; et.mulB *= 0.55f; }
    if (gStone) { et.mulR *= 0.62f; et.mulG *= 0.64f; et.mulB *= 0.72f; }
    SDL_Rect edst{ originX + sceneW - sprW - spriteScale() * 6 + spread - gEnemyNudge, floorY, sprW, sprH };
    // While idle, breathe between the two idle frames instead of standing on a
    // single one.
    int eframe = gEnemyFrame;
    if (es.animated && (eframe == F_IDLE_A || eframe == F_IDLE_B))
        eframe = ((SDL_GetTicks() / 600) % 2) ? F_IDLE_B : F_IDLE_A;
    // Ghost/Illusion: faded and spectral while the enemy can't be touched.
    // The box, not the frame: shots, sparks and numbers aim at the body.
    gEnemyRect = edst;
    // What it left behind stays where it stood, whatever the body does.
    if (gEnemyLeft >= 0 && es.sheet.count > gEnemyLeft) {
        SDL_Rect home = edst;
        home.x += gEnemyNudge;
        blitInBox(es, gEnemyLeft, home, Tint{}, 255, scale, originY, sceneH);
    }
    blitInBox(es, eframe, edst, et, (Uint8)std::min(gGhost ? 110 : 255, gEnemyAlpha), scale, originY, sceneH);

    // The bolt, drawn last so it passes in front of both fighters.
    if (gProjFrame >= 0) {
        const float t = gProjT < 0.0f ? 0.0f : (gProjT > 1.0f ? 1.0f : gProjT);
        const SDL_Rect& from = gProjReverse ? pdst : edst;
        const SDL_Rect& to   = gProjReverse ? edst
                             : (gBlowsAtAlly && gAllyRect.w > 0) ? gAllyRect : pdst;
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
            const SDL_Rect& at = (gBlowsAtAlly && gAllyRect.w > 0) ? gAllyRect : pdst;
            const int bx1 = at.x + at.w / 2;
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
            const SDL_Rect& at = (gBlowsAtAlly && gAllyRect.w > 0) ? gAllyRect : pdst;
            const int fx0 = at.x + at.w / 2, fx1 = edst.x + edst.w / 2;
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

    // The add stands inside the enemy, toward the middle of the field, at
    // summon size. Integer scaling only: a fractional step drops the
    // one-pixel features these sprites are mostly made of.
    if (gCompanion && gCompanion->loaded) {
        const int cs = summonScale();
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
    gVesselShown = -1;
    gVesselMote = -1.0f;
    gVesselPulse = -1.0f;
    gPlayerFrame = F_IDLE_A;
    gEnemyFrame = F_IDLE_A;
    gPlayerTint = Tint{};
    gEnemyTint = Tint{};
    gSlashFrame = -1;
    gCastFrame = -1;
    gEnemyNudge = 0;
    gEnemyAlpha = 255;
    gEnemyLeft = -1;
    gPlayerNudge = 0;
    gProjFrame = -1;
    gProjT = 0.0f;
    gProjTint = Tint{};
    gProjReverse = false;
    gProjScalePct = 30;
    gBeamFrame = -1;
    gFlameFrame = -1;
    gAllyNudge = 0;
    gAllyFrame = -1;
    gAllyTint = Tint{};
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
void setRestScene(int armorTier) { ensureInstalled(); gRestTier = armorTier; }

void setCutsceneShot(Cutscene scene, int shot) {
    ensureInstalled();
    gScene = scene == Cutscene::ENDING ? 1 : 0;
    gSceneShot = (shot >= 0 && shot < SCENES[gScene].shotCount) ? shot : -1;
    gSceneStart = SDL_GetTicks();
    gSceneSounded = -1;
}

void setEndingMoonless(bool on) { gEndingMoonless = on; }

bool cutsceneReady(Cutscene scene) {
    ensureInstalled();
    return wholeTexture(SCENES[scene == Cutscene::ENDING ? 1 : 0].file) != nullptr;
}

bool cutsceneShotPlaying() {
    return gSceneShot >= 0 && SDL_GetTicks() - gSceneStart < (Uint32)shotIntroMs();
}

void finishCutsceneShot() {
    if (gSceneShot < 0) return;
    gSceneStart = SDL_GetTicks() - (Uint32)shotIntroMs();
    const CutsceneShot& s = shotDef();
    gSceneSounded = s.first + s.intro - 1;
}

int cutsceneBottom() {
    const SDL_Rect d = sceneRect();
    return d.y + d.h;
}

void drawItemIcon(SDL_Renderer* r, int index, const SDL_Rect& dst) {
    (void)r;
    if (index >= MEDAL_ICON0) { drawMedal(index - MEDAL_ICON0, dst); return; }
    ensureInstalled();
    blit(lib().items, index, dst, Tint{});
}

// Loaded on first use, like the wordmarks: the title screen's achievement list
// can want one before any fight has loaded the library.
void drawMedal(int frame, const SDL_Rect& dst) {
    static Sheet medals;
    static bool tried = false;
    if (!tried) { tried = true; medals = loadSheet(basePath() + "assets/sprites/medals.png", 24); }
    // Until the crimson moon is on the sheet, the gold medal stands in for it.
    if (medals.ok()) blit(medals, frame < medals.count ? frame : 2, dst, Tint{});
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

void popNumberAlly(int amount, PopKind kind) {
    ensureInstalled();
    popIn(gAllyRect, amount, kind);
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
void setPortraitRows(int rows) { gPortraitCap = rows; }

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
    if (gArrivalPending) gEnemyAlpha = 0;   // not out of the eclipse yet
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

// Sends a bolt between the fighters; reverse=true for the knight's own
// spells. muzzleX/muzzleY are percentages of the shooter's sprite from
// ProjectileTable.h, -1 to use the value derived from the sheet.
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
    // A ranged attacker holds its ground; a diving flyer (closeIn) still
    // crosses. Lunges are fractions of the gap between the fighters.
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

    // A ranged attack sends its projectile across (negative: nothing crosses,
    // as for the Wyvern and the Falcon). Not an early return: the resets below
    // must run, or a flyer stays frozen mid-lunge.
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

    // Step in on the wind-up, furthest forward on the swing itself, so a melee
    // exchange reads as a blow landing. Same fractions as the enemy lunge.
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
    const int reach = restingGap() * 55 / 100 + summonReachExtra();
    const bool anim = gCompanion->animated;
    if (anim) gCompanionFrame = F_ATK1;
    gCompanionNudge = reach * 25 / 55; hold(90);
    if (anim) gCompanionFrame = F_ATK2;
    gCompanionNudge = reach * 45 / 55; hold(90);
    if (anim) gCompanionFrame = F_ATK3;
    gCompanionNudge = reach; hold(120);
    gCompanionNudge = 0;
    gCompanionFrame = -1;
}

// Your raised dead take their turn: a step out at the enemy and back, and
// whatever it struck flinches if the blow landed. It stands in front of you,
// so it has a shorter way to go than you do.
void printAllyAttack(EnemyType type, BossType boss, bool connected, bool onCompanion) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    if (!gAlly || !gAlly->loaded) return;
    const int reach = restingGap() * 55 / 100 + summonReachExtra();
    const bool anim = gAlly->animated;
    if (anim) gAllyFrame = F_ATK1;
    gAllyNudge = reach * 25 / 55; hold(90);
    if (anim) gAllyFrame = F_ATK2;
    gAllyNudge = reach * 45 / 55; hold(90);
    if (anim) gAllyFrame = F_ATK3;
    gAllyNudge = reach;
    if (connected) {
        Platform::shake(140, 6.0f);
        popSparksIn(onCompanion ? gCompanionRect : gEnemyRect, true);
        if (onCompanion) { gCompanionFrame = F_HIT; gCompanionTint = HIT_FLASH; }
        else {
            if (artSet(type, boss).animated) gEnemyFrame = F_HIT;
            gEnemyTint = HIT_FLASH;
        }
    }
    hold(150);
    gAllyNudge = 0;
    gAllyFrame = -1;
    gEnemyFrame = F_IDLE_A;
    gEnemyTint = Tint{};
    gCompanionFrame = -1;
    gCompanionTint = Tint{};
}

// A blow it takes for you: its own flinch, where the knight's would have been.
void printAllyHit(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    if (!gAlly || !gAlly->loaded) return;
    if (gAlly->animated) gAllyFrame = F_HIT;
    gAllyTint = HIT_FLASH;
    Platform::shake(180, 9.0f);
    popSparksIn(gAllyRect, false);
    hold(110);
    gAllyTint = Tint{};
    gAllyFrame = -1;
}

// Up out of the ground: lying where it fell, then on its feet.
void printAllyRise(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    if (!gAlly || !gAlly->loaded || !gAlly->animated) return;
    gAllyFrame = F_DEATH; gAllyTint = DEATH_DARK; hold(160);
    gAllyTint = Tint{};                           hold(120);
    gAllyFrame = F_HIT;                           hold(120);
    gAllyFrame = -1;
}

// And back down where it stood, darkening, before it is gone.
void printAllyFall(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    if (!gAlly || !gAlly->loaded) return;
    if (gAlly->animated) gAllyFrame = F_DEATH;
    gAllyTint = DEATH_DARK;
    hold(320);
    gAllyTint = Tint{};
    gAllyFrame = -1;
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
    if (castsWithHands(s, boss)) {
        gEnemyFrame = F_SPELL; hold(noFlight ? 420 : 230);
        if (!noFlight) gEnemyFrame = F_IDLE_A;
    } else if (s.animated) {
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
    // Wind-up in his hand, then the spell travels across to the enemy.
    gPlayerFrame = 7; gCastFrame = fxIdx;
    hold(200);
    gCastFrame = -1;
    // The orb in his hand (every cast-fx frame puts it at x83-93%, y3-15%) leaves
    // from exactly there as the enemy orb shape in the same colour, and drops
    // onto the middle of the enemy.
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
    const bool hands = !onEnemy && castsWithHands(artSet(type, boss), boss);
    if (hands) gEnemyFrame = F_SPELL;
    // Long enough to read as a hit of colour, short enough that a move made only
    // of a status does not stall the turn.
    hold(360);
    if (hands) gEnemyFrame = F_IDLE_A;
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

// The moon's red at strength k (0 none, 1 full): the colour drained toward
// crimson and darkened, as the moon's own creatures are drawn.
static Tint moonTint(float k) {
    Tint t;
    t.mulR = 1.0f - 0.10f * k; t.mulG = 1.0f - 0.60f * k; t.mulB = 1.0f - 0.55f * k;
    t.addR = (int)(96 * k); t.addG = 0; t.addB = (int)(14 * k);
    return t;
}

// The moon's crimson wisp: the Vampire's, the one red wisp on the projectile
// sheet. It flies from that sheet (isCast false); the cast sheet has five
// frames, so asked of it, this frame drew nothing.
static int moonWisp() {
    int wisp = ProjectileTable::GEN_UNDEAD;
    for (int i = 0; i < ProjectileTable::kByNameCount; i++)
        if (std::string(ProjectileTable::kByName[i].enemy) == "Vampire") wisp = ProjectileTable::kByName[i].frame;
    return wisp;
}

void printBattleMoonstruck(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    Audio::playSFX("moonstruck");
    const bool hands = castsWithHands(artSet(type, boss), boss);
    if (hands) gEnemyFrame = F_SPELL;      // the moon's fire held up in its hands
    // It flares: two pulses of the moon's red through it, the second harder.
    for (int pulse = 0; pulse < 2; ++pulse) {
        const float peak = pulse == 0 ? 0.7f : 1.0f;
        for (int i = 0; i <= 6; ++i) { gEnemyTint = moonTint(peak * i / 6.0f); hold(28); }
        for (int i = 6; i >= 0; --i) { gEnemyTint = moonTint(peak * i / 6.0f); hold(28); }
    }
    gEnemyTint = Tint{};
    // A crimson wisp crosses to him, out of the fire in its near hand when it
    // holds it up (frame 11's flame sits at about (22, 2) of the 56x36 frame:
    // a fifth into the sprite box, at its top).
    flyProjectile(moonWisp(), Tint{}, 34, /*reverse*/false, /*isCast*/false,
                  hands ? 20 : -1, hands ? 0 : -1);
    // And the moon's red takes him, then sinks in and stays as his aura.
    Platform::shake(320, 5.0f);
    popSparks(false);
    for (int i = 10; i >= 0; --i) { gPlayerTint = moonTint(i / 10.0f); hold(36); }
    gPlayerTint = Tint{};
    if (hands) gEnemyFrame = F_IDLE_A;
}

void setBattleAuras(AuraFlags knight, AuraFlags enemy) {
    gAuraKnight = knight;
    gAuraEnemy = enemy;
}

void setEnemyGhost(bool on) { gGhost = on; }
void setEnemyStone(bool on) { gStone = on; }

void setGearTiers(int weaponTiers, int armorTiers) {
    ensureInstalled();
    gWeaponTiers = weaponTiers;
    gArmorTiers  = armorTiers;
}

// Force the lazy Library to build now: it decodes roughly twenty PNGs and
// creates two GPU textures for each. Called from the title screen, where a
// pause is expected, rather than inside the opening beat of the first fight.
void preload() { (void)lib(); ensureInstalled(); }



void setBattleBackdrop(int encounterNumber) {
    ensureInstalled();
    // Past the peak, the ruined church: its moon eclipsed for the last fight.
    if (encounterNumber > 50) {
        Library& L = lib();
        gBgSheet = (encounterNumber >= 60 && L.churchEclipseBg.ok()) ? &L.churchEclipseBg
                 : L.churchBg.ok() ? &L.churchBg : &L.bg[4];
        gBgPrev = nullptr; gBgFade = 1.0f;
        gBgFrame = gBgPrevFrame = -1;
        gArrivalPending = false;
        applyGround();
        return;
    }
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
    // given on any other Moonstruck fight.
    const SDL_Color groundTo = groundFromBackdrop(to, 26);
    if (to->ok()) {
        gBgPrev = gBgSheet;
        gBgSheet = to;
        gBgFade = 0.0f;
    }
    setEnemyVariant(trueName);
    gTrueForm = true;   // it keeps the Shadow Knight's name, so not found by it
    // Inside the glare the knight bends into the new shape: stooping, something
    // between the two, the true form coming together. Each one shakes the
    // screen, and the white hides everything but shape.
    {
        const ArtSet& t = artSet(gType, gBoss);
        if (t.sheet.count > F_MORPH3) {
            gEnemyFrame = F_MORPH1; Platform::shake(180, 2.0f); hold(140);
            gEnemyFrame = F_MORPH2; Platform::shake(200, 3.0f); hold(140);
            gEnemyFrame = F_MORPH3; Platform::shake(220, 2.4f); hold(160);
        }
    }
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
    gCompanion = spriteKey.empty() ? nullptr : summonSheet(spriteKey);
}

void setBlowsAtAlly(bool on) { gBlowsAtAlly = on; }

void setAlly(const std::string& spriteKey) {
    ensureInstalled();
    gAlly = spriteKey.empty() ? nullptr : summonSheet(spriteKey);
    gAllyNudge = 0;
    gAllyFrame = -1;
    gAllyTint = Tint{};
}

void setEnemyVariant(const std::string& enemyName) {
    ensureInstalled();
    gNamedVariant = nullptr;
    gCompanion = nullptr;   // a new fight never inherits the last one's add
    // Run.cpp prefixes second-wave enemies with "Greater" and bosses with
    // "Ancient", so the name is enough to know which pass this is. A name
    // that starts with "The" takes it after: "The Greater Unchosen".
    gShadeTier = (enemyName.rfind("Greater ", 0) == 0 || enemyName.rfind("Ancient ", 0) == 0
                  || enemyName.rfind("The Greater ", 0) == 0) ? 1 : 0;
    gTrueForm = enemyName.find("Moonstruck Shadow Knight") != std::string::npos;
    gWideField = enemyName.find("Lich") != std::string::npos
              || enemyName.find("False Moon") != std::string::npos
              || enemyName.find("Shadow Knight") != std::string::npos;
    static ArtSet cache[NAMED_COUNT];
    static bool tried[NAMED_COUNT] = { false };
    for (int i = 0; i < NAMED_COUNT; i++) {
        if (enemyName.find(NAMED_TABLE[i].key) == std::string::npos) continue;
        if (!tried[i]) {
            cache[i] = loadNamed(NAMED_TABLE[i]);
            tried[i] = true;
        }
        // A missing sheet leaves gNamedVariant unset so artSet() falls through
        // to the type's generic sprite instead of drawing nothing.
        if (cache[i].loaded) gNamedVariant = &cache[i];
        return;
    }
}

// The False Moon was the moon: when it dies, the eclipse in the church's sky
// goes out with it, fading from the breach over about a second and taking its
// light off the stone, so the ruin is left under the stars. The ground turns
// with the sky, as it does when the peak becomes the true form's arena. Only
// from the eclipse itself, so nothing else that dies there touches the sky.
void moonGoesOut() {
    Library& L = lib();
    if (!L.churchGoneBg.ok() || gBgSheet != &L.churchEclipseBg) return;
    const SDL_Color groundFrom = groundFromBackdrop(gBgSheet, 26);
    const SDL_Color groundTo = groundFromBackdrop(&L.churchGoneBg, 26);
    gBgPrev = gBgSheet;
    gBgSheet = &L.churchGoneBg;
    const int STEPS = 24, MS = 45;
    for (int i = 0; i <= STEPS; ++i) {
        const float k = (float)i / STEPS;
        gBgFade = k;
        Platform::setGroundColor(SDL_Color{
            (Uint8)(groundFrom.r + (groundTo.r - groundFrom.r) * k),
            (Uint8)(groundFrom.g + (groundTo.g - groundFrom.g) * k),
            (Uint8)(groundFrom.b + (groundTo.b - groundFrom.b) * k), 255 });
        hold(MS);
    }
    gBgPrev = nullptr;
    gBgFade = 1.0f;
    Platform::setGroundColor(groundTo);
}

void printBattleDeath(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    const ArtSet& s = artSet(type, boss);

    // A sheet with crack frames (the true form) gets a death sequence: it turns
    // away with its sword lowered and shakes as the cracks open until it bursts.
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
    } else {
        if (s.animated) gEnemyFrame = F_DEATH;
        gEnemyTint = DEATH_DARK;
        hold(300);
    }
    if (boss == BossType::FALSE_MOON) moonGoesOut();
}

// The true form in the run the church is open: it does not die at the peak,
// it runs, so it is spared its cracked death. It reels from the last blow,
// lets go of its sword, turns and runs back into the dark, fading as it
// goes. The sword stays standing where it was planted (the False Moon has
// none), and both stay as they are until the next fight sets the scene.
void printBattleFlee(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    const ArtSet& s = artSet(type, boss);
    gEnemyTint = HIT_FLASH;
    if (s.animated) gEnemyFrame = F_HIT;
    Platform::shake(260, 2.5f);
    hold(160);
    gEnemyTint = Tint{};
    hold(320);
    // It goes as the empty armour went in the intro: the dark comes over it,
    // it sinks into the ground it stands on, and the pool it leaves runs off
    // into the dark behind it.
    if (s.sheet.count > F_POOL_SMALL) {
        gEnemyLeft = F_LEFT_SWORD;                // the sword stays planted
        gEnemyFrame = F_LETGO;
        hold(260);
        const int holds[] = { 200, 260, 200, 200, 160 };
        for (int i = 0; i < 5; ++i) {
            gEnemyFrame = F_SINK1 + i;
            if (i == 2) Audio::playSFXPitched("wind", 0.7f);
            hold(holds[i]);
        }
        const int away = spriteW() * 5 / 4;
        const int STEPS = 12;
        for (int i = 1; i <= STEPS; ++i) {
            const float k = (float)i / STEPS;
            gEnemyFrame = k < 0.4f ? F_POOL : F_POOL_SMALL;
            gEnemyNudge = -(int)(away * k);
            gEnemyAlpha = (int)(255 * std::min(1.0f, 1.6f * (1.0f - k)));
            hold(50);
        }
        gEnemyAlpha = 0;
        gEnemyNudge = 0;
        gEnemyFrame = F_IDLE_A;
        hold(560);                                // the peak empty but for the sword
        return;
    }
    const bool letsGo = s.sheet.count > F_TURNED;
    if (letsGo) {
        gEnemyLeft = F_LEFT_SWORD;                // the sword stays planted
        gEnemyFrame = F_LETGO;
        hold(260);
        gEnemyFrame = F_TURNED;
        hold(140);
    }
    Audio::playSFXPitched("wind", 0.8f);
    const int away = spriteW() * 5 / 4;           // well back, into the dark behind it
    const int STEPS = 12;
    for (int i = 1; i <= STEPS; ++i) {
        const float k = (float)i / STEPS;
        gEnemyNudge = -(int)(away * k * k);           // slow to start, then gone
        gEnemyAlpha = (int)(255 * (1.0f - k));
        Tint dark;                                     // and into the dark as it goes
        dark.mulR = dark.mulG = dark.mulB = 1.0f - 0.5f * k;
        gEnemyTint = dark;
        hold(45);
    }
    gEnemyAlpha = 0;
    gEnemyTint = Tint{};
    gEnemyFrame = F_IDLE_A;
    hold(letsGo ? 560 : 240);          // the peak empty but for the sword
}

// Whether the False Moon comes out of the eclipse: its sheet has the
// arrival frames and the church has its moon's waking. Called as the fight's
// backdrop goes up, before anything is drawn: the ruin under its plain moon,
// and the False Moon not there yet.
bool prepareArrival(BossType boss) {
    ensureInstalled();
    Library& L = lib();
    if (boss != BossType::FALSE_MOON || L.FALSEMOON.sheet.count <= F_ARRIVE8
        || !L.churchWakeBg.ok() || !L.churchBg.ok() || !L.churchEclipseBg.ok()) return false;
    gBgSheet = &L.churchBg;
    gBgPrev = nullptr; gBgFade = 1.0f;
    gBgFrame = gBgPrevFrame = -1;
    applyGround();
    gArrivalPending = true;
    gEnemyAlpha = 0;
    return true;
}

// The False Moon comes out of the eclipse. The church's moon opens the
// intro's eye, looks down at the knight, and its pupil opens until it fills
// the moon: the eclipse, a bell going low as it does. Then the eye opens
// again where the False Moon will stand, shuts into a dark moon, widens into
// its ring, and it climbs out. The fight's music starts as it takes shape
// (`track`, nothing until then). Only after prepareArrival.
void printBattleArrival(EnemyType type, BossType boss, const char* track) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    if (!gArrivalPending) return;
    Library& L = lib();
    gEnemyAlpha = 0;
    hold(600);                                    // the plain moon, for a moment
    // Its eye: a slit, open, turned on him, then shutting.
    gBgSheet = &L.churchWakeBg;
    const int wake[] = { 320, 360, 760, 220, 220, 420 };
    const int frames = std::min(6, L.churchWakeBg.count);
    for (int i = 0; i < frames; ++i) {
        gBgFrame = i;
        if (i == 5) Audio::playSFXPitched("church_bell", 0.6f);   // the moon is all pupil
        hold(wake[i]);
    }
    // Its light goes off the stone, the ground with it.
    const SDL_Color groundFrom = groundFromBackdrop(&L.churchBg, 26);
    const SDL_Color groundTo = groundFromBackdrop(&L.churchEclipseBg, 26);
    gBgPrev = &L.churchWakeBg; gBgPrevFrame = frames - 1;
    gBgSheet = &L.churchEclipseBg; gBgFrame = -1;
    const int STEPS = 12;
    for (int i = 0; i <= STEPS; ++i) {
        const float k = (float)i / STEPS;
        gBgFade = k;
        Platform::setGroundColor(SDL_Color{
            (Uint8)(groundFrom.r + (groundTo.r - groundFrom.r) * k),
            (Uint8)(groundFrom.g + (groundTo.g - groundFrom.g) * k),
            (Uint8)(groundFrom.b + (groundTo.b - groundFrom.b) * k), 255 });
        hold(40);
    }
    gBgPrev = nullptr; gBgPrevFrame = -1; gBgFade = 1.0f;
    Platform::setGroundColor(groundTo);
    hold(300);
    // Where it will stand, the eye again: it shuts into a dark moon, and
    // the dark moon becomes its ring.
    gArrivalPending = false;
    gEnemyAlpha = 255;
    const int holds[] = { 420, 380, 300, 280, 240, 300, 340, 420 };
    for (int i = 0; i < 8; ++i) {
        gEnemyFrame = F_ARRIVE1 + i;
        if (i == 2) Audio::playSFXPitched("church_bell", 0.5f);                       // its eye shuts
        if (F_ARRIVE1 + i == F_RISING && track) Audio::playBGM(std::string(track));   // it takes shape
        if (i == 7) Platform::shake(260, 3.0f);                                        // it is out
        hold(holds[i]);
    }
    gEnemyFrame = F_IDLE_A;
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

// ---- the moon takes its vessel --------------------------------------------------------------
// The knight's own pixels for a pose, his armour tier with his blade over it,
// read from the gear sheets themselves (the textures keep none), so the moon
// takes him as he is dressed.
namespace {
struct Rgba { std::vector<unsigned char> px; int w = 0, h = 0; };

Rgba loadRgba(const std::string& rel) {
    Rgba p;
    int comp = 0;
    if (unsigned char* d = stbi_load((basePath() + rel).c_str(), &p.w, &p.h, &comp, 4)) {
        p.px.assign(d, d + (size_t)p.w * p.h * 4);
        stbi_image_free(d);
    } else {
        p.w = p.h = 0;
    }
    return p;
}

struct KnightSheets { Rgba armour, weapon, plain; };
KnightSheets knightSheets() {
    KnightSheets k;
    k.armour = loadRgba("assets/sprites/player_armor.png");
    if (k.armour.w <= 0) k.plain = loadRgba("assets/sprites/player.png");
    k.weapon = loadRgba("assets/sprites/player_weapon.png");
    return k;
}

// 30x32 RGBA of the knight in pose `frame`.
std::vector<unsigned char> knightPixels(const KnightSheets& k, int frame) {
    std::vector<unsigned char> out(30 * 32 * 4, 0);
    const int poses = std::max(1, lib().player.count);
    auto layer = [&](const Rgba& sheet, int tier) {
        if (sheet.w <= 0) return false;
        const int tiers = sheet.w / (30 * poses);
        const int t = std::max(0, std::min(tier, tiers - 1));
        const int x0 = (t * poses + frame) * 30;
        for (int y = 0; y < 32 && y < sheet.h; ++y)
            for (int x = 0; x < 30; ++x) {
                const unsigned char* s = &sheet.px[((size_t)y * sheet.w + x0 + x) * 4];
                unsigned char* d = &out[((size_t)y * 30 + x) * 4];
                const float a = s[3] / 255.0f;
                if (a <= 0.0f) continue;
                for (int c = 0; c < 3; ++c) d[c] = (unsigned char)(s[c] * a + d[c] * (1.0f - a) + 0.5f);
                d[3] = (unsigned char)std::min(255, (int)(s[3] + d[3] * (1.0f - a) + 0.5f));
            }
        return true;
    };
    if (!layer(k.armour, gArmorTiers)) layer(k.plain, 0);
    layer(k.weapon, gWeaponTiers);
    return out;
}

float lumOf(const unsigned char* p) { return 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]; }

// Him as the moon wears him: black, the moon's red round the outside of him
// brighter where the light finds it, its red flecks through him; the moon's
// own armour (tools/trophy_moon_style.py), so the vessel looks like it.
std::vector<unsigned char> asVessel(const std::vector<unsigned char>& src) {
    static const unsigned char BODY[3] = { 12, 6, 10 }, BODY_LIT[3] = { 24, 10, 16 };
    static const unsigned char RIM[3] = { 214, 46, 44 }, RIM_DIM[3] = { 76, 15, 21 };
    static const unsigned char SPECK[3] = { 128, 24, 32 }, SPECK_HOT[3] = { 220, 64, 48 };
    std::vector<unsigned char> out = src;
    float lo = 255.0f, hi = 0.0f;
    for (int i = 0; i < 30 * 32; ++i)
        if (src[i * 4 + 3]) { const float l = lumOf(&src[i * 4]); lo = std::min(lo, l); hi = std::max(hi, l); }
    auto solid = [&](int x, int y) { return x >= 0 && x < 30 && y >= 0 && y < 32 && src[((size_t)y * 30 + x) * 4 + 3] > 0; };
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 30; ++x) {
            unsigned char* d = &out[((size_t)y * 30 + x) * 4];
            if (!d[3]) continue;
            const unsigned char* c;
            if (!solid(x - 1, y) || !solid(x, y - 1)) c = RIM;
            else if (!solid(x + 1, y) || !solid(x, y + 1)) c = RIM_DIM;
            else {
                const int h = (x * 73 + y * 151) % 43;
                const float t = (lumOf(&src[((size_t)y * 30 + x) * 4]) - lo) / std::max(1.0f, hi - lo);
                c = h == 0 ? SPECK_HOT : (h == 11 || h == 29) ? SPECK : (t > 0.55f ? BODY_LIT : BODY);
            }
            d[0] = c[0]; d[1] = c[1]; d[2] = c[2];
        }
    return out;
}

// The slit of his visor: the widest dark run a few rows into his helm, the
// way make_trophy_gear finds it. Its middle four pixels.
std::vector<std::pair<int, int>> visorOf(const std::vector<unsigned char>& src) {
    std::vector<std::pair<int, int>> eye;
    int top = -1;
    for (int y = 0; y < 32 && top < 0; ++y)
        for (int x = 0; x < 30; ++x) if (src[((size_t)y * 30 + x) * 4 + 3]) { top = y; break; }
    if (top < 0) return eye;
    for (int y = top + 3; y < std::min(top + 6, 32); ++y) {
        int bestA = -1, bestN = 0, a = -1, n = 0;
        for (int x = 0; x <= 30; ++x) {
            const bool dark = x < 30 && src[((size_t)y * 30 + x) * 4 + 3] && lumOf(&src[((size_t)y * 30 + x) * 4]) < 90.0f;
            if (dark) { if (n == 0) a = x; ++n; }
            else { if (n > bestN) { bestN = n; bestA = a; } n = 0; }
        }
        if (bestN >= 4) {
            const int mid = bestA + bestN / 2;
            for (int x = mid - 2; x < mid + 2; ++x) eye.push_back({ x, y });
            return eye;
        }
    }
    return eye;
}

void lightVisor(std::vector<unsigned char>& px, const std::vector<std::pair<int, int>>& eye, float k) {
    static const unsigned char EYE[3] = { 214, 58, 52 }, EYE_HOT[3] = { 255, 120, 90 };
    for (size_t i = 0; i < eye.size(); ++i) {
        unsigned char* d = &px[((size_t)eye[i].second * 30 + eye[i].first) * 4];
        if (!d[3]) continue;
        const unsigned char* c = (i % 4 == 1 || i % 4 == 2) ? EYE_HOT : EYE;
        for (int j = 0; j < 3; ++j) d[j] = (unsigned char)(d[j] + (c[j] - d[j]) * k + 0.5f);
    }
}

SDL_Texture* textureOf(std::vector<unsigned char>& px) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), 30, 32, 32, 30 * 4, SDL_PIXELFORMAT_RGBA32);
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(Platform::renderer(), s);
    SDL_FreeSurface(s);
    if (t) {
        SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    return t;
}

// The frames: the dark spreading through him from his chest, where it came
// into him, as it took him at the pond in the intro: a red edge where it is
// still taking him and the outline of it boiling a little from one frame to
// the next, slow to begin, reaching his far edge on the last frame; his visor
// lighting as it finishes; then him standing as the vessel, both breaths.
// What it has not reached yet is him as the piece left him: EMPTIED.
const int VESSEL_STEPS = 14;
Tint emptiedTint() {
    Tint t;
    t.mulR = 0.66f; t.mulG = 0.68f; t.mulB = 0.78f;
    return t;
}
void buildVessel() {
    for (SDL_Texture* t : gVesselTex) if (t) SDL_DestroyTexture(t);
    gVesselTex.clear();
    const KnightSheets sheets = knightSheets();
    const std::vector<unsigned char> a = knightPixels(sheets, F_IDLE_A), b = knightPixels(sheets, F_IDLE_B);
    const std::vector<unsigned char> va = asVessel(a);
    const std::vector<std::pair<int, int>> eye = visorOf(a);
    float cx = 0, cy = 0, n = 0;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 30; ++x)
            if (a[((size_t)y * 30 + x) * 4 + 3]) { cx += x; cy += y; n += 1; }
    if (n > 0) { cx /= n; cy = cy / n - 2.0f; }                      // his chest
    else { cx = 15.0f; cy = 14.0f; }
    gVesselChestX = (cx + 0.5f) * 100.0f / 30.0f;
    gVesselChestY = (cy + 0.5f) * 100.0f / 32.0f;
    float far = 0.0f;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 30; ++x)
            if (a[((size_t)y * 30 + x) * 4 + 3]) far = std::max(far, std::hypot(x - cx, (y - cy) * 0.85f));
    const Tint dim = emptiedTint();
    for (int k = 0; k < VESSEL_STEPS; ++k) {
        const float t = (k + 1) / (float)VESSEL_STEPS;
        const float reach = (far + 2.6f) * std::pow(t, 1.3f);       // the boil is 2.2 at most
        std::vector<unsigned char> px = a;
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 30; ++x) {
                unsigned char* d = &px[((size_t)y * 30 + x) * 4];
                if (!d[3]) continue;
                const float dist = std::hypot(x - cx, (y - cy) * 0.85f)
                    + 1.3f * std::sin(x * 1.7f + y * 0.9f + k * 1.9f) + 0.9f * std::sin(y * 2.3f - x * 0.7f - k);
                const unsigned char* v = &va[((size_t)y * 30 + x) * 4];
                if (dist < reach) { d[0] = v[0]; d[1] = v[1]; d[2] = v[2]; continue; }
                d[0] = (unsigned char)(d[0] * dim.mulR + 0.5f);
                d[1] = (unsigned char)(d[1] * dim.mulG + 0.5f);
                d[2] = (unsigned char)(d[2] * dim.mulB + 0.5f);
                if (dist < reach + 1.4f) {
                    d[0] = (unsigned char)(d[0] + (214 - d[0]) * 0.8f);
                    d[1] = (unsigned char)(d[1] + (46 - d[1]) * 0.8f);
                    d[2] = (unsigned char)(d[2] + (44 - d[2]) * 0.8f);
                }
            }
        if (t >= 0.8f) lightVisor(px, eye, std::min(1.0f, (t - 0.8f) / 0.2f));
        gVesselTex.push_back(textureOf(px));
    }
    std::vector<unsigned char> fa = va, fb = asVessel(b);
    lightVisor(fa, eye, 1.0f);
    lightVisor(fb, visorOf(b), 1.0f);
    gVesselTex.push_back(textureOf(fa));
    gVesselTex.push_back(textureOf(fb));
}

// The eclipse ends: the church's sky goes back to its red moon, the ground
// with it, as the moon came back into the pond in the intro once it had him.
void eclipseEnds() {
    Library& L = lib();
    if (!L.churchBg.ok() || gBgSheet != &L.churchEclipseBg) return;
    const SDL_Color groundFrom = groundFromBackdrop(gBgSheet, 26);
    const SDL_Color groundTo = groundFromBackdrop(&L.churchBg, 26);
    gBgPrev = gBgSheet;
    gBgSheet = &L.churchBg;
    const int STEPS = 24;
    for (int i = 0; i <= STEPS; ++i) {
        const float k = (float)i / STEPS;
        gBgFade = k;
        Platform::setGroundColor(SDL_Color{
            (Uint8)(groundFrom.r + (groundTo.r - groundFrom.r) * k),
            (Uint8)(groundFrom.g + (groundTo.g - groundFrom.g) * k),
            (Uint8)(groundFrom.b + (groundTo.b - groundFrom.b) * k), 255 });
        hold(45);
    }
    gBgPrev = nullptr;
    gBgFade = 1.0f;
    Platform::setGroundColor(groundTo);
}
} // namespace

// The moon takes its vessel. The last piece of him goes up out of him and
// into the moon, and he greys without it; the False Moon holds up its fire,
// goes dark and comes apart, and what it is crosses to him as the crimson
// wisp; the dark spreads through him from his chest, his visor lights the
// moon's red, and he stands as the moon wears him, in its own armour's look.
// The eclipse ends: it has its body.
void printBattleVessel(EnemyType type, BossType boss) {
    ensureInstalled();
    gPortraitOnly = false;
    gType = type; gBoss = boss;
    showScene();
    buildVessel();
    const ArtSet& s = artSet(type, boss);
    gPlayerFrame = F_IDLE_A; gPlayerTint = Tint{}; gPlayerNudge = 0;
    gEnemyTint = Tint{}; gEnemyNudge = 0;
    hold(300);
    // the last piece of him goes up into the moon
    Audio::playSFXPitched("special", 1.5f);
    const Tint emptied = emptiedTint();
    const int RISE = 40;
    for (int i = 0; i <= RISE; ++i) {
        const float t = i / (float)RISE;
        const float k = std::min(1.0f, t / 0.3f);
        gVesselMote = t;
        gPlayerTint.mulR = 1.0f + (emptied.mulR - 1.0f) * k;
        gPlayerTint.mulG = 1.0f + (emptied.mulG - 1.0f) * k;
        gPlayerTint.mulB = 1.0f + (emptied.mulB - 1.0f) * k;
        hold(32);
    }
    gVesselMote = -1.0f;
    for (int i = 0; i <= 10; ++i) { gVesselPulse = i / 10.0f; hold(30); }
    gVesselPulse = -1.0f;
    hold(250);
    // it holds up its fire, goes dark, and comes apart into what it is
    const bool hands = castsWithHands(s, boss);
    if (hands) gEnemyFrame = F_SPELL;
    for (int i = 0; i <= 6; ++i) { gEnemyTint = moonTint(i / 6.0f); hold(30); }
    Audio::stopBGM();                       // its music began as it took shape; it ends as it lets go
    Audio::playSFX("moonstruck");
    for (int i = 0; i <= 10; ++i) {
        const float k = i / 10.0f;
        Tint t;
        t.mulR = 1.0f - 0.80f * k; t.mulG = 1.0f - 0.92f * k; t.mulB = 1.0f - 0.88f * k;
        t.addR = (int)(70 * (1.0f - k)); t.addB = (int)(10 * (1.0f - k));
        gEnemyTint = t;
        gEnemyAlpha = (int)(255 * (1.0f - k * k));
        hold(34);
    }
    gEnemyAlpha = 0;
    // What it is crosses to him: the moon's crimson wisp, from where its heart
    // was to his chest, slower than a shot.
    gProjSrcXPct = 50; gProjSrcPct = 45;
    gProjDstXPct = (int)(gVesselChestX + 0.5f); gProjDstPct = (int)(gVesselChestY + 0.5f);
    gProjIsCast = false; gProjReverse = false; gProjFall = false;
    gProjScalePct = 36; gProjTint = Tint{};
    gProjFrame = moonWisp();
    for (int i = 0; i <= 16; ++i) { gProjT = i / 16.0f; hold(28); }
    gProjFrame = -1; gProjT = 0.0f; gProjIsCast = true; gProjScalePct = 30;
    // and it takes him
    Platform::shake(300, 3.5f);
    for (int k = 0; k < VESSEL_STEPS; ++k) { gVesselShown = k; hold(75); }
    gVesselShown = VESSEL_STEPS;
    gPlayerTint = Tint{};
    Audio::playSFXPitched("church_bell", 0.45f);
    hold(500);
    eclipseEnds();
    hold(900);
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
