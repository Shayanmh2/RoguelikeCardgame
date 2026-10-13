// Tables and little helpers shared by all the Game*.cpp files. Internal.
#pragma once
#include "Card.h"
#include "Enemy.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// Raise Undead: what each dead thing comes back as. Health is a share of your
// max HP (capped), the hit is a share of the card through your weapon, and
// both go up the further along the road it died. Skinny ones hit harder, big
// ones last longer. The caps (50, then 75 on the mountain, 100 for the Dragon)
// keep a summon a summon, so it never turns into a second player.
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

// What the final boss hits for when you end a turn without playing anything.
// Its real moves are your own cards, so this is the light one.
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

// Relic ids, names and texts. The id is the bit in the save, so only ever
// add to the end.
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
    { "Scholar's Lens",        "read its next move", "Regular enemies show their next move beside their health, whether it is their own move or one of their basic ones. At a rest site, it shows what kind of enemy is next and what it hits with.", false },
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
    { "Petrifying Gaze",      "stone creeps up your legs. Kill it within 6 turns or it is over. Once per fight." },
    { "Eclipse Ward",         "raises a heavy guard and heals 8." },
};

// The first two shapes meet a deck that's still mostly starters, so they hit
// softer and their Moon Scent doesn't climb as far.
static const float MOON_ATTACK[5] = { 0.9f, 1.1f, 1.35f, 1.35f, 1.35f };
static const float MOON_FRENZY[5] = { 1.3f, 1.3f, 1.6f, 1.6f, 1.6f };
inline std::string timesText(float m) { std::ostringstream o; o << "x" << m; return o.str(); }
// The first three shapes haven't figured out bodies yet: Moon Scent lasts
// one blow, then the Maul, and they're wide open (x1.5) until then. The last
// two can hold a frenzy for three turns.
static const int MOON_LEARNING_LAST = 2;
// The werewolf's Howl heals 6%. A tenth would undo most of your turn.
inline int howlHeal(int maxHp) { return std::max(1, (maxHp * 6 + 50) / 100); }

// The Sexton's Grave Dirt: dead cards in your deck, three max, gone after the
// fight.
static const char* const GRAVE_DIRT = "Grave Dirt";
static const int GRAVE_DIRT_MAX = 3;
// The Assassin's ambush, chance per card you play, once a turn. Was 45 and it
// went off basically every turn, which was miserable. 10 feels right.
static const int ASSASSIN_AMBUSH_PCT = 10;
inline bool isGraveDirt(const Card& c) { return c.getName() == GRAVE_DIRT; }

// The False Moon's Moonstruck clock: ten turns. Every two it takes back a
// piece of you, in the order you got them back. At zero you're its vessel.
static const int MOON_CLOCK_TURNS = 10;
static const int MOON_WINBACK_UNDER = 5;   // quarters win turns back only below this
// A turn let go against the False Moon: this often it takes one of the Moon
// Shades' shapes and uses that shape's own move, instead of the light swing.
static const int FALSE_MOON_SHADE_PCT = 50;
static const int MOON_THREAD_TURNS = 5;    // the Red Thread leaves you at least this many

// Bosses get their own death sound (and hit sounds).
inline const char* deathSfx(bool isBoss) { return isBoss ? "boss_death" : "dead"; }

// ugh, this one. The Sit a while passages are saved as a bitmask by their
// position in the list, so putting a new one in the middle (the Mountain's
// third) shifts every passage after it by one. Old saves get moved along on
// load so nobody loses what they've heard. Layout 2 = with the new fire.
static const int SIT_LAYOUT = 2;
static const int SIT_ADDED_AT = 14;
inline unsigned long long sitsToLayout2(unsigned long long heard) {
    const unsigned long long below = heard & ((1ULL << SIT_ADDED_AT) - 1);
    return below | ((heard >> SIT_ADDED_AT) << (SIT_ADDED_AT + 1));
}

// Keepsakes from the camps (GameStops.cpp), one bit each in Game::keepsakes,
// in the same order as the camps.
enum KeepsakeId { KEEP_WICK = 0, KEEP_CHARM, KEEP_SNARE, KEEP_NET, KEEP_CANDLE, KEEPSAKE_COUNT };
static const int WICK_BONUS = 3;    // the Lamplighter's Wick, on every Fire attack
static const int CANDLE_HEAL = 15;  // the Templar's Candle, once a fight below half your health

struct EquipTier { std::string name; int bonus; };

// Gear is a % of the card's own value, so it scales with your deck. It used
// to be a flat bonus, and that ran away with cheap cards.
static const int GEAR_PCT_CAP = 100000;

// Gear name and bonus escalate per tier claimed; the last tier repeats.
inline EquipTier weaponTierAt(int tier) {
    static const std::vector<EquipTier> tiers = {
        // Tier 1 has to be big enough to actually change a starter: at +8% a
        // 5 damage card just rounds back to 5.
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
        // Rusted mail first, since that's what the sprite actually is.
        {"Rusted Mail", 15}, {"Iron Plating", 16}, {"Steel Plating", 17},
        {"Ivory Plate", 18}, {"Mythril Plating", 20}, {"Legendary Aegis", 22},
        {"Shadow Plate", 23}, {"Moon Plate", 24}
    };
    int idx = std::min(tier, (int)tiers.size() - 1);
    return tiers[idx];
}

// Worked out from the tier count, so saves only store the tier.
inline int gearPercentFor(int tiers, bool weapon) {
    int p = 0;
    for (int i = 0; i < tiers; i++) p += (weapon ? weaponTierAt(i) : armorTierAt(i)).bonus;
    return std::min(GEAR_PCT_CAP, p);
}

// Taunt and Fear miss 1 in 5, minus Fortune. Shared so the player's and the
// true form's versions can't drift apart again (they did once).
inline bool provokeFizzles(int luck) {
    static thread_local std::mt19937 g(std::random_device{}());
    return std::uniform_int_distribution<>(1, 100)(g) <= 20 - luck;
}
