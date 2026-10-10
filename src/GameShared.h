// Tables and small helpers that Game.cpp and the other files of the Game
// class (GameEnemy, GameMirror, GameChurch, GameSave) all read. Internal
// to the game's sources.
#pragma once
#include "Card.h"
#include "Enemy.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// Raise Undead: what each of the dead rises as. Its body is a share of your
// max HP, never past its own ceiling, and its strike a share of the card's
// number through your weapon, and they grow the further up the road it fell:
// the Skeleton in the first dungeon is the weakest and the Dragon below the
// peak the strongest. Thin ones hit harder, fat ones last longer, the way the
// Lich's own adds do, and each one further up the road is at least as much as
// the one before it. The ceiling keeps a summon a summon: 50 for the dead of
// the first four areas, 75 for the mountain's, 100 for the Dragon, so it never
// rises with a boss's health.
struct RaisedDead { const char* name; int bodyPct; int bodyCap; int strikePct; const char* verb; };
static const RaisedDead RAISED_DEAD[] = {
    { "Skeleton", 20,  50, 100, "claws" },
    { "Ghoul",    30,  50, 100, "bites" },
    { "Wraith",   15,  50, 150, "rakes" },
    { "Banshee",  25,  50, 150, "screams at" },
    { "Specter",  20,  50, 175, "chills" },
    { "Revenant", 40,  75, 175, "cuts" },
    { "Lich",     30,  75, 200, "blasts" },
    { "Dragon",   45, 100, 250, "breathes on" },
};
inline const RaisedDead* raisedDead(const std::string& name) {
    for (const RaisedDead& d : RAISED_DEAD)
        if (name == d.name) return &d;
    return nullptr;
}
// Its health, for your max HP: the share, under the ceiling.
inline int raisedBody(const RaisedDead& d, int maxHp) {
    return std::max(1, std::min(d.bodyCap, maxHp * d.bodyPct / 100));
}

// The final boss's swing when you end a turn without playing a card, as a
// share of its attack: its real moves are the cards it plays against yours,
// so this is the lighter blow.
static const int KNIGHT_TURN_END_PCT = 40;

inline const char* typeWord(DamageType t) {
    switch (t) {
        case DamageType::SMASH:  return "Smash";
        case DamageType::PIERCE: return "Pierce";
        case DamageType::FIRE:   return "Fire";
        case DamageType::POISON: return "Poison";
        case DamageType::WIND:   return "Wind";
        default:                 return "none";
    }
}

// Relic ids, names and texts. The id is the bit in relicsOwned and in the
// save, so append only.
namespace Relic {
enum Id { VENOM_VIAL, EMBER_HEART, GALE_FEATHER, WEIGHTED_POMMEL, HOURGLASS, BONE_DICE,
          FORGE_HAMMER, WARDEN_LANTERN, RED_THREAD, SCHOLAR_LENS, SEAL_FRAGMENT,
          MOON_LOCKET, GLASS_MOON, COUNT };
struct Info { const char* name; const char* face; const char* text; bool cursed; };
const Info INFO[COUNT] = {
    { "Venom Vial",            "poison +50%",        "The Poison you put on enemies deals 50% more damage.", false },
    { "Ember Heart",           "burn +50%",          "The Burn you put on enemies deals 50% more damage.", false },
    { "Gale Feather",          "rend +50%",          "The Rend you put on enemies deals 50% more damage.", false },
    { "Weighted Pommel",       "smash can weaken",   "Your Smash attacks have a 35% chance to Weaken the enemy for 2 turns.", false },
    { "Hourglass",             "+1 energy turn 1",   "You have 1 extra energy on the first turn of every fight.", false },
    { "Bone Dice",             "reroll rewards",     "You can reroll the cards on offer once on every card reward.", false },
    { "Forge Hammer",          "forging heals",      "Forging a card at a rest site also heals 15% of your max HP.", false },
    { "Warden's Lantern",      "armor every turn",   "You start every turn with armor worth 5% of your max HP.", false },
    { "Red Thread",            "revive once",        "Once this run, a blow that would kill you brings you back at half your health instead.", false },
    { "Scholar's Lens",        "read its next move", "Regular enemies show their next move beside their health, whether it is their own move or one of their basic ones.", false },
    { "Vigil Ember",           "+8% dmg per vigil",  "Your attacks deal 8% more damage for every vigil you have put out.", false },
    { "Moonlit Locket",        "??? every area",     "Whatever it is that follows you will find you in every area from here on.", false },
    { "Glass Moon",            "+25% dealt & taken", "Your attacks deal 25% more damage, and every blow against you lands 25% harder.", true },
};
}

// Its own move in each shape, beside the Scent and the Maul it always has.
struct MoonMove { const char* name; const char* info; };
static const MoonMove MOON_MOVES[5] = {
    { "Grave Silk",           "Weaken 2 and Poison 3 at once: bone-white thread that rots what it holds." },
    { "Lunar Mirage",         "you draw 2 fewer cards next turn, and it gains armor while you cannot see it." },
    { "Howl at the Red Moon", "heals 6% of its health and gains +2 attack, up to +6." },
    { "Petrifying Gaze",      "stone creeps up your legs: kill it within 6 turns or it is over. Once per fight." },
    { "Eclipse Ward",         "raises a heavy guard and heals 8." },
};

// The first two shapes meet a deck that is still mostly starters, so they hit
// softer and their Moon Scent frenzy climbs less far. By shape, as MOON_MOVES
// is.
static const float MOON_ATTACK[5] = { 0.9f, 1.1f, 1.35f, 1.35f, 1.35f };
static const float MOON_FRENZY[5] = { 1.3f, 1.3f, 1.6f, 1.6f, 1.6f };
inline std::string timesText(float m) { std::ostringstream o; o << "x" << m; return o.str(); }
// The first three shapes are still working out how a body is used: Moon Scent
// lasts them one blow, the Maul that always comes the turn after, and leaves
// them exposed to you until then (x1.5, as the true form's Berserk does). The
// last two have learned to hold a frenzy for three turns.
static const int MOON_LEARNING_LAST = 2;
// The werewolf's Howl: 6% of its health. A tenth would undo most of a turn's
// work.
inline int howlHeal(int maxHp) { return std::max(1, (maxHp * 6 + 50) / 100); }

// The Sexton's Grave Dirt: a card that does nothing, three at most, gone when
// the fight is.
static const char* const GRAVE_DIRT = "Grave Dirt";
static const int GRAVE_DIRT_MAX = 3;
// The Assassin's ambush: the chance per card you play, once a turn. 45 had it
// strike nearly every turn (the user, 2026-10-10: 10).
static const int ASSASSIN_AMBUSH_PCT = 10;
inline bool isGraveDirt(const Card& c) { return c.getName() == GRAVE_DIRT; }

// The False Moon's Moonstruck: ten turns, and every two it takes back one of
// the pieces the road gave you, in the order it gave them. At zero it has the
// last of them, and you.
static const int MOON_CLOCK_TURNS = 10;
static const int MOON_WINBACK_UNDER = 5;   // quarters win turns back only below this
// A turn let go against the False Moon: this often it takes one of the Moon
// Shades' shapes and uses that shape's own move, instead of the light swing.
static const int FALSE_MOON_SHADE_PCT = 50;
static const int MOON_THREAD_TURNS = 5;    // the Red Thread leaves you at least this many

// A boss falling is the beat the run has been building to, so it gets its
// own cue, and so do its blows.
inline const char* deathSfx(bool isBoss) { return isBoss ? "boss_death" : "dead"; }

struct EquipTier { std::string name; int bonus; };

// Gear is a percentage of a card's own value, so it scales with the deck you
// built instead of paying out once per card played. No ceiling: a percentage
// cannot run away the way a flat bonus did.
static const int GEAR_PCT_CAP = 100000;

// Gear name and bonus escalate per tier claimed; the last tier repeats.
inline EquipTier weaponTierAt(int tier) {
    static const std::vector<EquipTier> tiers = {
        // The first tier has to be large enough to move a starter card: at +8% a
        // 5-damage card rounds straight back to 5.
        {"Rusty Blade", 15}, {"Iron Sword", 16}, {"Steel Blade", 17},
        {"Ebon Blade", 18}, {"Mythril Edge", 20}, {"Legendary Blade", 22},
        // The trophies keep climbing the ladder past the Legendary.
        {"Shadow Blade", 23}, {"Moon Blade", 24}
    };
    int idx = std::min(tier, (int)tiers.size() - 1);
    return tiers[idx];
}

inline EquipTier armorTierAt(int tier) {
    static const std::vector<EquipTier> tiers = {
        // Rusted mail first: the honest step up from leather. Named for what it
        // is, because that is what the sprite shows.
        {"Rusted Mail", 15}, {"Iron Plating", 16}, {"Steel Plating", 17},
        {"Ivory Plate", 18}, {"Mythril Plating", 20}, {"Legendary Aegis", 22},
        {"Shadow Plate", 23}, {"Moon Plate", 24}
    };
    int idx = std::min(tier, (int)tiers.size() - 1);
    return tiers[idx];
}

// Derived from the tier count rather than accumulated, so a save file only has
// to store the tier and the percentage rebuilds itself correctly on load.
inline int gearPercentFor(int tiers, bool weapon) {
    int p = 0;
    for (int i = 0; i < tiers; i++) p += (weapon ? weaponTierAt(i) : armorTierAt(i)).bonus;
    return std::min(GEAR_PCT_CAP, p);
}

// Taunt and Fear both miss one time in five, `luck` points less (yours,
// from Fortune; the true form's Fear gets none). Shared, so the two cannot
// drift apart the way the two copies of the effect table did.
inline bool provokeFizzles(int luck) {
    static thread_local std::mt19937 g(std::random_device{}());
    return std::uniform_int_distribution<>(1, 100)(g) <= 20 - luck;
}
