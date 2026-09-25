#include "Game.h"
#include "Audio.h"
#include "Colors.h"
#include "UIHelper.h"
#include "EnemyArt.h"
#include "ProjectileTable.h"
#include "Console.h"
#include "CardBar.h"
#include "Hud.h"
#include "Platform.h"
#include <SDL.h>
#include <algorithm>
#include <iostream>
#include <random>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept> // catch(std::out_of_range) below; reaches us transitively today
                     // on all three toolchains, but Deck.cpp proved that is luck.

Game::Game() : playerDeck(), enemy("Enemy", 50, 8, 4, EnemyType::MELEE), currentRun(), playerHealth(100), maxPlayerHealth(100), playerArmor(0), playerArmorPersistTurns(0), playerEnergy(3), maxEnergy(3), turnNumber(1), playerTurnActive(true), running(false), inEncounter(false), equipDamagePercent(0), equipArmorPercent(0), weaponTier(0), armorTier(0), counterAttackActive(false), parryActive(false), counterBonusValue(0), parryBonusValue(0) {}

// Elemental effects get a matching sound; everything else uses "special".
static const char* effectSoundName(CardEffect effect) {
    switch (effect) {
        case CardEffect::POISON: return "poison";
        case CardEffect::BURN:   return "fire";
        case CardEffect::REND:   return "wind";
        case CardEffect::STUN:   return "volt";
        case CardEffect::HEAL:   return "heal";
        default:                 return "special";
    }
}

// The stripe along a card's top edge says what KIND of thing it is. Items
// and boons get their own colours, so a weapon drop never looks like an
// attack card.
namespace Stripe {
    constexpr int ATTACK  =  9;   // red
    constexpr int DEFEND  = 12;   // blue
    constexpr int SPECIAL = 13;   // purple
    constexpr int ITEM    = 10;   // green  - equipment and consumables
    constexpr int BOON    = 15;   // white  - the one-off run choices
}

// One short line for a card face; the full sentence lives in the details
// panel. elemChance > 0 shows the live elemental chance, which Attunement
// raises above the 10% the card's own text quotes.
static std::string cardFaceLine(const Card& c, int shownValue, int elemChance = 0) {
    std::string base;
    switch (c.getType()) {
        case CardType::ATTACK: base = std::to_string(shownValue) + " dmg";   break;
        case CardType::DEFEND: base = "+" + std::to_string(shownValue) + " armor"; break;
        default:               base = std::to_string(shownValue) + " stk";   break;
    }
    const char* extra = nullptr;
    switch (c.getEffect()) {
        case CardEffect::POISON:     extra = "poison";   break;
        case CardEffect::BURN:       extra = "burn";     break;
        case CardEffect::REND:       extra = "rend";     break;
        case CardEffect::STUN:       extra = "stun";     break;
        case CardEffect::WEAK:       extra = "weaken";   break;
        case CardEffect::COUNTER:    extra = "counter";  break;
        case CardEffect::PARRY:      extra = "riposte";  break;
        case CardEffect::PIERCE:     extra = "pierce";   break;
        case CardEffect::FORTIFY:    extra = "fortify";  break;
        case CardEffect::STRENGTH:   extra = "strength"; break;
        case CardEffect::DOUBLE_HIT: extra = "hits x2";  break;
        case CardEffect::IMPAIR:     extra = "impair";   break;
        case CardEffect::CHIP:       extra = "chip";     break;
        case CardEffect::HEAL:       base  = "to " + std::to_string(shownValue) + "%"; break;
        case CardEffect::WARD:       extra = "ward";     break;
        case CardEffect::TAUNT:      extra = "taunt";    break;
        case CardEffect::FEAR:       extra = "fear";     break;
        // Six characters is what fits beside a three-digit damage figure;
        // "unstoppable" was rendering as "unstop".
        case CardEffect::TRUESTRIKE: extra = "true";    break;
        case CardEffect::TRUE_DOUBLE: extra = "true x2"; break;
        default: break;
    }
    if (extra) base += "  " + std::string(extra);
    if (!extra && elemChance > 0 && c.getType() == CardType::ATTACK) {
        const char* st = c.getElemType() == DamageType::FIRE   ? "burn"
                       : c.getElemType() == DamageType::POISON ? "psn"
                       : c.getElemType() == DamageType::WIND   ? "rend" : nullptr;
        if (st) base += "  " + std::to_string(elemChance) + "% " + st;
    }
    return base;
}

// Card -> widget. Every screen that shows cards wants the same conversion, so
// it lives here rather than being rebuilt per screen.
static CardBar::Card toWidget(const Card& c, int shownValue, bool disabled = false) {
    CardBar::Card w;
    w.name      = c.getName();
    w.effect    = cardFaceLine(c, shownValue);
    w.elemTag   = c.getTypeTag();
    w.typeLabel = c.getTypeString();
    w.cost      = c.getCost();
    w.risk      = c.hasDrawback();
    w.disabled  = disabled;
    w.nameColor = Console::xterm256Public(
                      c.isLegendary() ? 220
                    : c.isSuperRare() ? 218
                    : c.isRare()      ? 153
                    : c.isStarter()   ?   7
                                      : 120);
    w.tint = Console::xterm256Public(
                 (c.getType() == CardType::ATTACK) ? Stripe::ATTACK
               : (c.getType() == CardType::DEFEND) ? Stripe::DEFEND
                                                   : Stripe::SPECIAL);
    return w;
}

// What one upgrade would do to this card. Card::upgrade() adds 3 to the value
// and takes 1 off the cost with a floor of 1, so both are predictable without
// having to actually apply it.
static int upgradedCost(const Card& c)  { return c.getCost() > c.minCost() ? c.getCost() - 1 : c.getCost(); }
static int upgradedValue(const Card& c) { return c.getValue() + 3; }

// Compact "6 -> 9 dmg" for the card face, which clips at about fifteen
// characters. Both numbers arrive already geared: the hand shows geared values,
// so the forge has to as well.
static std::string upgradeFaceLine(const Card& c, int v, int u) {
    if (c.getEffect() == CardEffect::HEAL)
        return "to " + std::to_string(v) + "% -> " + std::to_string(u) + "%";
    const char* unit = (c.getType() == CardType::ATTACK) ? " dmg"
                     : (c.getType() == CardType::DEFEND) ? " armor" : " stk";
    return std::to_string(v) + " -> " + std::to_string(u) + unit;
}

static std::string rarityWord(const Card& c) {
    if (c.isLegendary()) return "LEGENDARY";
    if (c.isSuperRare()) return "SUPER RARE";
    if (c.isRare())      return "RARE";
    if (c.isStarter())   return "STARTER";
    return "";
}

// Card name tint by rarity; legendary gets bold gold instead of a pastel.
static const char* rarityTint(const Card& c) {
    if (c.isLegendary()) return Color::LEGENDARY_TINT;
    if (c.isStarter())   return Color::CARD_NAME;
    if (c.isSuperRare()) return Color::SUPER_RARE_TINT;
    if (c.isRare())      return Color::RARE_TINT;
    return Color::COMMON_TINT;
}

// How often an enemy reaches for its own moves; the rest of the time it runs its
// archetype kit. Shared with displayEnemyInfo() so the odds the player is shown
// are the odds the turn logic actually rolls.
static const int kSignatureChance = 40;

// A boon every this many encounters, for the whole run.
static const int BOON_INTERVAL = 12;
// What one broken seal adds to every enemy, as a percentage of its base. They
// stack: three seals is +45% health and +30% attack.
static const int SEAL_HP_PCT  = 15;
static const int SEAL_ATK_PCT = 10;

// ---- attack types ----------------------------------------------------------
// What a resisted or exposed type does to a blow: gentler than the 1.5x the
// player gets, because the player chooses their cards but not what hits them.
static const int ARMOUR_TYPE_PCT = 25;

static const char* typeWord(DamageType t) {
    switch (t) {
        case DamageType::SMASH:  return "Smash";
        case DamageType::PIERCE: return "Pierce";
        case DamageType::FIRE:   return "Fire";
        case DamageType::POISON: return "Poison";
        case DamageType::WIND:   return "Wind";
        default:                 return "none";
    }
}

// Each armour, by sheet tier: what it turns and what gets through it. No piece
// is strictly better than another, which is what makes the rest site's
// Equipment tab a choice. Only the Aegis has no weakness, as the last tier.
struct ArmourProfile { DamageType resist1, resist2, weak; };
static const ArmourProfile ARMOUR_PROFILE[9] = {
    // Leather fears Fire, not Pierce: Pierce is the commonest blow in the game,
    // and the first Fire (the Wizard, third fight) comes late enough to teach.
    { DamageType::POISON, DamageType::NONE,  DamageType::FIRE   },  // Leather: sealed hide, and it burns
    { DamageType::WIND,   DamageType::NONE,  DamageType::SMASH  },  // Rusted Mail: a gale passes the rings, a hammer crushes them
    { DamageType::PIERCE, DamageType::NONE,  DamageType::FIRE   },  // Iron Plating: arrows glance, iron holds heat
    { DamageType::SMASH,  DamageType::NONE,  DamageType::WIND   },  // Steel Plating: shrugs off blows, too heavy for a gale
    { DamageType::FIRE,   DamageType::NONE,  DamageType::POISON },  // Ivory Plate: bone does not burn, but it drinks
    { DamageType::PIERCE, DamageType::WIND,  DamageType::SMASH  },  // Mythril: light and rigid, dents easily

    // Even the Aegis has a gap. Without one it was the end of armour as a
    // choice: you took it and never opened the Equipment tab again.
    { DamageType::FIRE,   DamageType::SMASH, DamageType::POISON },  // Legendary Aegis: proof against blow and flame, porous to venom

    // The two trophies, a step up from the Aegis with holes of their own.
    { DamageType::PIERCE, DamageType::WIND,  DamageType::FIRE   },  // Shadow Plate: nothing catches it, but it was never made for flame
    { DamageType::POISON, DamageType::FIRE,  DamageType::SMASH  },  // Moon Plate: it does not rot and does not burn, and it shatters
};

static std::string armourProfileText(int tier) {
    const ArmourProfile& p = ARMOUR_PROFILE[std::max(0, std::min(8, tier))];
    std::string t = std::string("resists ") + typeWord(p.resist1);
    if (p.resist2 != DamageType::NONE) t += std::string(", ") + typeWord(p.resist2);
    t += p.weak != DamageType::NONE ? std::string("; weak to ") + typeWord(p.weak) : std::string("; no weakness");
    return t;
}

// ---- weapons ---------------------------------------------------------------
// Each weapon's passive, by sheet tier. The gear percentage is the sum of every
// tier claimed whatever is worn, so this is what changing weapon changes.
static const int LEGEND_BLADE_FLAT = 6;
static const char* WEAPON_PASSIVE[9] = {
    "no passive",
    "15% chance to poison",
    "first attack +3",
    "pierce ignores 2 defense",
    "heals 10% of damage",
    "first attack costs 1 less",
    "+6 damage on every attack",
    "ignores enemy defense",
    "first attack hits twice",
};
static const char* WEAPON_PASSIVE_LONG[9] = {
    "A practice sword. No passive.",
    "Rust gets into the wound: every hit has a 15% chance to apply Poison 2.",
    "The first attack you play each turn deals 3 more damage.",
    "Finds the gaps: your Pierce attacks ignore 2 more of the enemy's defense.",
    "Drinks what it cuts: you heal 10% of the damage you deal, at least 1.",
    "Light in the hand: the first attack you play each turn costs 1 less energy.",
    "Every attack card deals 6 more damage, added before your gear bonus.",
    "Taken off the thing that was wearing your face: your attacks slip past the "
    "enemy's defense entirely.",
    "It answers to the moon: the first attack you play each turn lands twice, the "
    "second time from its reflection, just as hard.",
};

// ---- relics ----------------------------------------------------------------
// One of three every 12 encounters from the 6th, so they fall between the
// boons. The id is the bit in relicsOwned and in the save, so append only.
namespace Relic {
enum Id { VENOM_VIAL, EMBER_HEART, GALE_FEATHER, WEIGHTED_POMMEL, HOURGLASS, BONE_DICE,
          FORGE_HAMMER, WARDEN_LANTERN, RED_THREAD, SCHOLAR_LENS, SEAL_FRAGMENT,
          MOON_LOCKET, GLASS_MOON, COUNT };
struct Info { const char* name; const char* face; const char* text; bool cursed; };
const Info INFO[COUNT] = {
    { "Venom Vial",            "poison lasts +2",    "Poison you apply lasts 2 turns longer.", false },
    { "Ember Heart",           "burn +2 a tick",     "Burn you apply deals 2 more damage every tick.", false },
    { "Gale Feather",          "rend +2 swings",     "Rend you apply lasts for 2 more of the enemy's attacks.", false },
    { "Weighted Pommel",       "smash can weaken",   "Your Smash attacks have a 20% chance to Weaken the enemy for 2 turns.", false },
    { "Hourglass",             "+1 energy turn 1",   "You have 1 extra energy on the first turn of every fight.", false },
    { "Bone Dice",             "reroll rewards",     "Once on each card reward after a fight, you can reroll the cards on offer.", false },
    { "Forge Hammer",          "forging heals",      "Forging a card at a rest site also heals 15% of your max HP.", false },
    { "Warden's Lantern",      "+8 armor to start",  "You start every fight with 8 armor.", false },
    { "Red Thread",            "survive once",       "Once this run, a blow that would kill you leaves you at 1 HP instead.", false },
    { "Scholar's Lens",        "read its next move", "Regular enemies show their next move beside their health: their own move, or which of their basic moves.", false },
    { "Vigil Ember",           "+5% dmg per vigil",  "Your attacks deal 5% more for every vigil you have put out.", false },
    { "Moonlit Locket",        "??? twice as often", "Whatever it is that follows you turns up twice as often.", false },
    { "Glass Moon",            "+25% dealt & taken", "Your attacks deal 25% more, and every blow against you lands 25% harder.", true },
};
}
static const int RELIC_FIRST = 6, RELIC_INTERVAL = 12;
// Relic cards: a yellow stripe and a purple name, so they never read as gear
// (green) or a card. The cursed one keeps a red name. Icons follow the pouch
// in items.png, in Relic::Id order.
static const SDL_Color RELIC_STRIPE{ 240, 200, 70, 255 };
static const SDL_Color RELIC_NAME{ 190, 130, 255, 255 };
static const int POUCH_ICON = 14, RELIC_ICON0 = 15;

// Its own move in each shape, beside the Scent and the Maul it always has.
struct MoonMove { const char* name; const char* info; };
static const MoonMove MOON_MOVES[5] = {
    { "Grave Silk",           "Weaken 2 and Poison 3 at once: bone-white thread that rots what it holds." },
    { "Lunar Mirage",         "you draw 2 fewer cards next turn, and it gains armor while you cannot see it." },
    { "Howl at the Red Moon", "heals a tenth of its health and gains +2 attack, up to +6." },
    { "Petrifying Gaze",      "stone creeps up your legs: kill it within 6 turns or it is over. Once per fight." },
    { "Eclipse Ward",         "raises a heavy guard and heals 8." },
};

static int signatureChanceFor(const std::string& name) {
    auto has = [&](const char* k) { return name.find(k) != std::string::npos; };
    // The secret fight should play like ITS fight, not like any other beast:
    // its own moves outnumber every kit move put together.
    if (has("Moonstruck")) return 60;
    // Two full hits in one turn; at 40% these were the moves that felt constant.
    if (has("Enforcer") || has("Manticore")) return 30;
    // A summon that then stays on the field and attacks every turn.
    if (has("Lich")) return 25;
    return kSignatureChance;
}

// Higher = rarer; sorts card lists highest-rarity-first (Forge, View Deck).
static int rarityRank(const Card& c) {
    if (c.isLegendary()) return 4;
    if (c.isSuperRare()) return 3;
    if (c.isRare())      return 2;
    if (c.isStarter())   return 0;
    return 1; // uncommon reward-tier
}

// A boss falling is the beat the run has been building to, so it gets its
// own cue, and so do its blows.
static const char* deathSfx(bool isBoss) { return isBoss ? "boss_death" : "dead"; }

static const char* effectToStr(CardEffect e) {
    switch (e) {
        case CardEffect::POISON:     return "POISON";
        case CardEffect::BURN:       return "BURN";
        case CardEffect::REND:       return "REND";
        case CardEffect::STUN:       return "STUN";
        case CardEffect::WEAK:       return "WEAK";
        case CardEffect::COUNTER:    return "COUNTER";
        case CardEffect::PARRY:      return "PARRY";
        case CardEffect::PIERCE:     return "PIERCE";
        case CardEffect::FORTIFY:    return "FORTIFY";
        case CardEffect::STRENGTH:   return "STRENGTH";
        case CardEffect::DOUBLE_HIT: return "DOUBLE_HIT";
        case CardEffect::IMPAIR:     return "IMPAIR";
        case CardEffect::CHIP:       return "CHIP";
        case CardEffect::HEAL:       return "HEAL";
        case CardEffect::WARD:       return "WARD";
        case CardEffect::TAUNT:      return "TAUNT";
        case CardEffect::FEAR:       return "FEAR";
        case CardEffect::TRUESTRIKE: return "TRUESTRIKE";
        case CardEffect::TRUE_DOUBLE: return "TRUE_DOUBLE";
        case CardEffect::SCRAP:          return "SCRAP";
        case CardEffect::RECKLESS:       return "RECKLESS";
        case CardEffect::SELFWEAK:       return "SELFWEAK";
        case CardEffect::OVEREXTEND:     return "OVEREXTEND";
        case CardEffect::BLOODPRICE:     return "BLOODPRICE";
        case CardEffect::WILDCHARGE:     return "WILDCHARGE";
        case CardEffect::BERSERK:        return "BERSERK";
        case CardEffect::TURTLE:         return "TURTLE";
        case CardEffect::EMBERBLADE:     return "EMBERBLADE";
        case CardEffect::ADRENALINE:     return "ADRENALINE";
        case CardEffect::SHATTERPOINT:   return "SHATTERPOINT";
        case CardEffect::BLOODPACT:      return "BLOODPACT";
        case CardEffect::UNSTABLEWARD:   return "UNSTABLEWARD";
        case CardEffect::ALLIN:          return "ALLIN";
        case CardEffect::LASTSTAND:      return "LASTSTAND";
        case CardEffect::BORROWED:       return "BORROWED";
        case CardEffect::PACTRUIN:       return "PACTRUIN";
        case CardEffect::SACRIFICE:      return "SACRIFICE";
        default:                     return "NONE";
    }
}
static CardEffect strToEffect(const std::string& s) { return Card::effectFromString(s); }
static const char* dmgToStr(DamageType t) {
    switch (t) {
        case DamageType::SMASH:  return "SMASH";
        case DamageType::PIERCE: return "PIERCE";
        case DamageType::FIRE:   return "FIRE";
        case DamageType::POISON: return "POISON";
        case DamageType::WIND:   return "WIND";
        default:                 return "NONE";
    }
}
static DamageType strToDmg(const std::string& s) { return Card::damageTypeFromString(s); }
static const char* cardTypeToStr(CardType t) {
    switch (t) {
        case CardType::ATTACK: return "ATTACK";
        case CardType::DEFEND: return "DEFEND";
        default:                return "SPECIAL";
    }
}
static CardType strToCardType(const std::string& s) {
    if (s == "ATTACK") return CardType::ATTACK;
    if (s == "DEFEND") return CardType::DEFEND;
    return CardType::SPECIAL;
}

struct EquipTier { std::string name; int bonus; };

// Gear name colour, on the same rarity ladder the cards use: a player who
// reads card colours already knows what a gear name is worth.
static int equipTintFor(int tier) {
    switch (tier) {
        case 0:  return 7;     // wooden sword, leather: common white
        case 1:
        case 2:  return 120;   // rusty, iron: uncommon green
        case 3:  return 153;   // steel: rare blue
        case 4:
        case 5:  return 218;   // ivory, mythril: super rare
        case 6:
        case 7:  return 220;   // legendary, and the Shadow Knight's: gold
        default: return 203;   // the moon's: red, like the slit it watches through
    }
}

// items.png is indexed by position and the trophy icons had to go on the end
// of it, or every relic index would have shifted by four.
static int weaponIconFor(int tier) { return tier <= 6 ? tier : 28 + (tier - 7); }
static int armorIconFor(int tier)  { return tier <= 6 ? 7 + tier : 30 + (tier - 7); }

// Gear is a percentage of a card's own value, so it scales with the deck you
// built instead of paying out once per card played. No ceiling: a percentage
// cannot run away the way a flat bonus did.
static const int GEAR_PCT_CAP = 100000;

// Gear name and bonus escalate per tier claimed; the last tier repeats.
static EquipTier weaponTierAt(int tier) {
    static const std::vector<EquipTier> tiers = {
        // The first tier has to be large enough to move a starter card. At +8% a
        // 5-damage card rounded straight back to 5 and the drop felt like nothing.
        {"Rusty Blade", 15}, {"Iron Sword", 16}, {"Steel Blade", 17},
        {"Ebon Blade", 18}, {"Mythril Edge", 20}, {"Legendary Blade", 22},
        // The trophies keep climbing the ladder past the Legendary.
        {"Shadow Blade", 23}, {"Moon Blade", 24}
    };
    int idx = std::min(tier, (int)tiers.size() - 1);
    return tiers[idx];
}
static EquipTier armorTierAt(int tier) {
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
static int gearPercentFor(int tiers, bool weapon) {
    int p = 0;
    for (int i = 0; i < tiers; i++) p += (weapon ? weaponTierAt(i) : armorTierAt(i)).bonus;
    return std::min(GEAR_PCT_CAP, p);
}

void Game::init() {
    // One of each starter card; duplicates only ever come from later card rewards.
    playerDeck.addCard(Card("Quick Jab", "Deal 3 damage.", CardType::ATTACK, 0, 3));
    playerDeck.addCard(Card("Slash", "Deal 4 damage.", CardType::ATTACK, 1, 4));
    playerDeck.addCard(Card("Bash", "Deal 6 damage. Counts as a Smash attack, so it hits harder against enemies weak to Smash and lands softer against those that resist it.", CardType::ATTACK, 2, 6, CardEffect::NONE, false, DamageType::SMASH));
    playerDeck.addCard(Card("Lunge", "Deal 6 damage. Counts as a Pierce attack, so it hits harder against enemies weak to Pierce and lands softer against those that resist it.", CardType::ATTACK, 2, 6, CardEffect::NONE, false, DamageType::PIERCE));
    // Scrap Shield in place of Defend: free, but it nicks you, so the opening
    // deck has a block that competes with an attack for the turn rather than
    // for the energy.
    playerDeck.addCard(Card("Scrap Shield", "Gain 5 armor. You take 1 damage.",
                            CardType::DEFEND, 0, 5, CardEffect::SCRAP));
    playerDeck.addCard(Card("Brace", "Gain 5 armor.", CardType::DEFEND, 1, 5));
    playerDeck.addCard(Card("Parry",
        "Block the enemy's next attack and riposte for 1.5x their attack "
        "plus 3, ignoring their defense, with a chance to stun them. "
        "How big a blow you can catch is your armor plus 9: too heavy a hit "
        "breaks the guard. Ranged enemies are blocked but stand too far away "
        "to riposte.",
        // Cost 2, not 3: the cost it is balanced at, since starters are not
        // upgradable.
        CardType::SPECIAL, 2, 3, CardEffect::PARRY));

    applyUpgrades();
    
    playerDeck.shuffle();
    
    running = true;
}

// One-line flavor text per regular enemy, matched the same way as the move
// hints below (substring on the base name, so the "Greater " prefix on Hard
// still matches). Grouped by zone/theme, not by type.
static std::string enemyFlavorText(const std::string& enemyName) {
    auto has = [&](const char* k){ return enemyName.find(k) != std::string::npos; };
    // Bosses first: "Shadow Knight" also contains "Knight", so these are
    // tested before the roster, and the true form before the Shadow Knight.
    if (has("Moonstruck Shadow Knight")) return "It has stopped trying to look like him. It is only fighting off the sleep now. Every stance he ever dropped, it kept.";
    if      (has("Stone Colossus")) return "The dungeon's foundation stood up one day. Everything since has been rubble it walked through.";
    else if (has("Vile Witch"))     return "She bought every guard in these halls, and still does her own poisoning.";
    else if (has("Thunder Beast"))  return "The storm over this forest isn't weather. It has been following him for years.";
    else if (has("Hydra"))          return "Take a head and the lake hands it another. The lake has always been generous with it.";
    else if (has("Undead Dragon"))  return "Died on this peak an age ago and has never once accepted the terms.";
    else if (has("Shadow Knight"))  return "It has watched you the whole way up, and knows your deck better than you do.";
    // The Dungeon
    else if (has("Goblin"))    return "A scrawny dungeon-scavenger, quick with a blade and quicker to flee.";
    else if (has("Orc"))       return "A dull-eyed brute muscled into the front ranks by sheer size.";
    else if (has("Wizard"))    return "A washed-up spellcaster who never left the ruins he once studied in.";
    else if (has("Skeleton"))  return "Old bones stirred back to motion by whatever still lingers down here.";
    else if (has("Spider"))    return "Fat and pale from years in the dark, spinning webs across forgotten halls.";
    else if (has("Archer"))    return "A hooded scavenger who'd rather put an arrow in you from range.";
    else if (has("Bandit"))    return "A dungeon-squatter who's learned every blind corner to strike from.";
    else if (has("Warden"))    return "Once kept these halls locked; now it keeps anything that still moves inside.";
    else if (has("Sage"))      return "An old hermit who mistook the ruins for a place to be left alone.";
    // The Dark Dungeon
    else if (has("Ghoul"))     return "Something that used to be a person, now driven by hunger alone.";
    else if (has("Basilisk"))  return "A pale-eyed reptile whose stare has turned half this dungeon to statues.";
    else if (has("Assassin"))  return "A shape that waits in the dark until you've already committed to a move.";
    else if (has("Knight"))    return "A knight who took the witch's coin and never looked back.";
    else if (has("Sentinel"))  return "Corrupted long ago, it still thinks it's guarding something worth guarding.";
    else if (has("Enchanter")) return "A charm-caster who'd rather empty your hand than fight for it.";
    else if (has("Wraith"))    return "A grudge with no body left to carry it, drifting the witch's halls.";
    else if (has("Serpent"))   return "Something long and patient, coiling where the torchlight gives out.";
    else if (has("Omneye"))    return "A single vast eye the witch keeps chained to watch her domain.";
    // The Wicked Forest
    else if (has("Raider"))    return "Ambushes from the treeline, gone before the storm-thunder fades.";
    else if (has("Barbarian")) return "A tribal warrior who calls this storm-choked wood home.";
    else if (has("Mystic"))    return "A veiled seer who fades from sight whenever the lightning does.";
    else if (has("Banshee"))   return "Her scream carries further than the thunder, and cuts twice as deep.";
    else if (has("Wolf"))      return "Runs in packs no one's ever seen more than one member of at a time.";
    else if (has("Falcon"))    return "A hunter and her trained falcon, moving as a single predator.";
    else if (has("Berserker")) return "Feral even by the forest's standards, and getting worse the longer this drags on.";
    else if (has("Guardian"))  return "An eagle-headed knight who's nested in this canopy longer than anyone can say.";
    else if (has("Vampire"))   return "Elegant, patient, and never quite as far away as she seems.";
    // The Dark Lake
    else if (has("Specter"))   return "A shape the fog keeps almost showing you, and never quite does.";
    else if (has("Cockatrice"))return "Half bird, half serpent, all of it eager to turn you to stone.";
    else if (has("Deadeye"))   return "A marksman who's had nothing to do out here but perfect one shot.";
    else if (has("Warrior"))   return "Shipwrecked here years ago and never found a way back to shore.";
    else if (has("Bastion"))   return "A drowned wall of a man, still holding a line no one else remembers.";
    else if (has("Spellmaster"))return "Brews plague in the lake's stagnant shallows, and drinks it like water.";
    else if (has("Moonstruck"))return "The dark moon in a borrowed shape, worn a little wrong. It has followed you since the first gate, learning how you move.";
    else if (has("Revenant"))  return "Rose from the lakebed still furious about how it got there.";
    else if (has("Fleshmass")) return "A heap of wrong-colored flesh the lake spat up and never wanted back.";
    else if (has("Wyvern"))    return "Nests in the reeds at the lake's edge, half-drowned and twice as vicious for it.";
    // The Mountain
    else if (has("Gladiator")) return "Fought his way up from the lowlands and never stopped climbing.";
    else if (has("Paladin"))   return "A holy knight whose halo hasn't dimmed even this far from anything holy.";
    else if (has("Sorcerer"))  return "Never seemed to notice the mountain froze around him. Maybe he did that.";
    else if (has("Lich"))      return "The oldest thing on this mountain, and the only one still keeping score.";
    else if (has("Manticore")) return "A lion's hunger with a scorpion's patience, prowling the high crags.";
    else if (has("Enforcer"))  return "Keeps discipline on this mountain the way a hammer keeps discipline on stone.";
    else if (has("Fortress"))  return "Less a soldier than a wall that decided to start moving.";
    else if (has("Archon"))    return "Something that used to be holy, fallen far enough to land on this peak.";
    return "";
}

// Scrollable replay of what has happened this fight. Console::history() stores
// the raw bytes, escapes included, so replaying a line reproduces the colour it
// was printed in - the log reads exactly as it did when it scrolled past.
void Game::displayActionLog() const {
    const auto& lines = Console::history();
    if (lines.empty()) return;

    // Capture has to be off in here or the viewer would log itself, and every
    // redraw would append another copy of the log to the log.
    Console::setHistoryCapture(false);

    int top = std::max(0, (int)lines.size() - 1);   // start at the most recent
    while (true) {
        UIHelper::clearScreen();
        const int page = std::max(4, Console::rows() - 6);
        top = std::min(top, std::max(0, (int)lines.size() - page));
        top = std::max(0, top);

        std::cout << Color::BOLD << "ACTION HISTORY" << Color::RESET
                  << Color::DIM << "   this run"
                  << "   (" << lines.size() << " lines)" << Color::RESET << "\n";
        std::cout << Color::DIM
                  << "-------------------------------------------------------------"
                  << Color::RESET << "\n";
        for (int i = top; i < (int)lines.size() && i < top + page; i++)
            std::cout << lines[i] << "\n";
        std::cout << Color::DIM << "\n"
                  << "[up/down to scroll   any other key to return]"
                  << Color::RESET << "\n";

        // Wheel or arrows: a blocking key read would never see the wheel, so
        // this polls both and draws frames in between.
        bool leave = false;
        while (true) {
            int wheel = Platform::takeWheel();
            if (wheel != 0) { top -= wheel * 3; break; }
            Platform::KeyEvent k = Platform::pollKey();
            if (k.key == Platform::Key::UP)        { top -= 3; break; }
            else if (k.key == Platform::Key::DOWN) { top += 3; break; }
            else if (k.key != Platform::Key::NONE) { leave = true; break; }
            int cx, cy;
            if (Platform::takeClick(cx, cy)) { leave = true; break; }
            Platform::frame();
        }
        if (leave) break;
    }
    Console::setHistoryCapture(true);
}

// Everything the run has given you, on one screen: the numbers the HUD only
// hints at, the gear and what it does, every boon, every relic, the seals, and
// any price a card is still charging.
void Game::displayPlayerInfo() const {
    // The knight himself, in the gear he is wearing, the way View Enemy opens
    // with the thing you are fighting.
    EnemyArt::printPlayerPortrait();
    auto head = [](const char* t) {
        std::cout << "\n  " << Color::BOLD << Color::YELLOW << t << Color::RESET << "\n";
    };
    auto row = [](const std::string& k, const std::string& v) {
        std::cout << "    " << Color::DIM << k << Color::RESET << "  " << v << "\n";
    };
    std::cout << "\n" << Color::BOLD << Color::CYAN << "THE KNIGHT" << Color::RESET << "\n";
    std::cout << "  HP:  " << hpColor(playerHealth, maxPlayerHealth) << playerHealth << "/" << maxPlayerHealth
              << Color::RESET;
    if (maxHpDebt > 0)
        std::cout << Color::DAMAGE << "   (" << maxHpDebt << " max HP owed until this fight ends)" << Color::RESET;
    std::cout << "\n  Energy: " << playerEnergy << "/" << maxEnergy
              << "     Cards a turn: " << (BASE_HAND_SIZE + handSizeBonus + upgrades.getDrawBonus())
              << "     Armor: " << playerArmor;
    if (playerArmorPersistTurns > 0) std::cout << " (holds " << playerArmorPersistTurns << " more turns)";
    std::cout << "\n";
    const std::string st = playerStatus.summary();
    if (!st.empty()) std::cout << "  Status:" << st << "\n";

    head("GEAR");
    const std::string wName = wornWeapon == 0 ? std::string("Wooden Sword") : weaponTierAt(wornWeapon - 1).name;
    const std::string aName = wornArmor == 0 ? std::string("Leather Armor") : armorTierAt(wornArmor - 1).name;
    row("Weapon", wName + ": " + WEAPON_PASSIVE_LONG[std::max(0, std::min(8, wornWeapon))]);
    row("      ", "every attack card +" + std::to_string(weaponPct()) + "%"
                   + (roadGearPct ? " (" + std::to_string(roadGearPct) + "% of it the road's own kit)" : std::string())
                   + (upgrades.getDamageBonus() + roadBonus
                      ? ", +" + std::to_string(upgrades.getDamageBonus() + roadBonus) + " flat" : std::string()));
    row("Armor ", aName + ": " + armourProfileText(wornArmor) + " (25% either way)");
    row("      ", "every defend card +" + std::to_string(armorPct()) + "%"
                   + (roadGearPct ? " (" + std::to_string(roadGearPct) + "% of it the road's own kit)" : std::string())
                   + (upgrades.getArmorBonus() + roadBonus
                      ? ", +" + std::to_string(upgrades.getArmorBonus() + roadBonus) + " flat" : std::string()));

    head("BOONS");
    bool any = false;
    if (runLuck > 0)           { any = true; row("Fortune   ", "x" + std::to_string(runLuck) + ": +" + std::to_string(luckBonus()) + "% on every roll"); }
    if (handSizeBonus > 0)     { any = true; row("Endurance ", "+" + std::to_string(handSizeBonus) + " card(s) every turn"); }
    if (rewardChoiceBonus > 0) { any = true; row("Foresight ", "+" + std::to_string(rewardChoiceBonus) + " card(s) on every reward"); }
    if (attunementBoons > 0)   { any = true; row("Attunement", "elemental attacks land their status " + std::to_string(attunementChance()) + "% of the time"); }
    if (gearInterval < 3)      { any = true; row("Scavenger ", "gear every " + std::to_string(gearInterval) + " encounters"); }
    if (!any) std::cout << "    " << Color::DIM << "none yet: one every " << BOON_INTERVAL << " encounters" << Color::RESET << "\n";

    head("RELICS");
    any = false;
    for (int i = 0; i < Relic::COUNT; i++) {
        if (!hasRelic(i)) continue;
        any = true;
        std::string text = Relic::INFO[i].text;
        if (i == Relic::RED_THREAD && redThreadUsed) text += " (spent)";
        std::cout << "    " << (Relic::INFO[i].cursed ? Color::RED : Color::MAGENTA) << Relic::INFO[i].name
                  << Color::RESET << "  " << text << "\n";
    }
    if (!any) std::cout << "    " << Color::DIM << "none yet: the first on encounter " << RELIC_FIRST << Color::RESET << "\n";

    head("THE ROAD");
    row("Road      ", std::string(modeName()) + (currentRun.getDifficulty() > 0
        ? ": enemies scale as if " + std::to_string(currentRun.getDifficulty() * 50) + " fights had come before"
        : std::string()));
    row("Encounter ", std::to_string(currentRun.getCurrentEncounter()) + " of 50, " + std::to_string(currentRun.getEncountersWon()) + " won");
    row("Vigils    ", sealsBroken == 0 ? std::string("all still burning")
        : std::to_string(sealsBroken) + " out: everything ahead +" + std::to_string(sealsBroken * SEAL_HP_PCT)
          + "% health, +" + std::to_string(sealsBroken * SEAL_ATK_PCT) + "% attack");
    // What he has got back, and what is still out there. The five are in the
    // order the road hands them over.
    {
        static const char* PIECES[5] = { "strength", "wits", "speed", "the way he moves", "soul" };
        const int back = std::max(0, std::min(5, currentRun.areaBossesCleared()));
        std::string had, lost;
        for (int i = 0; i < 5; ++i) {
            std::string& into = (i < back) ? had : lost;
            if (!into.empty()) into += ", ";
            into += PIECES[i];
        }
        row("Yourself  ", back == 5 ? std::string("all of it back") + Color::RESET
                                    : had.empty() ? std::string("still in pieces: ") + lost
                                                  : "back: " + had + Color::DIM + "   still out there: " + lost);
    }
    // Named the way the encounter is named: it is still a secret.
    row("???       ", std::to_string(moonstruckMet) + " met this run");

    std::string now;
    auto add = [&](const std::string& t) { now += "    " + t + "\n"; };
    if (vulnerableTurns > 0)   add("Exposed: you take x" + std::to_string((int)(vulnerableMult * 100)) + "% damage until your next turn.");
    if (cardDamagePenalty > 0) add("Your cards deal " + std::to_string(cardDamagePenalty) + " less this turn.");
    if (pendingDamagePenalty > 0) add("Your cards will deal " + std::to_string(pendingDamagePenalty) + " less next turn.");
    if (cardSoftenPct > 0)     add("Your attacks deal " + std::to_string(cardSoftenPct) + "% less this turn.");
    if (energyDebt > 0)        add("You start next turn " + std::to_string(energyDebt) + " energy short.");
    if (cardLimitThisTurn > 0) add("Only " + std::to_string(std::max(0, cardLimitThisTurn - cardsPlayedThisTurn)) + " more card(s) this turn.");
    if (pactOfRuinActive)      add("The Pact of Ruin: every card costs 6 HP, every attack festers.");
    if (noHealThisEncounter)   add("You cannot heal until this fight ends.");
    if (extraTurnsPending > 0) add(std::to_string(extraTurnsPending) + " borrowed turn(s) to come, and a stun after each.");
    if (statusWardTurns > 0)   add("Status Guard: ailments blocked for " + std::to_string(statusWardTurns) + " more turn(s).");
    if (!now.empty()) { head("RIGHT NOW"); std::cout << now; }
}

void Game::displayEnemyInfo() const {
    // Includes any self-buff, so the numbers on this screen are the numbers
    // the enemy will actually hit for right now.
    int atk = enemy.getBaseAttack() + enemy.getBonusAttack();
    int def = enemy.getBaseDefense(); // used below for move-estimate formulas, not the live display

    EnemyArt::print(EnemyArt::getWalkFrame(enemy.getType(), enemy.getBossType()));

    std::cout << "\n" << Color::BOLD << Color::RED << enemy.getName() << Color::RESET << "\n";
    std::string flavor = enemyFlavorText(enemy.getName());
    if (!flavor.empty())
        std::cout << "  " << Color::DIM << flavor << Color::RESET << "\n";
    std::cout << "  HP:  " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
              << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << "\n";
    // ARM and DEF are both flat damage reduction, so they're shown as one combined
    // DEF figure - a "defend" action just temporarily raises this number.
    std::cout << "  ATK: " << Color::RED << atk << Color::RESET
               << "  | DEF: " << Color::BLUE << (def + enemy.getArmor()) << Color::RESET
               << " (flat damage reduction on every hit you land)\n";
    if (enemy.getWeakness() != DamageType::NONE)
        std::cout << "  " << Color::YELLOW << "Weakness: " << enemy.getWeaknessLabel()
                   << " (+50% dmg from matching attacks)" << Color::RESET << "\n";
    if (enemy.getResistance() != DamageType::NONE)
        std::cout << "  " << Color::DIM << "Resistant: " << enemy.getResistanceLabel()
                   << " (-50% dmg from matching attacks)" << Color::RESET << "\n";
    enemy.displayStatusEffects("  ");

    std::cout << "\n" << Color::DIM << "Possible moves (chance per turn):" << Color::RESET << "\n";

    if (enemy.isBoss()) {
        switch (enemy.getBossType()) {
            // Percentages are the roll buckets in bossAction(); keep them in step.
            // Each line carries what the move does, not just how often.
            case BossType::STONE_COLOSSUS:
                std::cout << "  " << Color::RED << "Crush" << Color::RESET << " (55%, " << (atk + 4) << " dmg) - a heavy melee strike\n";
                std::cout << "  " << Color::ARMOR_CLR << "Fortify" << Color::RESET << " (30%, Armor +8) - digs in\n";
                std::cout << "  " << Color::RED << "Earthquake Slam" << Color::RESET << " (15%, 15 dmg) - ignores your armor entirely\n";
                break;
            case BossType::VILE_WITCH:
                std::cout << "  " << Color::CARD_SPECIAL << "Plague" << Color::RESET << " (40%, Poison 4 + Burn 2) - both at once\n";
                std::cout << "  " << Color::RED << "Strike" << Color::RESET << " (30%, " << atk << " dmg) - a direct attack\n";
                std::cout << "  " << Color::HEAL << "Life Siphon" << Color::RESET << " (15%, heals 20) - drains your health into herself\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Toxic Eruption" << Color::RESET << " (15%, Poison 6) - the ground itself festers\n";
                break;
            case BossType::WARLORD:
                std::cout << "  " << Color::RED << "Heavy Strike" << Color::RESET << " (73%, " << atk << " dmg) - and it grows +1 attack, up to +10\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Battlecry" << Color::RESET << " (15%, Weaken 3) - no attack that turn\n";
                std::cout << "  " << Color::STUN_CLR << "Thunderstrike" << Color::RESET << " (12%, stun) - you lose your next turn\n";
                break;
            case BossType::HYDRA:
                std::cout << "  " << Color::CARD_SPECIAL << "Venomous Bite" << Color::RESET << " (30%, Poison 5) - venom in the wound\n";
                std::cout << "  " << Color::RED << "Many Heads" << Color::RESET << " (25%, " << atk
                          << " dmg x" << hydraHeads << ") - one bite per head it still has\n";
                std::cout << "  " << Color::RED << "Bite" << Color::RESET << " (25%, " << atk << " dmg) - a direct attack\n";
                std::cout << "  " << Color::HEAL << "Regrowth" << Color::RESET << " (20%, heals 18) - two grow back: +1 head, up to "
                          << HYDRA_HEADS_MAX << "\n";
                break;
            case BossType::DRAGON:
                std::cout << "  " << Color::RED << "Cursed Bite" << Color::RESET << " (30%, " << atk
                          << " dmg + Rend 3) - the wound opens again each time it strikes\n";
                std::cout << "  " << Color::RED << "Claw Rake" << Color::RESET << " (25%, " << (atk + 5) << " dmg) - ignores your armor\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Wing Buffet" << Color::RESET << " (25%, Weaken 3) - knocks you off balance\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Fire Breath" << Color::RESET << " (20%, Burn 8) - a wall of flame\n";
                break;
            case BossType::SHADOW_KNIGHT:
                std::cout << "  " << Color::CARD_SPECIAL << "Dark Mirror" << Color::RESET << " (100%) - plays a shadow copy of a random card from YOUR deck\n";
                if (enemy.getName().find("Moonstruck") != std::string::npos) {
                    std::cout << "  " << Color::MAGENTA << "  True form: every card comes back whole. Stances are real,"
                              << " drawbacks are paid, extra moves are taken." << Color::RESET << "\n";
                    std::cout << "  " << Color::DIM << "  What your cards cost you, they cost it: health, armour, moves"
                              << " next round, its ability to heal." << Color::RESET << "\n";
                }
                std::cout << "  " << Color::DIM << "  Your attacks become its strikes, your armor its guard, your potions its mending." << Color::RESET << "\n";
                std::cout << "  " << Color::DIM << "  The bigger your deck's numbers, the harder it hits back." << Color::RESET << "\n";
                break;
            default: break;
        }
    } else {
        auto nameHas = [&](const char* k){ return enemy.getName().find(k) != std::string::npos; };
        // Lines are collected first so each group is printed under a heading that
        // states its share of turns. Every tag is a chance PER TURN: an enemy's own
        // moves share S% of its turns, the archetype kit the rest.
        {
            const DamageType bt = enemyAttackType();
            const int m = armourTypeMod(bt);
            std::cout << "  " << Color::DIM << "Its blows are " << Color::RESET << Color::BOLD
                      << typeWord(bt) << Color::RESET;
            if (m < 0) std::cout << Color::GREEN << "   your armor resists them (-25%)" << Color::RESET;
            if (m > 0) std::cout << Color::RED << "   your armor is weak to them (+25%)" << Color::RESET;
            std::cout << "\n";
            const std::string intent = lensIntent();
            if (!intent.empty())
                std::cout << "  " << Color::CYAN << "Scholar's Lens: next turn, " << intent
                          << " (unless you taunt or frighten it)" << Color::RESET << "\n";
            std::cout << "\n";
        }
        std::string buf;
        auto line = [&](const char* clr, const char* mv, const std::string& tag, const std::string& desc){
            buf += std::string("  ") + clr + mv + Color::RESET;
            if (!tag.empty()) buf += std::string(Color::DIM) + " (" + tag + ")" + Color::RESET;
            buf += " - " + desc + "\n";
        };
        auto flush = [&](const std::string& heading){
            if (buf.empty()) return;
            std::cout << "  " << Color::DIM << heading << Color::RESET << "\n" << buf;
            buf.clear();
        };
        auto pct   = [](int p){ return std::to_string(p) + "%"; };
        auto dmg   = [](int d){ return std::to_string(d) + " dmg"; };
        auto tag   = [](const std::string& p, const std::string& t){ return p + ", " + t; };
        auto armor = [](int a){ return "Armor +" + std::to_string(a); };
        const int S   = signatureChanceFor(enemy.getName());
        const int kit = 100 - S;
        const std::string all = pct(S);
        auto sig    = [&](int share) { return pct(S * share / 100); };
        auto kitPct = [&](int share) { return pct(share * kit / 100); };

        bool named = true;
        // MELEE
        if      (nameHas("Goblin"))    line(Color::RED, "Jab", tag(all, dmg(atk)), "a quick strike.");
        else if (nameHas("Bandit"))    line(Color::RED, "Dagger Throw", tag(all, dmg(atk)), "a hurled blade. Parry blocks it but cannot riposte.");
        else if (nameHas("Raider"))    line(Color::RED, "Bash", tag(all, dmg(atk)), "a heavy smash.");
        else if (nameHas("Warrior"))   line(Color::RED, "Pierce", tag(all, dmg(atk)), "a lunge that bypasses half your armor.");
        else if (nameHas("Knight"))    line(Color::ARMOR_CLR, "Shield Bash", tag(all, dmg(std::max(1, atk / 2))), "raises " + armor(def) + ", then chips you.");
        else if (nameHas("Berserker")) {
            line(Color::RED, "Roar", sig(45), "+2 attack, up to +6. Once maxed it swings instead.");
            line(Color::RED, "Frenzy", tag(sig(55), dmg(atk)), "a wild swing.");
        }
        else if (nameHas("Gladiator")) line(Color::RED, "Uppercut", tag(all, dmg(atk + 3)), "a brutal blow that bypasses half your armor.");
        else if (nameHas("Enforcer"))  line(Color::RED, "Combo Strike", tag(all, dmg(atk) + " x2"), "hits you twice.");
        // TANK
        else if (nameHas("Guardian"))  line(Color::RED, "Whirlwind", tag(all, dmg(atk)), "a sweep that bypasses half your armor.");
        else if (nameHas("Barbarian")) line(Color::ARMOR_CLR, "Iron Skin", tag(all, armor(def + 6)), "hardens up; half the time it also weakens you 2.");
        else if (nameHas("Sentinel"))  line(Color::ARMOR_CLR, "Fortify", tag(all, armor(def + 4)), "stacks armor.");
        else if (nameHas("Warden"))    line(Color::RED, "Smackdown", tag(all, dmg(atk + 1)), "a solid hit.");
        else if (nameHas("Paladin")) {
            line(Color::RED, "Judgement", tag(sig(60), dmg(atk + 2 + 3 * (paladinJudgements + 1))),
                 "ignores your armor, and hits 3 harder every time it lands.");
            line(Color::HEAL, "Absolution", sig(40),
                 "heals " + std::to_string(20 + enemy.getBaseDefense())
                 + " and burns off every ailment you have put on it.");
        }
        else if (nameHas("Bastion")) {
            line(Color::ARMOR_CLR, "Wall", tag(sig(60), armor(def + 8)), "an impenetrable wall.");
            line(Color::RED, "Challenge", sig(40), "next turn you can only play ATTACK cards.");
        }
        else if (nameHas("Fortress"))  line(Color::ARMOR_CLR, "Shield Bash", tag(all, dmg(std::max(1, atk / 2))), "braces for " + armor(def) + ", then bashes you.");
        else if (nameHas("Orc"))       line(Color::RED, "Body Slam", tag(all, dmg(atk + 2)), "a crushing blow.");
        // CASTER
        else if (nameHas("Sage"))      line(Color::BURN_CLR, "Torch", tag(all, "Burn 5"), "a hurled flame. Below a third of its HP, 40% of these heal it ~" + std::to_string(8 + def / 2) + " instead.");
        else if (nameHas("Archon"))    line(Color::BURN_CLR, "Hellfire", tag(all, "Burn 12"), "fire from above. Below a third of its HP, 35% of these heal it ~" + std::to_string(10 + def / 2) + " instead.");
        else if (nameHas("Spellmaster"))line(Color::POISON_CLR, "Virulent Plague", tag(all, "Poison 14"), "a festering curse. Below a third of its HP, 35% of these heal it ~" + std::to_string(10 + def / 2) + " instead.");
        else if (nameHas("Enchanter")) line(Color::CARD_SPECIAL, "Tempt", all, "your next hand is 2 cards smaller.");
        else if (nameHas("Sorcerer"))  line(Color::BLUE, "Ice Blast", tag(all, "Weaken 2"), "and your next hand is a card smaller.");
        else if (nameHas("Vampire"))   line(Color::MAGENTA, "Vampiric Drain", tag(all, dmg(10)), "a bite: she heals +6 and gains +1 attack, and weakens you 2.");
        else if (nameHas("Mystic"))    line(Color::CYAN, "Illusion", all, "takes no damage on your next turn.");
        // RANGED
        else if (nameHas("Deadeye"))   line(Color::RED, "Dead Shot", tag(all, dmg(atk)), "a shot that bypasses half your armor.");
        else if (nameHas("Wyvern"))    line(Color::RED, "Flying Gnash", tag(all, dmg(atk + 2)), "a diving bite that bypasses half your armor.");
        else if (nameHas("Omneye")) {
            line(Color::RED, "Eye-Beam", tag(sig(70), dmg(atk + 2)), "a beam that bypasses half your armor.");
            line(Color::WEAK_CLR, "Gaze", tag(sig(30), "Weaken 2"), "an unsettling stare.");
        }
        else if (nameHas("Assassin")) {
            // Not a turn move: it fires during YOUR turn, so its turns use the
            // generic ranged moves listed with it.
            line(Color::RED, "Ambush", "45% per card you play, " + dmg(atk), "once per turn, strikes from the shadows past half your armor.");
            named = false;
        }
        else if (nameHas("Falcon"))    line(Color::CYAN, "Gouge", tag(all, dmg(atk + 1)), "a wind-borne dive that rakes past half your armor.");
        // BEAST
        else if (nameHas("Wolf"))      line(Color::RED, "Bite", tag(all, dmg(atk)), "a lunging bite.");
        else if (nameHas("Spider"))    line(Color::CARD_SPECIAL, "Web Trap", tag(all, "Weaken 2"), "a sticky snare.");
        else if (nameHas("Serpent"))   line(Color::CARD_SPECIAL, "Entangle", tag(all, "Weaken 3"), "coils around you.");
        else if (nameHas("Basilisk"))  line(Color::MAGENTA, "Curse", tag(all, "once"), "lose the run if it isn't dead in 7 turns. After that, those turns go to its other moves.");
        else if (nameHas("Cockatrice"))line(Color::MAGENTA, "Petrifying Bite", tag(all, dmg(atk)), "bites, then 5 turns to kill it or you turn to stone. Once per fight.");
        else if (nameHas("Manticore")) line(Color::RED, "Twin Maw", tag(all, dmg(atk) + " x2"), "both heads bite in the same lunge.");
        else if (nameHas("Fleshmass")) line(Color::MAGENTA, "Bind", tag(all, dmg(atk)), "a lash that draws blood limits you to 1 card next turn.");
        // UNDEAD
        else if (nameHas("Ghoul"))     line(Color::CARD_SPECIAL, "Chomp", tag(all, dmg(atk)), "heals itself +8 and poisons you 3.");
        else if (nameHas("Banshee"))   line(Color::CARD_SPECIAL, "Wailing Scream", tag(all, "Weaken 2"), "and she gains +2 attack, up to +6.");
        else if (nameHas("Specter") || nameHas("Wraith")) line(Color::CYAN, "Ghost", all, "takes no damage on your next turn.");
        else if (nameHas("Moonstruck")) {
            const MoonMove& own = MOON_MOVES[std::max(0, std::min(4, moonZone))];
            // Straight percentages, not the shared sig() wording: this is the
            // one enemy whose moves are not gated behind a signature roll.
            line(Color::STRENGTH_CLR, "Moon Scent", "40% when calm", "works itself into a frenzy: its blows hit x1.6 for 3 turns.");
            line(Color::RED, "Moonlit Maul", tag("32% calm, 78% frenzied", dmg(atk + 3)), "a savage blow, x1.6 while the scent lasts.");
            line(Color::MAGENTA, own.name, "28% calm, 22% frenzied", own.info);
        }
        else if (nameHas("Revenant"))  line(Color::CYAN, "Parry", all, "your next blow lands in full, and it hits you back just as hard.");
        else if (nameHas("Lich"))      line(Color::MAGENTA, "Raise Undead", tag(all, "one of the dead, 16-34 HP"), "whichever it raises soaks your attacks and strikes every turn. While it stands, those turns go to its other moves.");
        else named = false;

        // No turn signature of its own: these are the moves on its S% of turns.
        if (!named) {
            switch (enemy.getType()) {
                case EnemyType::MELEE:
                    line(Color::RED, "Attack", tag(sig(70), dmg(atk)), "reduced by your armor.");
                    line(Color::ARMOR_CLR, "Defend", tag(sig(30), armor(def)), "braces.");
                    break;
                case EnemyType::RANGED:
                    line(Color::RED, "Pierce attack", tag(sig(60), dmg(atk)), "bypasses half your armor.");
                    line(Color::ARMOR_CLR, "Defend", tag(sig(20), armor(std::max(1, def - 1))), "braces.");
                    line(Color::CARD_SPECIAL, "Crippling shot", tag(sig(20), "Weaken 2"), "you deal less damage for 2 turns.");
                    break;
                case EnemyType::TANK:
                    line(Color::ARMOR_CLR, "Defend", tag(sig(65), armor(def)), "braces.");
                    line(Color::RED, "Attack", tag(sig(35), dmg(std::max(1, atk - 2))), "reduced by your armor.");
                    break;
                case EnemyType::CASTER:
                    if (enemy.getHealth() < enemy.getMaxHealth() / 3) {
                        line(Color::HEAL, "Heal", tag(sig(60), "~" + std::to_string(8 + def / 2) + " HP"), "it is below a third of its HP.");
                        line(Color::RED, "Attack", tag(sig(40), dmg(atk + 1)), "a plain cast.");
                    } else {
                        line(Color::CARD_SPECIAL, "Poison Bolt", tag(sig(40), "Poison 3"), "2 dmg a turn for 6 turns.");
                        line(Color::CARD_SPECIAL, "Fireball", tag(sig(20), "Burn 2"), "3 dmg a turn for 2 turns.");
                        line(Color::RED, "Attack", tag(sig(40), dmg(atk + 1)), "a plain cast. Below a third of its HP it heals instead.");
                    }
                    break;
                case EnemyType::BEAST:
                    line(Color::RED, "Attack", tag(sig(60), dmg(atk)), "reduced by your armor.");
                    line(Color::CARD_SPECIAL, "Venomous bite", tag(sig(25), "Poison 3"), "2 dmg a turn for 6 turns.");
                    line(Color::ARMOR_CLR, "Defend", tag(sig(15), armor(std::max(1, def - 1))), "braces.");
                    break;
                case EnemyType::UNDEAD:
                    line(Color::RED, "Attack", tag(sig(75), dmg(atk)), "reduced by your armor.");
                    line(Color::CARD_SPECIAL, "Chilling touch", tag(sig(25), "Weaken 2"), "you deal less damage for 2 turns.");
                    break;
                default: break;
            }
        }
        flush("  Its own moves (" + pct(S) + " of turns):");

        // The archetype kit, which every enemy runs on the rest of its turns.
        auto printKit = [&]() {
            switch (enemy.getType()) {
                case EnemyType::MELEE:
                    line(Color::RED, "Swing", tag(kitPct(60), dmg(atk)), "a plain blow.");
                    line(Color::RED, "Wind up", tag(kitPct(25), "+2 atk"), "no damage this turn; up to +6.");
                    line(Color::RED, "Heavy swing", tag(kitPct(15), dmg(atk + 3)), "a committed blow.");
                    break;
                case EnemyType::TANK:
                    line(Color::ARMOR_CLR, "Brace", tag(kitPct(45), armor(def + 2)), "digs in.");
                    line(Color::RED, "Heavy blow", tag(kitPct(40), dmg(atk + 2)), "a hard hit.");
                    line(Color::ARMOR_CLR, "Shove", tag(kitPct(15), dmg(std::max(1, atk / 2))), "braces for " + armor(def) + ", then hits.");
                    break;
                case EnemyType::RANGED:
                    if (enemyIsFlyer()) {
                        line(Color::RED, "Dive", tag(kitPct(60), dmg(atk)), "comes in fast and is gone before Parry can answer.");
                        line(Color::WEAK_CLR, "Wingbeat", tag(kitPct(25), "Weaken 2"), "a gust in your face.");
                        line(Color::RED, "Double rake", tag(kitPct(15), dmg(std::max(1, atk / 2)) + " x2"), "two passes on a wingtip.");
                        break;
                    }
                    line(Color::RED, "Shot", tag(kitPct(60), dmg(atk)), "bypasses half your armor.");
                    line(Color::WEAK_CLR, "Weakening shot", tag(kitPct(25), "Weaken 2"), "clips your arm.");
                    line(Color::RED, "Double shot", tag(kitPct(15), dmg(std::max(1, atk / 2)) + " x2"), "both bypass half your armor.");
                    break;
                case EnemyType::CASTER:
                    line(Color::RED, "Force bolt", tag(kitPct(45), dmg(atk)), "a plain cast.");
                    line(Color::WEAK_CLR, "Hex", tag(kitPct(30), "Weaken 2"), "a settling curse.");
                    line(Color::HEAL, "Mend / brace", tag(kitPct(25), "~" + std::to_string(6 + def) + " HP"), "heals when below half HP, else braces for " + armor(def) + ".");
                    break;
                case EnemyType::BEAST:
                    line(Color::RED, "Lunge", tag(kitPct(55), dmg(atk)), "teeth and claws.");
                    line(Color::RED, "Frenzy", tag(kitPct(30), "+2 atk"), "no damage this turn; up to +6.");
                    line(Color::ARMOR_CLR, "Brace", tag(kitPct(15), armor(def)), "hunkers down.");
                    break;
                case EnemyType::UNDEAD:
                    line(Color::RED, "Claw", tag(kitPct(50), dmg(atk)), "dead hands.");
                    line(Color::POISON_CLR, "Grave rot", tag(kitPct(30), "Poison 3"), "a festering touch.");
                    line(Color::HEAL, "Drain", tag(kitPct(20), "~" + std::to_string(5 + def) + " HP"), "heals itself.");
                    break;
                default: break;
            }
            flush("  Its other moves (" + pct(kit) + " of turns):");
        };
        printKit();
    }
    std::cout << "\n";
}

// Mirrors the attack path in playCardFromHand. Kept deliberately close to it:
// if one changes and the other does not, the preview starts lying.
int Game::previewDamage(const Card& c) const {
    if (c.getType() != CardType::ATTACK) return 0;
    if (lichAddAlive) return 0;                       // the skeleton soaks it all
    const bool trueStrike = (c.getEffect() == CardEffect::TRUESTRIKE
                          || c.getEffect() == CardEffect::TRUE_DOUBLE);
    if (enemyInvulnerable && !trueStrike) return 0;

    const bool pierce = trueStrike || c.getEffect() == CardEffect::PIERCE
                                   || c.getEffect() == CardEffect::OVEREXTEND;
    const int  hits   = (c.getEffect() == CardEffect::DOUBLE_HIT
                      || c.getEffect() == CardEffect::TRUE_DOUBLE) ? 2 : 1;
    // The Moon Blade: the turn's first attack is played twice.
    const bool reflected = wornWeapon == TIER_MOON && attacksPlayedThisTurn == 0;

    DamageType weakness = enemy.getWeakness();
    bool hitsWeakness = weakness != DamageType::NONE &&
                        (c.getPhysType() == weakness || c.getPhysType2() == weakness
                         || c.getElemType() == weakness);
    DamageType resistance = enemy.getResistance();
    bool hitsResistance = !trueStrike && resistance != DamageType::NONE &&
                          (c.getPhysType() == resistance || c.getPhysType2() == resistance
                           || c.getElemType() == resistance);

    int dmg = liveValue(c);
    dmg = (int)(dmg * playerStatus.getWeakMultiplier() * playerStatus.getStrengthMultiplier());
    if (hitsWeakness)   dmg = (int)(dmg * 1.5);
    if (hitsResistance) dmg = (int)(dmg * 0.5);
    if (enemyVulnerableTurns > 0) dmg = dmg * 3 / 2;                                   // the true form's Berserk
    if (enemyParryStance && !trueStrike && enemy.isBoss()) dmg = (int)(dmg * 0.5);   // the true form's parry

    // Armour soaks per hit and is spent as it soaks, same as Enemy::takeDamage.
    int armor = enemy.getArmor(), lost = 0;
    for (int i = 0; i < hits * (reflected ? 2 : 1); i++) {
        int def = pierce ? 0 : enemy.getBaseDefense();
        if (wornWeapon == 3 && (c.getPhysType() == DamageType::PIERCE || c.getPhysType2() == DamageType::PIERCE))
            def = std::max(0, def - 2);
        if (wornWeapon == TIER_SHADOW) def = 0;
        int dealt = calculateDamage(dmg, def);
        lost  += std::max(0, dealt - armor);
        armor  = std::max(0, armor - dealt);
    }
    return std::min(lost, enemy.getHealth());
}

// Rounded, not truncated. Integer division silently ate small gains: at +8% a
// 6-damage Strike came out 6.48 and displayed as 6, so a whole weapon drop
// changed nothing visible on any card below 13 damage.
static int applyPct(int base, int pct) {
    if (base <= 0) return 0;
    return (base * (100 + pct) + 50) / 100;
}

int Game::atkWithGear(int rawValue) const {
    int flat = rawValue + upgrades.getDamageBonus() + roadBonus;
    // The Legendary Blade's weight goes in before the percentage, like a
    // card's own value; the Iron Sword's opening cut the same way.
    if (wornWeapon == 6) flat += LEGEND_BLADE_FLAT;
    if (wornWeapon == 2 && (attacksPlayedThisTurn == 0 || openingAttack)) flat += 3;
    int v = applyPct(flat, weaponPct());
    if (hasRelic(Relic::SEAL_FRAGMENT) && sealsBroken > 0) v = v * (100 + 5 * sealsBroken) / 100;
    if (hasRelic(Relic::GLASS_MOON)) v = v * 125 / 100;
    // What the drawback cards charge: a flat cut from Reckless Swing, a
    // percentage from Heavy Guard. Both run through here so the hand, the
    // preview and the swing itself all quote the same number.
    if (cardSoftenPct > 0) v = v * (100 - cardSoftenPct) / 100;
    v -= cardDamagePenalty;
    return std::max(1, v);
}

int Game::attunementChance() const {
    // 10% base, and each Attunement boon adds 8 points to it.
    return std::min(100, 10 + attunementBoons * 8);
}

int Game::defWithGear(int rawValue) const {
    return applyPct(rawValue + upgrades.getArmorBonus() + roadBonus, armorPct());
}

// SPECIAL cards (heals, buffs, taunts) are deliberately untouched by gear.
int Game::gearedValue(const Card& c, int rawValue) const {
    if (c.getType() == CardType::ATTACK) return atkWithGear(rawValue);
    if (c.getType() == CardType::DEFEND) return defWithGear(rawValue);
    return rawValue;
}

// What a card will land for right now, before Weak and Strength. Most cards
// are their printed value through gear; All In and Last Stand read the
// board instead, so their faces show what the card is actually worth.
int Game::liveValue(const Card& c) const {
    switch (c.getEffect()) {
        // The armour is thrown as it stands. It already carries the armour
        // percentage, so the weapon percentage deliberately stays out of it.
        case CardEffect::ALLIN:     return playerArmor * 2;
        case CardEffect::LASTSTAND: return std::max(0, maxPlayerHealth - playerHealth) + c.getValue();
        default:                    return gearedValue(c, c.getValue());
    }
}

bool Game::hasRelic(int id) const { return (relicsOwned >> id) & 1; }

// What a card costs to play right now. The Mythril Edge takes 1 off the first
// attack each turn; everything else costs what it says.
int Game::effectiveCost(const Card& c) const {
    int cost = c.getCost();
    if (wornWeapon == 5 && c.getType() == CardType::ATTACK && attacksPlayedThisTurn == 0)
        cost = std::max(0, cost - 1);
    return cost;
}

// The kind of blow each enemy deals, for the armour: by name, with its
// archetype as the fallback. Bosses and Moonstruck forms first, since
// "Shadow Knight" contains "Knight".
DamageType Game::enemyAttackType() const {
    using D = DamageType;
    static const std::pair<const char*, D> TABLE[] = {
        {"Moonstruck Shadow Knight", D::PIERCE},
        {"Moonstruck Weaver", D::POISON}, {"Moonstruck Beguiler", D::WIND},
        {"Moonstruck Gorgon", D::POISON}, {"Moonstruck Templar", D::SMASH}, {"Moonstruck", D::PIERCE},
        {"Colossus", D::SMASH}, {"Witch", D::POISON}, {"Thunder", D::WIND}, {"Hydra", D::POISON},
        {"Dragon", D::FIRE},    {"Shadow Knight", D::PIERCE},
        // The Dungeon
        {"Goblin", D::PIERCE}, {"Orc", D::SMASH}, {"Wizard", D::FIRE}, {"Skeleton", D::PIERCE},
        {"Spider", D::POISON}, {"Archer", D::PIERCE}, {"Bandit", D::PIERCE}, {"Warden", D::SMASH},
        {"Sage", D::WIND},
        // The Dark Dungeon
        {"Ghoul", D::POISON}, {"Basilisk", D::POISON}, {"Assassin", D::PIERCE}, {"Knight", D::PIERCE},
        {"Sentinel", D::SMASH}, {"Enchanter", D::WIND}, {"Wraith", D::WIND}, {"Serpent", D::POISON},
        {"Omneye", D::FIRE},
        // The Wicked Forest
        {"Raider", D::PIERCE}, {"Barbarian", D::SMASH}, {"Mystic", D::WIND}, {"Banshee", D::WIND},
        {"Wolf", D::PIERCE}, {"Falcon", D::PIERCE}, {"Berserker", D::SMASH}, {"Guardian", D::SMASH},
        {"Vampire", D::PIERCE},
        // The Dark Lake
        {"Specter", D::WIND}, {"Cockatrice", D::POISON}, {"Gladiator", D::PIERCE}, {"Bastion", D::SMASH},
        {"Spellmaster", D::POISON}, {"Warrior", D::SMASH}, {"Sorcerer", D::WIND}, {"Fortress", D::SMASH},
        {"Wyvern", D::PIERCE},
        // The Mountain
        {"Deadeye", D::PIERCE}, {"Enforcer", D::SMASH}, {"Revenant", D::PIERCE}, {"Manticore", D::POISON},
        {"Lich", D::POISON}, {"Paladin", D::SMASH}, {"Fleshmass", D::SMASH}, {"Archon", D::FIRE},
    };
    const std::string n = enemy.getName();
    for (const auto& e : TABLE)
        if (n.find(e.first) != std::string::npos) return e.second;
    switch (enemy.getType()) {
        case EnemyType::TANK:   return D::SMASH;
        case EnemyType::CASTER: return D::FIRE;
        case EnemyType::UNDEAD: return D::SMASH;
        default:                return D::PIERCE;
    }
}

int Game::armourTypeMod(DamageType t) const {
    if (t == DamageType::NONE) return 0;
    const ArmourProfile& p = ARMOUR_PROFILE[std::max(0, std::min(8, wornArmor))];
    if (t == p.resist1 || t == p.resist2) return -ARMOUR_TYPE_PCT;
    if (t == p.weak) return ARMOUR_TYPE_PCT;
    return 0;
}

// Everything that rides on one of your hits landing: the weapon's passive and
// the relics that key off a hit.
void Game::onPlayerHit(const Card& card, int hpLost) {
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> d100(1, 100);
    if (wornWeapon == 4 && !noHealThisEncounter && playerHealth < maxPlayerHealth) {
        const int heal = std::min(maxPlayerHealth - playerHealth, std::max(1, hpLost / 10));
        playerHealth += heal;
        EnemyArt::popNumber(heal, false, EnemyArt::PopKind::HEAL);
        std::cout << "  " << Color::HEAL << "The Ebon Blade drinks. +" << heal << " HP." << Color::RESET << "\n";
    }
    if (!enemy.isAlive()) return;
    if (wornWeapon == 1 && d100(gen) <= 15 && applyEnemyStatus(StatusType::POISON, 2))
        std::cout << "  " << Color::POISON_CLR << "Rust gets into the wound. Poison 2." << Color::RESET << "\n";
    const bool smash = card.getPhysType() == DamageType::SMASH || card.getPhysType2() == DamageType::SMASH;
    if (smash && hasRelic(Relic::WEIGHTED_POMMEL) && d100(gen) <= 20 && applyEnemyStatus(StatusType::WEAK, 2))
        std::cout << "  " << Color::WEAK_CLR << "The pommel rings its skull. Weakened." << Color::RESET << "\n";
}

// The relics that act once at the start of every fight.
void Game::applyFightStartRelics() {
    attacksPlayedThisTurn = 0;
    openingAttack = false;
    if (hasRelic(Relic::WARDEN_LANTERN)) playerArmor += 8;
    if (hasRelic(Relic::HOURGLASS)) playerEnergy += 1;
    rollLens();
}

// Scholar's Lens: draw the enemy's next rolls now, so its move can be shown.
// enemyTurn() uses these instead of drawing its own, so what is shown is what
// it does, short of a taunt or a fear changing its mind.
void Game::rollLens() {
    if (!hasRelic(Relic::SCHOLAR_LENS)) { lensRoll = lensSigRoll = -1; return; }
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> d(0, 99);
    lensRoll = d(gen);
    lensSigRoll = d(gen);
}

// The move those rolls pick, named the way View Enemy names it. Bosses keep
// their secrets: their turns are not built from the archetype kits.
std::string Game::lensIntent() const {
    if (lensRoll < 0 || enemy.isBoss() || !enemy.isAlive()) return "";
    if (lensSigRoll < signatureChanceFor(enemy.getName())) return "its own move";
    const int r = lensRoll;
    const bool canBuff = enemy.getBonusAttack() < 6;
    switch (enemy.getType()) {
        case EnemyType::MELEE:  return r < 60 ? "Swing" : (r < 85 && canBuff) ? "Wind up" : "Heavy swing";
        case EnemyType::TANK:   return r < 45 ? "Brace" : r < 85 ? "Heavy blow" : "Shove";
        case EnemyType::RANGED:
            if (enemyIsFlyer()) return r < 60 ? "Dive" : r < 85 ? "Wingbeat" : "Double rake";
            return r < 60 ? "Shot" : r < 85 ? "Weakening shot" : "Double shot";
        case EnemyType::CASTER:
            return r < 45 ? "Force bolt" : r < 75 ? "Hex"
                 : enemy.getHealth() < enemy.getMaxHealth() / 2 ? "Mend" : "Brace";
        case EnemyType::BEAST:  return r < 55 ? "Lunge" : (r < 85 && canBuff) ? "Frenzy" : "Brace";
        case EnemyType::UNDEAD: return r < 50 ? "Claw" : r < 80 ? "Grave rot" : "Knit";
        default:                return "";
    }
}

// A relic, one of three, every 12 encounters from the 6th. One may be cursed:
// it wears the same gold outline as the cards that cost you something.
void Game::offerRelic() {
    std::vector<int> pool;
    for (int i = 0; i < Relic::COUNT; i++) if (!hasRelic(i)) pool.push_back(i);
    if (pool.empty()) return;
    static thread_local std::mt19937 gen(std::random_device{}());
    std::shuffle(pool.begin(), pool.end(), gen);
    if (pool.size() > 3) pool.resize(3);

    std::vector<CardBar::Card> widgets;
    for (int id : pool) {
        const Relic::Info& r = Relic::INFO[id];
        CardBar::Card w;
        w.name = r.name;
        w.elemTag = "[RELIC]";
        w.effect = r.face;
        w.note = r.cursed ? "cursed: it costs you" : "lasts the run";
        w.risk = r.cursed;
        w.tint = RELIC_STRIPE;
        w.nameColor = RELIC_NAME;   // the gold ring is what marks the cursed one
        w.icon = RELIC_ICON0 + id;
        w.item = true;
        widgets.push_back(w);
    }
    UIHelper::clearScreen();
    while (true) {
        std::vector<CardBar::Action> acts{ CardBar::Action{ "Leave them", false } };
        const int choice = CardBar::pick("Something was left here for you. Take one.", widgets, acts,
                                         (int)widgets.size());
        if (choice <= -2) {
            const int ci = -2 - choice;
            if (ci >= 0 && ci < (int)pool.size())
                CardBar::showDetail(widgets[ci], Relic::INFO[pool[ci]].text, "RELIC",
                                    Relic::INFO[pool[ci]].cursed ? "CURSED" : "", 0);
            continue;
        }
        if (choice < 0 || choice >= (int)pool.size()) { notice("You leave them where they lie."); return; }
        const Relic::Info& r = Relic::INFO[pool[choice]];
        if (!confirm(std::string("Take the ") + r.name + "? Relics last the whole run.")) continue;
        relicsOwned |= 1 << pool[choice];
        Audio::playSFX("upgrade");
        notice(std::string(r.name) + ". " + r.text);
        return;
    }
}

// Every broken seal adds pctPerSeal percent of the base, so they stack
// linearly: the fifth seal is as noticeable as the first.
int Game::sealScaled(int base, int pctPerSeal) const {
    return base * (100 + sealsBroken * pctPerSeal) / 100;
}

int Game::calculateDamage(int attackValue, int defenseValue) const {
    int damage = attackValue - defenseValue;
    return (damage < 0) ? 0 : damage;
}

bool Game::spendEnergy(int cost) {
    if (playerEnergy < cost) {
        std::cout << Color::DAMAGE << "Not enough energy! Need " << cost
                  << " but only have " << playerEnergy << " remaining." << Color::RESET << "\n";
        return false;
    }
    playerEnergy -= cost;
    return true;
}

void Game::resetEnergy() {
    // Adrenaline spends next turn's energy now, and this is where the bill lands.
    playerEnergy = std::max(0, maxEnergy - energyDebt);
    if (energyDebt > 0) {
        std::cout << "  " << Color::DIM << "Last turn's rush leaves you " << energyDebt
                  << " energy short." << Color::RESET << "\n";
        energyDebt = 0;
    }
}

// Taunt and Fear both miss one time in ten. Shared, so the two cannot drift
// apart the way the two copies of the effect table did.
static bool provokeFizzles() {
    static thread_local std::mt19937 g(std::random_device{}());
    return std::uniform_int_distribution<>(1, 100)(g) <= 20;
}

void Game::applyCardEffect(const Card& card) {
    int val = card.getValue();
    switch (card.getEffect()) {
        case CardEffect::POISON:
            EnemyArt::printBattleCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::POISON);
            if (applyEnemyStatus(StatusType::POISON, val)) {
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::POISON, true);
                std::cout << "  " << Color::POISON_CLR << "Applied " << val << " Poison to enemy! ("
                          << (val + 1) / 2 << " dmg/turn for 6 turns)" << Color::RESET << "\n";
            }
            break;
        case CardEffect::BURN:
            EnemyArt::printBattleCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::BURN);
            if (applyEnemyStatus(StatusType::BURN, val)) {
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::BURN, true);
                std::cout << "  " << Color::BURN_CLR << "Applied " << val << " Burn to enemy! ("
                          << val + val / 2 << " dmg/turn for 2 turns)" << Color::RESET << "\n";
            }
            break;
        case CardEffect::REND:
            EnemyArt::printBattleCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::REND);
            if (applyEnemyStatus(StatusType::REND, val)) {
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::REND, true);
                std::cout << "  " << Color::REND_CLR << "Applied " << val << " Rend to enemy! ("
                          << val << " damage the next 3 times it attacks)" << Color::RESET << "\n";
            }
            break;
        case CardEffect::STUN:
            EnemyArt::printBattleCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::STUN);
            if (tryStunEnemy()) {
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::STUN, true);
                std::cout << "  " << Color::STUN_CLR << "Enemy is STUNNED! They'll lose their next turn!" << Color::RESET << "\n";
            } else {
                std::cout << "  " << Color::DIM << "The boss resists the stun!" << Color::RESET << "\n";
            }
            break;
        case CardEffect::WEAK: {
            // Multiplier scales with rarity: Uncommon 1.5x, Rare 1.75x, Super Rare 2x.
            EnemyArt::printBattleCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::WEAK);
            double weakMult = card.isSuperRare() ? 2.0 : card.isRare() ? 1.75 : 1.5;
            if (applyEnemyStatus(StatusType::WEAK, 3, weakMult)) {
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::WEAK, true);
                std::cout << "  " << Color::WEAK_CLR << "Applied Weak to enemy for 3 turns! (deals " << weakMult << "x less damage)" << Color::RESET << "\n";
            }
            break;
        }
        case CardEffect::COUNTER:
            counterAttackActive = true;
            counterWasLegendary = card.isLegendary();
            counterBonusValue = val;
            std::cout << "  " << Color::CYAN << "You brace for a counterattack. If they strike, you hit back for double"
                      << (val > 0 ? (" +" + std::to_string(val)) : std::string()) << "." << Color::RESET << "\n";
            break;
        case CardEffect::PARRY:
            parryActive = true;
            parryBonusValue = val;
            std::cout << "  " << Color::CYAN << "You raise your guard. If they attack, you'll parry"
                      << (val > 0 ? (" (+" + std::to_string(val) + " riposte)") : std::string()) << " and stun them." << Color::RESET << "\n";
            break;
        case CardEffect::SACRIFICE:
        case CardEffect::HEAL: {
            if (noHealThisEncounter) {
                std::cout << "  " << Color::MAGENTA << "Your wounds refuse to close. Nothing heals this fight."
                          << Color::RESET << "\n";
                break;
            }
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::HEAL);
            int before = playerHealth;
            {
                int before = playerHealth;
                const int healed = Card::healAmount(val, playerHealth, maxPlayerHealth);
                playerHealth = std::min(maxPlayerHealth, playerHealth + healed);
                if (playerHealth > before)
                    EnemyArt::popNumber(playerHealth - before, false, EnemyArt::PopKind::HEAL);
            }
            std::cout << "  " << Color::HEAL << "Recovered " << (playerHealth - before) << " HP!" << Color::RESET
                      << " (" << playerHealth << "/" << maxPlayerHealth << ")\n";
            break;
        }
        case CardEffect::STRENGTH: {
            // This path is for pure-buff SPECIAL cards like Strengthen; Bloodlust
            // takes the ATTACK path below. Both read the same ladder now.
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::STRENGTH);
            double buff = card.strengthMultiplier();
            playerStatus.apply(StatusType::STRENGTH, 2, 1.5, buff);
            std::cout << "  " << Color::STRENGTH_CLR << "Strength surges! x" << buff << " damage for 2 turns!" << Color::RESET << "\n";
            break;
        }
        case CardEffect::BLOODPRICE:
            playerEnergy += val;
            playerHealth = std::max(0, playerHealth - 6);
            Audio::playSFX("special");
            std::cout << "  " << Color::ENERGY_CLR << "+" << val << " energy" << Color::RESET
                      << Color::DAMAGE << ", bought with 6 HP." << Color::RESET
                      << " (" << playerHealth << "/" << maxPlayerHealth << ")\n";
            break;
        case CardEffect::ADRENALINE:
            playerEnergy += val;
            energyDebt += 2;
            Audio::playSFX("special");
            std::cout << "  " << Color::ENERGY_CLR << "+" << val << " energy now" << Color::RESET
                      << Color::WEAK_CLR << ", and 2 less next turn." << Color::RESET << "\n";
            break;
        case CardEffect::BERSERK:
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::STRENGTH);
            playerStatus.apply(StatusType::STRENGTH, 2, 1.5, 1.5);
            vulnerableTurns = 1;
            vulnerableMult  = 1.5;
            std::cout << "  " << Color::STRENGTH_CLR << "You throw your guard away and wind up: x1.5 damage"
                      << Color::RESET << Color::DAMAGE << ", and x1.5 taken until your next turn."
                      << Color::RESET << "\n";
            break;
        case CardEffect::BLOODPACT: {
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::STRENGTH);
            // Paid out of the maximum, not current health, so a heal cannot buy it
            // back; endEncounterEffects() returns it. 15% of the ceiling the fight
            // started with, so every pact costs the same.
            const int paid = std::max(1, (maxPlayerHealth + maxHpDebt) * 15 / 100);
            maxHpDebt += paid;
            maxPlayerHealth = std::max(1, maxPlayerHealth - paid);
            playerHealth = std::min(playerHealth, maxPlayerHealth);
            playerStatus.apply(StatusType::STRENGTH, 3, 1.5, 2.0);
            std::cout << "  " << Color::STRENGTH_CLR << "x2 damage for 3 turns" << Color::RESET
                      << Color::DAMAGE << ", paid with " << paid << " max HP for this fight."
                      << Color::RESET << " (" << playerHealth << "/" << maxPlayerHealth << ")\n";
            break;
        }
        case CardEffect::BORROWED: {
            extraTurnsPending++;
            // Max HP falls for the rest of the fight (endEncounterEffects() hands it
            // back), off the starting ceiling for the same reason as Blood Pact.
            const int loss = (maxPlayerHealth + maxHpDebt) * 40 / 100;
            maxHpDebt += loss;
            maxPlayerHealth = std::max(1, maxPlayerHealth - loss);
            playerHealth = std::min(playerHealth, maxPlayerHealth);
            // The stun is booked, not applied: endPlayerTurn() lands it on the turn
            // after the extra one, the turn the enemy gets for free.
            borrowedStunsPending++;
            Audio::playSFX("legendary");
            std::cout << "  " << Color::BOLD << Color::CYAN << "Time folds. You move again immediately."
                      << Color::RESET << Color::DAMAGE << " It will cost you a turn, and "
                      << loss << " max HP for this fight." << Color::RESET << "\n";
            break;
        }
        // Taunt and Fear are one card pointed in opposite directions, and both
        // are a read rather than a guarantee: one time in ten it does not land.
        case CardEffect::TAUNT:
            if (provokeFizzles()) {
                std::cout << "  " << Color::DIM << "The taunt glances off. They are not listening." << Color::RESET << "\n";
                break;
            }
            enemyTauntTurns = 2;
            std::cout << "  " << Color::RED << "You taunt the enemy! They're much more likely to attack for their next 2 turns." << Color::RESET << "\n";
            break;
        case CardEffect::FEAR:
            if (!enemyCanDefend()) {
                std::cout << "  " << Color::DIM << "It has no guard to hide behind. There is nothing for the fear to do." << Color::RESET << "\n";
                break;
            }
            if (provokeFizzles()) {
                std::cout << "  " << Color::DIM << "It holds your gaze without flinching. The fear does not take." << Color::RESET << "\n";
                break;
            }
            enemyFearTurns = 2;
            std::cout << "  " << Color::CYAN << "The enemy recoils! They're much more likely to brace than to act for their next 2 turns." << Color::RESET << "\n";
            break;
        default:
            break;
    }
}

namespace {
    // StatusType -> flash color; returns false for types with no flash (buffs)
    bool glowFor(StatusType type, EnemyArt::CastGlow& out) {
        switch (type) {
            case StatusType::POISON: out = EnemyArt::CastGlow::POISON; return true;
            case StatusType::BURN:   out = EnemyArt::CastGlow::BURN;   return true;
            case StatusType::STUN:   out = EnemyArt::CastGlow::STUN;   return true;
            case StatusType::WEAK:   out = EnemyArt::CastGlow::WEAK;   return true;
            default: return false;
        }
    }
}

// Routes ailments through Dodge Reversal/Status Guard before applying them.
void Game::applyPlayerStatus(StatusType type, int amount, double weakMultiplier) {
    EnemyArt::CastGlow glow;
    if (counterAttackActive) {
        counterAttackActive = false;
        int reflectedAmount = amount * 2 + counterBonusValue;
        applyEnemyStatus(type, reflectedAmount, weakMultiplier);
        Audio::playSFX(counterWasLegendary ? "legendary" : "special");
        std::cout << "  " << Color::GREEN << "Dodge Reversal! You reverse the effect back onto the enemy, doubled, +"
                  << counterBonusValue << "!" << Color::RESET << "\n";
        if (glowFor(type, glow))
            EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), glow, true);
        return;
    }
    if (statusWardTurns > 0) {
        // Holds for its whole duration rather than popping on the first
        // ailment, so it answers a caster that throws two in a turn.
        std::cout << "  " << Color::CYAN << "Status Guard blocks the ailment!" << Color::RESET
                  << " " << Color::DIM << "(" << statusWardTurns
                  << (statusWardTurns == 1 ? " turn" : " turns") << " left)" << Color::RESET << "\n";
        return;
    }
    playerStatus.apply(type, amount, weakMultiplier);
    if (glowFor(type, glow))
        EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), glow, false);
}

// Same idea in reverse; false means warded (caller skips its own "applied" message).
bool Game::applyEnemyStatus(StatusType type, int amount, double weakMultiplier) {
    // The skeleton stands between you and its master, so a cloud, a curse or a
    // hex lands on it the same way a blade does. It has no status of its own, so
    // it takes the stack as damage.
    if (lichAddAlive) {
        const int soaked = std::max(1, amount);
        lichAddHp = std::max(0, lichAddHp - soaked);
        EnemyArt::popNumberAdd(soaked, EnemyArt::PopKind::DAMAGE);
        Audio::playSFX(lichAddHp <= 0 ? "dead" : "special");
        std::cout << "  " << Color::MAGENTA << "The summoned " << lichAddName << " takes it instead: "
                  << soaked << " damage." << Color::RESET
                  << " (" << lichAddName << " HP: " << lichAddHp << "/" << lichAddMaxHp << ")\n";
        if (lichAddHp <= 0) {
            lichAddAlive = false;
            EnemyArt::setCompanion("");
            std::cout << "  " << Color::MAGENTA << "The summoned " << lichAddName
                      << " crumbles to dust!" << Color::RESET << "\n";
        }
        UIHelper::pause(200);
        return false;
    }
    if (enemyStatusWardActive) {
        enemyStatusWardActive = false;
        std::cout << "  " << Color::CYAN << "The shadow's guard blocks the ailment!" << Color::RESET << "\n";
        return false;
    }
    enemy.applyStatus(type, amount, weakMultiplier);
    // The relics that deepen what you inflict.
    if (type == StatusType::POISON && hasRelic(Relic::VENOM_VIAL))   enemy.extendStatus(type, 2);
    if (type == StatusType::BURN   && hasRelic(Relic::EMBER_HEART))  enemy.extendStatus(type, 2);
    if (type == StatusType::REND   && hasRelic(Relic::GALE_FEATHER)) enemy.extendStatus(type, 2);
    return true;
}

// Fear needs something to work on, so it refuses against an enemy with no
// self-protective move. "Guard" is wider than armor: phasing out, a parry
// stance, a summon and a self-heal all count. test_fear re-derives this
// from enemyTurn().
bool Game::enemyCanDefend() const {
    // Bosses never reach enemyTurn() - bossAction() has no brace path at all.
    if (enemy.isBoss()) return false;

    // The archetype kit is the answer: TANK, CASTER, BEAST and UNDEAD each have
    // a defensive or restorative turn in their three moves, MELEE and RANGED do
    // not. Deriving it here keeps Fear from needing a hand-kept name list.
    switch (enemy.getType()) {
        case EnemyType::TANK:
        case EnemyType::CASTER:
        case EnemyType::BEAST:
        case EnemyType::UNDEAD:
            return true;
        default:
            break;
    }

    // One exception: the Knight is MELEE but its signature is a shield bash, so
    // it does raise a guard even though its archetype template never would.
    return enemy.getName().find("Knight") != std::string::npos;
}

// Silent when warded: callers print their own resisted message.
bool Game::tryStunEnemy() {
    if (enemyStatusWardActive) {
        enemyStatusWardActive = false;
        return false;
    }
    return enemy.tryApplyStun();
}

void Game::refreshBattleAuras() {
    // Cheap, and this runs whenever the scene is about to be redrawn, so a drop
    // taken between fights shows on the knight without another call site.
    EnemyArt::setGearTiers(wornWeapon, wornArmor);
    EnemyArt::AuraFlags knight, foe;
    knight.strength = playerStatus.hasStrength();
    knight.weak     = playerStatus.hasWeak();
    knight.poison   = playerStatus.hasPoison();
    knight.burn     = playerStatus.hasBurn();
    knight.rend     = playerStatus.hasRend();
    knight.stun     = playerStatus.hasStun();
    foe.strength = enemy.hasStrength();   // Moon Scent: glows red, as the knight does
    foe.weak   = enemy.hasWeak();
    foe.poison = enemy.hasPoison();
    foe.burn   = enemy.hasBurn();
    foe.rend   = enemy.hasRend();
    foe.stun   = enemy.hasStun();
    EnemyArt::setBattleAuras(knight, foe);
}

void Game::playCardFromHand(int index) {
    try {
        const Card& card = playerDeck.getCardFromHand(index - 1);

        if (playerDeck.isCardUsed(index - 1)) {
            std::cout << Color::DIM << "Card " << index << " already played this turn." << Color::RESET << "\n";
            return;
        }
        if (playerBoundTurn && cardsPlayedThisTurn >= 1) {
            std::cout << Color::MAGENTA << "The tentacles hold fast - only one card while BOUND." << Color::RESET << "\n";
            return;
        }
        if (cardLimitThisTurn > 0 && cardsPlayedThisTurn >= cardLimitThisTurn) {
            std::cout << Color::MAGENTA << "The shockwave has to settle. No more cards this turn."
                      << Color::RESET << "\n";
            return;
        }
        if (!spendEnergy(effectiveCost(card))) return;

        Card playedCard = playerDeck.playCard(index - 1);

        cardsPlayedThisTurn++;
        // The first attack of the turn: the Iron Sword and the Mythril Edge act
        // on it. openingAttack holds while it resolves and clears whichever way
        // this function returns.
        openingAttack = playedCard.getType() == CardType::ATTACK && attacksPlayedThisTurn == 0;
        if (playedCard.getType() == CardType::ATTACK) attacksPlayedThisTurn++;
        struct ClearOpening { bool& f; ~ClearOpening() { f = false; } } clearOpening{ openingAttack };
        payPactOfRuin();
        lastActionWasCardPlay = true;
        lastPlayedCardType = playedCard.getType();
        lastPlayedCardWasRisky = playedCard.hasDrawback();
        lastPlayedPhysType = playedCard.getPhysType();
        lastPlayedPhysType2 = playedCard.getPhysType2();

        std::cout << "Played: [" << playedCard.getName() << "] (Cost: " << playedCard.getCost() << ")\n";
        // Legendaries announce themselves. Stance cards are the exception:
        // playing one only arms it, and the cue belongs on the payoff, so
        // those fire it when they actually resolve instead.
        if (playedCard.isLegendary() && playedCard.getEffect() != CardEffect::COUNTER
                                     && playedCard.getEffect() != CardEffect::PARRY)
            Audio::playSFX("legendary");

        // Sacrifice leaves the deck for the rest of the fight rather than
        // shuffling back in. endEncounterEffects() returns it.
        if (playedCard.getEffect() == CardEffect::SACRIFICE) {
            exhausted.push_back(playedCard);
            playerDeck.removeCardByName(playedCard.getName());
        }

        if (playedCard.getType() == CardType::ATTACK) {
            // One tear per attack card, not per hit: a double-hit card is still
            // one swing as far as the wound is concerned.
            tickPlayerRend();
            // Reckoning: ignores defense like PIERCE, and additionally refuses
            // every reduction the enemy can put in the way.
            const CardEffect eff = playedCard.getEffect();
            bool trueStrike     = (eff == CardEffect::TRUESTRIKE || eff == CardEffect::TRUE_DOUBLE);
            bool pierce         = trueStrike || (eff == CardEffect::PIERCE)
                                               || (eff == CardEffect::OVEREXTEND);
            bool doubleHit      = (eff == CardEffect::DOUBLE_HIT || eff == CardEffect::TRUE_DOUBLE);
            // The Moon Blade: the turn's first attack is played twice, the second
            // time by its reflection, every hit as hard as the first.
            const bool reflected = wornWeapon == TIER_MOON && openingAttack;
            const int  ownHits   = doubleHit ? 2 : 1;
            int  hits           = ownHits * (reflected ? 2 : 1);
            double weakMult     = playerStatus.getWeakMultiplier();
            double strengthMult = playerStatus.getStrengthMultiplier();

            DamageType weakness = enemy.getWeakness();
            bool hitsWeakness = weakness != DamageType::NONE &&
                                (playedCard.getPhysType() == weakness || playedCard.getPhysType2() == weakness
                                 || playedCard.getElemType() == weakness);
            DamageType resistance = enemy.getResistance();
            bool hitsResistance = !trueStrike && resistance != DamageType::NONE &&
                                  (playedCard.getPhysType() == resistance || playedCard.getPhysType2() == resistance
                                   || playedCard.getElemType() == resistance);

            // A parry stance ripostes after the blow. The Revenant's lets the blow
            // land whole; the true form's copy of your Parry turns half of it.
            bool revenantParried = enemyParryStance && !trueStrike;
            if (enemyParryStance && !trueStrike) {
                enemyParryStance = false;
                std::cout << "  " << Color::MAGENTA
                          << (enemy.isBoss() ? "It parries, catching your blow!"
                                             : "The Revenant parries, but your blow still lands!")
                          << Color::RESET << "\n";
            }
            double parryFactor = (revenantParried && enemy.isBoss()) ? 0.5 : 1.0;
            // What the blow was worth, totalled over every hit of the card.
            // The riposte is measured in this rather than in its own attack.
            int parriedDamage = 0;

            for (int hitNum = 1; hitNum <= hits && (lichAddAlive || enemy.isAlive()); hitNum++) {
                // All In throws your guard rather than the card's own number, and
                // it deliberately skips the weapon percentage: the armour it spends
                // already carries the armour percentage.
                int bonusDamage  = (eff == CardEffect::ALLIN)
                                 ? playerArmor * 2
                                 : atkWithGear(playedCard.getValue());
                bonusDamage = (int)(bonusDamage * weakMult * strengthMult);
                if (hitsWeakness) bonusDamage = (int)(bonusDamage * 1.5);
                if (hitsResistance) bonusDamage = (int)(bonusDamage * 0.5);
                // The true form's Berserk: it threw its guard away to hit harder.
                if (enemyVulnerableTurns > 0) bonusDamage = bonusDamage * 3 / 2;
                bonusDamage = (int)(bonusDamage * parryFactor);
                const bool reflectionHit = hitNum > ownHits;

                std::string hitLabel = reflectionHit ? "Its reflection strikes too: dealt "
                                     : doubleHit ? ("Hit " + std::to_string(hitNum) + ": dealt ") : "Dealt ";
                auto printTags = [&]() {
                    if (hitsWeakness)      std::cout << " " << Color::YELLOW << "[Weakness! x1.5]" << Color::RESET;
                    if (hitsResistance)    std::cout << " " << Color::DIM << "[Resisted x0.5]" << Color::RESET;
                    if (weakMult < 1.0)    std::cout << " " << Color::WEAK_CLR << "[Weakened]" << Color::RESET;
                    if (strengthMult > 1.0)std::cout << " " << Color::STRENGTH_CLR << "[Strength x" << strengthMult << "]" << Color::RESET;
                    if (trueStrike)        std::cout << " " << Color::MAGENTA << "[Unstoppable]" << Color::RESET;
                    else if (pierce)       std::cout << " " << Color::MAGENTA << "[Armor-Piercing]" << Color::RESET;
                    if (parryFactor < 1.0) std::cout << " " << Color::MAGENTA << "[Parried x0.5]" << Color::RESET;
                    if (enemyVulnerableTurns > 0) std::cout << " " << Color::YELLOW << "[Exposed x1.5]" << Color::RESET;
                };

                if (lichAddAlive) {
                    // Whatever the Lich raised bodyguards it - the add soaks direct hits (no armor) until cut down.
                    int before = lichAddHp;
                    lichAddHp = std::max(0, lichAddHp - std::max(0, bonusDamage));
                    int lost = before - lichAddHp;
                    // On the skeleton, not on the Lich standing behind it.
                    EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), playedCard.getElemType(),
                                             lost > 0, /*onCompanion*/true);
                    EnemyArt::popNumberAdd(lost, EnemyArt::PopKind::DAMAGE);
                    Audio::playSFX(lichAddHp <= 0 ? "dead" : "attack");
                    std::cout << "  " << Color::PLAYER_ATTACK << hitLabel << lost << " damage to the summoned "
                              << lichAddName << "!"
                              << Color::RESET << " (" << lichAddName << " HP: " << lichAddHp
                              << "/" << lichAddMaxHp << ")";
                    printTags();
                    std::cout << "\n";
                    if (lichAddHp <= 0) { lichAddAlive = false; EnemyArt::setCompanion("");
                        std::cout << "  " << Color::MAGENTA << "The summoned " << lichAddName
                                  << " crumbles to dust!" << Color::RESET << "\n"; }
                } else if (enemyInvulnerable && !trueStrike) {
                    EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), playedCard.getElemType(), false);
                    std::cout << "  " << Color::DIM << "Your attack passes through the phased form. No damage!" << Color::RESET << "\n";
                } else {
                    int defenseValue = pierce ? 0 : enemy.getBaseDefense();
                    // The Steel Blade finds the gaps in a Pierce attack.
                    if (wornWeapon == 3 && (playedCard.getPhysType() == DamageType::PIERCE
                                            || playedCard.getPhysType2() == DamageType::PIERCE))
                        defenseValue = std::max(0, defenseValue - 2);
                    // The Shadow Blade slips past any guard.
                    if (wornWeapon == TIER_SHADOW) defenseValue = 0;
                    int damageDealt  = calculateDamage(bonusDamage, defenseValue);
                    // The true form's reversal: the blow comes back at you, and
                    // only a quarter of it reaches the knight.
                    if (enemyReflectNext && !trueStrike) {
                        enemyReflectNext = false;
                        const int quarter = damageDealt / 4;
                        enemy.takeDamage(quarter);
                        // Half the blow, and never more than 40% of your health, so one big swing
                        // into this cannot end the run on the spot.
                        const int turned = std::min(damageDealt / 2, maxPlayerHealth * 2 / 5);
                        const int back = std::max(0, turned - playerArmor);
                        playerArmor = std::max(0, playerArmor - turned);
                        playerHealth = std::max(0, playerHealth - back);
                        const bool held = trySecondWind();
                        EnemyArt::printBattleKnightHit(enemy.getType(), enemy.getBossType());
                        EnemyArt::popNumber(back, false, EnemyArt::PopKind::DAMAGE);
                        Audio::playSFXPitched("hit", 0.8f);
                        std::cout << "  " << Color::MAGENTA << "It sidesteps and turns your own blow on you: "
                                  << back << " damage. It takes " << quarter << "." << Color::RESET << "\n";
                        if (held)
                            std::cout << "  " << Color::BOLD << Color::YELLOW << "You stay on your feet at 1 HP."
                                      << Color::RESET << "\n";
                        continue;
                    }
                    int hpBefore = enemy.getHealth();
                    enemy.takeDamage(damageDealt);
                    int hpLost = hpBefore - enemy.getHealth();
                    if (revenantParried) parriedDamage += hpLost;
                    EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), playedCard.getElemType(), hpLost > 0);
                    EnemyArt::popNumber(hpLost > 0 ? hpLost : (damageDealt - hpLost), true,
                                        hpLost <= 0    ? EnemyArt::PopKind::BLOCKED
                                        : hitsWeakness ? EnemyArt::PopKind::WEAK_HIT
                                                       : EnemyArt::PopKind::DAMAGE);
                    int armorBlocked = damageDealt - hpLost;
                    Audio::playSFX(!enemy.isAlive() ? deathSfx(enemy.isBoss()) : "attack");
                    std::cout << "  " << Color::PLAYER_ATTACK << hitLabel << hpLost << " damage to enemy!"
                              << Color::RESET << " (Enemy HP: "
                              << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                              << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")";
                    printTags();
                    if (armorBlocked > 0)
                        std::cout << " " << Color::ARMOR_CLR << "[" << armorBlocked << " blocked by armor]" << Color::RESET;
                    std::cout << "\n";

                    if (hpLost > 0) onPlayerHit(playedCard, hpLost);

                    // Under the pact every attack festers, element or not.
                    if (pactOfRuinActive && enemy.isAlive()) {
                        if (applyEnemyStatus(StatusType::BURN, 3))
                            std::cout << "  " << Color::BURN_CLR << "The pact sets the wound alight. Burn 3."
                                      << Color::RESET << "\n";
                        if (applyEnemyStatus(StatusType::REND, 2))
                            std::cout << "  " << Color::REND_CLR << "And it will not close. Rend 2."
                                      << Color::RESET << "\n";
                    }
                    // 10% chance per hit to also inflict the matching ailment: wind, poison
                    // and fire all behave the same way on hit.
                    const DamageType elem = playedCard.getElemType();
                    if (enemy.isAlive() && (elem == DamageType::POISON || elem == DamageType::FIRE
                                            || elem == DamageType::WIND)) {
                        std::random_device rd;
                        std::mt19937 gen(rd());
                        // One chance for every elemental attack, raised only by
                        // Attunement. Nothing skips this roll: a card that applied its
                        // element for free made the boon worthless.
                        if (std::uniform_int_distribution<>(1, 100)(gen) <= attunementChance()) {
                            // The value applied is 3, but the tick is not: poison pays
                            // half of it over six turns, burn half again over two. These
                            // numbers mirror StatusEffects::apply() and must move with it.
                            if (elem == DamageType::POISON) {
                                if (applyEnemyStatus(StatusType::POISON, 3)) {
                                    EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::POISON, true);
                                    std::cout << "  " << Color::POISON_CLR << "The venom seeps in! Poisoned for 2 dmg/turn." << Color::RESET << "\n";
                                }
                            } else if (elem == DamageType::FIRE) {
                                if (applyEnemyStatus(StatusType::BURN, 3)) {
                                    EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::BURN, true);
                                    std::cout << "  " << Color::BURN_CLR << "The flames catch! Burning for 4 dmg/turn." << Color::RESET << "\n";
                                }
                            } else {
                                if (applyEnemyStatus(StatusType::REND, 3)) {
                                    EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::REND, true);
                                    std::cout << "  " << Color::REND_CLR << "The cut runs deep! 3 damage each time it swings." << Color::RESET << "\n";
                                }
                            }
                        }
                    }
                }
                // let each hit's sound finish before the next one cuts in
                if (hitNum < hits) UIHelper::pause(300);
            }

            if (revenantParried && playerHealth > 0 && enemy.isAlive()) {
                std::cout << "  " << Color::MAGENTA << (enemy.isBoss() ? "The moon ripostes!" : "The Revenant ripostes!")
                          << Color::RESET << "\n";
                UIHelper::pause(150);
                // Your own blow, handed back whole. Half its own attack made
                // the riposte a rounding error against anything you would
                // actually parry, which made the stance not worth taking.
                enemyStrikePlayer(std::max(1, parriedDamage), false, enemy.getWeakMultiplier());
            }

            // What the attack drawback cards charge, once the swing has landed.
            switch (eff) {
                case CardEffect::RECKLESS:
                    // Two overswings in a turn cost four, not two. Refreshing
                    // instead of adding made the second one free.
                    pendingDamagePenalty += 2;
                    std::cout << "  " << Color::WEAK_CLR << "You overswing. Your cards deal "
                              << pendingDamagePenalty << " less next turn." << Color::RESET << "\n";
                    break;
                case CardEffect::OVEREXTEND:
                    nextHandPenalty = std::max(nextHandPenalty, 1);
                    std::cout << "  " << Color::WEAK_CLR << "You are out of position. One fewer card next turn."
                              << Color::RESET << "\n";
                    break;
                case CardEffect::WILDCHARGE:
                    if (playerArmor > 0) {
                        playerArmor = 0;
                        armorBroken = true;
                        std::cout << "  " << Color::WEAK_CLR << "You charge in open. Your armor is gone."
                                  << Color::RESET << "\n";
                    }
                    break;
                case CardEffect::EMBERBLADE:
                    // Nothing lands on a phased enemy: the blade passed through it.
                    if (enemyInvulnerable) break;
                    if (enemy.isAlive() && applyEnemyStatus(StatusType::BURN, 4)) {
                        EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(),
                                                         EnemyArt::CastGlow::BURN, true);
                        std::cout << "  " << Color::BURN_CLR << "The blade sets them alight! Burn 4."
                                  << Color::RESET << "\n";
                    }
                    playerStatus.apply(StatusType::BURN, 2);
                    std::cout << "  " << Color::BURN_CLR << "The flames lick back at you. Burn 2."
                              << Color::RESET << "\n";
                    break;
                case CardEffect::SHATTERPOINT:
                    cardLimitThisTurn = cardsPlayedThisTurn + 1;
                    std::cout << "  " << Color::WEAK_CLR << "The blow costs you your footing. One more card this turn."
                              << Color::RESET << "\n";
                    break;
                case CardEffect::ALLIN:
                    playerArmor = 0;
                    armorBroken = true;
                    playerStatus.apply(StatusType::WEAK, 3, 1.5);
                    std::cout << "  " << Color::WEAK_CLR << "Everything you had, spent. You are Weakened and unguarded."
                              << Color::RESET << "\n";
                    break;
                case CardEffect::PACTRUIN:
                    pactOfRuinActive = true;
                    noHealThisEncounter = true;
                    std::cout << "  " << Color::BOLD << Color::MAGENTA
                              << "The pact takes hold. Your attacks fester, your wounds will not close, "
                              << "and every card costs blood." << Color::RESET << "\n";
                    break;
                default: break;
            }

            if (playedCard.getEffect() == CardEffect::STRENGTH) {
                // Bloodlust's path. Same ladder as the SPECIAL buff cards.
                EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::STRENGTH);
                double strengthBuff = playedCard.strengthMultiplier();
                playerStatus.apply(StatusType::STRENGTH, 2, 1.5, strengthBuff);
                std::cout << "  " << Color::STRENGTH_CLR << "Strength surges! x" << strengthBuff << " damage for 2 turns!" << Color::RESET << "\n";
                // Bloodlust burns out: the crash is booked now and lands when the
                // fury runs out, so the card is a trade rather than a free buff.
                if (playedCard.isLegendary()) {
                    bloodlustCrashPending = 2;
                    std::cout << "  " << Color::DIM << "It will cost you when the fury fades."
                              << Color::RESET << "\n";
                }
            }
        } else if (playedCard.getType() == CardType::DEFEND) {
            int bonusArmor = defWithGear(playedCard.getValue());
            playerArmor += bonusArmor;
            // Shield Bash hits as well as guards, so it plays the lunge in its own
            // block below rather than bracing in place and striking from afar.
            if (playedCard.getEffect() != CardEffect::CHIP)
                EnemyArt::printBattleBlock(enemy.getType(), enemy.getBossType());
            Audio::playSFX("defend");
            std::cout << "  " << Color::ARMOR_CLR << "Gained " << bonusArmor << " armor!"
                      << Color::RESET << " (Total: " << Color::ARMOR_CLR << playerArmor << Color::RESET << ")\n";

            if (playedCard.getEffect() == CardEffect::FORTIFY) {
                playerArmorPersistTurns = 3;
                std::cout << "  " << Color::CYAN << "Fortified! This armor won't fade for 3 turns (until broken)." << Color::RESET << "\n";
            }
            if (playedCard.getEffect() == CardEffect::IMPAIR) {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> coinFlip(0, 1);
                bool triggered = coinFlip(gen) == 0;
                if (triggered && applyEnemyStatus(StatusType::WEAK, 2)) {
                    EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::WEAK, true);
                    std::cout << "  " << Color::WEAK_CLR << "The impact staggers the enemy! Weakened for 3 turns!" << Color::RESET << "\n";
                } else if (!triggered) {
                    std::cout << "  " << Color::DIM << "(No impair this time.)" << Color::RESET << "\n";
                }
            }
            if (playedCard.getEffect() == CardEffect::CHIP) {
                if (enemyInvulnerable) {
                    EnemyArt::printBattleShieldBash(enemy.getType(), enemy.getBossType(), false);
                    std::cout << "  " << Color::DIM << "The shield's edge passes through the phased form. No damage." << Color::RESET << "\n";
                } else {
                int chipDmg = 3;
                int hpBefore = enemy.getHealth();
                enemy.takeDamageRaw(chipDmg);
                int hpLost = hpBefore - enemy.getHealth();
                EnemyArt::printBattleShieldBash(enemy.getType(), enemy.getBossType(), hpLost > 0);
                Audio::playSFX(!enemy.isAlive() ? deathSfx(enemy.isBoss()) : "hit");
                std::cout << "  " << Color::PLAYER_ATTACK << "The shield's edge bites, dealing " << hpLost << " damage!"
                          << Color::RESET << " (Enemy HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                }
            }
            switch (playedCard.getEffect()) {
                case CardEffect::SCRAP:
                    playerHealth = std::max(0, playerHealth - 1);
                    std::cout << "  " << Color::DAMAGE << "The scrap edge nicks you for 1."
                              << Color::RESET << "\n";
                    break;
                case CardEffect::SELFWEAK:
                    // Same reasoning, with a floor under it: half your damage is
                    // as far as bracing can take you.
                    cardSoftenPct = std::min(50, cardSoftenPct + 10);
                    std::cout << "  " << Color::WEAK_CLR << "Braced heavy. Your attacks deal "
                              << cardSoftenPct << "% less this turn." << Color::RESET << "\n";
                    break;
                case CardEffect::TURTLE:
                    playerArmorPersistTurns = 3;
                    playerStatus.apply(StatusType::WEAK, 3, 1.5);
                    std::cout << "  " << Color::CYAN << "Dug in. The armor holds for 3 turns"
                              << Color::RESET << Color::WEAK_CLR << ", and you are Weakened while it does."
                              << Color::RESET << "\n";
                    break;
                case CardEffect::UNSTABLEWARD:
                    statusWardTurns = 5;
                    nextHandPenalty = std::max(nextHandPenalty, 2);
                    std::cout << "  " << Color::CYAN << "Warded for 5 turns"
                              << Color::RESET << Color::WEAK_CLR << ", but the working costs you two cards next turn."
                              << Color::RESET << "\n";
                    break;
                case CardEffect::LASTSTAND: {
                    // The armour is the wounds, whole, uncapped. The card's own value rides
                    // on top, so the forge still does something and the card is not dead at
                    // full health.
                    const int fromWounds = std::max(0, maxPlayerHealth - playerHealth)
                                         + playedCard.getValue();
                    playerArmor += fromWounds;
                    playerArmorPersistTurns = 3;
                    noHealThisEncounter = true;
                    std::cout << "  " << Color::ARMOR_CLR << "Your wounds harden: +" << fromWounds
                              << " armor for 3 turns." << Color::RESET << Color::WEAK_CLR
                              << " You cannot heal for the rest of this fight." << Color::RESET << "\n";
                    break;
                }
                default: break;
            }

            if (playedCard.getEffect() == CardEffect::WARD) {
                statusWardTurns = 2;
                std::cout << "  " << Color::CYAN << "Warded! The next ailment the enemy inflicts on you will be blocked." << Color::RESET << "\n";
            }
        } else if (playedCard.getType() == CardType::SPECIAL) {
            Audio::playSFX(effectSoundName(playedCard.getEffect()));
            applyCardEffect(playedCard);
        }
        if (playerEnergy > 0)
            std::cout << Color::DIM << "  (" << playerEnergy << " energy left)" << Color::RESET << "\n";
        refreshBattleAuras();
        triggerShadowKnightAmbush(); // no-op unless this fight is the Shadow Knight
        triggerAssassinAmbush();     // no-op unless the Assassin is armed this turn
    } catch (const std::out_of_range&) {
        std::cout << "Invalid card index!\n";
    }
}

// The player half of Rend: only the Shadow Knight can inflict it, and the
// wound opens when YOU swing, so it taxes an aggressive turn and costs
// nothing on a turn spent blocking.
void Game::tickPlayerRend() {
    int dmg = playerStatus.processRend();
    dmg = dmg * (100 + armourTypeMod(DamageType::WIND)) / 100;
    if (dmg <= 0) return;
    playerHealth = std::max(0, playerHealth - dmg);   // ignores armor, like the other DoTs
    std::cout << "  " << Color::REND_CLR << "Your own wound tears as you swing: "
              << dmg << " damage." << Color::RESET << "\n";
    refreshBattleAuras();
}

// Rend pays out when the enemy swings, not on the turn tick, so an enemy
// that attacks twice is torn twice and one that stalls never is. Returns
// true if the tear killed it, in which case the blow never lands.
bool Game::tickEnemyRend() {
    int dmg = enemy.processRend();
    if (dmg <= 0) return false;
    int before = enemy.getHealth();
    enemy.takeDamage(dmg);
    int lost = before - enemy.getHealth();
    EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(),
                                     EnemyArt::CastGlow::REND, true);
    std::cout << "  " << Color::REND_CLR << "The wound tears open as it swings: "
              << lost << " damage." << Color::RESET << "\n";
    refreshBattleAuras();
    return enemy.getHealth() <= 0;
}

// The projectile this enemy throws, looked up by name in the generated
// table so the art and its index come from one place. No entry falls back
// to the archetype.
static const ProjectileTable::Entry* projEntryFor(const std::string& n) {
    for (int i = 0; i < ProjectileTable::kByNameCount; i++)
        if (n.find(ProjectileTable::kByName[i].enemy) != std::string::npos)
            return &ProjectileTable::kByName[i];
    return nullptr;
}

// -1 means "fall back to the value derived from the sprite sheet".
int Game::enemyMuzzleX() const {
    const ProjectileTable::Entry* e = projEntryFor(enemy.getName());
    return e ? e->muzzleX : -1;
}
int Game::enemyMuzzleY() const {
    const ProjectileTable::Entry* e = projEntryFor(enemy.getName());
    return e ? e->muzzleY : -1;
}

int Game::enemyProjectile() const {
    const ProjectileTable::Entry* e = projEntryFor(enemy.getName());
    if (e) return e->frame;
    switch (enemy.getType()) {
        case EnemyType::RANGED: return ProjectileTable::GEN_RANGED;
        case EnemyType::CASTER: return ProjectileTable::GEN_CASTER;
        case EnemyType::BEAST:  return ProjectileTable::GEN_BEAST;
        case EnemyType::UNDEAD: return ProjectileTable::GEN_UNDEAD;
        default:                return ProjectileTable::GEN_MELEE;
    }
}

// The eye has one way of reaching you. Without this it fired its beam only on
// its own move and threw a little bolt across the sky on every shared ranged
// attack it rolled.
bool Game::enemyFiresBeam() const {
    return enemy.getName().find("Omneye") != std::string::npos;
}

bool Game::enemyRaisesFlames() const {
    return enemy.getName().find("Archon") != std::string::npos;
}

// The Wyvern and the Falcon dive in and pull away: they throw nothing, and
// Parry closes on empty air.
bool Game::enemyIsFlyer() const {
    const std::string n = enemy.getName();
    return n.find("Wyvern") != std::string::npos || n.find("Falcon") != std::string::npos;
}

// Archers shoot and casters cast; neither walks into sword range to do it.
bool Game::archetypeIsRanged() const {
    return enemy.getType() == EnemyType::RANGED || enemy.getType() == EnemyType::CASTER;
}

// One regular-enemy attack against the player: armour, Dodge Reversal and
// Parry, then damage. weakMult is read once per enemy turn, before Weak
// ticks. Bosses go through bossStrikesPlayer().
void Game::enemyStrikePlayer(int atk, bool pierceHalfArmor, double weakMult, bool ranged,
                             bool useAttackFrames, int projectile, bool closeIn,
                             bool fromCompanion, bool ignoreArmor) {
    // Weak scales it down, Strength scales it up - the mirror of what the
    // player's own two buffs do to their attacks.
    atk = (int)(atk * weakMult * enemy.getStrengthMultiplier());
    // Berserk Stance: you gave up your guard for the swing, and this is the bill.
    if (vulnerableTurns > 0) atk = (int)(atk * vulnerableMult);
    // What your armour makes of this kind of blow, and the Glass Moon's price.
    const DamageType blowType = enemyAttackType();
    const int typeMod = armourTypeMod(blowType);
    atk = atk * (100 + typeMod) / 100;
    if (hasRelic(Relic::GLASS_MOON)) atk = atk * 125 / 100;
    if (tickEnemyRend()) return;   // the tear finished it before the blow landed
    if (fromCompanion)
        EnemyArt::printCompanionAttack(enemy.getType(), enemy.getBossType());
    else if (enemyRaisesFlames())
        EnemyArt::printGroundFlames(enemy.getType(), enemy.getBossType(),
                                    ProjectileTable::FX_PILLAR);
    else
        EnemyArt::printBattleAttack(enemy.getType(), enemy.getBossType(), playerArmor > 0, ranged,
                                    useAttackFrames, projectile,
                                    enemyMuzzleX(), enemyMuzzleY(), closeIn);
    // Dodge Reversal fires before Parry when both are active (uncapped, higher priority)
    if (counterAttackActive) {
        counterAttackActive = false;
        if (counterWasLegendary) Audio::playSFX("legendary");
        int counterDmg = (int)((atk * 2 + counterBonusValue) * playerStatus.getStrengthMultiplier());
        int hpBefore = enemy.getHealth();
        enemy.takeDamage(counterDmg);
        int hpLost = hpBefore - enemy.getHealth();
        EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), DamageType::NONE, hpLost > 0);
        EnemyArt::popNumber(hpLost, true, EnemyArt::PopKind::DAMAGE);
        Audio::playSFX(!enemy.isAlive() ? deathSfx(enemy.isBoss()) : "attack");
        std::cout << Color::GREEN << "Dodge Reversal! You sidestep the attack and counter for " << hpLost << " damage!" << Color::RESET
                  << " (Enemy HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
        UIHelper::pause(200);
        return;
    }
    if (parryActive) {
        int parryCap = playerArmor + parryBonusValue * 3; // current armor + Parry's own bonus - stack armor first to parry bigger hits
        parryActive = false;
        if (atk <= parryCap) {
            // The same flag the sprite uses. A blow that never closed the distance -
            // an arrow, a spell, a thrown dagger - can be caught, but there is
            // nothing standing in front of you to hit back at.
            bool tooFarToRiposte = !enemy.isBoss() && ranged;
            if (tooFarToRiposte) {
                Audio::playSFX("special");
                if (enemyIsFlyer())
                    std::cout << Color::CYAN << "Parried!! But the creature flew away."
                              << Color::RESET << "\n";
                else
                    std::cout << Color::CYAN << "Parry! You block the shot. No damage taken, but they're too far away to riposte."
                              << Color::RESET << "\n";
                UIHelper::pause(300);
                return;
            }
            int riposteDmg = (int)((atk * 1.5 + parryBonusValue) * playerStatus.getStrengthMultiplier());
            int hpBefore = enemy.getHealth();
            enemy.takeDamage(riposteDmg); // ignores defense - takeDamage only accounts for armor
            int hpLost = hpBefore - enemy.getHealth();
            // Before the animation, so the cue lands with the blow.
            Audio::playSFX(hpLost > 0 ? "attack" : "special");
            EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), DamageType::NONE, hpLost > 0);
            EnemyArt::popNumber(hpLost, true, EnemyArt::PopKind::DAMAGE);
            bool stunned = tryStunEnemy();
            if (stunned && enemy.isAlive())
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::STUN, true);
            if (!enemy.isAlive()) Audio::playSFX(deathSfx(enemy.isBoss()));
            std::cout << Color::CYAN << "Parry! You deflect the blow. No damage taken. Riposte for " << hpLost
                      << " damage!" << (stunned ? " Enemy is stunned!" : " Enemy resists the stun!") << Color::RESET
                      << " (Enemy HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
            UIHelper::pause(300);
            return;
        } else {
            std::cout << Color::BOLD << Color::RED << "The blow is too powerful to parry! Your guard breaks!" << Color::RESET << "\n";
            UIHelper::pause(250);
        }
    }
    int effectiveArmor = ignoreArmor ? 0 : pierceHalfArmor ? (playerArmor / 2) : playerArmor;
    int actualDamage = atk - effectiveArmor;
    if (actualDamage < 0) actualDamage = 0;
    if (!ignoreArmor) playerArmor -= (pierceHalfArmor ? atk / 2 : atk);
    if (playerArmor < 0) playerArmor = 0;
    playerHealth -= actualDamage;
    if (playerHealth < 0) playerHealth = 0;
    const bool savedByThread = trySecondWind();
    if (actualDamage > 0) {
        EnemyArt::printBattleKnightHit(enemy.getType(), enemy.getBossType());
        EnemyArt::popNumber(actualDamage, false, EnemyArt::PopKind::DAMAGE);
    }
    Audio::playSFX("hit");
    std::cout << Color::DAMAGE << "Enemy attacks for " << actualDamage << " damage!" << Color::RESET;
    if (weakMult < 1.0)
        std::cout << " " << Color::WEAK_CLR << "[Weakened]" << Color::RESET;
    if (ignoreArmor) std::cout << " " << Color::MAGENTA << "[Ignores armor]" << Color::RESET;
    if (typeMod < 0) std::cout << " " << Color::GREEN << "[Resisted " << typeWord(blowType) << "]" << Color::RESET;
    if (typeMod > 0) std::cout << " " << Color::RED << "[Weak to " << typeWord(blowType) << "]" << Color::RESET;
    std::cout << "  HP: " << hpColor(playerHealth, maxPlayerHealth)
              << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
    if (savedByThread)
        std::cout << "  " << Color::BOLD << Color::YELLOW
                  << "The Red Thread holds. You stay on your feet at 1 HP." << Color::RESET << "\n";
    UIHelper::pause(200);
}

// Assassin only: after the player commits to a card, one random play this turn
// triggers a single free strike from the shadows. Armed once per player turn.
void Game::triggerAssassinAmbush() {
    if (!assassinAmbushArmed) return;
    if (!enemy.isAlive() || playerHealth <= 0) return;
    // ~45% per card played, so it usually lands once a turn without hitting every card.
    std::random_device rd; std::mt19937 gen(rd());
    if (std::uniform_int_distribution<>(0, 99)(gen) >= 45) return;
    assassinAmbushArmed = false;
    UIHelper::typeWrite(std::string("\n") + Color::BOLD + Color::RED + "The Assassin strikes from the shadows!" + Color::RESET + "\n");
    UIHelper::pause(200);
    // Thrown from the dark, like the Bandit's dagger: it never closes, so Parry
    // catches it without a riposte and the sprite holds its ground.
    enemyStrikePlayer(enemy.getBaseAttack() + enemy.getBonusAttack(), true,
                      enemy.getWeakMultiplier(), /*ranged*/true, /*useFrames*/true,
                      enemyProjectile());
    refreshBattleAuras();
}

// Re-arms per-turn enemy mechanics at the start of each player turn.
void Game::armPerTurnEnemyMechanics() {
    assassinAmbushArmed = enemy.isAlive() && enemy.getName().find("Assassin") != std::string::npos;
}

void Game::enemyTurn() {
    if (!enemy.isAlive()) return;

    refreshBattleAuras();

    // Armour from the enemy's own last turn goes now, so a Defend this turn
    // survives the player's whole next turn. The true form's holding guards
    // outlast it, the way yours do.
    if (enemyArmorHoldTurns > 0) enemyArmorHoldTurns--;
    else enemy.resetArmor();
    // Berserk's opening lasts until its turn comes round.
    enemyVulnerableTurns = 0;

    // Tick enemy status effects at the start of their turn. Poison/Burn are
    // elemental (Poison/Fire respectively), so they get the same weakness (+50%)
    // and resistance (-50%) treatment as a matching-tagged attack card would.
    int poisonDmg = enemy.processPoison();
    if (poisonDmg > 0) {
        bool poisonWeak   = enemy.getWeakness()   == DamageType::POISON;
        bool poisonResist = enemy.getResistance() == DamageType::POISON;
        if (poisonWeak)   poisonDmg = (int)(poisonDmg * 1.5);
        if (poisonResist) poisonDmg = (int)(poisonDmg * 0.5);
        enemy.takeDamageRaw(poisonDmg);
        std::cout << Color::POISON_CLR << "Poison:" << Color::RESET
                  << " enemy takes " << Color::PLAYER_ATTACK << poisonDmg << Color::RESET << " damage! ("
                  << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << " HP)";
        if (poisonWeak)   std::cout << " " << Color::YELLOW << "[Weakness! x1.5]" << Color::RESET;
        if (poisonResist) std::cout << " " << Color::DIM << "[Resisted x0.5]" << Color::RESET;
        std::cout << "\n";
        UIHelper::pause(250);
        if (!enemy.isAlive()) { Audio::playSFX(deathSfx(enemy.isBoss())); return; }
    }
    int burnDmg = enemy.processBurn();
    if (burnDmg > 0) {
        bool burnWeak   = enemy.getWeakness()   == DamageType::FIRE;
        bool burnResist = enemy.getResistance() == DamageType::FIRE;
        if (burnWeak)   burnDmg = (int)(burnDmg * 1.5);
        if (burnResist) burnDmg = (int)(burnDmg * 0.5);
        enemy.takeDamageRaw(burnDmg);
        std::cout << Color::BURN_CLR << "Burn:" << Color::RESET
                  << " enemy takes " << Color::PLAYER_ATTACK << burnDmg << Color::RESET << " damage! ("
                  << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << " HP)";
        if (burnWeak)   std::cout << " " << Color::YELLOW << "[Weakness! x1.5]" << Color::RESET;
        if (burnResist) std::cout << " " << Color::DIM << "[Resisted x0.5]" << Color::RESET;
        std::cout << "\n";
        UIHelper::pause(250);
        if (!enemy.isAlive()) { Audio::playSFX(deathSfx(enemy.isBoss())); return; }
    }
    if (enemy.processStun()) {
        UIHelper::typeWrite(std::string(Color::STUN_CLR) + "Enemy is STUNNED and loses their turn!" + Color::RESET + "\n");
        UIHelper::pause(400);
        return;
    }

    // Bosses have their own AI
    if (enemy.isBoss()) {
        bossAction();
        return;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> rollDist(0, 99);
    // Scholar's Lens drew this turn's rolls at the start of yours, and showed
    // what they pick. Use those, so what was shown is what happens.
    int roll = lensRoll >= 0 ? lensRoll : rollDist(gen);
    // One reroll when the roll lands in the same tenth as last turn's. Not a
    // ban on repeating - an enemy that has your number should be able to press
    // it - but three Howls in a row was not a fight, it was a wall.
    if (lensRoll < 0 && lastMoveRoll >= 0 && roll / 10 == lastMoveRoll / 10) roll = rollDist(gen);
    lastMoveRoll = roll;
    lensRoll = -1;

    // How often a feared enemy braces instead of taking its turn. Deliberately not
    // 100: a two-turn guaranteed skip would be far stronger than Taunt, which only
    // changes WHICH action happens, never whether one happens at all.
    const int FEAR_BRACE_CHANCE = 60;

    // Taunt: force this turn's roll into whichever bucket guarantees an Attack
    // action for this enemy type, rather than leaving it to chance.
    bool taunted = enemyTauntTurns > 0;
    if (taunted) {
        enemyTauntTurns--;
        switch (enemy.getType()) {
            case EnemyType::MELEE:  roll = 0;  break;
            case EnemyType::RANGED: roll = 0;  break;
            case EnemyType::TANK:   roll = 99; break;
            case EnemyType::CASTER: roll = 99; break;
            case EnemyType::BEAST:  roll = 0;  break;
            case EnemyType::UNDEAD: roll = 0;  break;
            default: break;
        }
    }

    // Apply WEAK penalty to attack, then tick it
    double weakMult = enemy.getWeakMultiplier();
    enemy.processWeak();

    bool volleyBroken = false;
    // doAttack takes its range from the archetype (RANGED and CASTER never
    // close); a melee enemy with a thrown move calls doAttackAt directly. A
    // lambda cannot default an argument that touches `this`.
    auto doAttackAt = [&](int atk, bool pierceHalfArmor, bool ranged, bool useFrames = true,
                          int projectile = -1, bool closeIn = false, bool ignoreArmor = false) {
        if (enemy.hasStun()) {
            if (!volleyBroken) {
                volleyBroken = true;
                std::cout << "  " << Color::CYAN
                          << "The stun lands mid-swing. The rest of the assault never comes."
                          << Color::RESET << "\n";
            }
            return;
        }
        if (enemyFiresBeam() && ranged && !closeIn) {
            // Drawn first and joined to the pupil, then the blow lands with
            // nothing else crossing the gap.
            EnemyArt::printEnemyBeam(enemy.getType(), enemy.getBossType(), ProjectileTable::FX_BEAM,
                                     enemyMuzzleX(), enemyMuzzleY());
            enemyStrikePlayer(atk, pierceHalfArmor, weakMult, ranged, /*useFrames*/false,
                              EnemyArt::Proj::NONE, closeIn, false, ignoreArmor);
            return;
        }
        enemyStrikePlayer(atk, pierceHalfArmor, weakMult, ranged, useFrames, projectile, closeIn,
                          false, ignoreArmor);
    };
    auto doAttack = [&](int atk, bool pierceHalfArmor) {
        // enemyProjectile(), not the default -1, which the art layer reads as
        // "nothing crosses the gap".
        doAttackAt(atk, pierceHalfArmor, archetypeIsRanged(), true, enemyProjectile());
    };

    auto doDefend = [&](int amt) {
        enemy.gainArmor(amt);
        // The player's own defend cue, pitched down: same action, other side.
        Audio::playSFXPitched("defend", 0.8f);
        bool fizzled = counterAttackActive || parryActive;
        counterAttackActive = false;
        parryActive = false;
        if (fizzled)
            std::cout << Color::ARMOR_CLR << "Enemy braces, +" << amt << " armor." << Color::RESET
                      << " " << Color::DIM << "(No attack. Your stance fizzles.)" << Color::RESET << "\n";
        else
            std::cout << Color::ARMOR_CLR << "Enemy braces, +" << amt << " armor (absorbs incoming damage)." << Color::RESET << "\n";
        UIHelper::pause(150);
    };

    EnemyType t = enemy.getType();
    // bonusAttack MUST be included: every +2 a regular enemy earns (a roar, a
    // scream, a frenzy) is spent here.
    int atk = enemy.getBaseAttack() + enemy.getBonusAttack();
    int def = enemy.getBaseDefense();

    // How often this enemy reaches for its own moves. The rest of the time it
    // runs its archetype template. Rolled separately from `roll`, which the few
    // enemies with two signature moves use to pick between them.
    const int SIGNATURE_CHANCE = signatureChanceFor(enemy.getName());
    const int sigRoll = lensSigRoll >= 0 ? lensSigRoll : rollDist(gen);
    lensSigRoll = -1;

    // Every move that inflicts something shows it crossing the field. proj
    // picks the art; -1 keeps the generic status orb.
    auto cast = [&](EnemyArt::CastGlow g, int proj = -1) {
        if (enemyFiresBeam()) {
            EnemyArt::printEnemyBeam(enemy.getType(), enemy.getBossType(), ProjectileTable::FX_BEAM,
                                     enemyMuzzleX(), enemyMuzzleY(),
                                     /*weakGlow*/g == EnemyArt::CastGlow::WEAK);
            return;
        }
        if (enemyRaisesFlames()) {
            EnemyArt::printGroundFlames(enemy.getType(), enemy.getBossType(),
                                        ProjectileTable::FX_PILLAR);
            return;
        }
        EnemyArt::printEnemyCast(enemy.getType(), enemy.getBossType(), g, proj, enemyMuzzleX(), enemyMuzzleY());
    };
    // For a status that RIDES an attack. The blow already crossed the field, so
    // firing a second bolt after it would read as two separate attacks; this just
    // washes the knight in the ailment colour.
    auto flash = [&](EnemyArt::CastGlow g) {
        EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), g, false);
    };
    // A status delivered by CONTACT rather than at range: the enemy closes the
    // gap the way an attack does, then the ailment lands. A chilling touch that
    // reached across the room without either sprite moving was the odd one out.
    auto touch = [&](EnemyArt::CastGlow g) {
        EnemyArt::printBattleAttack(enemy.getType(), enemy.getBossType(), playerArmor > 0,
                                    /*ranged*/false);
        EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), g, false);
    };
    // A dive: crosses the field like a brawler, throws nothing, and is away
    // again before Parry can answer it.
    auto dive = [&](int a) {
        doAttackAt(a, true, /*ranged*/true, /*useFrames*/true, EnemyArt::Proj::NONE, /*closeIn*/true);
    };
    auto themedGeneric = [&](const char* msg) {
        UIHelper::typeWrite(std::string(Color::MAGENTA) + msg + Color::RESET + "\n");
        UIHelper::pause(150);
    };
    auto applyEnemyStatusOnPlayerPoison = [&](int amt) {
        EnemyArt::printEnemyCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::POISON,
                                 enemyProjectile(), enemyMuzzleX(), enemyMuzzleY());
        applyPlayerStatus(StatusType::POISON, amt);
        Audio::playSFX("poison");
        std::cout << Color::POISON_CLR << "Grave rot sets in. Poison " << amt << "."
                  << Color::RESET << "\n";
        UIHelper::pause(150);
    };

    // Fear resolves here, once, as a flat chance to brace instead of acting:
    // unlike Taunt there is no defend bucket to force, and several enemies
    // have no defensive move at all.
    if (enemyFearTurns > 0 && enemyCanDefend()) {
        enemyFearTurns--;
        if (rollDist(gen) < FEAR_BRACE_CHANCE) {
            UIHelper::typeWrite(std::string(Color::CYAN)
                + "The enemy flinches back and throws up its guard." + Color::RESET + "\n");
            doDefend(def);
            return;
        }
    }

    // --- Archetype kits --------------------------------------------------
    // Three kinds of turn per archetype. MELEE and RANGED have no brace on
    // purpose: they are what Fear should fail against (see enemyCanDefend).
    auto archetypeTurn = [&](int r) {
        switch (enemy.getType()) {
            case EnemyType::MELEE:
                if (r < 60) { themedGeneric("It swings at you."); doAttack(atk, false); }
                else if (r < 85 && enemy.getBonusAttack() < 6) {
                    enemy.addBonusAttack(2);
                    std::cout << Color::RED << "It winds up, gaining +2 attack." << Color::RESET << "\n";
                    UIHelper::pause(150);
                } else { themedGeneric("It throws its weight into a heavy swing!"); doAttack(atk + 3, false); }
                break;
            case EnemyType::TANK:
                if (r < 45) doDefend(def + 2);
                else if (r < 85) { themedGeneric("It brings its weapon down hard!"); doAttack(atk + 2, false); }
                else {
                    // gainArmor, NOT doDefend: doDefend means "brace instead of attacking"
                    // and fizzles a pending Parry or Dodge Reversal, but this move braces and
                    // then swings.
                    enemy.gainArmor(def);
                    std::cout << Color::ARMOR_CLR << "It raises its guard (+" << def << " armor)."
                              << Color::RESET << "\n";
                    UIHelper::pause(150);
                    themedGeneric("It shoves forward behind its guard.");
                    doAttack(std::max(1, atk / 2), false);
                }
                break;
            case EnemyType::RANGED:
                // A flyer has no bow. It is RANGED so that Parry closes on empty
                // air as it climbs away, but a shot and a double shot were the
                // wrong moves entirely: it dives, rakes and beats its wings.
                if (enemyIsFlyer()) {
                    if (r < 60) { themedGeneric("It folds its wings and dives at you!"); dive(atk); }
                    else if (r < 85) {
                        // The gust itself crosses the field: wings, not a bolt.
                        cast(EnemyArt::CastGlow::WEAK, ProjectileTable::FX_WIND);
                        applyPlayerStatus(StatusType::WEAK, 2);
                        Audio::playSFXPitched("special", 0.85f);
                        std::cout << Color::WEAK_CLR << "Its wings beat the air into your face. You are Weakened." << Color::RESET << "\n";
                        UIHelper::pause(150);
                    } else {
                        themedGeneric("It rakes past twice, turning on a wingtip!");
                        dive(std::max(1, atk / 2));
                        if (playerHealth > 0) dive(std::max(1, atk / 2));
                    }
                    break;
                }
                if (r < 60) { themedGeneric("It looses a shot straight through your guard."); doAttack(atk, true); }
                else if (r < 85) {
                    cast(EnemyArt::CastGlow::WEAK, enemyProjectile());
                    applyPlayerStatus(StatusType::WEAK, 2);
                    Audio::playSFXPitched("special", 0.85f);
                    std::cout << Color::WEAK_CLR << "A shot clips your arm. You are Weakened." << Color::RESET << "\n";
                    UIHelper::pause(150);
                } else {
                    themedGeneric("It fires twice in quick succession!");
                    doAttack(std::max(1, atk / 2), true);
                    if (playerHealth > 0) doAttack(std::max(1, atk / 2), true);
                }
                break;
            case EnemyType::CASTER:
                if (r < 45) { themedGeneric("It hurls a bolt of raw force."); doAttack(atk, false); }
                else if (r < 75) {
                    cast(EnemyArt::CastGlow::WEAK, enemyProjectile());
                    applyPlayerStatus(StatusType::WEAK, 2);
                    Audio::playSFXPitched("special", 0.85f);
                    std::cout << Color::WEAK_CLR << "A hex settles over you. You are Weakened." << Color::RESET << "\n";
                    UIHelper::pause(150);
                } else if (enemy.getHealth() < enemy.getMaxHealth() / 2) {
                    int h = 6 + def;
                    enemy.heal(h);
                    std::cout << Color::HEAL << "It knits its wounds closed, healing " << h << " HP." << Color::RESET << "\n";
                    UIHelper::pause(150);
                } else doDefend(def);
                break;
            case EnemyType::BEAST:
                if (r < 55) { themedGeneric("It lunges at you with teeth and claws."); doAttack(atk, false); }
                else if (r < 85 && enemy.getBonusAttack() < 6) {
                    enemy.addBonusAttack(2);
                    std::cout << Color::RED << "It works itself into a frenzy, gaining +2 attack." << Color::RESET << "\n";
                    UIHelper::pause(150);
                } else doDefend(def);
                break;
            case EnemyType::UNDEAD:
                if (r < 50) { themedGeneric("It claws at you with dead hands."); doAttack(atk, false); }
                else if (r < 80) {
                    applyEnemyStatusOnPlayerPoison(3);
                } else {
                    int h = 5 + def;
                    enemy.heal(h);
                    themedGeneric("It drains the air around it and knits itself together.");
                    std::cout << Color::HEAL << "It heals " << h << " HP." << Color::RESET << "\n";
                    UIHelper::pause(150);
                }
                break;
            default:
                doAttack(atk, false);
                break;
        }
    };

    // The skeleton claws on every turn once it has stood for one, whichever way
    // its master spends its own turn.
    const bool skeletonActs = lichAddAlive;   // the dead it just raised waits a turn
    auto skeletonStrike = [&]() {
        if (!skeletonActs || !lichAddAlive || playerHealth <= 0 || !enemy.isAlive()) return;
        std::cout << Color::MAGENTA << "The summoned " << lichAddName << " strikes at you!"
                  << Color::RESET << "\n";
        UIHelper::pause(150);
        enemyStrikePlayer(lichAddAtk, false, 1.0, /*ranged*/false, /*useFrames*/true,
                          /*projectile*/-1, /*closeIn*/false, /*fromCompanion*/true);
    };

    // The signature fires on its own roll. Taunt skips the gate (every branch
    // attacks when taunted), and so does the Moonstruck: its three moves are
    // all it does, so it never falls into the shared archetype kit.
    const bool moonstruck = enemy.getName().find("Moonstruck") != std::string::npos;
    if (!taunted && !moonstruck && sigRoll >= SIGNATURE_CHANCE) { archetypeTurn(roll); skeletonStrike(); return; }

    // --- Signature moves ---------------------------------------------------
    // When the gate opens the move happens, so the odds shown are the odds you
    // get. A move that cannot be used right now hands the turn to the kit.
    auto nameHas = [&](const char* k){ return enemy.getName().find(k) != std::string::npos; };
    auto themed  = [&](const char* msg){ UIHelper::typeWrite(std::string(Color::MAGENTA) + msg + Color::RESET + "\n"); UIHelper::pause(150); };

    // MELEE
    if (nameHas("Goblin"))    { themed("Goblin jabs at you!"); doAttack(atk, false); return; }
    // Thrown, not swung: a MELEE enemy making a ranged attack. It holds its
    // ground and Parry catches the dagger without a riposte.
    if (nameHas("Bandit"))    { themed("Bandit hurls a dagger!"); doAttackAt(atk, false, /*ranged*/true, /*useFrames*/true, enemyProjectile()); return; }
    if (nameHas("Raider"))    { themed("Raider bashes with brute force!"); doAttack(atk, false); return; }
    if (nameHas("Warrior"))   { themed("Warrior lunges, piercing your guard!"); doAttack(atk, !taunted); return; }
    if (nameHas("Knight")) {
        if (taunted) { doAttack(atk, false); return; }
        enemy.gainArmor(def);
        std::cout << Color::ARMOR_CLR << "Knight raises its shield (+" << def << " armor), then bashes!" << Color::RESET << "\n";
        UIHelper::pause(150);
        doAttack(std::max(1, atk / 2), false);
        return;
    }
    if (nameHas("Berserker")) {
        // Two moves of its own, so this one still splits. Capped at +6.
        if (!taunted && roll < 45 && enemy.getBonusAttack() < 6) {
            enemy.addBonusAttack(2);
            std::cout << Color::RED << "Berserker roars, growing stronger! (+2 attack)" << Color::RESET << "\n";
            UIHelper::pause(200); return;
        }
        themed("Berserker swings in a frenzy!"); doAttack(atk, false); return;
    }
    if (nameHas("Gladiator")) { themed("Gladiator lands a brutal UPPERCUT!"); doAttack(atk + 3, true); return; }
    if (nameHas("Enforcer")) {
        themed("Enforcer unleashes a COMBO STRIKE!");
        doAttack(atk, false);
        if (playerHealth > 0 && enemy.isAlive()) doAttack(atk, false);
        return;
    }

    // TANK. Their braces live in the kit now, so the signature is the move itself.
    if (nameHas("Guardian"))  { themed("Guardian sweeps a WHIRLWIND!"); doAttack(atk, true); return; }
    if (nameHas("Barbarian")) {
        if (taunted) { doAttack(atk, false); return; }
        enemy.gainArmor(def + 6);
        std::cout << Color::ARMOR_CLR << "Barbarian hardens its IRON SKIN (+" << (def + 6) << " armor)!" << Color::RESET << "\n";
        UIHelper::pause(150);
        if (roll < 50) { flash(EnemyArt::CastGlow::WEAK); applyPlayerStatus(StatusType::WEAK, 2);
                         Audio::playSFXPitched("special", 0.85f); }
        return;
    }
    if (nameHas("Sentinel")) {
        if (taunted) { doAttack(atk, false); return; }
        enemy.gainArmor(def + 4);
        std::cout << Color::ARMOR_CLR << "Sentinel FORTIFIES (+" << (def + 4) << " armor)!" << Color::RESET << "\n";
        UIHelper::pause(150);
        return;
    }
    if (nameHas("Warden"))    { themed("Warden delivers a SMACKDOWN!"); doAttack(atk + 1, false); return; }
    if (nameHas("Paladin")) {
        // Two moves, split on the same roll the other two-move enemies use.
        // Judgement cannot be turtled and Absolution cannot be poisoned, so
        // the answer to it is to be quick, which nothing else up here asks.
        if (!taunted && roll < 40) {
            enemy.heal(20 + enemy.getBaseDefense());
            const bool cleansed = enemy.hasAnyStatus();
            enemy.clearStatuses();
            // The regular turn has no bossMend(): this is the self-buff glow
            // every other roster enemy uses when it mends itself.
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(),
                                          EnemyArt::SelfGlow::HEAL);
            Audio::playSFX("heal");
            themed("Paladin speaks an ABSOLUTION over itself.");
            std::cout << "  " << Color::HEAL << "It heals " << (20 + enemy.getBaseDefense()) << " HP"
                      << Color::RESET;
            if (cleansed)
                std::cout << Color::MAGENTA << ", and everything you put on it burns off" << Color::RESET;
            std::cout << ". (" << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
            UIHelper::pause(250);
            return;
        }
        paladinJudgements++;
        themed("Paladin passes JUDGEMENT on you!");
        // Through your armour entirely, as View Enemy says. Piercing half of it
        // let a big enough guard soak the whole judgement.
        doAttackAt(atk + 2 + 3 * paladinJudgements, false, archetypeIsRanged(), true,
                   enemyProjectile(), false, /*ignoreArmor*/true);
        return;
    }
    if (nameHas("Bastion")) {
        if (taunted) { doAttack(atk, false); return; }
        if (roll < 60) {
            enemy.gainArmor(def + 8);
            std::cout << Color::ARMOR_CLR << "Bastion raises an impenetrable wall (+" << (def + 8)
                      << " armor)!" << Color::RESET << "\n";
            UIHelper::pause(150);
        } else {
            // The challenge is the point of the armor: it stops you waiting
            // the wall out behind your own guard.
            playerAttackOnly = true;
            Audio::playSFXPitched("special", 0.85f);
            std::cout << Color::RED << "Bastion HAMMERS its shield and dares you to break it!" << Color::RESET
                      << " Next turn you can only play " << Color::CARD_ATTACK << "ATTACK" << Color::RESET << " cards.\n";
            UIHelper::pause(250);
        }
        return;
    }
    if (nameHas("Fortress")) {
        if (taunted) { doAttack(atk, false); return; }
        enemy.gainArmor(def);
        std::cout << Color::ARMOR_CLR << "Fortress braces (+" << def << " armor), then shield-bashes!" << Color::RESET << "\n";
        UIHelper::pause(150);
        doAttack(std::max(1, atk / 2), false);
        return;
    }
    if (nameHas("Orc"))       { themed("Orc hurls a crushing BODY SLAM!"); doAttack(atk + 2, false); return; }

    // CASTER. The big three still heal when badly hurt.
    const bool lowHp = enemy.getHealth() < enemy.getMaxHealth() / 3;
    if (nameHas("Sage")) {
        if (taunted) { doAttack(atk, false); return; }
        if (lowHp && roll < 40) { int h = 8 + def / 2; enemy.heal(h); std::cout << Color::HEAL << "Sage channels a healing light (+" << h << " HP)." << Color::RESET << "\n"; UIHelper::pause(200); }
        else { cast(EnemyArt::CastGlow::BURN, enemyProjectile()); applyPlayerStatus(StatusType::BURN, 5); Audio::playSFX("fire"); std::cout << Color::BURN_CLR << "Sage hurls a TORCH! Burn 5." << Color::RESET << "\n"; UIHelper::pause(250); }
        return;
    }
    if (nameHas("Archon")) {
        if (taunted) { doAttack(atk, false); return; }
        if (lowHp && roll < 35) { int h = 10 + def / 2; enemy.heal(h); std::cout << Color::HEAL << "Archon mends itself (+" << h << " HP)." << Color::RESET << "\n"; UIHelper::pause(200); }
        else {
            // Its own frames show fire climbing out of the floor around its feet.
            // Hellfire is that, spread across the arena, rising as it goes.
            EnemyArt::printGroundFlames(enemy.getType(), enemy.getBossType(),
                                        ProjectileTable::FX_PILLAR);
            applyPlayerStatus(StatusType::BURN, 12);
            Audio::playSFX("fire");
            std::cout << Color::BURN_CLR << "Archon calls down HELLFIRE! Burn 12." << Color::RESET << "\n";
            UIHelper::pause(250);
        }
        return;
    }
    if (nameHas("Spellmaster")) {
        if (taunted) { doAttack(atk, false); return; }
        if (lowHp && roll < 35) { int h = 10 + def / 2; enemy.heal(h); std::cout << Color::HEAL << "Spellmaster mends itself (+" << h << " HP)." << Color::RESET << "\n"; UIHelper::pause(200); }
        else { cast(EnemyArt::CastGlow::POISON, enemyProjectile()); applyPlayerStatus(StatusType::POISON, 14); Audio::playSFX("poison"); std::cout << Color::POISON_CLR << "Spellmaster spreads a VIRULENT PLAGUE! Poison 14." << Color::RESET << "\n"; UIHelper::pause(250); }
        return;
    }
    if (nameHas("Enchanter")) {
        if (taunted) { doAttack(atk, false); return; }
        nextHandPenalty = 2;
        Audio::playSFXPitched("special", 0.85f);
        // Relative, not "3 cards": the hand size moves with Endurance and upgrades.
        std::cout << Color::MAGENTA << "Enchanter TEMPTS you into hesitation." << Color::RESET
                  << " Your next hand is " << Color::CYAN << "2 cards smaller" << Color::RESET << ".\n";
        UIHelper::pause(250);
        return;
    }
    if (nameHas("Sorcerer")) {
        if (taunted) { doAttack(atk, false); return; }
        cast(EnemyArt::CastGlow::WEAK, enemyProjectile());
        applyPlayerStatus(StatusType::WEAK, 2);
        nextHandPenalty = std::max(nextHandPenalty, 1);
        Audio::playSFXPitched("special", 0.85f);
        std::cout << Color::BLUE << "Sorcerer hurls an ICE BLAST!" << Color::RESET
                  << " You are " << Color::WEAK_CLR << "Weakened 2" << Color::RESET
                  << " and your next hand loses a card.\n";
        UIHelper::pause(250);
        return;
    }
    if (nameHas("Vampire")) {
        if (taunted) { doAttack(atk, false); return; }
        std::cout << Color::MAGENTA << "Vampire sinks in a VAMPIRIC DRAIN!" << Color::RESET << "\n";
        UIHelper::pause(150);
        // A bite, not a spell: she closes in, so Parry can catch it, but her
        // attack frames show casting, so this swings without them.
        doAttackAt(10, false, /*ranged*/false, /*useFrames*/false);
        if (enemy.isAlive()) {
            enemy.heal(6); enemy.addBonusAttack(1);
            std::cout << Color::HEAL << "She drinks deep, mending herself (+6 HP) and growing stronger (+1 attack)." << Color::RESET << "\n";
        }
        if (playerHealth > 0) { flash(EnemyArt::CastGlow::WEAK); applyPlayerStatus(StatusType::WEAK, 2);
                                Audio::playSFXPitched("special", 0.85f); }
        UIHelper::pause(200);
        return;
    }
    if (nameHas("Mystic")) {
        if (taunted) { doAttack(atk, false); return; }
        if (enemyInvulnerable) { archetypeTurn(roll); return; }
        enemyInvulnerable = true;
        Audio::playSFXPitched("special", 0.85f);
        std::cout << Color::CYAN << "Mystic weaves an ILLUSION, splitting into fading copies." << Color::RESET
                  << " It takes no damage next turn.\n";
        UIHelper::pause(250);
        return;
    }

    // RANGED
    if (nameHas("Deadeye")) { themed("Deadeye lines up a DEAD SHOT!");
                              doAttackAt(atk, true, true, true, enemyProjectile()); return; }
    if (nameHas("Wyvern"))  { themed("Wyvern dives with a FLYING GNASH!");
                              doAttackAt(atk + 2, true, true, true, EnemyArt::Proj::NONE, /*closeIn*/true); return; }
    if (nameHas("Omneye")) {
        if (!taunted && roll < 30) {
            // The same ray it shoots with, washed blue: the eye has one way of
            // reaching you, and it was throwing an orb across the sky.
            EnemyArt::printEnemyBeam(enemy.getType(), enemy.getBossType(), ProjectileTable::FX_BEAM,
                                     enemyMuzzleX(), enemyMuzzleY(), /*weakGlow*/true);
            applyPlayerStatus(StatusType::WEAK, 2);
            Audio::playSFXPitched("special", 0.85f);
            std::cout << Color::WEAK_CLR << "Omneye's gaze unsettles you. Weakened 2." << Color::RESET << "\n";
            UIHelper::pause(200);
            return;
        }
        themed("Omneye fires a searing eye-beam!");
        // A beam, not a bolt: it stays joined to the pupil.
        EnemyArt::printEnemyBeam(enemy.getType(), enemy.getBossType(), ProjectileTable::FX_BEAM,
                                 enemyMuzzleX(), enemyMuzzleY());
        doAttackAt(atk + 2, true, true, /*useFrames*/false, EnemyArt::Proj::NONE); return;
    }
    if (nameHas("Falcon")) {
        // Nothing crosses the gap: it dives in and pulls out again, so Proj::NONE.
        themed("The falcon stoops and GOUGES on a howling gust!");
        doAttackAt(atk + 1, true, true, true, EnemyArt::Proj::NONE, /*closeIn*/true);
        return;
    }

    // BEAST
    if (nameHas("Wolf"))    { themed("Wolf lunges with a BITE!"); doAttack(atk, false); return; }
    if (nameHas("Spider"))  { if (taunted) { doAttack(atk, false); return; } cast(EnemyArt::CastGlow::WEAK, enemyProjectile()); applyPlayerStatus(StatusType::WEAK, 2); Audio::playSFXPitched("special", 0.85f); std::cout << Color::WEAK_CLR << "Spider snares you in a WEB TRAP! Weakened 2." << Color::RESET << "\n"; UIHelper::pause(200); return; }
    if (nameHas("Serpent")) { if (taunted) { doAttack(atk, false); return; } cast(EnemyArt::CastGlow::WEAK, enemyProjectile()); applyPlayerStatus(StatusType::WEAK, 3); Audio::playSFXPitched("special", 0.85f); std::cout << Color::WEAK_CLR << "Serpent ENTANGLES you! Weakened 3." << Color::RESET << "\n"; UIHelper::pause(200); return; }
    if (nameHas("Basilisk")) {
        if (taunted) { doAttack(atk, false); return; }
        if (curseTurnsLeft != 0) { archetypeTurn(roll); return; }
        // Its own purple breath, out of the mouth: the generic status orb sailed
        // across the sky with nothing to do with the creature below it.
        cast(EnemyArt::CastGlow::STUN, enemyProjectile());
        curseTurnsLeft = 7;
        Audio::playSFXPitched("special", 0.85f);
        std::cout << Color::BOLD << Color::MAGENTA << "Basilisk fixes you with a petrifying CURSE!" << Color::RESET << "\n"
                  << "  " << Color::RED << "Defeat it within 7 turns or turn to stone." << Color::RESET << "\n";
        UIHelper::pause(300);
        return;
    }
    if (nameHas("Cockatrice")) {
        // Reuses the Basilisk curse slot, so a countdown can never stack.
        if (taunted) { doAttack(atk, false); return; }
        if (curseTurnsLeft != 0) { archetypeTurn(roll); return; }
        themed("Cockatrice sinks in a PETRIFYING BITE!");
        doAttack(atk, false);
        if (playerHealth > 0) {
            curseTurnsLeft = 5;
            Audio::playSFXPitched("special", 0.85f);
            std::cout << "  " << Color::BOLD << Color::MAGENTA << "Stone creeps out from the wound!" << Color::RESET << "\n"
                      << "  " << Color::RED << "Defeat it within 5 turns or turn to stone." << Color::RESET << "\n";
            UIHelper::pause(300);
        }
        return;
    }
    if (nameHas("Manticore")) {
        themed("Manticore lunges with a TWIN MAW - both heads at once!");
        doAttack(atk, false);
        if (playerHealth > 0 && enemy.isAlive()) doAttack(atk, false);
        return;
    }
    if (nameHas("Fleshmass")) {
        themed("The Fleshmass lashes out with grasping tentacles!");
        int hpBefore = playerHealth;
        doAttack(atk, false);
        // BIND: a lash that draws blood coils on. Skips whiffs (dodged/parried/armor-eaten).
        if (playerHealth < hpBefore && playerHealth > 0) {
            fleshmassBindPending = true;
            Audio::playSFXPitched("special", 0.85f);
            std::cout << Color::BOLD << Color::MAGENTA << "Tentacles coil around you! BOUND:" << Color::RESET
                      << " you draw a single card next turn.\n";
            UIHelper::pause(250);
        }
        return;
    }

    // UNDEAD
    if (nameHas("Ghoul")) {
        if (taunted) { doAttack(atk, false); return; }
        themed("Ghoul CHOMPS down, feeding on you!");
        // It feeds on what it takes: a bite your armor eats whole feeds it nothing.
        int hpBefore = playerHealth;
        doAttack(atk, false);
        const int fed = playerHealth < hpBefore ? 8 : 0;
        if (enemy.isAlive() && fed > 0) { enemy.heal(fed); std::cout << Color::HEAL << "Ghoul heals " << fed << " HP from the bite." << Color::RESET << "\n"; }
        else if (enemy.isAlive()) { std::cout << "  " << Color::DIM << "It draws no blood, and feeds on nothing." << Color::RESET << "\n"; }
        if (playerHealth > 0) { flash(EnemyArt::CastGlow::POISON); applyPlayerStatus(StatusType::POISON, 3);
                                Audio::playSFX("poison"); }
        UIHelper::pause(200);
        return;
    }
    if (nameHas("Banshee")) {
        if (taunted) { doAttack(atk, false); return; }
        cast(EnemyArt::CastGlow::WEAK, enemyProjectile());
        applyPlayerStatus(StatusType::WEAK, 2);
        if (enemy.getBonusAttack() < 6) enemy.addBonusAttack(2);
        std::cout << Color::MAGENTA << "Banshee looses a WAILING SCREAM!" << Color::RESET
                  << " You are " << Color::WEAK_CLR << "Weakened 2" << Color::RESET
                  << " and she grows stronger (+2 attack).\n";
        UIHelper::pause(250);
        return;
    }
    if (nameHas("Specter") || nameHas("Wraith")) {
        if (taunted) { doAttack(atk, false); return; }
        if (enemyInvulnerable) { archetypeTurn(roll); return; }
        enemyInvulnerable = true;
        Audio::playSFXPitched("special", 0.85f);
        std::cout << Color::CYAN << "The spirit turns GHOSTLY, fading half out of sight." << Color::RESET
                  << " It takes no damage next turn.\n";
        UIHelper::pause(250);
        return;
    }
    if (nameHas("Revenant")) {
        if (taunted) { doAttack(atk, false); return; }
        enemyParryStance = true;
        // With a challenge attached, so the turn is a real decision.
        playerAttackOnly = true;
        Audio::playSFXPitched("special", 0.85f);
        std::cout << Color::CYAN << "Revenant raises a PARRY stance and dares you to swing." << Color::RESET
                  << " Next turn you can only play " << Color::CARD_ATTACK << "ATTACK" << Color::RESET << " cards.\n";
        UIHelper::pause(250);
        return;
    }
    // The secret fight. Moon Scent when calm, and while the scent lasts its
    // signature turns become the Maul, so its own moves never go quiet the way
    // they did when a lapsed-scent check dropped it to a plain attack.
    if (nameHas("Moonstruck")) {
        // Three moves in every shape: Moon Scent to work itself up, the Maul while
        // the scent lasts, and the one move the shape it copied taught it.
        const int z = std::max(0, std::min(4, moonZone));
        const bool calm = !enemy.hasStrength();
        // Every turn is one of these three: it works itself up, then comes at
        // you, and its own move stays the rarest of the three.
        const bool ownMove = !taunted && (calm ? roll >= 72 : roll >= 78);
        if (!taunted && calm && !ownMove && roll < 40) {
            enemy.applyStatus(StatusType::STRENGTH, 3, 1.5, 1.6);
            Audio::playSFXPitched("special", 0.85f);
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(),
                                          EnemyArt::SelfGlow::STRENGTH);
            std::cout << Color::BOLD << Color::MAGENTA << "The Moonstruck takes your MOON SCENT!"
                      << Color::RESET << " Its blows hit " << Color::STRENGTH_CLR << "x1.6"
                      << Color::RESET << " harder for 3 turns.\n";
            UIHelper::pause(300);
        } else if (ownMove) {
            themed((std::string("The Moonstruck uses ") + MOON_MOVES[z].name + "!").c_str());
            switch (z) {
                case 0:   // Grave Silk
                    cast(EnemyArt::CastGlow::POISON, enemyProjectile());
                    applyPlayerStatus(StatusType::WEAK, 2);
                    applyPlayerStatus(StatusType::POISON, 3);
                    Audio::playSFX("poison");
                    std::cout << Color::POISON_CLR << "Bone-white thread wraps you. Weakened 2, Poison 3."
                              << Color::RESET << "\n";
                    break;
                case 1: { // Lunar Mirage
                    cast(EnemyArt::CastGlow::WEAK, enemyProjectile());
                    nextHandPenalty = std::max(nextHandPenalty, 2);
                    const int guard = def + 6;
                    enemy.gainArmor(guard);
                    Audio::playSFXPitched("special", 0.8f);
                    std::cout << Color::MAGENTA << "The room doubles, then triples. You will draw 2 fewer cards,"
                              << " and it hides behind " << guard << " armor." << Color::RESET << "\n";
                    break;
                }
                case 2: { // Howl at the Red Moon
                    const int heal = std::max(1, enemy.getMaxHealth() / 10);
                    enemy.heal(heal);
                    if (enemy.getBonusAttack() < 6) enemy.addBonusAttack(2);
                    EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(),
                                                  EnemyArt::SelfGlow::STRENGTH);
                    Audio::playSFXPitched("special", 0.7f);
                    std::cout << Color::MAGENTA << "It howls at the red moon. +" << heal
                              << " HP, and its attack rises." << Color::RESET << "\n";
                    break;
                }
                case 3:   // Petrifying Gaze, the shape it copied being a cockatrice
                    cast(EnemyArt::CastGlow::STUN, enemyProjectile());
                    if (curseTurnsLeft != 0) {          // already ticking: it just mauls
                        themed("The Moonstruck drives its stone-hard beak in: MOONLIT MAUL!");
                        doAttack(atk + 3, false);
                        break;
                    }
                    curseTurnsLeft = 6;
                    Audio::playSFXPitched("special", 0.75f);
                    std::cout << Color::BOLD << Color::MAGENTA << "Its stare settles on you, and stone creeps up your legs!"
                              << Color::RESET << "\n  " << Color::RED
                              << "Kill it within 6 turns or turn to stone." << Color::RESET << "\n";
                    break;
                default: { // Eclipse Ward
                    const int guard = def * 2 + 8;
                    enemy.gainArmor(guard);
                    enemy.heal(8);
                    Audio::playSFXPitched("defend", 0.7f);
                    std::cout << Color::ARMOR_CLR << "The moon goes behind it. +" << guard
                              << " armor, and it heals 8." << Color::RESET << "\n";
                    break;
                }
            }
            UIHelper::pause(300);
        } else {
            static const char* MAUL[5] = {
                "The Moonstruck lashes out with bone and leg at once: MOONLIT MAUL!",
                "The Moonstruck strikes with a hand that was never there: MOONLIT MAUL!",
                "The Moonstruck tears into you with a MOONLIT MAUL!",
                "The Moonstruck drives its stone-hard beak in: MOONLIT MAUL!",
                "The Moonstruck brings a pale blade down on you: MOONLIT MAUL!",
            };
            themed(MAUL[z]);
            doAttack(atk + 3, false);
        }
        return;
    }
    if (nameHas("Lich")) {
        if (taunted) doAttack(atk, false);
        else if (lichAddAlive) archetypeTurn(roll);
        else {
            // Every undead on the roster except another Lich. A fat one you
            // have to chew through and a thin one that hurts are different
            // problems, and the Lich picks which one you get.
            struct Raised { const char* name; int hp; int atk; };
            static const Raised DEAD[] = {
                { "Skeleton", 24, 6 },   // the old one, still the middle of the range
                { "Ghoul",    30, 5 },
                { "Wraith",   18, 8 },
                { "Specter",  16, 7 },
                { "Banshee",  20, 7 },
                { "Revenant", 34, 5 },
            };
            const int n = (int)(sizeof(DEAD) / sizeof(DEAD[0]));
            const Raised& r = DEAD[rollDist(gen) % n];
            lichAddName  = r.name;
            lichAddMaxHp = r.hp; lichAddHp = r.hp; lichAddAtk = r.atk;
            lichAddAlive = true;
            EnemyArt::setCompanion(lichAddName);
            Audio::playSFXPitched("special", 0.85f);
            std::cout << Color::BOLD << Color::MAGENTA << "Lich RAISES a " << lichAddName
                      << " to fight at its side!" << Color::RESET
                      << " (" << lichAddName << " HP: " << lichAddHp << "/" << lichAddMaxHp << ")\n";
            UIHelper::pause(300);
        }
        skeletonStrike();
        return;
    }

    // Everything above is named; everything below is the fallback. TODO: the
    // Wizard, Skeleton and Archer still land here and are the flattest fights.
    switch (t) {
        case EnemyType::MELEE:
            if (roll < 70) doAttack(atk, false);
            else doDefend(def);
            break;
        case EnemyType::RANGED:
            if (roll < 60) {
                doAttack(atk, true); // pierce half armor
            } else if (roll < 80) {
                doDefend(std::max(1, def - 1));
            } else {
                cast(EnemyArt::CastGlow::WEAK, enemyProjectile());
                applyPlayerStatus(StatusType::WEAK, 2);
                Audio::playSFXPitched("special", 0.85f);
                std::cout << Color::WEAK_CLR << "Enemy fires a crippling shot! You are Weakened for 2 turns." << Color::RESET << "\n";
            }
            break;
        case EnemyType::TANK:
            if (roll < 65) doDefend(def); // was def+3, too much in early encounters
            else doAttack(std::max(1, atk - 2), false);
            break;
        case EnemyType::CASTER:
            if (enemy.getHealth() < enemy.getMaxHealth() / 3 && roll < 60) {
                int healAmt = 8 + (def / 2);
                enemy.heal(healAmt);
                std::cout << "Enemy casts heal and recovers " << healAmt << " HP! ("
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << ")\n";
            } else if (roll < 40) {
                cast(EnemyArt::CastGlow::POISON, enemyProjectile());
                applyPlayerStatus(StatusType::POISON, 3);
                Audio::playSFX("poison");
                std::cout << Color::POISON_CLR << "Enemy casts Poison Bolt! Poison 3: 2 damage a turn for 6 turns." << Color::RESET << "\n";
            } else if (roll < 60) {
                cast(EnemyArt::CastGlow::BURN, enemyProjectile());
                applyPlayerStatus(StatusType::BURN, 2);
                Audio::playSFX("fire");
                std::cout << Color::BURN_CLR << "Enemy casts Fireball! Burn 2: 3 damage a turn for 2 turns." << Color::RESET << "\n";
            } else {
                doAttack(atk + 1, false);
            }
            break;
        case EnemyType::BEAST:
            if (roll < 60) {
                doAttack(atk, false);
            } else if (roll < 85) {
                cast(EnemyArt::CastGlow::POISON, enemyProjectile());
                applyPlayerStatus(StatusType::POISON, 3);
                Audio::playSFX("poison");
                std::cout << Color::POISON_CLR << "Enemy sinks its fangs in with a venomous bite! You are poisoned for 3 stacks." << Color::RESET << "\n";
            } else {
                doDefend(std::max(1, def - 1));
            }
            break;
        case EnemyType::UNDEAD:
            if (roll < 75) {
                doAttack(atk, false);
            } else {
                touch(EnemyArt::CastGlow::WEAK);
                applyPlayerStatus(StatusType::WEAK, 2);
                Audio::playSFXPitched("special", 0.85f);
                std::cout << Color::WEAK_CLR << "Enemy's chilling touch saps your strength! Weakened for 2 turns." << Color::RESET << "\n";
            }
            break;
        default:
            doAttack(atk, false);
    }
}

void Game::payPactOfRuin() {
    if (!pactOfRuinActive) return;
    playerHealth = std::max(0, playerHealth - 6);
    std::cout << "  " << Color::DAMAGE << "The pact takes its 6 HP." << Color::RESET
              << " (" << playerHealth << "/" << maxPlayerHealth << ")\n";
    if (playerHealth <= 0) trySecondWind();
}

// Everything a drawback card took for the length of one fight comes back here.
void Game::endEncounterEffects() {
    enemyReflectNext = false;
    for (const Card& c : exhausted) playerDeck.addCard(c);
    exhausted.clear();
    if (maxHpDebt > 0) {
        maxPlayerHealth += maxHpDebt;
        maxHpDebt = 0;
    }
    pactOfRuinActive = false;
    noHealThisEncounter = false;
    cardDamagePenalty = pendingDamagePenalty = cardSoftenPct = 0;
    vulnerableTurns = 0; vulnerableMult = 1.0;
    energyDebt = 0; cardLimitThisTurn = 0;
    extraTurnsPending = 0; borrowedStunsPending = 0; bloodlustCrashPending = 0;
    enemyArmorHoldTurns = 0; enemyVulnerableTurns = 0;
    enemyNoHeal = false; enemyPactOfRuin = false;
    knightMoveDebt = 0; knightChainDepth = 0; knightSacrificeSpent = false;
}

void Game::resetArmor() {
    // Fortified armor lasts until its timer runs out or it's broken; the rest wipes each turn.
    // Enemy armor is handled separately (see enemyTurn()) - it needs to survive one turn
    // longer than this, or a Defend action would never actually block anything.
    if (playerArmorPersistTurns > 0) {
        playerArmorPersistTurns--;
    } else {
        playerArmor = 0;
    }
    // The warning belongs to the turn the card spent it on. Armour running out
    // at the end of a turn is ordinary and says nothing.
    armorBroken = false;
}

void Game::endPlayerTurn() {
    playerTurnActive = false;
    cardSoftenPct = 0;        // Heavy Guard only softens the turn it was played

    // Borrowed Time: another turn before the enemy gets one, and only then the
    // stun, so the free turn the enemy gets comes after the borrowed one.
    if (extraTurnsPending > 0) {
        extraTurnsPending--;
        if (borrowedStunsPending > 0) {
            borrowedStunsPending--;
            playerStatus.apply(StatusType::STUN, 1);
        }
        cardsPlayedThisTurn = 0; attacksPlayedThisTurn = 0;
        cardLimitThisTurn = 0;
        resetEnergy();
        playerDeck.resetDeck();
        const int again = std::max(1, BASE_HAND_SIZE + handSizeBonus + upgrades.getDrawBonus());
        for (int i = 0; i < again; ++i) { try { playerDeck.drawCard(); } catch (...) { break; } }
        playerTurnActive = true;
        UIHelper::typeWrite(std::string(Color::BOLD) + Color::CYAN
            + "Time folds back on itself. You move again." + Color::RESET + "\n");
        UIHelper::pause(250);
        return;
    }

    // Per-turn enemy debuffs/stances on the player expire as the turn they hit ends.
    playerAttackOnly = false;
    enemyInvulnerable = false;
    enemyParryStance = false;
    playerBoundTurn = false;
    EnemyArt::setEnemyGhost(false); // drop the fade before the enemy's own turn redraws
    int curseAtStart = curseTurnsLeft; // only tick down on turns the curse was already active (not the cast turn)

    // Loops instead of running once: if the player's new turn opens stunned, that
    // turn is skipped entirely (no hand shown, straight to another enemy turn) -
    // same as how a stunned enemy loses its turn in enemyTurn().
    bool playerStunned;
    do {
        UIHelper::typeWrite(std::string("\n") + Color::BOLD + "--- Enemy's Turn ---" + Color::RESET + "\n");
        UIHelper::pause(180);
        enemyTurn();
        // Some moves never touch these flags - announce a still-armed stance before clearing it.
        if (counterAttackActive) {
            counterAttackActive = false;
            std::cout << Color::DIM << "Your Dodge Reversal stance fizzles. The enemy never struck." << Color::RESET << "\n";
        }
        if (parryActive) {
            parryActive = false;
            std::cout << Color::DIM << "Your Parry stance fizzles. The enemy never struck." << Color::RESET << "\n";
        }
        resetArmor();
        turnNumber++;
        resetEnergy();

        // Tick player status effects (start of player's new turn).
        // Second wind is offered here too, not only in bossStrikesPlayer(): a
        // poison or burn tick at 1 HP would otherwise kill with the save unspent.
        int playerPoisonDmg = playerStatus.processPoison();
        playerPoisonDmg = playerPoisonDmg * (100 + armourTypeMod(DamageType::POISON)) / 100;
        if (playerPoisonDmg > 0) {
            playerHealth = std::max(0, playerHealth - playerPoisonDmg);
            std::cout << Color::POISON_CLR << "Poison:" << Color::RESET
                      << " you take " << Color::DAMAGE << playerPoisonDmg << Color::RESET
                      << " damage! (HP: " << hpColor(playerHealth, maxPlayerHealth)
                      << playerHealth << Color::RESET << ")\n";
            if (trySecondWind())
                std::cout << "  " << Color::BOLD << Color::YELLOW
                          << "You refuse to fall! Clinging to 1 HP, you survive the poison!"
                          << Color::RESET << "\n";
            UIHelper::pause(250);
        }
        int playerBurnDmg = playerStatus.processBurn();
        playerBurnDmg = playerBurnDmg * (100 + armourTypeMod(DamageType::FIRE)) / 100;
        if (playerBurnDmg > 0) {
            playerHealth = std::max(0, playerHealth - playerBurnDmg);
            std::cout << Color::BURN_CLR << "Burn:" << Color::RESET
                      << " you take " << Color::DAMAGE << playerBurnDmg << Color::RESET
                      << " damage! (HP: " << hpColor(playerHealth, maxPlayerHealth)
                      << playerHealth << Color::RESET << ")\n";
            if (trySecondWind())
                std::cout << "  " << Color::BOLD << Color::YELLOW
                          << "You refuse to fall! Clinging to 1 HP, you survive the flames!"
                          << Color::RESET << "\n";
            UIHelper::pause(250);
        }
        if (statusWardTurns > 0) statusWardTurns--;
        if (vulnerableTurns > 0 && --vulnerableTurns == 0) vulnerableMult = 1.0;
        // Reckless Swing's cut lands on the turn AFTER the swing, so it rolls
        // over here rather than at the moment it was played.
        cardDamagePenalty = pendingDamagePenalty;
        pendingDamagePenalty = 0;
        cardLimitThisTurn = 0;
        // WEAK/STRENGTH tick at end of player turn (after all attacks are resolved)
        const bool hadStrength = playerStatus.hasStrength();
        playerStatus.processWeak();
        playerStatus.processStrength();
        // Bloodlust: the crash arrives the moment the fury it granted runs out.
        if (bloodlustCrashPending > 0 && hadStrength && !playerStatus.hasStrength()) {
            bloodlustCrashPending = 0;
            playerStatus.apply(StatusType::WEAK, 3, 2.0);
            std::cout << "  " << Color::WEAK_CLR
                      << "The bloodlust drains out of you. Weakened." << Color::RESET << "\n";
        }

        // Discard remaining hand cards and draw a fresh hand for next turn
        // (Tempt/Ice Blast shrink the next hand via nextHandPenalty).
        playerDeck.resetDeck();
        int drawCount = std::max(1, BASE_HAND_SIZE + handSizeBonus
                                    + upgrades.getDrawBonus() - nextHandPenalty);
        // BOUND deals one card, not a full hand you may only spend one of.
        if (fleshmassBindPending) drawCount = 1;
        nextHandPenalty = 0;
        for (int i = 0; i < drawCount; ++i) {
            try { playerDeck.drawCard(); } catch (...) { break; }
        }

        // Basilisk curse countdown - a fight not finished in time is an automatic loss.
        if (curseAtStart > 0 && curseTurnsLeft > 0 && enemy.isAlive()) {
            curseTurnsLeft--;
            if (curseTurnsLeft == 0) {
                UIHelper::typeWrite(std::string("\n") + Color::BOLD + Color::MAGENTA + "The " + enemy.getName() + "'s curse takes hold. You turn to stone!" + Color::RESET + "\n");
                UIHelper::pause(500);
                playerHealth = 0;
            }
        }

        if (playerHealth <= 0 || !enemy.isAlive()) { playerTurnActive = true; return; }

        playerStunned = playerStatus.processStun();
        if (playerStunned) {
            UIHelper::typeWrite(std::string(Color::STUN_CLR) + "You are STUNNED and lose your turn!" + Color::RESET + "\n");
            UIHelper::pause(400);
        }
    } while (playerStunned);

    prepareShadowKnightMoves(); // no-op unless this fight is the Shadow Knight
    armPerTurnEnemyMechanics(); // re-arm the Assassin ambush for the new player turn
    rollLens();                 // Scholar's Lens: draw the enemy's next move now
    playerBoundTurn = fleshmassBindPending; // Fleshmass Bind lands on the turn after the lash
    fleshmassBindPending = false;
    if (playerBoundTurn && playerHealth > 0) {
        // It is still holding you, and holding hurts. A bind that only took
        // your cards read as the tentacles politely waiting.
        const int squeeze = std::max(2, (enemy.getBaseAttack() + enemy.getBonusAttack()) / 4);
        playerHealth = std::max(0, playerHealth - squeeze);
        EnemyArt::popNumber(squeeze, false, EnemyArt::PopKind::DAMAGE);
        std::cout << "  " << Color::MAGENTA << "The tentacles tighten: " << squeeze << " damage."
                  << Color::RESET << "\n";
        trySecondWind();
        UIHelper::pause(200);
    }
    cardsPlayedThisTurn = 0; attacksPlayedThisTurn = 0;
    playerTurnActive = true;
    UIHelper::waitForKey("  (press any key for your turn)");
    // handleInput() will clear and redraw the full state for the new turn
}

bool Game::checkGameOver() {
    if (playerHealth <= 0) {
        return true;
    }
    if (!enemy.isAlive()) {
        return true;
    }
    return false;
}

void Game::displayGameOver() {
    UIHelper::clearScreen();
    if (playerHealth <= 0) {
        EnemyArt::printBattleKnightDeath(enemy.getType(), enemy.getBossType());
        Audio::playSFX("lose");
        UIHelper::pause(300);
        UIHelper::printGameOverScreen(false, currentRun.getEncountersWon(), runStats.getTotalCardsCollected());
        // Dying to the thing on the peak is not the same as dying on the road,
        // because it is the one enemy that wanted the rest of him.
        const bool toTheMoon = enemy.isBoss() && enemy.getBossType() == BossType::SHADOW_KNIGHT;
        UIHelper::printCenteredWrapped(std::string(Color::DIM) + (toTheMoon
            ? "It kneels, takes what was left of you, and stands up wearing all of it. "
              "There is nothing of you it is missing now."
            : "Your road ends here, a long way short of whole. What falls is a shell with "
              "pieces of it still out there in the dark."), 64);
        std::cout << "\n";
    } else if (!enemy.isAlive()) {
        UIHelper::printGameOverScreen(true, currentRun.getEncountersWon(), runStats.getTotalCardsCollected());
        std::cout << "Enemy defeated! Onward to the next encounter!\n";
    }
}

bool Game::handleGameOverInput() {
    // Clear first: the picker draws over the console rather than replacing
    // it, so the run summary printed above would show through the buttons.
    UIHelper::clearScreen();
    // Two columns, matching the Rest site, the Continue/End run screen and the
    // upgrade list this leads into. Bare labels get centred instead, which made
    // the last three screens of a run each look laid out differently.
    std::vector<CardBar::Action> overActs{
        CardBar::Action{ "Play again", "keep your unlocks, carry one card into a fresh run", false },
        CardBar::Action{ "Quit",       "stop here and see your lifetime stats", false },
    };
    const std::string title = "Run over        "
        + std::to_string(currentRun.getEncountersWon()) + " encounters won        "
        + std::to_string(runStats.getTotalCardsCollected()) + " cards collected";
    return CardBar::pick(title, {}, overActs, 0) == 0;
}

// Called right before the deck resets to starters on a new run - lets the
// player rescue exactly one card (upgrades and all) from the run that just ended.
bool Game::selectCardToCarryOver(Card& outCard) {
    std::vector<Card> allCards = playerDeck.getAllCardsOrdered();
    if (allCards.empty()) return false;

    std::vector<CardBar::Card> widgets;
    for (const Card& c : allCards) widgets.push_back(toWidget(c, gearedValue(c, c.getValue())));
    std::vector<CardBar::Action> carryActs{ CardBar::Action{ "Leave them all behind", false } };

    int choice;
    while (true) {
        choice = CardBar::pick("Starting over. Pick one card to carry into your new run",
                               widgets, carryActs, 5);
        if (choice <= -2) {          // a card's "+" button
            int ci = -2 - choice;
            if (ci >= 0 && ci < (int)allCards.size())
                CardBar::showDetail(widgets[ci], allCards[ci].getDescription(),
                                    allCards[ci].getTypeString(), rarityWord(allCards[ci]),
                                    allCards[ci].getUpgradeCount());
            continue;
        }
        break;
    }
    if (choice < 0 || choice >= (int)allCards.size()) return false;

    outCard = allCards[choice];
    notice("You'll start your new run with " + outCard.getName() + ".");
    return true;
}

// Centred yes/no, drawn like every other choice in the game.
void Game::notice(const std::string& text) {
    UIHelper::clearScreen();
    UIHelper::padToCenter(3);
    UIHelper::printCenteredWrapped(text, 70);
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
    UIHelper::waitForKey("");
}

bool Game::confirm(const std::string& prompt) {
    std::vector<CardBar::Action> acts{ CardBar::Action{ "Yes", false },
                                       CardBar::Action{ "No",  false } };
    return CardBar::pick(prompt, {}, acts, 0) == 0;
}

void Game::syncHud() {
        Hud::State h;
        h.turn = turnNumber;
        h.energy = playerEnergy; h.maxEnergy = maxEnergy;
        // Nothing at all in the true form's phase: it is not encounter
        // fifty-one, and it is not a numbered fight. The counter going is
        // part of what says the road has run out.
        h.encounter = trueFormPhase ? std::string()
            : inSecretEncounter ? std::string("???")
            : currentRun.isBossEncounter() ? std::string("BOSS")
            : "Encounter " + std::to_string(currentRun.getCurrentEncounter());
        if (!trueFormPhase && runMode != Mode::NORMAL) h.encounter += std::string("   ") + modeName();
        if (!trueFormPhase && sealsBroken > 0) h.encounter += "   Vigils out " + std::to_string(sealsBroken);

        h.playerHp = playerHealth; h.playerMax = maxPlayerHealth;
        h.playerArmor = playerArmor;
        h.armorBroken = armorBroken;
        int totalDmg = upgrades.getDamageBonus();
        int totalArm = upgrades.getArmorBonus();
        // Shown as a permanent readout beside the bar, the way the enemy's ATK/DEF
        // are.
        h.playerAtk = totalDmg;
        h.playerDef = totalArm;
        h.playerAtkPct = weaponPct();
        h.playerDefPct = armorPct();
        // Colour is kept, not stripped: the panel renders the escapes, so an
        // ailment reads in its own colour exactly as it does in the log.
        std::string ptags;
        if (playerArmorPersistTurns > 0)
            ptags += std::string(Color::CYAN) + "[Fortified "
                   + std::to_string(playerArmorPersistTurns) + "] " + Color::RESET;
        // Otherwise the ward is invisible until something is blocked by it.
        if (vulnerableTurns > 0)
            ptags += std::string(" ") + Color::DAMAGE + "[Exposed x1.5]" + Color::RESET;
        if (cardDamagePenalty > 0)
            ptags += std::string(" ") + Color::WEAK_CLR + "[-"
                   + std::to_string(cardDamagePenalty) + " dmg]" + Color::RESET;
        if (cardSoftenPct > 0)
            ptags += std::string(" ") + Color::WEAK_CLR + "[-"
                   + std::to_string(cardSoftenPct) + "% dmg]" + Color::RESET;
        if (energyDebt > 0)
            ptags += std::string(" ") + Color::WEAK_CLR + "[-"
                   + std::to_string(energyDebt) + " energy next]" + Color::RESET;
        if (pactOfRuinActive)
            ptags += std::string(" ") + Color::MAGENTA + "[PACT]" + Color::RESET;
        else if (noHealThisEncounter)
            ptags += std::string(" ") + Color::MAGENTA + "[No healing]" + Color::RESET;
        if (statusWardTurns > 0)
            ptags += std::string(" ") + Color::CYAN + "[Guard:"
                   + std::to_string(statusWardTurns) + "t]" + Color::RESET;
        ptags += playerStatus.summary();
        h.playerTags = ptags;

        h.enemyName = enemy.getName();
        h.enemyHp = enemy.getHealth(); h.enemyMax = enemy.getMaxHealth();
        // Both readouts show what the enemy is working with right now: ATK includes
        // anything it has buffed itself by, DEF includes armor it has raised. The
        // ATK half was missing, so a wind-up visibly changed nothing.
        h.enemyAtk = enemy.getBaseAttack() + enemy.getBonusAttack();
        h.enemyDef = enemy.getBaseDefense() + enemy.getArmor();
        std::string etags = enemy.statusSummary();
        {
            const std::string intent = lensIntent();
            if (!intent.empty()) etags += std::string(" ") + Color::CYAN + "[Next: " + intent + "]" + Color::RESET;
        }
        if (enemyInvulnerable) etags += std::string(" ") + Color::CYAN + "[Phased: immune]" + Color::RESET;
        if (enemyReflectNext) etags += std::string(" ") + Color::MAGENTA + "[Reversal set]" + Color::RESET;
        if (enemyParryStance && enemy.isBoss()) etags += std::string(" ") + Color::MAGENTA + "[Parry stance]" + Color::RESET;
        // Taunt and Fear both change what it is about to do, so each gets a short
        // readout; this row already carries every status.
        if (enemyTauntTurns > 0)
            etags += std::string(" ") + Color::RED + "[Taunt:"
                   + std::to_string(enemyTauntTurns) + "t]" + Color::RESET;
        if (enemyFearTurns > 0)
            etags += std::string(" ") + Color::CYAN + "[Fear:"
                   + std::to_string(enemyFearTurns) + "t]" + Color::RESET;
        h.enemyTags = etags;

        if (curseTurnsLeft > 0)
            h.notice = "CURSED - turn to stone in " + std::to_string(curseTurnsLeft)
                     + (curseTurnsLeft == 1 ? " turn" : " turns")
                     + " unless the " + enemy.getName() + " falls";
        else if (playerAttackOnly) h.notice = "TAUNTED - attack cards only this turn";
        else if (playerBoundTurn)  h.notice = std::string("BOUND - a single card this turn")
                                            + (cardsPlayedThisTurn >= 1 ? " (spent)" : "");

        h.addActive = lichAddAlive;
        h.addName   = lichAddName;
        h.addHp     = lichAddHp;
        h.addMax    = lichAddMaxHp;
        Hud::set(h);
        Hud::setActive(true);
    }

void Game::handleInput() {
    if (!playerTurnActive) return;

    lastActionWasCardPlay = false;

    // The whole battle screen is redrawn every turn. Logging that redraw would
    // bury the actual events under repeated headers and card lists, so capture
    // stays off until the player has chosen and things start happening.
    Console::setHistoryCapture(false);
    // Deliberately NOT clearing: the text region is the combat log, and the
    // last thing that happened is what you want to see while choosing a card.
    refreshBattleAuras();
    EnemyArt::setEnemyGhost(enemyInvulnerable); // fade a phased enemy for the idle scene + idle ticks
    EnemyArt::printBattle(enemy.getType(), enemy.getBossType());

    syncHud();
    std::cout << "\n";

    // Per-enemy mechanic readouts. The Lich's skeleton is not among them:
    // it has a bar in the panel, so repeating it here was duplication.
    if (curseTurnsLeft > 0)
        std::cout << Color::BOLD << Color::MAGENTA << "CURSED" << Color::RESET
                  << "  Turn to stone in " << Color::RED << curseTurnsLeft << Color::RESET
                  << (curseTurnsLeft == 1 ? " turn" : " turns") << " unless the " << enemy.getName() << " falls!\n";
    if (playerAttackOnly)
        std::cout << Color::RED << "TAUNTED" << Color::RESET
                  << "  You may only play " << Color::CARD_ATTACK << "ATTACK" << Color::RESET << " cards this turn.\n";
    if (playerBoundTurn)
        std::cout << Color::BOLD << Color::MAGENTA << "BOUND" << Color::RESET
                  << "  Tentacles restrict you to one card this turn"
                  << (cardsPlayedThisTurn >= 1 ? " - it has been played." : ".") << "\n";

    
    int handCount = playerDeck.handSize();
    double weakMult = playerStatus.getWeakMultiplier();
    double strengthMult = playerStatus.getStrengthMultiplier();

    // Build hand lines + option index mapping for the side-by-side display
    std::vector<std::string> leftLines;
    std::vector<int>         optionIndices;
    std::vector<std::string> options;
    std::vector<bool>        disabled;
    std::vector<CardBar::Card> widgets;   // same hand, drawn as cards

    for (int i = 0; i < handCount; i++) {
        bool used       = playerDeck.isCardUsed(i);
        bool cantAfford = false, restricted = false;
        if (!used) {
            const Card& ci = playerDeck.getCardFromHand(i);
            cantAfford = effectiveCost(ci) > playerEnergy;
            restricted = playerAttackOnly && ci.getType() != CardType::ATTACK; // Revenant taunt
        }
        bool bound = !used && playerBoundTurn && cardsPlayedThisTurn >= 1; // Fleshmass Bind: one play spent

        std::string optLabel = "Select Card " + std::to_string(i + 1);
        if (used) optLabel += " (used)";
        else if (cantAfford) optLabel += " (no energy)";
        else if (bound) optLabel += " (bound)";
        else if (restricted) optLabel += " (taunted)";
        options.push_back(optLabel);
        disabled.push_back(used || cantAfford || restricted || bound);
        int optIdx = (int)options.size() - 1;

        if (used) {
            CardBar::Card w;
            w.name = "used";
            w.disabled = true;
            w.tint = Console::xterm256Public(8);   // grey: a spent card has no type
            w.nameColor = Console::xterm256Public(8);
            widgets.push_back(w);
            leftLines.push_back(std::string("  ") + Color::DIM + std::to_string(i + 1) + ". [USED]" + Color::RESET);
            optionIndices.push_back(optIdx);
            leftLines.push_back(""); optionIndices.push_back(-1);
        } else {
            const Card& c = playerDeck.getCardFromHand(i);
            // What it will actually land for: the printed value through gear, then
            // through Weak/Strength.
            int dispVal = liveValue(c);
            if (c.getType() == CardType::ATTACK)
                dispVal = (int)(std::max(0, dispVal) * weakMult * strengthMult);

            const char* typeColor = (c.getTypeString() == "ATTACK") ? Color::CARD_ATTACK
                                  : (c.getTypeString() == "DEFEND") ? Color::CARD_DEFEND
                                  : Color::CARD_SPECIAL;
            std::string valLabel = (c.getTypeString() == "ATTACK") ? "DMG"
                                 : (c.getTypeString() == "DEFEND") ? "ARM" : "STK";

            // Pad after the bracket so [ATTACK] lines up with [SPECIAL].
            std::string typeStr = c.getTypeString();
            std::string typeGap((size_t)(7 - (int)typeStr.size() + 1), ' ');
            // Tag sits right next to the name (fixed-width slot) rather than as a
            // trailing suffix - a trailing tag's presence/absence made each row a
            // different visible length, throwing off the right column's alignment.
            std::string namePad = c.getName();
            if (!c.getTypeTag().empty()) namePad += " " + std::string(Color::YELLOW) + c.getTypeTag() + Color::RESET;
            int nameVisLen = UIHelper::visibleLen(namePad);
            while (nameVisLen < 27) { namePad += ' '; nameVisLen++; }

            std::string mainLine =
                std::string("  ") + Color::DIM + std::to_string(i + 1) + "." + Color::RESET
                + " [" + typeColor + typeStr + Color::RESET + "]" + typeGap
                + Color::BOLD + rarityTint(c) + namePad + Color::RESET
                + " cost:" + Color::ENERGY_CLR + std::to_string(effectiveCost(c)) + Color::RESET
                + "  " + valLabel + ":" + Color::GREEN + std::to_string(dispVal) + Color::RESET;

            CardBar::Card w;
            w.name     = c.getName();
            w.effect   = cardFaceLine(c, dispVal, attunementChance());
            w.typeLabel = c.getTypeString();
            w.elemTag   = c.getTypeTag();   // [Smash] / [Pierce][Wind] / ...
            w.cost     = effectiveCost(c);
            w.risk     = c.hasDrawback();
            // The exact tints Colors.h prints with: 220 neon gold, 218 pale
            // pink, 153 sky blue, 120 pale green, white for starters. Picking
            // my own approximations lost the palette the game already had.
            w.nameColor = Console::xterm256Public(
                              c.isLegendary() ? 220
                            : c.isSuperRare() ? 218
                            : c.isRare()      ? 153
                            : c.isStarter()   ?   7
                                              : 120);
            w.disabled = cantAfford || restricted || bound;
            // Stripe = card type, in the same red / blue / magenta the text UI
            // uses for [ATTACK] / [DEFEND] / [SPECIAL].
            w.tint = Console::xterm256Public(
                         (c.getType() == CardType::ATTACK) ? 9
                       : (c.getType() == CardType::DEFEND) ? 12
                                                           : 13);
            widgets.push_back(w);

            std::string descLine = "     " + std::string(Color::DIM) + c.getDescription() + Color::RESET;

            leftLines.push_back(mainLine);    optionIndices.push_back(optIdx);
            leftLines.push_back(descLine);    optionIndices.push_back(-1);
            leftLines.push_back("");          optionIndices.push_back(-1);
        }
    }

    options.push_back("End Turn");    disabled.push_back(false);
    options.push_back("View Enemy");  disabled.push_back(false);
    options.push_back("View Player"); disabled.push_back(false);
    // "Status" retired: the combat panel shows HP, armor, energy and every
    // active ailment on both sides, live, which is all that screen listed.
    options.push_back("View Log");    disabled.push_back(Console::history().empty());

    std::cout << "\n";
    std::vector<CardBar::Action> acts;
    for (size_t i = (size_t)handCount; i < options.size(); i++)
        acts.push_back(CardBar::Action{ options[i], disabled[i] });

    // The text rows above still carry the full card detail; the widgets below
    // are what you actually pick from.
    // Hovering a card shows what it would take off the enemy.
    CardBar::setEnergy(playerEnergy, maxEnergy);
    CardBar::setHoverCallback([this](int i) {
        Hud::setPreview((i >= 0 && i < playerDeck.handSize())
                        ? previewDamage(playerDeck.getCardFromHand(i)) : 0);
    });
    int choice = CardBar::select(widgets, acts,
        [&]() {
            refreshBattleAuras();
            EnemyArt::animateBattleIdleAt(enemy.getType(), enemy.getBossType());
        });

    // Results below -1 are the "+" details button on card index (-2 - i).
    if (choice <= -2) {
        int ci = -2 - choice;
        if (ci >= 0 && ci < handCount && !playerDeck.isCardUsed(ci)) {
            const Card& c = playerDeck.getCardFromHand(ci);
            CardBar::showDetail(widgets[ci], c.getDescription(), c.getTypeString(),
                                // rarityWord(), not a second copy of the ladder: this one
                                // never checked isLegendary(), so Bloodlust (which carries
                                // both flags) read as SUPER RARE and Reckoning read as RARE.
                                rarityWord(c),
                                c.getUpgradeCount());
        }
        return;   // redraw the turn cleanly rather than resuming a stale layout
    }
    if (choice < 0) return;

    // Capture wraps only the branches where something actually happens, so
    // menus never pour their card lists into the log.
    if (choice < handCount) {
        Console::setHistoryCapture(true);
        playCardFromHand(choice + 1);
        if (checkGameOver()) { Console::setHistoryCapture(false); return; }
        UIHelper::pause(600);  // let the card result stay visible before redraw
        // A stun pending here was applied by what just happened (endPlayerTurn()
        // consumes the enemy's), so spending it costs the rest of this turn only.
        if (playerTurnActive && playerStatus.processStun()) {
            std::cout << "\n" << Color::STUN_CLR
                      << "[Stunned - the rest of your turn is lost]" << Color::RESET << "\n";
            UIHelper::pause(500);
            endPlayerTurn();
        } else if (playerEnergy <= 0 && playerTurnActive) {
            std::cout << "\n" << Color::DIM << "[No energy left - ending your turn automatically]" << Color::RESET << "\n";
            UIHelper::pause(400);
            endPlayerTurn();
        }
        Console::setHistoryCapture(false);
    } else if (choice == handCount) {
        Console::setHistoryCapture(true);
        endPlayerTurn();
        checkGameOver();
        Console::setHistoryCapture(false);
    } else if (choice == handCount + 1) {
        UIHelper::clearScreen();
        displayEnemyInfo();
        UIHelper::waitForKey();
    } else if (choice == handCount + 2) {
        UIHelper::clearScreen();
        displayPlayerInfo();
        UIHelper::waitForKey();
    } else {
        displayActionLog();
    }
}

Enemy Game::generateBossEnemy() {
    // x1.4 and +1: the mildest boss scaling that keeps all four area bosses
    // winnable, with encounter 20 still the tightest.
    int bossHealth  = currentRun.getEnemyHealth() * 14 / 10;
    int bossAttack  = currentRun.getEnemyAttack() + 1;
    int bossDefense = currentRun.getEnemyDefense();

    std::string name;
    EnemyType   etype;
    BossType    btype;

    switch (currentRun.getBossIndex()) {
        case 1:
            name  = "Vile Witch";
            etype = EnemyType::CASTER;
            btype = BossType::VILE_WITCH;
            break;
        case 2:
            name  = "Thunder Beast";
            etype = EnemyType::MELEE;
            btype = BossType::WARLORD;
            break;
        case 3:
            name  = "Hydra";
            etype = EnemyType::BEAST;
            btype = BossType::HYDRA;
            break;
        case 4:
            name  = "Undead Dragon";
            etype = EnemyType::RANGED;
            btype = BossType::DRAGON;
            break;
        case 5:
            name  = "Shadow Knight";
            etype = EnemyType::UNDEAD;
            btype = BossType::SHADOW_KNIGHT;
            break;
        default: // 0, and fallback
            name  = "Stone Colossus";
            etype = EnemyType::TANK;
            btype = BossType::STONE_COLOSSUS;
            break;
    }

    // The Shadow Knight is pinned to the player rather than the encounter curve.
    // Its threat is not its pool: it plays your own deck back at you, and that
    // already scales with how good your deck is.
    if (btype == BossType::SHADOW_KNIGHT) bossHealth = 200 + maxPlayerHealth;
    // The knight is always the knight here. Four seals and a met moon do not
    // change who walks in, only what gets up afterwards: see beginTrueForm().
    bossHealth = sealScaled(bossHealth, SEAL_HP_PCT);
    bossAttack = sealScaled(bossAttack, SEAL_ATK_PCT);

    int cycle = currentRun.getCycle();
    if (cycle == 1) name = "Ancient " + name;

    Enemy boss(name, bossHealth, bossAttack, bossDefense, etype);
    boss.setBossType(btype);
    if (btype == BossType::STONE_COLOSSUS) boss.gainArmor(10);
    return boss;
}

// Shared by bossAction() and the Shadow Knight's mirrored attacks (can't be a lambda - those resolve outside bossAction()).
void Game::bossStrikesPlayer(int damage, bool raw, bool closeIn, bool unstoppable) {
    double weakMult = enemy.getWeakMultiplier() * enemy.getStrengthMultiplier();
    if (vulnerableTurns > 0) damage = (int)(damage * vulnerableMult);
    // Bosses' blows have types too, and the Glass Moon charges here as well.
    damage = damage * (100 + armourTypeMod(enemyAttackType())) / 100;
    if (hasRelic(Relic::GLASS_MOON)) damage = damage * 125 / 100;
    if (tickEnemyRend()) return;   // the tear finished it before the blow landed
    // Bosses take their range from their archetype - the Undead Dragon is RANGED
    // and should breathe from where it stands rather than walking over first.
    EnemyArt::printBattleAttack(enemy.getType(), enemy.getBossType(), playerArmor > 0,
                                archetypeIsRanged(), /*useAttackFrames*/true,
                                /*projectile*/-1, enemyMuzzleX(), enemyMuzzleY(), closeIn);
    // Dodge Reversal fires before Parry when both are active (uncapped, higher priority).
    // An unstoppable blow (the true form's Truestrike) goes straight through
    // both, and leaves them standing for whatever comes next.
    if (counterAttackActive && !unstoppable) {
        counterAttackActive = false;
        if (counterWasLegendary) Audio::playSFX("legendary");
        int counterDmg = (int)((damage * 2 + counterBonusValue) * playerStatus.getStrengthMultiplier());
        int hpBefore = enemy.getHealth();
        enemy.takeDamage(counterDmg);
        int hpLost = hpBefore - enemy.getHealth();
        EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), DamageType::NONE, hpLost > 0);
        EnemyArt::popNumber(hpLost, true, EnemyArt::PopKind::DAMAGE);
        Audio::playSFX(!enemy.isAlive() ? deathSfx(enemy.isBoss()) : "attack");
        std::cout << Color::GREEN << "Dodge Reversal! You sidestep the boss's attack and counter for " << hpLost << " damage!" << Color::RESET
                  << " (Boss HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
        UIHelper::pause(200);
        return;
    }
    if (parryActive && !unstoppable) {
        int parryCap = playerArmor + parryBonusValue * 3; // current armor + Parry's own bonus - stack armor first to parry bigger hits
        parryActive = false;
        if (damage <= parryCap) {
            int riposteDmg = (int)((damage * 1.5 + parryBonusValue) * playerStatus.getStrengthMultiplier());
            int hpBefore = enemy.getHealth();
            enemy.takeDamage(riposteDmg); // ignores defense - takeDamage only accounts for armor
            int hpLost = hpBefore - enemy.getHealth();
            // Before the animation, so the cue lands with the blow.
            Audio::playSFX(hpLost > 0 ? "attack" : "special");
            EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), DamageType::NONE, hpLost > 0);
            EnemyArt::popNumber(hpLost, true, EnemyArt::PopKind::DAMAGE);
            bool stunned = tryStunEnemy();
            if (stunned && enemy.isAlive())
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::STUN, true);
            if (!enemy.isAlive()) Audio::playSFX(deathSfx(enemy.isBoss()));
            std::cout << Color::CYAN << "Parry! You deflect the blow. No damage taken. Riposte for " << hpLost
                      << " damage!" << (stunned ? " Boss is stunned!" : " Boss resists the stun!") << Color::RESET
                      << " (Boss HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
            UIHelper::pause(300);
            return;
        } else {
            std::cout << Color::BOLD << Color::RED << "The blow is too powerful to parry! Your guard breaks!" << Color::RESET << "\n";
            UIHelper::pause(250);
        }
    }
    if (raw || unstoppable) {
        playerHealth = std::max(0, playerHealth - damage);
        bool saved = trySecondWind();
        if (damage > 0) {
            EnemyArt::printBattleKnightHit(enemy.getType(), enemy.getBossType());
            EnemyArt::popNumber(damage, false, EnemyArt::PopKind::DAMAGE);
        }
        Audio::playSFX("boss_attack");
        std::cout << Color::BOLD << Color::DAMAGE << "  BOSS slams for " << damage
                  << " (ignores armor)!" << Color::RESET
                  << " HP: " << hpColor(playerHealth, maxPlayerHealth)
                  << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
        if (saved) {
            Audio::playSFX("special");
            std::cout << "  " << Color::BOLD << Color::YELLOW << "You refuse to fall! Clinging to 1 HP, you survive the killing blow!" << Color::RESET << "\n";
        }
    } else {
        int actual = std::max(0, damage - playerArmor);
        playerArmor = std::max(0, playerArmor - damage);
        playerHealth = std::max(0, playerHealth - actual);
        bool saved = trySecondWind();
        if (actual > 0) {
            EnemyArt::printBattleKnightHit(enemy.getType(), enemy.getBossType());
            EnemyArt::popNumber(actual, false, EnemyArt::PopKind::DAMAGE);
        }
        Audio::playSFX("boss_attack");
        std::cout << Color::BOLD << Color::DAMAGE << "  BOSS strikes for " << actual
                  << " damage!" << Color::RESET
                  << " HP: " << hpColor(playerHealth, maxPlayerHealth)
                  << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
        if (saved) {
            Audio::playSFX("special");
            std::cout << "  " << Color::BOLD << Color::YELLOW << "You refuse to fall! Clinging to 1 HP, you survive the killing blow!" << Color::RESET << "\n";
        }
    }
    if (weakMult < 1.0)
        std::cout << "  " << Color::WEAK_CLR << "[Weakened]" << Color::RESET << "\n";
    UIHelper::pause(200);
}

bool Game::trySecondWind() {
    if (playerHealth > 0) return false;
    if (bossSecondWindAvailable) {
        bossSecondWindAvailable = false;
        playerHealth = 1;
        return true;
    }
    // The Red Thread: once a run, any fight, any source.
    if (hasRelic(Relic::RED_THREAD) && !redThreadUsed) {
        redThreadUsed = true;
        playerHealth = 1;
        return true;
    }
    return false;
}

void Game::bossAction() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> rollDist(0, 99);
    int roll = rollDist(gen);

    // Taunt: force this turn's roll into whichever bucket guarantees an Attack
    // action for this boss type.
    if (enemyTauntTurns > 0) {
        enemyTauntTurns--;
        switch (enemy.getBossType()) {
            case BossType::STONE_COLOSSUS: roll = 99; break;
            case BossType::VILE_WITCH:     roll = 0;  break;
            case BossType::WARLORD:        roll = 50; break;
            case BossType::HYDRA:          roll = 80; break;
            case BossType::DRAGON:         roll = 80; break;
            case BossType::SHADOW_KNIGHT:  roll = -1; break; // plain strike
            default: break;
        }
    }

    double weakMult = enemy.getWeakMultiplier();
    enemy.processWeak(); // the one place this ticks - exactly once per round, regardless of boss type
    enemy.processStrength();
    int atk = (int)(std::max(0, enemy.getBaseAttack() + enemy.getBonusAttack()) * weakMult);

    // Every boss move that is not a plain attack shows something crossing the
    // field or landing on one of the two fighters.
    auto bossCast = [&](EnemyArt::CastGlow g, int proj = -1, int scalePct = 30) {
        EnemyArt::printEnemyCast(enemy.getType(), enemy.getBossType(), g, proj,
                                 enemyMuzzleX(), enemyMuzzleY(), scalePct);
    };
    auto bossTouch = [&](EnemyArt::CastGlow g) {
        EnemyArt::printBattleAttack(enemy.getType(), enemy.getBossType(), playerArmor > 0,
                                    /*ranged*/false);
        EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), g, false);
    };
    auto bossMend = [&]() {
        EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(),
                                      EnemyArt::SelfGlow::HEAL);
    };

    bool bossVolleyBroken = false;
    auto doAttack = [&](int damage, bool raw, bool closeIn = false) {
        if (enemy.hasStun()) {   // see the note on the regular doAttack
            if (!bossVolleyBroken) {
                bossVolleyBroken = true;
                std::cout << "  " << Color::CYAN
                          << "The stun lands mid-swing. The rest of the assault never comes."
                          << Color::RESET << "\n";
            }
            return;
        }
        bossStrikesPlayer(damage, raw, closeIn);
    };

    switch (enemy.getBossType()) {
        case BossType::STONE_COLOSSUS:
            if (roll < 15) {
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Stone Colossus uses EARTHQUAKE SLAM!" + Color::RESET + "\n");
                UIHelper::pause(300);
                doAttack(15, true);
            } else if (roll < 45) {
                EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(),
                                              EnemyArt::SelfGlow::STRENGTH);
                Audio::playSFXPitched("defend", 0.8f);
                enemy.gainArmor(8);
                std::cout << Color::MAGENTA << "Stone Colossus hardens!" << Color::RESET
                          << " +" << Color::ARMOR_CLR << 8 << Color::RESET
                          << " armor (" << enemy.getArmor() << " total)\n";
                UIHelper::pause(200);
            } else {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Stone Colossus strikes!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk + 4, false);
            }
            break;

        case BossType::VILE_WITCH:
            if (roll < 30) {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Vile Witch attacks!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
            } else if (roll < 70) {
                // Thrown from the staff head she holds high on the left.
                bossCast(EnemyArt::CastGlow::POISON, enemyProjectile());
                applyPlayerStatus(StatusType::POISON, 4);
                applyPlayerStatus(StatusType::BURN, 2);
                Audio::playSFX("poison");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Vile Witch casts PLAGUE!" + Color::RESET
                    + " You gain " + Color::POISON_CLR + "Poison 4" + Color::RESET
                    + " and " + Color::BURN_CLR + "Burn 2" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 85) {
                int healAmt = 20;
                // It drains YOU: the pull crosses the field, then she mends.
                bossCast(EnemyArt::CastGlow::WEAK, enemyProjectile());
                enemy.heal(healAmt);
                bossMend();
                std::cout << Color::MAGENTA << "Vile Witch siphons life, healing " << Color::HEAL
                          << healAmt << " HP!" << Color::RESET << " ("
                          << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                UIHelper::pause(250);
            } else {
                // Thrown larger than a bolt: the ground itself comes up.
                bossCast(EnemyArt::CastGlow::POISON, enemyProjectile(), 55);
                applyPlayerStatus(StatusType::POISON, 6);
                Audio::playSFX("poison");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Vile Witch casts TOXIC ERUPTION!" + Color::RESET
                    + " You gain " + Color::POISON_CLR + "Poison 6" + Color::RESET + "!\n");
                UIHelper::pause(350);
            }
            break;

        case BossType::WARLORD:
            if (roll < 12) {
                bossCast(EnemyArt::CastGlow::STUN);
                applyPlayerStatus(StatusType::STUN, 1);
                Audio::playSFXPitched("volt", 0.85f);
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Thunder Beast unleashes a THUNDERSTRIKE!" + Color::RESET
                    + " You are " + Color::STUN_CLR + "STUNNED" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 27) {
                // A roar is thrown at you, not cast: it works itself up and the
                // sound rolls over the knight.
                EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(),
                                              EnemyArt::SelfGlow::STRENGTH);
                applyPlayerStatus(StatusType::WEAK, 3);
                Audio::playSFXPitched("special", 0.85f);
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Thunder Beast roars a BATTLECRY!" + Color::RESET
                    + " You are " + Color::WEAK_CLR + "Weakened 3" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else {
                // Exclusive with the two above: a stun or a roar costs it the swing.
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Thunder Beast attacks!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
                if (enemy.getBonusAttack() < 10) {
                    enemy.addBonusAttack(1);
                    std::cout << Color::MAGENTA << "Thunder Beast grows stronger!" << Color::RESET
                              << " (total bonus +" << Color::RED << enemy.getBonusAttack() << Color::RESET << " attack)\n";
                    UIHelper::pause(200);
                }
            }
            break;

        case BossType::HYDRA:
            if (roll < 20) {
                const int healAmt = 18;
                enemy.heal(healAmt);
                bossMend();
                const bool grew = hydraHeads < HYDRA_HEADS_MAX;
                if (grew) hydraHeads++;
                std::cout << Color::MAGENTA
                          << (grew ? "Two grow back where one fell. The Hydra heals "
                                   : "The stumps knit shut. The Hydra heals ")
                          << Color::HEAL << healAmt << " HP" << Color::RESET;
                if (grew)
                    std::cout << Color::MAGENTA << ", and strikes with " << hydraHeads
                              << " heads from here." << Color::RESET;
                std::cout << " (" << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                UIHelper::pause(250);
            } else if (roll < 50) {
                // Fangs, not a spell: it closes, bites, and the venom follows.
                bossTouch(EnemyArt::CastGlow::POISON);
                applyPlayerStatus(StatusType::POISON, 5);
                Audio::playSFX("poison");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Hydra sinks its fangs in with a VENOMOUS BITE!" + Color::RESET
                    + " You gain " + Color::POISON_CLR + "Poison 5" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 75) {
                // One bite per head. Two is where it starts and five is where
                // it ends, so a fight that lets it mend four times is a very
                // different fight from one that does not.
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Hydra lashes out with "
                    + std::to_string(hydraHeads) + " HEADS AT ONCE!" + Color::RESET + "\n");
                UIHelper::pause(200);
                for (int h = 0; h < hydraHeads && enemy.isAlive() && playerHealth > 0; ++h)
                    doAttack(atk, false);
            } else {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Hydra bites!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
            }
            break;

        case BossType::DRAGON:
            if (roll < 20) {
                // A wall of flame out of the jaws, at well over a bolt's size.
                bossCast(EnemyArt::CastGlow::BURN, enemyProjectile(), 80);
                // Burn 8: four stacks was two ticks of chip damage at encounter 40.
                applyPlayerStatus(StatusType::BURN, 8);
                Audio::playSFX("fire");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Dragon unleashes FIRE BREATH!" + Color::RESET
                    + " You gain " + Color::BURN_CLR + "Burn 8" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 45) {
                // A gust, not a bolt: wind has its own art now.
                bossCast(EnemyArt::CastGlow::WEAK, ProjectileTable::FX_WIND, 70);
                applyPlayerStatus(StatusType::WEAK, 3);
                Audio::playSFXPitched("special", 0.85f);
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Dragon's WING BUFFET knocks you off balance!" + Color::RESET
                    + " You are " + Color::WEAK_CLR + "Weakened 3" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 70) {
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Dragon rakes with CLAW RAKE!" + Color::RESET + "\n");
                UIHelper::pause(200);
                // Claws mean closing, even though its breath is a ranged move.
                doAttack(atk + 5, true, /*closeIn*/true);
            } else {
                // It had two claw attacks and one of them was just "a direct
                // attack". A thing that died once and came back should leave
                // something behind when it bites.
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA
                    + "Dragon sinks a CURSED BITE into you!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false, /*closeIn*/true);
                if (playerHealth > 0) {
                    applyPlayerStatus(StatusType::REND, 3);
                    std::cout << "  " << Color::REND_CLR
                              << "The wound will not close: Rend 3, opening again each time it strikes."
                              << Color::RESET << "\n";
                }
                UIHelper::pause(250);
            }
            break;

        case BossType::SHADOW_KNIGHT: {
            // Leftover prepared moves play out here; Taunt forces one guaranteed strike instead.
            if (roll < 0) {
                // Taunted: it has to swing, and this is the swing.
                knightPreparedMoves.clear();
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Shadow Knight strikes!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(std::max(1, atk * 55 / 100), false);
                break;
            }
            // One for one: it answers each card as you play it, so its turn has
            // already happened. Emptying the queue here would give it an extra go.
            knightPreparedMoves.clear();
            UIHelper::typeWrite(std::string(Color::DIM)
                + "The shadow lowers your sword and waits." + Color::RESET + "\n");
            UIHelper::pause(200);
            break;
        }

        default:
            doAttack(atk, false);
    }
}

// Secretly picks up to 3 cards to mirror this round. No-op for every other enemy.
void Game::prepareShadowKnightMoves() {
    knightPreparedMoves.clear();
    if (!enemy.isBoss() || enemy.getBossType() != BossType::SHADOW_KNIGHT) return;

    std::vector<Card> deckCards = playerDeck.getAllCardsOrdered();
    // Sacrifice leaves the fight once the true form has played it.
    if (knightSacrificeSpent)
        deckCards.erase(std::remove_if(deckCards.begin(), deckCards.end(),
            [](const Card& c) { return c.getEffect() == CardEffect::SACRIFICE; }), deckCards.end());
    if (deckCards.empty()) return;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(deckCards.begin(), deckCards.end(), gen);
    // Whatever it borrowed from this round with Adrenaline and the rest is gone.
    const size_t owed = (size_t)std::max(0, std::min(3, knightMoveDebt));
    knightMoveDebt = 0;
    size_t count = std::min((size_t)3 - owed, deckCards.size());
    for (size_t i = 0; i < count; ++i) knightPreparedMoves.push_back(deckCards[i]);
}

// Reveals and plays one prepared move right after the player commits to a card.
void Game::triggerShadowKnightAmbush() {
    if (knightPreparedMoves.empty()) return;
    if (!enemy.isAlive() || playerHealth <= 0) return;
    if (!enemy.isBoss() || enemy.getBossType() != BossType::SHADOW_KNIGHT) return;
    if (enemyTauntTurns > 0) return; // taunted bosses get one guaranteed strike instead, in bossAction()
    if (enemy.hasStun()) return; // stunned enemies lose their whole turn, ambush included

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> pick(0, (int)knightPreparedMoves.size() - 1);
    int idx = pick(gen);
    Card mirrored = knightPreparedMoves[idx];
    knightPreparedMoves.erase(knightPreparedMoves.begin() + idx);

    UIHelper::typeWrite(std::string("\n") + Color::BOLD + Color::MAGENTA + "The Shadow Knight strikes back!" + Color::RESET + "\n");
    UIHelper::pause(200);
    executeShadowKnightMirror(mirrored);
    refreshBattleAuras();
}

// Plays out one mirrored card's effect against the player.
void Game::executeShadowKnightMirror(const Card& mirrored) {
    // Weakness and strength both come in here: bossStrikesPlayer() only shows
    // them. Nothing gave the knight strength until the true form learned your
    // Strengthen, Berserk and Blood Pact, so this is what makes those count.
    double weakMult = enemy.getWeakMultiplier() * enemy.getStrengthMultiplier();
    int atk = (int)(std::max(0, enemy.getBaseAttack() + enemy.getBonusAttack()) * weakMult);
    int v = std::max(1, mirrored.getValue());
    UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Shadow Knight mirrors your "
        + mirrored.getName() + "!" + Color::RESET + "\n");
    UIHelper::pause(300);

    // Pact of Ruin: every move it makes now costs it blood, as every card costs you.
    if (enemyPactOfRuin && enemy.getHealth() > 1) {
        const int blood = std::min(enemy.getHealth() - 1, std::max(6, enemy.getMaxHealth() * 2 / 100));
        enemy.takeDamageRaw(blood);
        std::cout << "  " << Color::DAMAGE << "The pact takes " << blood << " of its health." << Color::RESET
                  << " (" << hpColor(enemy.getHealth(), enemy.getMaxHealth()) << enemy.getHealth() << "/"
                  << enemy.getMaxHealth() << Color::RESET << ")\n";
    }

    // The true form plays the whole card. Anything it handles is done here;
    // what the knight already knew runs through the code below as before.
    const bool handled = trueFormPhase && trueFormMirror(mirrored, atk, v);
    if (handled) {
        // nothing more: trueFormMirror played it
    } else if (mirrored.getType() == CardType::ATTACK) {
        bool pierce = (mirrored.getEffect() == CardEffect::PIERCE);
        if (mirrored.getEffect() == CardEffect::DOUBLE_HIT) {
            bossStrikesPlayer(atk / 2 + v / 2, false);
            if (playerHealth > 0) bossStrikesPlayer(atk / 2 + v / 2, false);
        } else {
            bossStrikesPlayer(atk + v / 2, pierce);
        }
        switch (mirrored.getEffect()) {
            case CardEffect::POISON: applyPlayerStatus(StatusType::POISON, 3);
                std::cout << "  " << Color::POISON_CLR << "The shadow's blade drips venom, inflicting Poison 3!" << Color::RESET << "\n"; break;
            case CardEffect::BURN:   applyPlayerStatus(StatusType::BURN, 2);
                std::cout << "  " << Color::BURN_CLR << "The shadow's blade sears, inflicting Burn 2!" << Color::RESET << "\n"; break;
            case CardEffect::WEAK:   applyPlayerStatus(StatusType::WEAK, 2);
                std::cout << "  " << Color::WEAK_CLR << "The blow saps your strength, inflicting Weak 2!" << Color::RESET << "\n"; break;
            case CardEffect::STUN:   applyPlayerStatus(StatusType::STUN, 1);
                std::cout << "  " << Color::STUN_CLR << "The blow leaves you reeling! STUNNED!" << Color::RESET << "\n"; break;
            case CardEffect::STRENGTH:
                enemy.addBonusAttack(2);
                std::cout << "  " << Color::RED << "The shadow grows stronger! (+2 attack)" << Color::RESET << "\n"; break;
            default: break;
        }
    } else if (mirrored.getType() == CardType::DEFEND) {
        int armorGain = std::max(6, v);
        enemy.gainArmor(armorGain);
        std::cout << Color::MAGENTA << "The shadow raises your own guard against you!" << Color::RESET
                  << " +" << Color::ARMOR_CLR << armorGain << Color::RESET
                  << " armor (" << enemy.getArmor() << " total)\n";
        if (mirrored.getEffect() == CardEffect::CHIP) {
            UIHelper::pause(200);
            bossStrikesPlayer(v / 2 + 2, true);
        } else if (mirrored.getEffect() == CardEffect::IMPAIR) {
            applyPlayerStatus(StatusType::WEAK, 2);
            std::cout << "  " << Color::WEAK_CLR << "Its stance unsettles you, inflicting Weak 2!" << Color::RESET << "\n";
        } else if (mirrored.getEffect() == CardEffect::WARD) {
            enemyStatusWardActive = true;
            std::cout << "  " << Color::CYAN << "The shadow readies its own ward against your next ailment!" << Color::RESET << "\n";
        }
        UIHelper::pause(250);
    } else { // SPECIAL
        switch (mirrored.getEffect()) {
            case CardEffect::HEAL: {
                if (enemyNoHeal) {
                    std::cout << Color::DIM << "Its wounds refuse to close. The mending does nothing." << Color::RESET << "\n";
                    UIHelper::pause(200);
                    break;
                }
                // Your heal card mirrored back, run through the same floor rule against
                // ITS pool, then halved - the boss pool dwarfs yours and a full-strength
                // mirror undid two whole turns of damage.
                int healAmt = std::max(1, Card::healAmount(v, enemy.getHealth(),
                                                           enemy.getMaxHealth()) / 2);
                enemy.heal(healAmt);
                std::cout << Color::MAGENTA << "The shadow knits itself back together, healing " << Color::HEAL
                          << healAmt << " HP!" << Color::RESET << " ("
                          << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                UIHelper::pause(250);
                break;
            }
            case CardEffect::POISON:
                applyPlayerStatus(StatusType::POISON, v);
                Audio::playSFX("poison");
                std::cout << "  " << Color::POISON_CLR << "Shadow venom seeps in, inflicting Poison " << v << "!" << Color::RESET << "\n";
                UIHelper::pause(300);
                break;
            case CardEffect::BURN:
                applyPlayerStatus(StatusType::BURN, v);
                Audio::playSFX("fire");
                std::cout << "  " << Color::BURN_CLR << "Black flames catch, inflicting Burn " << v << "!" << Color::RESET << "\n";
                UIHelper::pause(300);
                break;
            case CardEffect::REND:
                applyPlayerStatus(StatusType::REND, v);
                std::cout << "  " << Color::REND_CLR << "The shadow opens a wound that tears when you strike: Rend "
                          << v << "!" << Color::RESET << "\n";
                UIHelper::pause(300);
                break;
            case CardEffect::WEAK:
                applyPlayerStatus(StatusType::WEAK, v);
                std::cout << "  " << Color::WEAK_CLR << "A creeping dread sets in, inflicting Weak " << v << "!" << Color::RESET << "\n";
                UIHelper::pause(300);
                break;
            case CardEffect::STUN:
                applyPlayerStatus(StatusType::STUN, 1);
                Audio::playSFX("volt");
                std::cout << "  " << Color::STUN_CLR << "Shadows bind you! STUNNED!" << Color::RESET << "\n";
                UIHelper::pause(300);
                break;
            default: // reactive cards (Dodge Reversal/Parry/Taunt)
                // The plain knight cannot hold a stance and lunges instead.
                // Its true form takes each one up for real.
                const CardEffect me = mirrored.getEffect();
                const bool stanceCard = me == CardEffect::COUNTER || me == CardEffect::PARRY
                                     || me == CardEffect::TAUNT;
                // Only a card that IS a stance becomes one. Everything unhandled
                // fell through to here, so the true form was answering a Blood
                // Pact with a reversal the player had never owned.
                if (stanceCard && enemy.getName().find("Moonstruck Shadow Knight") != std::string::npos) {
                    if (me == CardEffect::PARRY) {
                        enemyParryStance = true;
                        std::cout << Color::MAGENTA << "It takes your own parry stance. Your next blow will be caught"
                                  << " and answered." << Color::RESET << "\n";
                    } else if (me == CardEffect::TAUNT) {
                        playerAttackOnly = true;
                        std::cout << Color::MAGENTA << "It taunts you with your own taunt. Next turn you may only"
                                  << " attack." << Color::RESET << "\n";
                    } else {
                        enemyReflectNext = true;
                        std::cout << Color::MAGENTA << "It sets your own reversal. Your next blow will be turned"
                                  << " back on you." << Color::RESET << "\n";
                    }
                    Audio::playSFXPitched("special", 0.7f);
                    UIHelper::pause(300);
                    break;
                }
                std::cout << Color::MAGENTA << "The mirrored stance dissolves, and the shadow lunges!" << Color::RESET << "\n";
                UIHelper::pause(200);
                bossStrikesPlayer(atk, false);
                break;
        }
    }

    // Under the pact, every attack it makes festers, the way yours do.
    if (enemyPactOfRuin && mirrored.getType() == CardType::ATTACK && playerHealth > 0) {
        applyPlayerStatus(StatusType::BURN, 3);
        applyPlayerStatus(StatusType::REND, 2);
        std::cout << "  " << Color::BURN_CLR << "The wound festers: Burn 3 and Rend 2." << Color::RESET << "\n";
    }
}

// One more of its moves, straight away: a card it prepared if one is left, one
// of yours if not. Capped at two deep so a chain of them cannot run away.
void Game::knightExtraMove() {
    if (knightChainDepth >= 2 || !enemy.isAlive() || playerHealth <= 0) return;
    std::vector<Card> next;
    if (!knightPreparedMoves.empty()) {
        next.push_back(knightPreparedMoves.back());
        knightPreparedMoves.pop_back();
    } else {
        std::vector<Card> deck = playerDeck.getAllCardsOrdered();
        if (deck.empty()) return;
        static thread_local std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<> pick(0, (int)deck.size() - 1);
        next.push_back(deck[pick(gen)]);
    }
    ++knightChainDepth;
    UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "It moves again!" + Color::RESET + "\n");
    UIHelper::pause(150);
    executeShadowKnightMirror(next.front());
    --knightChainDepth;
}

// The cards the plain knight only half knew, played whole by the true form:
// what each one does and what it costs, the same bargain the card offers you.
// Returns false for anything the shared mirror already plays properly.
bool Game::trueFormMirror(const Card& mirrored, int atk, int v) {
    const CardEffect e = mirrored.getEffect();
    auto say = [](const std::string& colour, const std::string& text) {
        std::cout << "  " << colour << text << Color::RESET << "\n";
    };
    auto hp = [&]() {
        return std::string(" (") + hpColor(enemy.getHealth(), enemy.getMaxHealth())
             + std::to_string(enemy.getHealth()) + "/" + std::to_string(enemy.getMaxHealth())
             + Color::RESET + ")";
    };
    // A price paid in health never kills it: the card is a bargain, not a way out.
    auto pay = [&](int amount) {
        const int paid = std::max(0, std::min(enemy.getHealth() - 1, amount));
        enemy.takeDamageRaw(paid);
        return paid;
    };
    const int guard = std::max(6, v);

    if (mirrored.getType() == CardType::ATTACK) {
        switch (e) {
            case CardEffect::TRUESTRIKE:
                say(Color::MAGENTA, "It strikes clean through everything you put in its way.");
                bossStrikesPlayer(atk + v / 2, true, false, /*unstoppable*/true);
                return true;
            case CardEffect::TRUE_DOUBLE:
                say(Color::MAGENTA, "Twice, and nothing you raise can stop either.");
                bossStrikesPlayer(atk / 2 + v / 2, true, false, true);
                if (playerHealth > 0) bossStrikesPlayer(atk / 2 + v / 2, true, false, true);
                return true;
            case CardEffect::RECKLESS:
                bossStrikesPlayer(atk + v, false);
                enemy.applyStatus(StatusType::WEAK, 1);
                say(Color::WEAK_CLR, "It overswings. Its own blows come softer for a turn.");
                return true;
            case CardEffect::OVEREXTEND:
                bossStrikesPlayer(atk + v / 2, true);
                knightMoveDebt += 1;
                say(Color::WEAK_CLR, "It overreaches. One move fewer next round.");
                return true;
            case CardEffect::WILDCHARGE:
                bossStrikesPlayer(atk + v, false);
                enemy.resetArmor();
                enemyArmorHoldTurns = 0;
                say(Color::WEAK_CLR, "It charges in open. Its armour is gone.");
                return true;
            case CardEffect::EMBERBLADE:
                bossStrikesPlayer(atk + v / 2, false);
                if (playerHealth > 0) {
                    applyPlayerStatus(StatusType::BURN, 4);
                    say(Color::BURN_CLR, "The blade sets you alight! Burn 4.");
                }
                enemy.applyStatus(StatusType::BURN, 2);
                say(Color::BURN_CLR, "The flames lick back at it. Burn 2.");
                return true;
            case CardEffect::SHATTERPOINT:
                bossStrikesPlayer(atk + v, false);
                if (knightPreparedMoves.size() > 1)
                    knightPreparedMoves.erase(knightPreparedMoves.begin() + 1, knightPreparedMoves.end());
                say(Color::WEAK_CLR, "The blow costs it its footing. One more move at most this round.");
                return true;
            case CardEffect::ALLIN:
                bossStrikesPlayer(std::max(atk, enemy.getArmor() * 2), false);
                enemy.resetArmor();
                enemyArmorHoldTurns = 0;
                enemy.applyStatus(StatusType::WEAK, 3);
                say(Color::WEAK_CLR, "Everything it had, thrown. It is Weakened and unguarded.");
                return true;
            case CardEffect::PACTRUIN:
                bossStrikesPlayer(atk + v / 2, false);
                enemyPactOfRuin = true;
                enemyNoHeal = true;
                std::cout << "  " << Color::BOLD << Color::MAGENTA
                          << "It takes the pact. Its blows fester now, its wounds will not close, "
                          << "and every move costs it blood." << Color::RESET << "\n";
                return true;
            default:
                return false;
        }
    }

    if (mirrored.getType() == CardType::DEFEND) {
        auto guardUp = [&](int amount, const char* how) {
            enemy.gainArmor(amount);
            std::cout << Color::MAGENTA << how << Color::RESET << " +" << Color::ARMOR_CLR << amount
                      << Color::RESET << " armor (" << enemy.getArmor() << " total)\n";
        };
        switch (e) {
            case CardEffect::FORTIFY:
                guardUp(guard, "It fortifies behind your own guard!");
                enemyArmorHoldTurns = 3;
                say(Color::CYAN, "The armour will not fade for 3 turns.");
                break;
            case CardEffect::SCRAP:
                guardUp(guard, "It throws up your scrap shield!");
                pay(1);
                say(Color::DAMAGE, "The scrap edge nicks it for 1.");
                break;
            case CardEffect::SELFWEAK:
                guardUp(guard * 3 / 2, "It braces heavy behind your guard!");
                enemy.applyStatus(StatusType::WEAK, 1);
                say(Color::WEAK_CLR, "Its own blows soften for a turn.");
                break;
            case CardEffect::TURTLE:
                guardUp(guard, "It digs in behind your guard!");
                enemyArmorHoldTurns = 3;
                enemy.applyStatus(StatusType::WEAK, 3);
                say(Color::CYAN, "The armour holds for 3 turns, and it is Weakened while it does.");
                break;
            case CardEffect::UNSTABLEWARD:
                guardUp(guard, "It works your unstable ward!");
                enemyStatusWardActive = true;
                knightMoveDebt += 2;
                say(Color::CYAN, "Your next ailment will not take, and the working costs it two moves next round.");
                break;
            case CardEffect::LASTSTAND: {
                // Its wounds as a share of its health, turned into the same
                // share of yours, so the card is the same size in its hands.
                const int missing = enemy.getMaxHealth() - enemy.getHealth();
                const int fromWounds = missing * std::max(1, maxPlayerHealth)
                                     / std::max(1, enemy.getMaxHealth()) + v;
                guardUp(fromWounds, "Its wounds harden into armour!");
                enemyArmorHoldTurns = 3;
                enemyNoHeal = true;
                say(Color::WEAK_CLR, "It holds for 3 turns, and it cannot heal for the rest of the fight.");
                break;
            }
            default:
                return false;
        }
        UIHelper::pause(250);
        return true;
    }

    // SPECIAL
    switch (e) {
        case CardEffect::STRENGTH: {
            const double buff = mirrored.strengthMultiplier();
            enemy.applyStatus(StatusType::STRENGTH, 2, 1.5, buff);
            std::ostringstream o;
            o << "It takes your strength: x" << buff << " damage for 2 turns.";
            say(Color::STRENGTH_CLR, o.str());
            break;
        }
        case CardEffect::FEAR:
            if (provokeFizzles()) {
                say(Color::DIM, "Its stare slides off you. The fear does not take.");
                break;
            }
            nextHandPenalty = std::max(nextHandPenalty, 1);
            say(Color::WEAK_CLR, "Its stare gets into you. One fewer card next turn.");
            break;
        case CardEffect::BLOODPRICE: {
            const int paid = pay(std::max(6, enemy.getMaxHealth() * 2 / 100));
            std::cout << "  " << Color::DAMAGE << "It pays " << paid << " of its health to move again."
                      << Color::RESET << hp() << "\n";
            knightExtraMove();
            break;
        }
        case CardEffect::BERSERK:
            enemy.applyStatus(StatusType::STRENGTH, 2, 1.5, 1.5);
            enemyVulnerableTurns = 1;
            say(Color::STRENGTH_CLR, "It throws its guard away and winds up: x1.5 damage, and it takes x1.5 from you until its turn.");
            break;
        case CardEffect::ADRENALINE:
            knightMoveDebt += 1;
            say(Color::WEAK_CLR, "It borrows a move from next round.");
            knightExtraMove();
            break;
        case CardEffect::BLOODPACT: {
            const int paid = pay(enemy.getMaxHealth() * 15 / 100);
            enemy.applyStatus(StatusType::STRENGTH, 3, 1.5, 2.0);
            std::cout << "  " << Color::STRENGTH_CLR << "x2 damage for 3 turns" << Color::RESET
                      << Color::DAMAGE << ", paid with " << paid << " of its health." << Color::RESET << hp() << "\n";
            break;
        }
        case CardEffect::BORROWED: {
            const int paid = pay(enemy.getMaxHealth() * 15 / 100);
            knightMoveDebt += 3;
            std::cout << "  " << Color::BOLD << Color::CYAN << "Time folds for it. It moves twice more, now."
                      << Color::RESET << Color::DAMAGE << " It loses its next round, and " << paid
                      << " of its health." << Color::RESET << hp() << "\n";
            knightExtraMove();
            knightExtraMove();
            break;
        }
        case CardEffect::SACRIFICE: {
            if (knightSacrificeSpent || enemyNoHeal) {
                say(Color::DIM, "There is nothing left for it to give up.");
                break;
            }
            knightSacrificeSpent = true;
            const int before = enemy.getHealth();
            enemy.heal(enemy.getMaxHealth() * 30 / 100);
            const int mend = enemy.getHealth() - before;
            std::cout << "  " << Color::HEAL << "It gives up your Sacrifice and mends " << mend << " health."
                      << Color::RESET << hp() << Color::DIM << " It will not have that card again this fight."
                      << Color::RESET << "\n";
            break;
        }
        default:
            return false;
    }
    UIHelper::pause(250);
    return true;
}

void Game::offerBossReward() {
    std::vector<Card> rewards = rewardPool.generateRareRewards(3 + rewardChoiceBonus, maxEnergy,
                                    playerDeck.getAllCardNames(), currentRun.getBossIndex(),
                                    luckBonus());

    // Without this the loop below builds an empty option list and still puts up
    // a "choose one" screen with nothing on it but Skip. The rare pool runs dry
    // before the roster runs out of rare cards, so it is reachable in play.
    if (rewards.empty()) { offerExhaustedReward(); return; }

    std::vector<std::string> leftLines;
    std::vector<int>         optionIndices;
    std::vector<std::string> options;

    for (size_t i = 0; i < rewards.size(); i++) {
        const Card& c = rewards[i];
        const char* typeColor = (c.getTypeString() == "ATTACK") ? Color::CARD_ATTACK
                              : (c.getTypeString() == "DEFEND") ? Color::CARD_DEFEND
                              : Color::CARD_SPECIAL;
        const char* valLabel = (c.getTypeString() == "ATTACK") ? "DMG"
                             : (c.getTypeString() == "DEFEND") ? "ARM" : "STK";

        leftLines.push_back(std::string("  ") + Color::BOLD + std::to_string(i + 1) + "." + Color::RESET
            + " [" + typeColor + c.getTypeString() + Color::RESET + "] "
            + Color::BOLD + rarityTint(c) + c.getName() + Color::RESET
            + (c.getTypeTag().empty() ? "" : (" " + std::string(Color::YELLOW) + c.getTypeTag() + Color::RESET)));
        optionIndices.push_back((int)i);

        leftLines.push_back(std::string("     Cost:") + Color::ENERGY_CLR + std::to_string(c.getCost()) + Color::RESET
            + "  " + valLabel + ":" + Color::GREEN + std::to_string(c.getValue()) + Color::RESET);
        optionIndices.push_back(-1);

        leftLines.push_back(std::string("     ") + Color::DIM + c.getDescription() + Color::RESET);
        optionIndices.push_back(-1);

        leftLines.push_back("");
        optionIndices.push_back(-1);

        options.push_back("Select Card " + std::to_string(i + 1));
    }
    options.push_back("Skip");

    std::vector<CardBar::Card> bossWidgets;
    for (const Card& c : rewards) bossWidgets.push_back(toWidget(c, gearedValue(c, c.getValue())));
    // The Bone Dice work here too: this is the reward a run turns on.
    bool canReroll = hasRelic(Relic::BONE_DICE);

    while (true) {
        std::vector<CardBar::Action> bossActs{ CardBar::Action{ "Skip", false } };
        if (canReroll) bossActs.push_back(CardBar::Action{ "Reroll", "the Bone Dice: new cards, once", false });
        int choice = CardBar::pick("Boss reward, choose one", bossWidgets, bossActs,
                                   (int)bossWidgets.size());
        if (canReroll && choice == (int)rewards.size() + 1) {
            std::vector<Card> fresh = rewardPool.generateRareRewards(
                3 + rewardChoiceBonus, maxEnergy, playerDeck.getAllCardNames(),
                currentRun.getBossIndex(), luckBonus());
            canReroll = false;
            if (!fresh.empty()) {
                rewards = fresh;
                bossWidgets.clear();
                for (const Card& c : rewards) bossWidgets.push_back(toWidget(c, gearedValue(c, c.getValue())));
                Audio::playSFX("special");
            }
            continue;
        }
        if (choice <= -2) {
            int ci = -2 - choice;
            if (ci >= 0 && ci < (int)rewards.size())
                CardBar::showDetail(bossWidgets[ci], rewards[ci].getDescription(),
                                    rewards[ci].getTypeString(), rarityWord(rewards[ci]),
                                    rewards[ci].getUpgradeCount());
            continue;
        }

        if (choice < 0 || choice >= (int)rewards.size()) {
            const std::string confirmPrompt = "Skip this reward, take none of the three?";
            if (!confirm(confirmPrompt)) continue; // declined - back to the choices
            notice("Reward skipped.");
            return;
        }

        const std::string confirmPrompt = "Add " + rewards[choice].getName() + " to your deck?";
        if (!confirm(confirmPrompt)) continue; // declined - back to the choices

        playerDeck.addCard(rewards[choice]);
        runStats.addCardToRun();
        notice("Added " + rewards[choice].getName() + " to your deck.");
        return;
    }
}

void Game::offerExtraPlay() {
    // Raised from 5. With three cards a turn instead of five, the back half of a
    // run needs somewhere for a boss kill to go, and hitting the ceiling at the
    // fourth boss meant the last two gave nothing at all.
    const int MAX_ENERGY_CAP = 8;
    UIHelper::clearScreen();
    if (maxEnergy >= MAX_ENERGY_CAP) {
        notice("You are already at the max of " + std::to_string(MAX_ENERGY_CAP)
               + " energy per turn. Nothing more to gain here.");
        return;
    }

    std::vector<CardBar::Action> energyActs{
        CardBar::Action{ "Extra Energy", "+1 max energy per turn ("
                         + std::to_string(maxEnergy) + " to " + std::to_string(maxEnergy + 1) + ")", false },
        CardBar::Action{ "Skip", "leave it", false },
    };
    int choice = CardBar::pick("A hard-won boss kill leaves you invigorated.", {}, energyActs, 0);

    if (choice == 0) {
        maxEnergy = std::min(MAX_ENERGY_CAP, maxEnergy + 1);
        playerEnergy = maxEnergy;
        Audio::playSFX("upgrade");
        notice("You feel invigorated. Max energy per turn increased to "
               + std::to_string(maxEnergy) + ".");
    } else {
        notice("You let it pass.");
    }
}

// Narrative beats, three per zone: "enter" at the zone's first fight,
// "approach" right before its boss, "outro" after the boss falls. First
// time through only (cycle 0).
namespace {
    using Lines = std::vector<std::string>;
    struct ZoneStory { Lines enter, approach, outro; };
    const ZoneStory ZONE_STORY[5] = {
        { // The Dungeon -> Stone Colossus
          { "The knight passes through a rusted iron gate into a dungeon of wet stone, his "
            "footsteps the only sound in corridors that swallow torchlight before it can catch.",
            "He knows exactly what is missing, which helps less than it sounds. The gap "
            "behind his ribs sits there like a held breath.",
            "There is standing water in places. He stops looking down at it after the second "
            "time, and grips a wooden sword he is not sure he ever learned to use." },
          { "The passage finally opens into a vast chamber, and the ground itself seems to wake.",
            "A stone colossus rises from the rubble, older than the dungeon around it, and "
            "plants itself squarely in the only way forward." },
          { "Among the rubble, a small point of light rests in the dust: a piece of what was "
            "torn off him, picked up by the first thing that found it.",
            "It goes back into the hollow behind his ribs. His strength, or the start of it. "
            "The armor stops feeling like someone else's." } },
        { // The Dark Dungeon -> Vile Witch
          { "Past the gate the stone turns darker and colder. Someone has carved runes into "
            "the floor here, and a green light flickers with nothing to cast it.",
            "Somewhere ahead, voices are whispering. Every time he turns toward them, they "
            "stop." },
          { "The corridor opens on a chamber ringed with shattered cauldrons.",
            "A vile witch waits at its heart, with the patience of something that has already "
            "decided how this ends." },
          { "The cauldrons lie in pieces, and the wrongness in the air finally lifts.",
            "The second piece was on her altar, being studied. She had no idea what she had.",
            "It goes back in quietly, and takes with it a fog he had stopped noticing." } },
        { // The Wicked Forest -> Thunder Beast
          { "Trees older than the dungeon close overhead, branches woven so tight no daylight "
            "reaches the forest floor.",
            "The air itself feels charged, hair lifting on his arms with every step, thunder "
            "answering thunder in a storm that never quite arrives." },
          { "The storm finally breaks over a clearing at the heart of the wood.",
            "A thunder beast commands the canopy there, lightning coiled and ready." },
          { "When the last peal fades and the ozone smell clears, the forest seems to exhale "
            "with him.",
            "The third piece lies scorched into the earth where the beast fell. It had been "
            "carrying his speed around in its chest for years.",
            "His legs remember. He had forgotten they had forgotten." } },
        { // The Dark Lake -> Hydra
          { "The shoreline is black glass under a fog that swallows sound as readily as light.",
            "The lake gives back no stars. Only him, and a half second late.",
            "He pushes a rotting raft out into the mist and does not look over the side." },
          { "Out past the fog line the water answers with ripples that have nothing to do "
            "with the wind.",
            "The hydra wakes beneath the surface, unwilling to let anything cross unchallenged." },
          { "The lake stills once the last head falls silent, and the fourth piece drifts to "
            "him on the tide, unhurried, as if it had been waiting.",
            "The way he moves stops being effort and starts being current again." } },
        { // The Mountain -> Undead Dragon
          { "Past the treeline the world turns to wind and ice, a narrow ledge of slate the "
            "only path between him and the drop.",
            "Frost climbs his plate faster than his own breath can melt it. Where the ice is "
            "clear he can see himself in it, holding the sword the way he used to." },
          { "The ledge ends at a cave mouth colder than the wind outside.",
            "Something waits within: a dragon that died once and never quite left." },
          { "The dragon's frozen breath goes still.",
            "The last piece is in its chest, and it is warm. It is his soul, or the part of "
            "it that was torn loose, and it has been keeping something dead on its feet.",
            "He is whole now, everywhere except the one place it still has him: his shape, "
            "worn by something else, waiting at the top." } },
    };
    // The Peak: plays once, right before the Shadow Knight (encounter 50). No
    // matching outro - handleGameVictory() already covers that beat.
    const Lines PEAK_APPROACH = {
        "Above the clouds the sky turns a bruised purple, and the moon hangs so low and so "
        "dark it seems to be waiting too.",
        "At the summit's edge stands a knight in his own armor, carrying his own sword. It is "
        "the thing from the reflection, wearing the only shape it ever wanted, and it has had "
        "the whole climb to practise."
    };
    // When the Shadow Knight falls with the true form earned: it drops his
    // shape and fights to escape the sleep.
    const Lines TRUE_FORM = {
        "Its vigils are broken, every one. The long slumber is coming, and nothing it stole "
        "can keep it awake now.",
        "It stops pretending to be the knight. His face, his shape, none of it matters any "
        "more. It only needs to escape what is coming, and it tears free of him to try."
    };
    // "Sit a while" at the rest site. Stage is how many pieces of him are back,
    // which is also where he is: 0 the dungeon, 4 the mountain, 5 the night
    // before the peak. Read in order, once each.
    struct SitPassage { int stage; Lines lines; };
    const SitPassage SIT[] = {
        { 0, { "He gets the fire going on the third try. His hands know what they are for. They "
               "just keep arriving late.",
               "He tries to picture his own face and gets the helmet instead. That is probably "
               "fair. He wore it more.",
               "One of the pieces that was taken from him is down here, somewhere further in. He "
               "cannot say how he knows, but he is sure of it." } },
        { 0, { "Water drips somewhere behind him, steady as a clock. He shifts until the puddle "
               "is at his back.",
               "He remembers how it started, or the edge of it. A clear night, a still pond, and "
               "a moon in the water that was brighter than the one in the sky.",
               "He looked at it too long. That was all it needed from him." } },
        { 0, { "Not much comes back about who he was before all this.",
               "People used to go quiet when he walked into a room. He cannot remember a single "
               "one of their names, only the quiet.",
               "Whoever he was, people knew him. He holds on to that, and it is enough to get "
               "up for in the morning." } },

        { 1, { "His arms are his own again. He keeps closing his hand around a stick of "
               "firewood just to feel the grip hold.",
               "The colossus never knew what it had. It just got stronger one day and never "
               "asked why.",
               "He wonders how many things out here are like that. A little too strong, a "
               "little too awake, and no idea who they have to thank for it." } },
        { 1, { "The green light has got into the fire too. He feeds it until it burns orange "
               "again.",
               "He is starting to see what the moon leaves behind it. A light in the chest of "
               "something that should not have one. A patience that is not its own.",
               "It did not only scatter him. It set vigils in the hearts of the worst things on "
               "the road, so that while they burned it would never have to sleep." } },
        { 1, { "He counts what he has back on his fingers, like a child. Strength. Then four "
               "gaps, each one the shape of something he used to be good at.",
               "Four more pieces, then, carried by four more things like the colossus, and none "
               "of them will hand theirs over.",
               "He had all five his whole life. He is not leaving any of them out here." } },

        { 2, { "With his wits back, he can finally think about the thing in the moon, and why "
               "it came for him.",
               "It has never had a shape of its own. It did not take him to be a knight. It "
               "took him to be someone, and out of everyone it watched, he was the one worth "
               "being.",
               "That is why it kept his face for itself. Without it, it would be nothing "
               "again." } },
        { 2, { "The storm grumbles over the canopy and never breaks. He has stopped flinching "
               "at it.",
               "Now and then something on this road is not quite right. It stands too still, or "
               "it is put together wrong, like a copy made from memory. He is fairly sure that "
               "is the moon, practising.",
               "Every shape it tries is a little closer to right. That is the part he does not "
               "like." } },
        { 2, { "Something moves out past the firelight and stops when he looks. Not an animal. "
               "An animal would have run.",
               "It is pacing him. Not following, exactly. Walking alongside, a long way off, "
               "the way you copy someone's walk to learn it.",
               "He does not get up. Let it watch. It learned him from the outside once "
               "already, and it got him wrong." } },

        { 3, { "No dry wood by the lake. He sits in the dark and lets his eyes adjust.",
               "He keeps his back to the water. He has stopped looking at it, but he can feel "
               "it looking at him.",
               "This is where it lives, if it lives anywhere. Not in the moon itself, but in the "
               "moon on the water, and in every still surface that has ever held a copy of "
               "something." } },
        { 3, { "He is fast again. He catches a spark out of the air without thinking, then "
               "sits staring at his hand.",
               "He remembers the night now, most of it. The moon in the pond going dark. "
               "Something climbing out of the water to meet him. The sound of his own armor "
               "hitting the ground with nobody in it.",
               "He is fairly sure he was not afraid. He would like to be sure." } },
        { 3, { "The moon sits lower every night. He does not think that is the season.",
               "Whatever is up there is running out of time, and it knows exactly who is coming "
               "for it.",
               "For the first time since the pond, he is not the one being hunted." } },

        { 4, { "The wind keeps trying to take the fire. He builds a wall of stones around it, "
               "and it holds.",
               "He has almost all of himself back. He expected that to feel like something. It "
               "feels like carrying a full pack instead of an empty one. Heavier, and better.",
               "When the cloud thins he can see the peak. Something up there is standing very "
               "still, in a shape he knows." } },
        { 4, { "He sits down without thinking about how. That came back on the lake, and he "
               "keeps noticing it: all the small things a body does on its own.",
               "The thing at the top has been copying those small things for years and never got "
               "one of them right. It makes a very good knight, standing still.",
               "The moment it has to move, it has to guess." } },
        { 4, { "He wonders if he was the first. It took him apart too neatly for that. It had "
               "practice from somewhere.",
               "There may be other people out there in pieces, scattered through places like the "
               "ones he has just climbed out of.",
               "He cannot put them back together. He can end the thing that took them apart. "
               "He supposes that is a kind of help." } },

        { 5, { "The last fire. He knows it the way he knows most things now, without being "
               "told.",
               "His soul sits where it should, warm, and the hollow behind his ribs is gone. "
               "All he is missing is his face, and his name in someone's mouth, and the thing "
               "at the top is wearing both.",
               "He puts the fire out himself. He wants to be the one who does it." } },
    };
    const int SIT_COUNT = (int)(sizeof(SIT) / sizeof(SIT[0]));

    // Plays once, at the start of every new run, before the first fight.
    const Lines INTRO = {
        "There is something that lives in the moon's reflection. It has no shape of its own "
        "and a long appetite for other people's.",
        "It spent a long time watching people and finding none of them worth the "
        "trouble. Then it found a knight who was, and it decided it would rather be him "
        "than keep watching him.",
        "The night the moon went dark it came down and took him apart. His strength, his "
        "speed, the sure way his hands knew a blade, and under all of it, his soul.",
        "The pieces went where torn things go: out into the dark places between here and "
        "the peak, and whatever found them first kept them.",
        "There is an old word for someone the moon has been at. " + std::string(Color::BOLD)
        + Color::WHITE + "Moonstruck" + Color::RESET + Color::DIM + ". Nobody ever meant it "
        "like this.",
        "What is left of him stands up anyway, and picks up a wooden sword. A soul in pieces "
        "can still feel where its pieces are."
    };

    void showStoryBeat(const Lines& lines) {
        UIHelper::clearScreen();
        Hud::setActive(false);       // no combat panel over a story beat

        // Estimate the wrapped height first so the block can sit in the middle
        // rather than climbing down from the top of a tall window.
        const int measure = 74;
        int height = 0;
        for (const std::string& line : lines)
            height += 1 + (int)(line.size() / (size_t)measure) + 1;
        UIHelper::padToCenter(height);

        for (const std::string& line : lines) {
            UIHelper::printCenteredWrapped(std::string(Color::DIM) + line + Color::RESET, measure, true);
            std::cout << "\n";
        }
        std::cout << "\n";
        UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
        UIHelper::waitForKey("");
    }

    // A passage under the fire scene: the picture in the top of the screen,
    // the words starting below it rather than centred through it.
    void showSitBeat(const Lines& lines, int armorTier) {
        UIHelper::clearScreen();
        Hud::setActive(false);
        EnemyArt::setRestScene(armorTier);
        for (int i = 0, pad = Console::rows() * 48 / 100; i < pad; i++) std::cout << "\n";
        for (const std::string& line : lines) {
            UIHelper::printCenteredWrapped(std::string(Color::DIM) + line + Color::RESET, 74, true);
            std::cout << "\n";
        }
        UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
        UIHelper::waitForKey("");
        EnemyArt::setRestScene(-1);
        // The rest site menu comes straight back after this and draws over
        // whatever is on the console, so the passage has to go with the fire.
        UIHelper::clearScreen();
    }
}

// Odds for the Moonstruck: 2% per eligible fight (not a boss, past
// encounter 5), once per area, a different shape in each.
static const int SECRET_CHANCE_PERCENT = 2;
static const int SECRET_EARLIEST       = 6;

static int moonZoneFor(int encounter) { return ((encounter - 1) / 10) % 5; }

// The shape it wears in each area. None of the names contain a regular
// enemy's, because moves, sprites and lore are looked up by what the name
// contains.
struct MoonForm { const char* name; EnemyType type; const char* omen; const char* warning; const char* parting; };
static const MoonForm MOON_FORMS[5] = {
    { "Moonstruck Weaver (Undead)", EnemyType::UNDEAD,
      "The torches gutter red, all at once, as if something breathed on them.",
      "Something is trying to be a spider and a skeleton at the same time, and it has "
      "put the legs on wrong.",
      "Bone, then legs, then a shape you almost know, and then nothing at all." },
    { "Moonstruck Beguiler (Caster)", EnemyType::CASTER,
      "The runes in the floor go dark, then bleed red.",
      "Someone in a pointed hat has been singing your name, and has nearly got it.",
      "Its song stops halfway through your name. It was closer this time." },
    { "Moonstruck (Beast)", EnemyType::BEAST,
      "The moon comes up wrong. Everything that was making noise stops at once.",
      "It has been a wolf for some time now, and it is holding the shape.",
      "" },
    { "Moonstruck Gorgon (Beast)", EnemyType::BEAST,
      "The moon on the water turns red before the moon above it does.",
      "Whatever is on the shore does not blink, and does not need to any more.",
      "Its stare goes out like a lamp, and the stiffness leaves your joints with it." },
    { "Moonstruck Templar (Tank)", EnemyType::TANK,
      "The dusk turns the colour of a wound, and the wind stops dead.",
      "Something in pale armor has been climbing after you, and it climbs like a man now.",
      "The wind takes what is left of it up toward the peak. It has what it came for." },
};


bool Game::rollSecretEncounter() {
    if (inSecretEncounter) return false;
    if (currentRun.isBossEncounter()) return false;
    if (currentRun.getCurrentEncounter() < SECRET_EARLIEST) return false;
    // moonZonesSeen is cleared when a run starts, so every run gets its own
    // chance at all five.
    if (moonZonesSeen & (1 << moonZoneFor(currentRun.getCurrentEncounter()))) return false;
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> d(1, 100);
    // Luck applies here above all: a run that has not found this yet is exactly
    // the run that should get better odds for having invested in Fortune.
    const int chance = SECRET_CHANCE_PERCENT * (hasRelic(Relic::MOON_LOCKET) ? 2 : 1);
    return d(gen) <= chance + luckBonus();
}

void Game::beginSecretEncounter() {
    moonZone = moonZoneFor(currentRun.getCurrentEncounter());
    moonZonesSeen |= 1 << moonZone;
    moonstruckMet++;
    inSecretEncounter = true;
    const MoonForm& form = MOON_FORMS[moonZone];

    // The music stops: the omen arrives in silence, and the track comes back
    // as bgm_secret when the thing is in front of you.
    Audio::stopBGM();

    UIHelper::clearScreen();
    UIHelper::padToCenter(4);
    UIHelper::printCenteredWrapped(std::string(Color::DIM) + form.omen + Color::RESET, 68, true);
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::BOLD) + Color::RED + form.warning + Color::RESET);
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
    UIHelper::waitForKey("");

    UIHelper::clearScreen();
    EnemyArt::setSecretBackdrop(moonZone);
    Audio::playBGM("bgm_secret");   // off the zone rotation entirely
    Audio::playSFX("boss");
    playerDeck.resetDeck();
    int drawCount = BASE_HAND_SIZE + handSizeBonus + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    // Built off the fight it interrupts rather than a fixed statline, so it
    // stays a step above whatever the zone is currently throwing at you.
    int health  = sealScaled((int)(currentRun.getEnemyHealth()  * 1.6) + 30, SEAL_HP_PCT);
    int attack  = sealScaled((int)(currentRun.getEnemyAttack()  * 1.35) + 2, SEAL_ATK_PCT);
    int defense = (int)(currentRun.getEnemyDefense() * 1.2) + 1;
    enemy = Enemy(form.name, health, attack, defense, form.type);

    // By full name, so the form's own sheet wins over the forest's werewolf.
    EnemyArt::setEnemyVariant(enemy.getName());
    Console::pushHistory("");
    playerHealth = std::max(1, playerHealth);
    playerArmor = 0;
    playerArmorPersistTurns = 0;
    playerStatus.reset();
    enemyParryStance = false;
    playerAttackOnly = false;
    // Both are set to 2 and ticked down once per enemy turn, so killing the
    // enemy inside that window would carry the effect into the next fight.
    enemyTauntTurns = 0;
    enemyFearTurns = 0;
    playerBoundTurn = false;
    curseTurnsLeft = 0;
    lichAddAlive = false;
    EnemyArt::setCompanion("");
    turnNumber = 1;
    cardsPlayedThisTurn = 0; attacksPlayedThisTurn = 0;
    resetEnergy();
    bossSecondWindAvailable = false;
    applyFightStartRelics();
    playerTurnActive = true;
    inEncounter = true;
    running = true;
}

// Phase two of the Shadow Knight, the only fight with one: the knight's
// health plus 100, and its attack with every buff the knight earned.
void Game::beginTrueForm() {
    trueFormPhase = true;
    // A hundred more than the knight it came out of: this is the real fight.
    const int hp  = enemy.getMaxHealth() + 100;
    // Bonus attack included: the knight may have been buffed on its way down,
    // and the form that gets up out of it inherits that, plus two of its own.
    const int atk = enemy.getBaseAttack() + enemy.getBonusAttack() + 2;
    const int def = enemy.getBaseDefense();

    EnemyArt::printBattleDeath(enemy.getType(), enemy.getBossType());
    Audio::playSFX("win");
    UIHelper::pause(300);
    // Into the log, not a page of its own: the change is one continuous shot.
    // Captured too, since the log box only holds a handful of lines.
    Console::setHistoryCapture(true);
    Console::pushHistory("");
    for (const std::string& line : TRUE_FORM) {
        // Wrapped before it is typed: the log keeps each row as it comes, and
        // a long line ran straight off the right of it.
        std::string rows = "\n";
        for (const std::string& row : UIHelper::wrapRows(line))
            rows += std::string(Color::DIM) + row + Color::RESET + "\n";
        UIHelper::typeWrite(rows);
        UIHelper::pause(200);
    }
    // Read at your own pace. The prompt stays out of the log's history: it is
    // not part of the story.
    Console::setHistoryCapture(false);
    UIHelper::waitForKey("  (press any key)");
    Console::setHistoryCapture(true);

    // Back to full, and said out loud. What is about to stand up is a second
    // boss, and finishing the first one on nine health should not decide it.
    playerHealth = maxPlayerHealth;
    playerStatus.reset();
    noHealThisEncounter = false;
    Audio::playSFX("heal");

    // No clear: the transformation happens on the screen the fight was
    // already on. The counter empties first, so it is gone before the change
    // rather than blinking out in the middle of it.
    syncHud();
    UIHelper::clearScreen();
    EnemyArt::setBattleBackdrop(currentRun.getCurrentEncounter());
    // No setEnemyVariant here: the knight is still the knight, and the name
    // table would match it on "Knight" and hand back a roster melee sheet.
    // Bosses draw from their own art, which is already what is on screen.
    EnemyArt::printBattle(EnemyType::UNDEAD, BossType::SHADOW_KNIGHT);

    Audio::playBGM("bgm_secret");
    Audio::playSFX("boss");

    enemy = Enemy("Moonstruck Shadow Knight", hp, atk, def, EnemyType::UNDEAD);
    enemy.setBossType(BossType::SHADOW_KNIGHT);
    // The sprite bleaches, the moon comes up inside the glare, and the peak
    // turns into the last arena behind it.
    EnemyArt::transformToTrueForm(4, enemy.getName());
    Console::pushHistory("");
    std::cout << Color::BOLD << Color::MAGENTA
              << "It stands back up wearing none of your face at all." << Color::RESET << "\n";

    // A fresh hand for a fresh phase, but the health and armour you finished
    // the knight on: this is one fight, not two.
    playerDeck.resetDeck();
    int drawCount = BASE_HAND_SIZE + handSizeBonus + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }
    enemyParryStance = false;
    enemyReflectNext = false;
    playerAttackOnly = false;
    enemyTauntTurns = 0;
    enemyFearTurns = 0;
    // And nothing the knight ran up follows the true form in.
    enemyArmorHoldTurns = 0; enemyVulnerableTurns = 0;
    enemyNoHeal = false; enemyPactOfRuin = false;
    knightMoveDebt = 0; knightChainDepth = 0; knightSacrificeSpent = false;
    playerBoundTurn = false;
    enemyInvulnerable = false;
    lichAddAlive = false;
    EnemyArt::setCompanion("");
    lastMoveRoll = -1;
    turnNumber = 1;
    cardsPlayedThisTurn = 0; attacksPlayedThisTurn = 0;
    resetEnergy();
    // A second save for a second phase: the first was spent on the knight.
    bossSecondWindAvailable = true;
    Hud::setActive(true);
    playerTurnActive = true;
    inEncounter = true;
    running = true;
    Console::setHistoryCapture(false);
}

// Beating it: one guaranteed Super Rare, then straight on to the fight it
// interrupted. No rest site, no equipment roll - this was never on the map.
void Game::showIntro() {
    showStoryBeat(INTRO);
}

void Game::handleSecretWin() {
    inSecretEncounter = false;
    Hud::setActive(false);
    EnemyArt::printBattleDeath(enemy.getType(), enemy.getBossType());
    Audio::playSFX("win");
    UIHelper::pause(300);

    UIHelper::clearScreen();
    UIHelper::showHeadline("THE HUNT ENDS", 214, 66, 58);
    for (int i = 0, pad = Console::rows() * 42 / 100; i < pad; i++) std::cout << "\n";
    UIHelper::printCentered(std::string(Color::DIM)
        + "It does not leave a body. Only the move it had been practising." + Color::RESET);
    // Each shape leaves the same way, and each says a little more about what
    // was wearing it.
    if (*MOON_FORMS[moonZone].parting) {
        std::cout << "\n";
        UIHelper::printCentered(std::string(Color::DIM) + MOON_FORMS[moonZone].parting + Color::RESET);
    }
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
    UIHelper::waitForKey("");
    UIHelper::showHeadline("", 0, 0, 0);

    // Always a legendary: the encounter is a 2% roll, once a run. Super rare is
    // the fallback for a deck that already owns every legendary.
    std::vector<Card> prize;
    {
        static thread_local std::mt19937 lg(std::random_device{}());
        std::vector<Card> legs = rewardPool.getUnownedLegendaries(playerDeck.getAllCardNames());
        if (!legs.empty()) {
            std::uniform_int_distribution<> pick(0, (int)legs.size() - 1);
            prize.push_back(legs[pick(lg)]);
        }
    }
    if (prize.empty())
        prize = rewardPool.generateSuperRareReward(playerDeck.getAllCardNames());
    if (!prize.empty()) {
        std::vector<CardBar::Card> w{ toWidget(prize[0], gearedValue(prize[0], prize[0].getValue())) };
        std::vector<CardBar::Action> acts{ CardBar::Action{ "Take it", false } };
        while (true) {
            int ch = CardBar::pick("It was carrying this", w, acts, 1);
            if (ch <= -2) {
                CardBar::showDetail(w[0], prize[0].getDescription(), prize[0].getTypeString(),
                                    rarityWord(prize[0]), prize[0].getUpgradeCount());
                continue;
            }
            break;
        }
        playerDeck.addCard(prize[0]);
        runStats.addCardToRun();
        Audio::playSFX("upgrade");
        notice("Added " + prize[0].getName() + " to your deck.");
    } else {
        notice("You already own every card it could have been carrying.");
    }
    startEncounter();   // the fight it interrupted, still at the same number
}

// Losing it does not end the run. It loses interest and moves off, and the
// fight it interrupted happens anyway - just with nothing gained and whatever
// health you crawled away with.
void Game::handleSecretDefeat() {
    inSecretEncounter = false;
    Hud::setActive(false);
    playerHealth = 1;
    playerStatus.reset();
    playerArmor = 0;
    playerArmorPersistTurns = 0;
    Audio::playSFX("lose");
    UIHelper::clearScreen();
    UIHelper::padToCenter(4);
    UIHelper::printCenteredWrapped(std::string(Color::DIM)
        + "You go down. It stands over you long enough to be sure, loses interest, "
          "and is gone before you can lift your head."
        + Color::RESET, 68, true);
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::BOLD) + Color::YELLOW
        + "You are alive, at 1 HP, and no better armed." + Color::RESET);
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
    UIHelper::waitForKey("");
    startEncounter();
}

void Game::startEncounter() {

    if (rollSecretEncounter()) { beginSecretEncounter(); return; }

    if (currentRun.getCycle() == 0) {
        if (currentRun.isBossEncounter()) {
            int bossIdx = currentRun.getBossIndex(); // 0..5
            if (bossIdx == 5) showStoryBeat(PEAK_APPROACH); // right before the Shadow Knight
            // TRUE_FORM is read in beginTrueForm(), when the knight gets back up.
            else if (bossIdx >= 0 && bossIdx < 5) showStoryBeat(ZONE_STORY[bossIdx].approach);
        } else if (currentRun.getRegularIndex() % 9 == 0) {
            int zone = currentRun.getRegularIndex() / 9; // 0..4, one per zone
            if (zone >= 0 && zone < 5) showStoryBeat(ZONE_STORY[zone].enter);
        }
    }
    UIHelper::clearScreen();
    EnemyArt::setBattleBackdrop(currentRun.getCurrentEncounter());
    // BGM tracks the same 10-encounter segments as the backdrop, so the music
    // turns over right after each boss. Falls back to the base track until
    // per-segment files (bgm2..bgm5) exist in sounds/.
    Audio::playBGM(((currentRun.getCurrentEncounter() - 1) / 10) % 5);
    playerDeck.resetDeck();
    int drawCount = BASE_HAND_SIZE + handSizeBonus + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    trueFormPhase = false;   // whatever rose last wave does not carry over
    hydraHeads = 2;          // and the Hydra starts every fight with two
    paladinJudgements = 0;   // and the Paladin has not judged anyone yet
    if (currentRun.isBossEncounter()) {
        enemy = generateBossEnemy();
        bossSecondWindAvailable = true;
    } else {
        int health  = sealScaled(currentRun.getEnemyHealth(), SEAL_HP_PCT);
        int attack  = sealScaled(currentRun.getEnemyAttack(), SEAL_ATK_PCT);
        int defense = currentRun.getEnemyDefense();

        // The 44 regular enemies, ordered so no two fights in a row share a type,
        // each zone escalating and fitting its theme.
        struct RosterEntry { EnemyType type; const char* name; };
        static const RosterEntry ROSTER[44] = {
            // 1-9, The Dungeon, before the Stone Colossus
            {EnemyType::MELEE, "Goblin"},   {EnemyType::TANK, "Orc"},
            {EnemyType::CASTER, "Wizard"},  {EnemyType::UNDEAD, "Skeleton"},
            {EnemyType::BEAST, "Spider"},   {EnemyType::RANGED, "Archer"},
            {EnemyType::MELEE, "Bandit"},   {EnemyType::TANK, "Warden"},
            {EnemyType::CASTER, "Sage"},
            // 11-19, The Dark Dungeon, before the Vile Witch
            {EnemyType::UNDEAD, "Ghoul"},   {EnemyType::BEAST, "Basilisk"},
            {EnemyType::RANGED, "Assassin"},{EnemyType::MELEE, "Knight"},
            {EnemyType::TANK, "Sentinel"},  {EnemyType::CASTER, "Enchanter"},
            {EnemyType::UNDEAD, "Wraith"},  {EnemyType::BEAST, "Serpent"},
            {EnemyType::RANGED, "Omneye"},
            // 21-29, The Wicked Forest, before the Thunder Beast
            {EnemyType::MELEE, "Raider"},   {EnemyType::TANK, "Barbarian"},
            {EnemyType::CASTER, "Mystic"},  {EnemyType::UNDEAD, "Banshee"},
            {EnemyType::BEAST, "Wolf"},     {EnemyType::RANGED, "Falcon"},
            {EnemyType::MELEE, "Berserker"},{EnemyType::TANK, "Guardian"},
            {EnemyType::CASTER, "Vampire"},
            // 31-39, The Dark Lake, before the Hydra
            // Type run: UNDEAD BEAST MELEE TANK CASTER MELEE CASTER TANK RANGED
            {EnemyType::UNDEAD, "Specter"}, {EnemyType::BEAST, "Cockatrice"},
            {EnemyType::MELEE, "Gladiator"},{EnemyType::TANK, "Bastion"},
            {EnemyType::CASTER, "Spellmaster"}, {EnemyType::MELEE, "Warrior"},
            {EnemyType::CASTER, "Sorcerer"},{EnemyType::TANK, "Fortress"},
            {EnemyType::RANGED, "Wyvern"},
            // 41-48, The Mountain, before the Dragon + Shadow Knight finale
            // Type run: RANGED MELEE UNDEAD BEAST UNDEAD TANK BEAST CASTER
            {EnemyType::RANGED, "Deadeye"}, {EnemyType::MELEE, "Enforcer"},
            {EnemyType::UNDEAD, "Revenant"},{EnemyType::BEAST, "Manticore"},
            {EnemyType::UNDEAD, "Lich"},    {EnemyType::TANK, "Paladin"},
            {EnemyType::BEAST, "Fleshmass"},{EnemyType::CASTER, "Archon"},
        };
        int r = rosterIndexFor(currentRun.getRegularIndex());
        EnemyType etype = ROSTER[r].type;
        std::string name = ROSTER[r].name;
        int cycle = currentRun.getCycle();
        if (cycle == 1) name = "Greater " + name;
        std::string typeLabel = (etype == EnemyType::MELEE) ? "Melee"
                              : (etype == EnemyType::RANGED) ? "Ranged"
                              : (etype == EnemyType::TANK)   ? "Tank"
                              : (etype == EnemyType::CASTER) ? "Caster"
                              : (etype == EnemyType::BEAST)  ? "Beast" : "Undead";
        name += " (" + typeLabel + ")";
        enemy = Enemy(name, health, attack, defense, etype);
    }

    EnemyArt::setEnemyVariant(enemy.getName());
    prepareShadowKnightMoves(); // no-op unless this fight is the Shadow Knight

    // Divider in the run-long log, so scrolling back through a whole run stays
    // navigable. Written directly because capture is off outside a turn.
    Console::pushHistory("");
    Console::pushHistory(std::string(Color::BOLD)
        + (currentRun.isBossEncounter() ? Color::MAGENTA : Color::CYAN)
        + "=== " + (currentRun.isBossEncounter() ? "BOSS" : "Encounter "
            + std::to_string(currentRun.getCurrentEncounter()))
        + ": " + enemy.getName() + " ===" + Color::RESET);

    // Fresh fight: clear every per-enemy signature mechanic from the last one.
    playerAttackOnly = false;
    // Both are set to 2 and ticked down once per enemy turn, so killing the
    // enemy inside that window would carry the effect into the next fight.
    enemyTauntTurns = 0;
    enemyFearTurns = 0;
    enemyInvulnerable = false;
    enemyParryStance = false;
    nextHandPenalty = 0;
    curseTurnsLeft = 0;
    lichAddAlive = false;
    lichAddHp = lichAddMaxHp = lichAddAtk = 0;
    lastMoveRoll = -1;
    inSecretEncounter = false;
    EnemyArt::setCompanion("");
    fleshmassBindPending = false;
    playerBoundTurn = false;
    cardsPlayedThisTurn = 0; attacksPlayedThisTurn = 0;
    endEncounterEffects();      // no drawback carries into the next fight
    armPerTurnEnemyMechanics(); // arm the Assassin ambush if this fight is the Assassin

    inEncounter = true;
    turnNumber = 1;
    playerTurnActive = true;
    playerArmor = 0;
    playerEnergy = maxEnergy;
    playerStatus.reset();
    // Don't let an armed-but-unconsumed Status Guard carry into a new fight.
    statusWardTurns = 0;
    enemyStatusWardActive = false;
    applyFightStartRelics();

    // No run-stats dump or encounter banner: the combat panel already shows the
    // encounter, its difficulty and both HP bars, and the log should hold
    // events rather than open with a header nobody reads twice.
    if (currentRun.isBossEncounter()) Audio::playSFX("boss");

    refreshBattleAuras();
    EnemyArt::printBattle(enemy.getType(), enemy.getBossType());

    // Panel up, then a short beat before the hand arrives. The panel and scene
    // draw immediately, so a longer pause only holds a finished screen with no
    // cards on it.
    syncHud();

    // Four of the six bosses carry a vigil, and you can see it burning before
    // it does anything. The Dragon and the Shadow Knight carry none: the
    // Dragon never offered one, and the last one is what they are for. Typed
    // into the log once the boss is on screen; typed before the scene, it sat
    // alone on a blank screen for the half second it took.
    static const char* VIGIL_TELL[4] = {
        "There is a light somewhere under its chest that has nothing to do with stone.",
        "She is lit from the inside, and it is not her doing the lighting.",
        "There is a light in its chest that does not flicker with the rest of the storm.",
        "One of the throats has a light in it. The others do not.",
    };
    const int bi = currentRun.getBossIndex();
    if (currentRun.isBossEncounter() && bi >= 0 && bi < 4) {
        Console::setHistoryCapture(true);
        for (const std::string& row : UIHelper::wrapRows(VIGIL_TELL[bi]))
            UIHelper::typeWrite(std::string(Color::DIM) + row + Color::RESET + "\n");
        Console::setHistoryCapture(false);
    }
    UIHelper::pause(120);
    // handleInput() will render the hand + menu side-by-side on first input
}

void Game::nextEncounter() {
    currentRun.nextEncounter();
    startEncounter();
}

// The forge screen, shared with the card reward once the pool runs dry.
// Returns true only if a card was actually upgraded.
bool Game::forgeMenu(const std::string& baseTitle) {
    if (playerDeck.totalCards() == 0) {
        notice("Your deck is empty. Nothing to upgrade.");
        return false;
    }

    std::vector<Card> allCards = playerDeck.getAllCardsOrdered();
    // Anything that cannot be upgraded sinks to the bottom. It is greyed out and
    // unpickable, so leaving it interleaved by rarity pushed the cards you can
    // actually act on down the list and onto later pages.
    std::stable_sort(allCards.begin(), allCards.end(),
        [](const Card& a, const Card& b) {
            const bool aDone = a.getUpgradeCount() >= a.getMaxUpgrades();
            const bool bDone = b.getUpgradeCount() >= b.getMaxUpgrades();
            if (aDone != bDone) return bDone;          // upgradable first
            return rarityRank(a) > rarityRank(b);
        });

    // Group identical cards together - picking a group upgrades every copy at once.
    std::vector<const Card*> groupCard;
    std::vector<int>         groupCount;
    for (size_t i = 0; i < allCards.size(); i++) {
        const Card& c = allCards[i];
        bool merged = false;
        for (size_t g = 0; g < groupCard.size(); g++) {
            if (groupCard[g]->getName() == c.getName()) {
                groupCount[g]++;
                merged = true;
                break;
            }
        }
        if (!merged) {
            groupCard.push_back(&allCards[i]);
            groupCount.push_back(1);
        }
    }

    // Paginated: a long collection makes a menu tall enough to trigger the
    // redraw-duplication glitch (the same root cause as the How To Play and
    // Tutorial fixes).
    const int PAGE_SIZE = 8;
    int totalGroups = (int)groupCard.size();
    int totalPages  = std::max(1, (totalGroups + PAGE_SIZE - 1) / PAGE_SIZE);
    int page = 0;
    bool committed = false;

    while (true) {
        int startIdx = page * PAGE_SIZE;
        int endIdx   = std::min(startIdx + PAGE_SIZE, totalGroups);

        // Same widgets as everywhere else, with the upgrade state as the
        // card's note line and maxed cards greyed out rather than listed
        // and then refused.
        std::vector<CardBar::Card> widgets;
        for (int g = startIdx; g < endIdx; g++) {
            const Card& c = *groupCard[g];
            int upgradesLeft = c.getMaxUpgrades() - c.getUpgradeCount();
            bool maxed = upgradesLeft <= 0;
            // Only ATTACK and DEFEND get the flat bonuses; specials use the
            // raw card value, so they show unmodified.
            const int nowVal  = gearedValue(c, c.getValue());
            const int nextVal = gearedValue(c, upgradedValue(c));
            CardBar::Card w = toWidget(c, nowVal, maxed);
            // On the forge the useful number is what it becomes, not what
            // it currently is - that is the decision being made here.
            if (!maxed) w.effect = upgradeFaceLine(c, nowVal, nextVal);
            if (groupCount[g] > 1) w.name += " x" + std::to_string(groupCount[g]);
            w.note = maxed ? "maxed"
                           : (std::to_string(upgradesLeft) + " upgrade"
                              + (upgradesLeft != 1 ? "s" : "") + " left");
            widgets.push_back(w);
        }

        std::string title = baseTitle;
        if (totalPages > 1)
            title += "   page " + std::to_string(page + 1) + "/" + std::to_string(totalPages);

        std::vector<CardBar::Action> acts;
        if (totalPages > 1) {
            acts.push_back(CardBar::Action{ "Previous page", page == 0 });
            acts.push_back(CardBar::Action{ "Next page",     page >= totalPages - 1 });
        }
        acts.push_back(CardBar::Action{ "Return", false });

        int choice = CardBar::pick(title, widgets, acts, 4);
        const int shown = endIdx - startIdx;

        if (choice <= -2) {                       // "+" opens the card text
            int ci = -2 - choice;
            if (ci >= 0 && ci < shown) {
                const Card& c = *groupCard[startIdx + ci];
                int left = c.getMaxUpgrades() - c.getUpgradeCount();
                std::string text = c.getDescription();
                if (left > 0) {
                    // showDetail word-wraps with no newline handling, so this reads as another
                    // sentence. Geared at the upgraded value, since gear is a percentage.
                    text += "   Upgrading takes it to " + std::to_string(gearedValue(c, upgradedValue(c)));
                    text += (c.getType() == CardType::DEFEND) ? " armor" 
                          : (c.getType() == CardType::ATTACK) ? " damage" : " value";
                    if (upgradedCost(c) < c.getCost())
                        text += " and drops the cost from " + std::to_string(c.getCost())
                              + " to " + std::to_string(upgradedCost(c));
                    else if (c.getCost() <= c.minCost())
                        text += " (cost stays at " + std::to_string(c.getCost())
                              + "; this card will not go lower)";
                    // upgradesLeft counts the one about to be applied, so
                    // what remains afterwards is one fewer.
                    const int after = left - 1;
                    text += after > 0
                          ? (". " + std::to_string(after) + " more upgrade"
                             + (after != 1 ? "s" : "") + " after that.")
                          : ". That is its last upgrade.";
                } else {
                    text += "   This card is fully upgraded.";
                }
                CardBar::showDetail(widgets[ci], text, c.getTypeString(),
                                    rarityWord(c), c.getUpgradeCount());
            }
            continue;
        }
        if (choice < 0) break;                    // backed out, nothing committed
        if (choice >= shown) {
            int act = choice - shown;
            if (totalPages > 1 && act == 0) { page--; continue; }
            if (totalPages > 1 && act == 1) { page++; continue; }
            break;                                // Return
        }

        int groupIdx = startIdx + choice;
        std::string beforeName = groupCard[groupIdx]->getName();
        std::string afterName  = beforeName + "+";

        const std::string confirmPrompt = "Upgrade " + beforeName + " to " + afterName + "?";
        if (!confirm(confirmPrompt)) continue; // declined - back to this page

        int upgradedCount = playerDeck.upgradeCardGroup(beforeName);
        if (upgradedCount > 0) {
            Audio::playSFX("upgrade");
            std::vector<Card> updated = playerDeck.getAllCardsOrdered();
            auto it = std::find_if(updated.begin(), updated.end(),
                                    [&](const Card& c) { return c.getName() == afterName; });
            std::string done = (upgradedCount > 1 ? ("All " + std::to_string(upgradedCount) + " ") : std::string())
                             + beforeName + (upgradedCount > 1 ? " cards" : "")
                             + " upgraded to " + afterName + ".";
            if (it != updated.end())
                done += "  cost " + std::to_string(it->getCost())
                      + ", value " + std::to_string(it->getValue()) + ".";
            if (hasRelic(Relic::FORGE_HAMMER) && playerHealth < maxPlayerHealth) {
                const int heal = std::min(maxPlayerHealth - playerHealth, maxPlayerHealth * 15 / 100);
                playerHealth += heal;
                done += "  The hammer's heat mends you: +" + std::to_string(heal) + " HP.";
            }
            notice(done);
        }
        committed = true;
        break;
    }
    return committed;
}

void Game::restSite() {
    // Rest/Forge commit and end the visit; Return loops back to this menu.
    while (true) {
    // Buttons rather than a text list, so the rest site matches the reward and
    // forge screens it sits between. The descriptions ride on the labels since
    // there are no cards here to carry them.
    std::vector<CardBar::Action> siteActs{
        CardBar::Action{ "Rest", "heal to full  (" + std::to_string(playerHealth)
                         + "/" + std::to_string(maxPlayerHealth) + " HP)", false },
        // The step scales with the card's rarity, so the label quotes the range
        // rather than one number that is wrong for most cards.
        CardBar::Action{ "Forge",     "upgrade a card  (+2 to +5 value by rarity, first upgrade -1 cost)", false },
        CardBar::Action{ "Equipment", "choose what to wear", false },
        CardBar::Action{ "View Deck", "browse and discard", false },
        CardBar::Action{ "Skip",      "press on without resting", false },
    };
    // Something new to remember here, or no button at all. The rest right
    // after a boss already counts that boss's piece. First time through only.
    int sitNext = -1;
    if (currentRun.getCycle() == 0) {
        const int stage = std::max(0, std::min(5, currentRun.areaBossesCleared()
                                                  + (currentRun.isBossEncounter() ? 1 : 0)));
        int i = satCount;
        while (i < SIT_COUNT && SIT[i].stage < stage) i++;
        if (i < SIT_COUNT && SIT[i].stage == stage) sitNext = i;
    }
    if (sitNext >= 0)
        siteActs.insert(siteActs.end() - 1,
                        CardBar::Action{ "Sit a while", "by the fire, and remember something", false });
    const int skipChoice = (int)siteActs.size() - 1;
    const int sitChoice  = sitNext >= 0 ? skipChoice - 1 : -2;
    int siteChoice = CardBar::pick("Rest site", {}, siteActs, 0);
    if (siteChoice < 0) siteChoice = skipChoice;   // ESC leaves without resting

    if (siteChoice == sitChoice) {
        // Free, and the rest site is still there afterwards.
        satCount = sitNext + 1;
        showSitBeat(SIT[sitNext].lines, wornArmor);
        continue;
    } else if (siteChoice == 0) {
        const std::string confirmPrompt = "Rest and heal to full?  (" + std::to_string(playerHealth)
                                        + "/" + std::to_string(maxPlayerHealth) + " HP)";
        if (!confirm(confirmPrompt)) continue; // declined - back to the rest site menu

        playerHealth = maxPlayerHealth;
        Audio::playSFX("heal");
        notice("You rest and fully recover to " + std::to_string(maxPlayerHealth) + " HP.");
        break; // committed - progress as normal
    } else if (siteChoice == 1) {
        if (forgeMenu("Forge   pick a card to upgrade")) break; // committed - progress as normal
        continue;                                              // nothing forged - back to the menu
    } else if (siteChoice == 2) {
        equipmentMenu();
        continue; // changing gear is free too
    } else if (siteChoice == 3) {
        viewDeckManage();
        continue; // browsing/discarding never costs your rest site visit - back to the rest site menu
    } else {
        const std::string confirmPrompt = "Skip the rest site and press on?";
        if (!confirm(confirmPrompt)) continue; // declined - back to the rest site menu

        notice("You press on without resting.");
        break;
    }
    } // while(true)
}

// One piece of gear as a card. Tier is the sheet tier: 0 is the starting kit,
// 1-6 the six named tiers in the order they drop.
static CardBar::Card gearCard(bool weapon, int tier, bool wearing) {
    CardBar::Card c;
    c.name = tier == 0 ? std::string(weapon ? "Wooden Sword" : "Leather Armor")
                       : (weapon ? weaponTierAt(tier - 1) : armorTierAt(tier - 1)).name;
    c.elemTag = weapon ? "[WEAPON]" : "[ARMOR]";
    c.effect = weapon ? std::string(WEAPON_PASSIVE[std::max(0, std::min(8, tier))]) : armourProfileText(tier);
    c.note = wearing ? "wearing" : "";
    c.icon = weapon ? weaponIconFor(tier) : armorIconFor(tier);
    c.item = true;
    c.tint = Console::xterm256Public(Stripe::ITEM);
    c.nameColor = Console::xterm256Public(equipTintFor(tier));
    return c;
}

// Everything claimed so far, and what is being worn. Wearing a piece only
// changes the look: the bonuses are the sum of every tier picked up. Two
// steps, slot then piece, so the list never outgrows the screen.
void Game::equipmentMenu() {
    while (true) {
        std::vector<CardBar::Card> slots{ gearCard(true, wornWeapon, true), gearCard(false, wornArmor, true) };
        slots[0].note = "change weapon";
        slots[1].note = "change armor";
        // The relics held, after the two slots, so everything the run has
        // given you is on one screen.
        std::vector<int> held;
        for (int i = 0; i < Relic::COUNT; i++) {
            if (!hasRelic(i)) continue;
            CardBar::Card r;
            r.name = Relic::INFO[i].name;
            r.elemTag = "[RELIC]";
            r.effect = Relic::INFO[i].face;
            r.risk = Relic::INFO[i].cursed;
            r.tint = RELIC_STRIPE;
            r.nameColor = RELIC_NAME;
            r.icon = RELIC_ICON0 + i;
            r.item = true;
            slots.push_back(r);
            held.push_back(i);
        }
        std::vector<CardBar::Action> back{ CardBar::Action{ "Back", "to the rest site", false } };
        const int slot = CardBar::pick(held.empty() ? "Equipment   pick a slot" : "Equipment and relics",
                                       slots, back, std::min(6, (int)slots.size()));
        if (slot <= -2) {
            const int ci = -2 - slot;
            if (ci == 0) CardBar::showDetail(slots[0], WEAPON_PASSIVE_LONG[wornWeapon], "WEAPON", "", 0);
            else if (ci == 1) CardBar::showDetail(slots[1], "It " + armourProfileText(wornArmor)
                                                  + ": 25% less damage from what it resists, 25% more from its weakness.",
                                                  "ARMOR", "", 0);
            else if (ci - 2 < (int)held.size())
                CardBar::showDetail(slots[ci], Relic::INFO[held[ci - 2]].text, "RELIC", "", 0);
            continue;
        }
        if (slot >= 2 && slot < (int)slots.size()) {
            CardBar::showDetail(slots[slot], Relic::INFO[held[slot - 2]].text, "RELIC", "", 0);
            continue;
        }
        if (slot < 0 || slot > 1) return;

        const bool weapon = slot == 0;
        // Everything claimed on this road, trophies included: they are rungs
        // on the same ladder now, not a cupboard you always have access to.
        const int top = std::min(maxGearTier(), weapon ? weaponTier : armorTier);
        std::vector<int> choices;
        for (int t = top; t >= 0; t--) choices.push_back(t);
        if (choices.size() <= 1) {
            notice(weapon ? "The wooden sword is the only weapon you have found so far."
                          : "The leather armor is the only armor you have found so far.");
            continue;
        }
        std::vector<CardBar::Card> pieces;
        for (int t : choices)
            pieces.push_back(gearCard(weapon, t, t == (weapon ? wornWeapon : wornArmor)));
        const int chosen = CardBar::pick(weapon ? "Weapons   pick one to carry" : "Armor   pick one to wear",
                                       pieces, back, 4);
        if (chosen < 0 || chosen >= (int)choices.size()) continue;
        (weapon ? wornWeapon : wornArmor) = choices[chosen];
        EnemyArt::setGearTiers(wornWeapon, wornArmor);
        Audio::playSFX("upgrade");
    }
}

void Game::viewDeckManage() {
    int page = 0;
    while (true) {
        UIHelper::clearScreen();
        if (playerDeck.totalCards() == 0) {
            notice("Your deck is empty.");
            return;
        }

        std::vector<Card> allCards = playerDeck.getAllCardsOrdered();
        std::stable_sort(allCards.begin(), allCards.end(),
            [](const Card& a2, const Card& b2) { return rarityRank(a2) > rarityRank(b2); });

        // Group identical cards (dupes are just how the deck deals out cards).
        std::vector<const Card*> groupCard;
        std::vector<int>         groupCount;
        for (size_t i = 0; i < allCards.size(); i++) {
            const Card& c = allCards[i];
            bool merged = false;
            for (size_t g = 0; g < groupCard.size(); g++)
                if (groupCard[g]->getName() == c.getName()) { groupCount[g]++; merged = true; break; }
            if (!merged) { groupCard.push_back(&allCards[i]); groupCount.push_back(1); }
        }

        // Ten to a page, two rows of five. A full deck runs to dozens of unique
        // cards, which as widgets would not fit a screen at a readable size.
        const int PER_PAGE = 10;
        const int pages = std::max(1, ((int)groupCard.size() + PER_PAGE - 1) / PER_PAGE);
        page = std::max(0, std::min(page, pages - 1));
        const int first = page * PER_PAGE;
        const int count = std::min(PER_PAGE, (int)groupCard.size() - first);

        std::vector<CardBar::Card> widgets;
        for (int i = 0; i < count; i++) {
            const Card& c = *groupCard[first + i];
            CardBar::Card w = toWidget(c, gearedValue(c, c.getValue()));
            if (groupCount[first + i] > 1) w.name += " x" + std::to_string(groupCount[first + i]);
            // This screen is a library, not the forge. The note says which it is
            // on every card, since the two screens are otherwise identical and
            // the forge is where upgrades happen.
            const int ups = c.getUpgradeCount();
            // Red, and a verb: this screen sits one menu away from the forge
            // and looks identical, and picking here destroys a card.
            w.note = ups > 0 ? ("discards one  +" + std::to_string(ups)) : std::string("discards one");
            w.noteColor = SDL_Color{ 226, 96, 88, 255 };
            widgets.push_back(w);
        }

        // Say what picking a card actually does - it is a destructive action
        // and the grid alone does not imply it.
        std::string title = "Your Deck   picking a card DISCARDS a copy   "
                          + std::to_string(allCards.size()) + " cards, "
                          + std::to_string(groupCard.size()) + " unique";
        if (pages > 1) title += "   page " + std::to_string(page + 1) + "/" + std::to_string(pages);

        std::vector<CardBar::Action> acts;
        if (pages > 1) {
            acts.push_back(CardBar::Action{ "Previous page", page == 0 });
            acts.push_back(CardBar::Action{ "Next page",     page >= pages - 1 });
        }
        acts.push_back(CardBar::Action{ "Return", false });

        int choice = CardBar::pick(title, widgets, acts, 5);

        if (choice <= -2) {   // "+" opens the full card text
            int ci = -2 - choice;
            if (ci >= 0 && ci < count) {
                const Card& c = *groupCard[first + ci];
                CardBar::showDetail(widgets[ci], c.getDescription(), c.getTypeString(),
                                    rarityWord(c), c.getUpgradeCount());
            }
            continue;
        }
        if (choice < 0) return;

        if (choice >= count) {
            int act = choice - count;
            if (pages > 1 && act == 0) { page--; continue; }
            if (pages > 1 && act == 1) { page++; continue; }
            return;                                   // Return
        }

        if (playerDeck.totalCards() <= 1) {
            notice("You must keep at least one card.");
            continue;
        }

        std::string name = groupCard[first + choice]->getName();
        const std::string confirmPrompt = "Discard one " + name + "?";
        if (!confirm(confirmPrompt)) continue;

        if (playerDeck.removeCardByName(name)) {
            notice("Discarded one " + name + " from your deck.");
            // stay here: browsing and discarding never cost the rest site visit
        }
    }
}

void Game::handleEncounterWin() {
    if (inSecretEncounter) { handleSecretWin(); return; }
    // The knight goes down and the moon stands up in it. Checked before the
    // encounter is counted as won, because it has not been.
    if (enemy.getBossType() == BossType::SHADOW_KNIGHT && !trueFormPhase && trueFormEarned()) {
        beginTrueForm();
        return;
    }
    Hud::setActive(false);   // the fight is over: no stale HP bars on the rewards
    currentRun.winEncounter();
    EnemyArt::printBattleDeath(enemy.getType(), enemy.getBossType());
    Audio::playSFX("win");
    UIHelper::pause(300);
    UIHelper::clearScreen();
    // Drawn in the display face over the console rather than printed into it:
    // at grid size "Victory!" read as one more line of log text.
    UIHelper::showHeadline("Victory!", 134, 209, 107);
    for (int i = 0, pad = Console::rows() * 42 / 100; i < pad; i++) std::cout << "\n";
    UIHelper::pause(300);
    UIHelper::printCentered("Enemies defeated: " + std::string(Color::GREEN)
                            + std::to_string(currentRun.getEncountersWon()) + Color::RESET);

    if (enemy.isBoss()) {
        int bossOccurrence = currentRun.getBossNumber(); // 1-indexed, including this one

        const int BOSS_HP_BOOST = 50; // permanent, every boss kill
        maxPlayerHealth += BOSS_HP_BOOST;
        playerHealth += BOSS_HP_BOOST;
        Audio::playSFX("heal");
        std::cout << "\n";
        UIHelper::printCentered(std::string(Color::HEAL) + "Victory strengthens you. Max HP permanently increased by "
                                + std::to_string(BOSS_HP_BOOST) + "  (" + std::to_string(playerHealth)
                                + "/" + std::to_string(maxPlayerHealth) + ")" + Color::RESET);
        UIHelper::pause(500);

        // Zone outro: the soul fragment recovered from this boss. Bosses 1-5
        // (Colossus through Dragon) each close out a zone; the Shadow Knight
        // (6) has its own ending in handleGameVictory() instead.
        if (currentRun.getCycle() == 0 && bossOccurrence >= 1 && bossOccurrence <= 5) {
            UIHelper::waitForKey();
            UIHelper::showHeadline("", 0, 0, 0);
            showStoryBeat(ZONE_STORY[bossOccurrence - 1].outro);
        }
        UIHelper::showHeadline("", 0, 0, 0);

        // Every Shadow Knight kill, not only the first: each one closes a wave,
        // hands out a legendary and asks whether to go round again. The ending
        // story inside is what stays first-time-only.
        if (enemy.getBossType() == BossType::SHADOW_KNIGHT) {
            UIHelper::showHeadline("", 0, 0, 0);
            handleGameVictory();
            return;
        }

        // Announce the regular-reward rarity gate lifting. Legendary is never
        // mentioned here: it stays a silent boss-reward-only rarity. Waits for a
        // keypress so the next screen cannot blow past it.
        if (bossOccurrence == 1) {
            std::cout << "\n";
            UIHelper::printCentered(std::string(Color::BOLD) + Color::YELLOW
                                    + "rare cards have been unlocked" + Color::RESET);
            UIHelper::waitForKey();
        } else if (bossOccurrence == 2) {
            std::cout << "\n";
            UIHelper::printCentered(std::string(Color::BOLD) + Color::YELLOW
                                    + "super rare cards have been unlocked" + Color::RESET);
            UIHelper::waitForKey();
        }

        offerBossReward();
        // Every 2nd boss defeated (occurrence 2, 4, 6...) also grants a shot at +1 max energy.
        if (bossOccurrence % 2 == 0) offerExtraPlay();
        // Each area closes with a seal. The Dragon's would sit one fight before
        // the Shadow Knight's, so the mountain's comes after the peak instead.
        if (enemy.getBossType() != BossType::DRAGON) offerSeal();
    } else {
        // No keypress here on purpose: 44 regular wins a run, and a prompt on
        // every one of them is 44 keys of friction. The banner just holds.
        UIHelper::pause(700);
        UIHelper::showHeadline("", 0, 0, 0);
        offerCardReward();
    }

    if (currentRun.getCurrentEncounter() % gearInterval == 0)
        offerEquipmentDrop();

    // Every 12th encounter, for as long as the run lasts.
    if (currentRun.getCurrentEncounter() % BOON_INTERVAL == 0)
        offerBoon();

    // Relics on the 6th and every 12th after, so they fall between the boons.
    {
        const int enc = currentRun.getCurrentEncounter();
        if (enc >= RELIC_FIRST && (enc - RELIC_FIRST) % RELIC_INTERVAL == 0) offerRelic();
    }

    restSite();

    offerContinueOrEndRun();
}

// Which road to walk. Only cleared roads are listed, so a first run never
// sees this screen. Returns false if the player backed out.
bool Game::chooseMode(Mode& out, bool& carryWinningRun) {
    carryWinningRun = false;
    if (clearedMask == 0) { out = Mode::NORMAL; return true; }
    while (true) {
        std::vector<CardBar::Action> acts;
        std::vector<Mode> modes;
        acts.push_back(CardBar::Action{ "Normal", "the fifty as they come", false });
        modes.push_back(Mode::NORMAL);
        if (clearedMask & 1) {
            acts.push_back(CardBar::Action{ "Random", "the same fifty, in an order you have not fought before", false });
            modes.push_back(Mode::RANDOM);
            acts.push_back(CardBar::Action{ "Hard", "every enemy as strong as fifty fights further on, and redder with it", false });
            modes.push_back(Mode::HARD);
        }
        if (clearedMask & 2) {
            acts.push_back(CardBar::Action{ "Random Hard",
                "the hard fifty, in an order you have not fought before", false });
            modes.push_back(Mode::RANDOM_HARD);
        }
        acts.push_back(CardBar::Action{ "Back", "to the menu", false });

        UIHelper::clearScreen();
        const int pick = CardBar::pick("Choose your road", {}, acts, 0);
        if (pick < 0 || pick >= (int)modes.size()) return false;
        out = modes[pick];

        // The harder roads were opened by a run that finished the fifty, and
        // that run is still on file. Walking them with a starting deck is
        // allowed, but it is not what the unlock is for.
        if ((out == Mode::HARD || out == Mode::RANDOM_HARD) && hasWinSave()) {
            std::vector<CardBar::Action> carry{
                CardBar::Action{ "Carry your winning run", "its deck, gear, boons and relics, at encounter 1", false },
                CardBar::Action{ "Start fresh", "a starting deck, and starting gear built for this road", false },
                CardBar::Action{ "Back", "pick another road", false },
            };
            UIHelper::clearScreen();
            const int c = CardBar::pick("You have a run that finished the fifty.", {}, carry, 0);
            if (c < 0 || c == 2) continue;
            carryWinningRun = (c == 0);
        }
        return true;
    }
}

// Start a run on the chosen road. Carrying brings the winning run's deck and
// everything it earned, but not its progress: encounter 1, full health, no
// seals broken and the moon not yet met.
void Game::startRunInMode(Mode m, bool carryWinningRun) {
    if (!carryWinningRun && difficultyFor(m) > 0) applyRoadStart(m);
    if (carryWinningRun && loadWinSave()) {
        playerHealth = maxPlayerHealth;
        playerArmor = 0;
        playerArmorPersistTurns = 0;
        playerStatus.reset();
        sealsBroken = 0;
        moonZonesSeen = 0;
        moonstruckMet = 0;
        satCount = 0;
        redThreadUsed = false;
        randomSeed = 0;          // a new road gets its own order
        currentSaveSlot = 0;     // and owns no slot until it is saved
        roadBonus = 0;           // the carried run brings its own numbers
        roadGearPct = 0;
    }
    runMode = m;
    currentRun.startRun();
    currentRun.setDifficulty(difficultyFor(m));
    buildRandomOrder();
    moonZonesSeen = 0;
    EnemyArt::setGearTiers(wornWeapon, wornArmor);
}

// A fresh run on Hard meets enemies scaled as if fifty fights had
// happened, so the knight arrives with what those fights would have given
// him: health, energy, full gear and a veteran's card bonus. Only the deck
// is a beginner's.
void Game::applyRoadStart(Mode m) {
    (void)m;   // both harder roads are the same fight, in a different order
    maxPlayerHealth = 260;
    playerHealth = maxPlayerHealth;
    maxEnergy = 5;
    playerEnergy = maxEnergy;
    // No gear handed over: the ladder still starts at the Rusty Blade. What
    // changes is the kit he starts in - the same wooden sword and leather
    // armour, made for a road where the first enemy has 500 health.
    roadGearPct = 90;
    roadBonus   = 10;
}

// Random mode: a shuffled bag of the 44 regulars, so nothing repeats until
// all have been fought, rebuilt from the saved seed so a loaded run keeps
// its order.
void Game::buildRandomOrder() {
    randomOrder.clear();
    if (runMode != Mode::RANDOM && runMode != Mode::RANDOM_HARD) return;
    if (randomSeed == 0) randomSeed = (unsigned)std::random_device{}();
    for (int i = 0; i < 44; i++) randomOrder.push_back(i);
    std::mt19937 gen(randomSeed);
    std::shuffle(randomOrder.begin(), randomOrder.end(), gen);
}

int Game::rosterIndexFor(int regularIndex) const {
    if (randomOrder.empty()) return regularIndex % 44;
    return randomOrder[regularIndex % (int)randomOrder.size()];
}

const char* Game::modeName() const {
    switch (runMode) {
        case Mode::RANDOM:      return "Random";
        case Mode::HARD:        return "Hard";
        case Mode::RANDOM_HARD: return "Random Hard";
        default:                return "Normal";
    }
}

bool Game::trueFormEarned() const { return sealsBroken >= 4 && moonstruckMet >= 1; }

// The vigil in the boss's heart, offered once that boss is down. Putting
// it out makes everything ahead tougher; all four out is what the true
// form needs.
void Game::offerSeal() {
    UIHelper::clearScreen();
    Hud::setActive(false);
    EnemyArt::setSealFrame(0);
    const std::string title = sealsBroken == 0
        ? "A vigil burns in its chest. The moon set it there so it would not have to sleep"
        : "Another vigil. " + std::to_string(sealsBroken) + " already out";
    std::vector<CardBar::Action> acts{
        CardBar::Action{ "Put out the vigil", "it answers: everything ahead +" + std::to_string(SEAL_HP_PCT)
                         + "% health and +" + std::to_string(SEAL_ATK_PCT) + "% attack for the rest of the run", false },
        CardBar::Action{ "Leave it burning", "let it keep its night, and press on as you are", false },
    };
    // Undimmed, with the buttons below the seal rather than over it.
    CardBar::setNextGridStyle(false, 64);
    const int choice = CardBar::pick(title, {}, acts, 0);
    if (choice != 0 || !confirm("Put it out? It stays out for the rest of the run.")) {
        EnemyArt::setSealFrame(-1);
        return;
    }

    // Three cracks and the break, each on its own beat.
    UIHelper::clearScreen();
    for (int f = 1; f <= 4; f++) {
        EnemyArt::setSealFrame(f);
        if (f < 4) {
            Audio::playSFXPitched("hit", 0.6f + 0.12f * f);
            Platform::shake(160, 2.0f + f);
            UIHelper::pause(420);
        } else {
            Audio::playSFX("boss");
            Platform::shake(360, 7.0f);
            UIHelper::pause(700);
        }
    }
    sealsBroken++;
    for (int i = 0, pad = Console::rows() * 64 / 100; i < pad; i++) std::cout << "\n";
    UIHelper::printCentered(std::string(Color::BOLD) + Color::RED
        + "The vigil goes out. Somewhere above you, something that cannot afford to sleep "
          "stops pretending it has time." + Color::RESET);
    UIHelper::printCentered(std::string(Color::DIM) + "Everything ahead now has +"
        + std::to_string(sealsBroken * SEAL_HP_PCT) + "% health and +"
        + std::to_string(sealsBroken * SEAL_ATK_PCT) + "% attack." + Color::RESET);
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
    UIHelper::waitForKey("");
    EnemyArt::setSealFrame(-1);
}

void Game::handleGameVictory() {
    // Normal and Random get the first ending; Hard gets its own.
    const bool firstTime = runMode == Mode::NORMAL || runMode == Mode::RANDOM;
    UIHelper::waitForKey();
    UIHelper::clearScreen();
    UIHelper::padToCenter(4);
    UIHelper::printCenteredWrapped(std::string(Color::BOLD) + Color::MAGENTA
        + (firstTime
           ? "The false moon is put out. It goes back down into its long sleep, and it takes "
             "nothing of him with it."
           : "It is put out again, on a road that was never meant to be walked twice.")
        + Color::RESET, 68, true);
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
    UIHelper::waitForKey("");

    // The ending gets the display face, same as the victory banner.
    UIHelper::clearScreen();
    UIHelper::showHeadline("VICTORY ETERNAL", 240, 200, 60);
    for (int i = 0, pad = Console::rows() * 44 / 100; i < pad; i++) std::cout << "\n";
    UIHelper::printCenteredWrapped(firstTime
        ? "The knight stands on the peak wearing all of himself again: his strength, his "
          "speed, his hands, his soul, and the shape nothing else is walking around in any "
          "more. Whole, and no longer only a legend."
        : "Fifty encounters on a road that hits like a hundred. You went back up knowing "
          "exactly what was waiting, and it still was not enough. There is nothing on this "
          "mountain that has not already lost to you.", 64);
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
    UIHelper::waitForKey("");
    UIHelper::showHeadline("", 0, 0, 0);

    std::vector<Card> legendaries = rewardPool.getUnownedLegendaries(playerDeck.getAllCardNames());
    if (!legendaries.empty()) {
        // Random rather than front(): with three legendaries, always handing
        // out the first in pool order meant the same card every run.
        static thread_local std::mt19937 lg2(std::random_device{}());
        std::uniform_int_distribution<> lpick(0, (int)legendaries.size() - 1);
        const Card& leg = legendaries[lpick(lg2)];
        playerDeck.addCard(leg);
        runStats.addCardToRun();
        Audio::playSFX("upgrade");
        UIHelper::clearScreen();
        UIHelper::padToCenter(9);
        UIHelper::printCentered(std::string(Color::BOLD) + Color::YELLOW
                                + "From the dissolving shadow you claim its heart" + Color::RESET);
        std::cout << "\n";
        UIHelper::printCentered(std::string(Color::BOLD) + rarityTint(leg) + leg.getName()
                                + Color::RESET + "  " + Color::DIM + "(LEGENDARY)" + Color::RESET);
        UIHelper::printCenteredWrapped(std::string(Color::DIM) + leg.getDescription() + Color::RESET, 64);
        std::cout << "\n";
        UIHelper::printCenteredWrapped(std::string(Color::DIM)
            + "Added to your deck. If you play again, you can carry one card into "
              "the new run, even this one." + Color::RESET, 64);
        std::cout << "\n";
        UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
        UIHelper::waitForKey("");
    } else {
        UIHelper::clearScreen();
        notice("You already wield the legendary art. Claim a final trophy instead.");
        offerBossReward();
    }

    inEncounter = false;
    Hud::setActive(false);   // the panel belongs to the fight

    // Fifty is the whole game now. What a clear buys is the harder roads, and
    // a permanent record of the run that cleared it: dying later empties a
    // save slot, but it can never take this away.
    const int before = clearedMask;
    const int beforeSets = unlockedSets;
    recordClear();

    UIHelper::clearScreen();
    UIHelper::padToCenter(4);
    if (!(before & 1) && (clearedMask & 1)) {
        UIHelper::printCenteredWrapped(std::string(Color::BOLD) + Color::YELLOW
            + "Two new roads open behind you." + Color::RESET, 68, true);
        std::cout << "\n";
        UIHelper::printCenteredWrapped(std::string(Color::DIM)
            + "It is asleep, not gone, and what it dreams about is the road. "
              "RANDOM: the same fifty, in an order you have never fought. "
              "HARD: the dream, where it remembers every step you took. Start either from the "
              "menu, with this run's deck or a fresh one." + Color::RESET, 68, true);
    } else if (!(before & 2) && (clearedMask & 2)) {
        UIHelper::printCenteredWrapped(std::string(Color::BOLD) + Color::YELLOW
            + "RANDOM HARD opens." + Color::RESET, 68, true);
        std::cout << "\n";
        UIHelper::printCenteredWrapped(std::string(Color::DIM)
            + "The same hard fifty, in an order you have never walked. Dreams do not keep "
              "things where you left them." + Color::RESET, 68, true);
    } else {
        UIHelper::printCenteredWrapped(std::string(Color::DIM)
            + "There is no harder road left. This one is yours." + Color::RESET, 68, true);
    }
    // The set is the part you keep: a rung above the Legendary gear on every
    // run's ladder from here on.
    std::cout << "\n";
    if (!(beforeSets & 1) && (unlockedSets & 1)) {
        UIHelper::printCenteredWrapped(std::string(Color::BOLD) + Color::MAGENTA
            + "You take the plate and the blade it wore of you. They fit, which is the part "
              "that takes getting used to."
            + Color::RESET, 68, true);
        UIHelper::printCenteredWrapped(std::string(Color::DIM)
            + "From now on they turn up in every run, one step past the Legendary gear."
            + Color::RESET, 68, true);
    } else if (!(beforeSets & 2) && (unlockedSets & 2)) {
        UIHelper::printCenteredWrapped(std::string(Color::BOLD) + Color::MAGENTA
            + "You take what was under the plate. Lighter than it looks, and colder than it "
              "has any business being."
            + Color::RESET, 68, true);
        UIHelper::printCenteredWrapped(std::string(Color::DIM)
            + "From now on it turns up in every run, at the very top of the gear."
            + Color::RESET, 68, true);
    }
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
    UIHelper::waitForKey("");

    deleteCurrentSave();
    currentRun.loseRun(); // main loop routes to finishRun()
}

void Game::offerContinueOrEndRun(bool justWonEncounter) {
    UIHelper::clearScreen();
    const std::string title = std::string(justWonEncounter ? "Round complete" : "Resume run")
        + "        " + std::to_string(currentRun.getEncountersWon()) + " cleared"
        + "        " + std::to_string(playerHealth) + "/" + std::to_string(maxPlayerHealth) + " HP";
    // All three outcomes are on screen, so nobody finishes a run without
    // realising they could keep it.
    std::vector<CardBar::Action> contActs{
        CardBar::Action{ "Continue",      "enter the next encounter", false },
        CardBar::Action{ "Save and quit", "keep this run, resume it from the main menu", false },
        CardBar::Action{ "End run",       "finish here without saving", false },
    };
    int choice = CardBar::pick(title, {}, contActs, 0);
    if (choice < 0) choice = 0;

    if (choice == 0) {
        if (justWonEncounter) nextEncounter();
        else startEncounter(); // the loaded encounter hasn't been fought yet - don't skip past it
    } else {
        if (choice == 1) {
            const int slot = chooseSaveSlot("Save this run", /*forSaving*/true);
            if (slot == 0) return offerContinueOrEndRun(justWonEncounter);  // backed out
            // Saving skips nextEncounter(), so advance the counter here or the
            // save would point at the fight just won and replay it on load.
            if (justWonEncounter) currentRun.nextEncounter();
            saveGame(slot);
            notice("Saved to slot " + std::to_string(slot)
                   + ". Pick Load Save on the main menu to carry on. If you die, this save goes with you.");
        }
        inEncounter = false;
    Hud::setActive(false);   // the panel belongs to the fight
        currentRun.loseRun();
    }
}

// Every card the pool can reach at this cost is already in the deck, which
// can happen inside one run, so this has to be a real reward. A forge visit
// goes first.
void Game::offerExhaustedReward() {
    bool anyUpgradable = false;
    for (const Card& c : playerDeck.getAllCardsOrdered()) {
        if (c.getUpgradeCount() < c.getMaxUpgrades()) { anyUpgradable = true; break; }
    }

    if (anyUpgradable) {
        notice("You already carry every card this road can offer."
               "  The forge is lit instead.");
        if (forgeMenu("Nothing new to take   upgrade a card instead")) return;
    }

    // Passing no owned names is the point: same weighted roll, duplicates
    // allowed. generateWeightedRewards still erases each pick from its pool, so
    // the three on screen are distinct from each other.
    std::vector<Card> dupes = rewardPool.generateWeightedRewards(
        3 + rewardChoiceBonus, upgrades.isActive(4), maxEnergy, {},
        std::min(2, currentRun.getCurrentEncounter() / 10), luckBonus());
    if (dupes.empty()) { notice("Nothing left to offer."); return; }

    presentCardChoice(dupes, "Nothing new left   take a second copy",
                      "Skip this reward, take none of the three?");
}

void Game::offerCardReward() {
    UIHelper::clearScreen();
    bool rarityBoost = upgrades.isActive(4);
    // Gated by bosses defeated: Uncommon only, +Rare after 1st boss, +Super Rare after 2nd.
    // The harder roads are post-game: the rarity gate has already been passed
    // once, and gating it again only makes the first ten fights poorer.
    int maxRarityUnlocked = (runMode == Mode::HARD || runMode == Mode::RANDOM_HARD)
                          ? 2 : std::min(2, currentRun.getCurrentEncounter() / 10);
    std::vector<Card> rewards = rewardPool.generateWeightedRewards(
        3 + rewardChoiceBonus, rarityBoost, maxEnergy,
        playerDeck.getAllCardNames(), maxRarityUnlocked, luckBonus());

    if (rewards.empty()) { offerExhaustedReward(); return; }

    presentCardChoice(rewards, "Pick a card to add to your deck",
                      "Skip this reward, take none of the three?",
                      [=]() {
                          return rewardPool.generateWeightedRewards(
                              3 + rewardChoiceBonus, rarityBoost, maxEnergy,
                              playerDeck.getAllCardNames(), maxRarityUnlocked, luckBonus());
                      });
}

// The three-card pick screen, shared by the normal reward and the exhausted
// fallback so both behave identically (details on "+", confirm before taking).
void Game::presentCardChoice(const std::vector<Card>& offered,
                             const std::string& title, const std::string& skipPrompt,
                             std::function<std::vector<Card>()> reroll) {
    std::vector<Card> rewards = offered;
    std::vector<CardBar::Card> widgets;
    for (const Card& c : rewards) widgets.push_back(toWidget(c, gearedValue(c, c.getValue())));
    // Bone Dice: one reroll per screen.
    bool canReroll = reroll && hasRelic(Relic::BONE_DICE);

    while (true) {
        std::vector<CardBar::Action> acts{ CardBar::Action{ "Skip", false } };
        if (canReroll) acts.push_back(CardBar::Action{ "Reroll", "the Bone Dice: new cards, once", false });
        int choice = CardBar::pick(title, widgets, acts, (int)widgets.size());
        if (canReroll && choice == (int)rewards.size() + 1) {
            std::vector<Card> fresh = reroll();
            canReroll = false;
            if (!fresh.empty()) {
                rewards = fresh;
                widgets.clear();
                for (const Card& c : rewards) widgets.push_back(toWidget(c, gearedValue(c, c.getValue())));
                Audio::playSFX("special");
            }
            continue;
        }

        // "+" opens that card's details, then drops back to the same choice.
        if (choice <= -2) {
            int ci = -2 - choice;
            if (ci >= 0 && ci < (int)rewards.size())
                CardBar::showDetail(widgets[ci], rewards[ci].getDescription(),
                                    rewards[ci].getTypeString(), rarityWord(rewards[ci]),
                                    rewards[ci].getUpgradeCount());
            continue;
        }

        if (choice < 0 || choice >= (int)rewards.size()) {
            if (!confirm(skipPrompt)) continue;
            notice("Reward skipped.");
            return;
        }

        // Taking is the costly misclick, not skipping: an unwanted card sits in
        // the deck for the rest of the run and only comes out at a rest site.
        // The boss screen already asked - this is the one seen every fight.
        const std::string takePrompt = "Add " + rewards[choice].getName() + " to your deck?";
        if (!confirm(takePrompt)) continue;   // declined - back to the three

        playerDeck.addCard(rewards[choice]);
        runStats.addCardToRun();
        notice("Added " + rewards[choice].getName() + " to your deck.");
        return;
    }
}

void Game::applyUpgrades() {
    maxPlayerHealth += upgrades.getHealthBonus();
    playerHealth = maxPlayerHealth;
}

// Each point of Luck adds this many percentage points to every roll in the
// run. Kept in one place so "increases all odds" stays literally true rather
// than something that has to be remembered at each call site.
int Game::luckBonus() const { return runLuck * 2; }

void Game::offerBoon() {
    UIHelper::clearScreen();
    // Shown as cards rather than plain buttons so they read as something you are
    // picking up, and so they can carry the white boon stripe.
    auto boonCard = [](const std::string& name, const std::string& face,
                       const std::string& note) {
        CardBar::Card b;
        b.name = name; b.effect = face; b.note = note;
        b.elemTag = "[BOON]";
        b.tint = Console::xterm256Public(Stripe::BOON);
        b.nameColor = Console::xterm256Public(Stripe::BOON);
        return b;
    };
    std::vector<CardBar::Card> widgets{
        boonCard("Fortune",   "+2% all rolls", "rarity, drops, every chance roll"),
        boonCard("Endurance", "+1 card/turn",  "every turn, for the rest of the run"),
        boonCard("Foresight", "+1 reward",     "one more card to choose from on every reward"),
        boonCard("Attunement","+8% elements",  "your elemental attacks land their status more often"),
    };
    // Scavenger is only offered while there is room to shorten the gap. Taking
    // it twice would otherwise be a wasted pick on a screen of five.
    const bool canScavenge = gearInterval > 2;
    if (canScavenge)
        widgets.push_back(boonCard("Scavenger", "gear sooner",
                                   "equipment every " + std::to_string(gearInterval - 1)
                                   + " encounters instead of " + std::to_string(gearInterval)));
    std::vector<CardBar::Action> acts{ CardBar::Action{ "Take none", "walk on empty-handed", false } };
    // The face only has room for a few words; this is what "+" shows.
    std::vector<std::string> details = {
        "Every chance roll for the rest of this run is 2 percentage points kinder, "
        "including card rarity and reward drops. Stacks each time you take it.",
        "Draw one extra card at the start of every turn for the rest of this run.",
        "Every card reward screen offers one extra card to choose from for the rest of "
        "this run.",
        "Your elemental attacks apply their status far more readily: Fire leaves Burn, "
        "Poison leaves Poison, Wind leaves Rend. The chance rises by 8 points each time "
        "you take this, from the base 10.",
    };
    if (canScavenge)
        details.push_back("Equipment drops arrive every " + std::to_string(gearInterval - 1)
                          + " encounters instead of every " + std::to_string(gearInterval)
                          + " for the rest of the run.");
    std::vector<std::string> names = { "Fortune", "Endurance", "Foresight", "Attunement" };
    if (canScavenge) names.push_back("Scavenger");
    int choice = -1;
    while (true) {
        choice = CardBar::pick("A moment of respite. Take one, and keep it.", widgets, acts, 3);
        // "+" opens the details and comes back to the same three; it must never
        // fall through to the "< 0 means the first one" guard.
        if (choice <= -2) {
            const int ci = -2 - choice;
            if (ci >= 0 && ci < (int)widgets.size())
                CardBar::showDetail(widgets[ci], details[ci], "BOON", "", 0);
            continue;
        }
        // Escape and "Take none" both mean walking away, which cannot be undone.
        if (choice < 0 || choice >= (int)widgets.size()) {
            if (!confirm("Walk on without taking a boon?")) continue;
            notice("You walk on, taking nothing.");
            return;
        }
        if (!confirm("Take " + names[choice] + "? It lasts the whole run.")) continue;
        break;
    }

    if (choice == 3) {
        attunementBoons++;
        Audio::playSFX("upgrade");
        notice("Attunement. Your elemental attacks now land their status "
               + std::to_string(attunementChance()) + "% of the time.");
    } else if (choice == 4) {
        gearInterval = std::max(2, gearInterval - 1);
        Audio::playSFX("upgrade");
        notice("Scavenger. Equipment turns up every " + std::to_string(gearInterval)
               + " encounters from here on.");
    } else if (choice == 1) {
        handSizeBonus++;
        Audio::playSFX("upgrade");
        notice("Endurance. You will draw " + std::to_string(BASE_HAND_SIZE + handSizeBonus)
               + " cards a turn from here on.");
    } else if (choice == 2) {
        rewardChoiceBonus++;
        Audio::playSFX("upgrade");
        notice("Foresight. Reward screens will offer "
               + std::to_string(3 + rewardChoiceBonus) + " cards from here on.");
    } else {
        runLuck++;
        Audio::playSFX("upgrade");
        notice("Fortune. Every roll this run is " + std::to_string(luckBonus())
               + " points kinder.");
    }
}

void Game::offerEquipmentDrop() {
    EquipTier weapon = weaponTierAt(weaponTier);
    EquipTier armor  = armorTierAt(armorTier);
    int hpBoost = 30;
    // The trophies say which clear put them on the ladder.
    const int nextW = std::min(maxGearTier(), weaponTier + 1);
    const int nextA = std::min(maxGearTier(), armorTier + 1);
    auto trophyWon = [](int tier) {
        return tier >= TIER_MOON   ? "Yours for clearing the hard road. "
             : tier >= TIER_SHADOW ? "Yours for clearing the fifty. " : "";
    };

    // Same widgets as the card screens: this is a three way pick, so it reads
    // better as three panels than as a text list with a menu beside it.
    std::vector<CardBar::Card> widgets;
    {
        CardBar::Card w;
        const bool wMax = weaponTier >= maxGearTier();
        w.name = weapon.name; w.elemTag = "[WEAPON]";
        w.disabled = wMax;
        w.effect = wMax ? std::string("nothing better on this road")
                        : "+" + std::to_string(weapon.bonus) + "% dmg";
        w.note = WEAPON_PASSIVE[std::min(maxGearTier(), weaponTier + 1)];
        w.tint = Console::xterm256Public(Stripe::ITEM);
        w.nameColor = Console::xterm256Public(equipTintFor(std::min(maxGearTier(), weaponTier + 1)));
        // The piece itself, in its own colours: tier 0 on the sheets is the
        // starting kit, so the one on offer is one past what has been claimed.
        w.icon = weaponIconFor(std::min(maxGearTier(), weaponTier + 1));
        w.item = true;
        widgets.push_back(w);

        CardBar::Card a2;
        const bool aMax = armorTier >= maxGearTier();
        a2.name = armor.name; a2.elemTag = "[ARMOR]";
        a2.disabled = aMax;
        a2.effect = aMax ? std::string("nothing better on this road")
                         : "+" + std::to_string(armor.bonus) + "% armor";
        a2.note = armourProfileText(std::min(maxGearTier(), armorTier + 1));
        a2.tint = Console::xterm256Public(Stripe::ITEM);
        a2.nameColor = Console::xterm256Public(equipTintFor(std::min(maxGearTier(), armorTier + 1)));
        a2.icon = armorIconFor(std::min(maxGearTier(), armorTier + 1));
        a2.item = true;
        widgets.push_back(a2);

        CardBar::Card h;
        h.name = "Health Pouch"; h.elemTag = "[VIGOR]";
        h.effect = "+" + std::to_string(hpBoost) + " max HP";
        h.note = std::to_string(maxPlayerHealth) + " to " + std::to_string(maxPlayerHealth + hpBoost);
        h.tint = Console::xterm256Public(Stripe::ITEM);
        h.nameColor = Console::xterm256Public(120);
        h.icon = POUCH_ICON;
        h.item = true;
        widgets.push_back(h);
    }

    // What "+" shows. The face only fits a few words, and the running total is
    // what tells the player whether another tier is worth more than the HP.
    const std::string details[] = {
        std::string(trophyWon(nextW)) + "Every attack card deals " + std::to_string(weapon.bonus)
        + "% more damage for the rest of this run. Your weapon bonus goes from +" + std::to_string(weaponPct())
        + "% to +" + std::to_string(gearPercentFor(weaponTier + 1, true) + roadGearPct) + "%, and the "
          "numbers on your cards update to match. " + WEAPON_PASSIVE_LONG[nextW],
        std::string(trophyWon(nextA)) + "Every defend card gives " + std::to_string(armor.bonus)
        + "% more armor for the rest of this run. Your armor bonus goes from +" + std::to_string(armorPct())
        + "% to +" + std::to_string(gearPercentFor(armorTier + 1, false) + roadGearPct) + "%, and the "
          "numbers on your cards update to match. It " + armourProfileText(nextA)
        + ": 25% less damage from what it resists, 25% more from its weakness.",
        "Raises your max HP by " + std::to_string(hpBoost) + " for the rest of this run and "
        "heals you by the same amount. Max HP goes from " + std::to_string(maxPlayerHealth)
        + " to " + std::to_string(maxPlayerHealth + hpBoost) + ".",
    };
    const char* typeLabels[] = { "WEAPON", "ARMOR", "VIGOR" };

    while (true) {
        std::vector<CardBar::Action> acts{ CardBar::Action{ "Leave it behind", false } };
        int choice = CardBar::pick("You spot some gear on the ground", widgets, acts, 3);
        // "+" opens the item's description and comes back to the same three.
        if (choice <= -2) {
            const int ci = -2 - choice;
            if (ci >= 0 && ci < (int)widgets.size())
                CardBar::showDetail(widgets[ci], details[ci], typeLabels[ci], "", 0);
            continue;
        }

        if (choice == 3 || choice < 0) {
            notice("You leave it behind.");
            return;
        }

        std::string prompt = (choice == 0) ? ("Take the " + weapon.name + "?")
                            : (choice == 1) ? ("Take the " + armor.name + "?")
                                            : "Take the Health Pouch?";
        const std::string confirmPrompt = prompt;
        if (!confirm(confirmPrompt)) continue; // declined - back to the choices

        std::string result;
        if (choice == 0) {
            // Capped: the ladder ends at the top rung this player has opened,
            // and a counter drifting past it would keep offering the same
            // piece as if it were new.
            weaponTier = std::min(maxGearTier(), weaponTier + 1);
            wornWeapon = weaponTier;   // new gear goes straight on
            equipDamagePercent = gearPercentFor(weaponTier, true);
            Audio::playSFX("upgrade");
            result = "You equip the " + weapon.name + ". +"
                   + std::to_string(weapon.bonus) + "% damage (now +"
                   + std::to_string(equipDamagePercent) + "%).";
        } else if (choice == 1) {
            armorTier = std::min(maxGearTier(), armorTier + 1);
            wornArmor = armorTier;
            equipArmorPercent = gearPercentFor(armorTier, false);
            Audio::playSFX("upgrade");
            result = "You equip the " + armor.name + ". +"
                   + std::to_string(armor.bonus) + "% armor per defend (now +"
                   + std::to_string(equipArmorPercent) + "%).";
        } else {
            maxPlayerHealth += hpBoost;
            playerHealth += hpBoost;
            Audio::playSFX("heal");
            result = "You consume the Health Pouch. Max HP permanently increased by "
                   + std::to_string(hpBoost) + " (" + std::to_string(playerHealth)
                   + "/" + std::to_string(maxPlayerHealth) + ").";
        }
        notice(result);
        return;
    }
}

void Game::selectUpgrades() {
    upgrades.checkAndUnlockUpgrades(runStats.getTotalEncountersWon(), runStats.getTotalCardsCollected());
    upgrades.selectActiveUpgrades();
}

void Game::displayRunStats() const {
    currentRun.displayRunStats();
}

int Game::showMainMenu() {
    while (true) {
        bool hasSave = anySaveExists();
        std::cout << "\n";
        std::vector<std::string> opts = {"Start Game"};
        if (hasSave) opts.push_back("Load Save");
        opts.push_back("How to Play");
        opts.push_back("Settings");
        opts.push_back("Quit");

        int choice = UIHelper::titleMenu(opts);
        // By label: "Load Save" only exists sometimes, so counting positions
        // would break.
        const std::string chosen = (choice >= 0 && choice < (int)opts.size()) ? opts[choice] : "Quit";
        UIHelper::showTitleBanner(false);
        if (chosen == "Start Game") return 0;
        if (chosen == "Load Save")  return 1;
        if (chosen == "How to Play" || chosen == "Settings") {
            if (chosen == "Settings") showSettings();
            else                      showHowToPlay();
            UIHelper::clearScreen();
            Hud::setActive(false);   // nothing from in there belongs on the title
            UIHelper::printTitle();
            continue;
        }
        return 2; // Quit or ESC
    }
}

std::string Game::progressPath() const { return Audio::saveDir() + "progress.dat"; }
std::string Game::settingsPath() const { return Audio::saveDir() + "settings.cfg"; }

void Game::loadSettings() {
    std::ifstream in(settingsPath());
    if (!in.is_open()) return;
    std::string tag;
    while (in >> tag) {
        if      (tag == "TEXT")  in >> optTextSpeed;
        else if (tag == "PACE")  in >> optPace;
        else if (tag == "MUSIC") in >> optMusic;
        else if (tag == "SFX")   in >> optSfx;
    }
    applySettings();
}

void Game::saveSettings() const {
    std::ofstream out(settingsPath(), std::ios::trunc);
    if (!out.is_open()) return;
    out << "MOONSTRUCK_SETTINGS_V1\n"
        << "TEXT "  << optTextSpeed << "\n"
        << "PACE "  << optPace      << "\n"
        << "MUSIC " << optMusic     << "\n"
        << "SFX "   << optSfx       << "\n";
}

// The four values reach four different systems, so this is the one place
// that knows all of them and the only thing the screen has to call.
void Game::applySettings() const {
    UIHelper::setTextSpeed(optTextSpeed);
    Platform::setPacePercent(optPace);
    Audio::setMusicVolume(optMusic);
    Audio::setSfxVolume(optSfx);
}

void Game::showSettings() {
    // Four sliders and a way out. Left and right move the row under the
    // cursor, and the value is applied as it moves, so a volume is heard
    // while it is being set rather than after the screen closes.
    auto speedWord = [](int v) {
        return std::to_string(v) + "%   " + (v == 0 ? "instant" : v < 80 ? "slow"
                                           : v <= 140 ? "normal" : "fast");
    };
    // Stored as how long each beat holds, so a bigger number is a slower
    // game. Shown the other way round: on a speed control, right is faster.
    auto paceWord = [](int v) {
        return std::to_string(300 - v) + "%   " + (v >= 190 ? "slow" : v >= 130 ? "normal"
                                                 : v >= 95 ? "quick" : "snappy");
    };
    auto volWord = [](int v) {
        return std::to_string(v) + "%   " + (v == 0 ? "off" : v < 35 ? "quiet"
                                           : v < 75 ? "medium" : "full");
    };
    auto applied = [this]() { applySettings(); };
    auto heard   = [this]() { applySettings(); if (optSfx > 0) Audio::playSFX("special"); };

    // The pace row keeps its range the right way up and flips the bar
    // instead, so the value stays clampable and right still means faster.
    CardBar::Action pace{ "Combat pace", &optPace, 60, 240, 10, paceWord, applied };
    pace.invert = true;
    std::vector<CardBar::Action> acts{
        CardBar::Action{ "Text speed",  &optTextSpeed,   0, 300, 20, speedWord, applied },
        pace,
        CardBar::Action{ "Music",       &optMusic,       0, 100,  5, volWord,   applied },
        CardBar::Action{ "Sound",       &optSfx,         0, 100,  5, volWord,   heard   },
        CardBar::Action{ "Back", "keep these and return", false },
    };
    CardBar::pick("Settings        left and right to set", {}, acts, 0);
    saveSettings();
}
std::string Game::winSavePath() const { return Audio::saveDir() + "winrun.dat"; }

// What has been cleared. Its own file beside the three slots, because dying
// clears a slot and this is the one thing a death must never take away.
void Game::loadProgress() {
    std::ifstream in(progressPath());
    if (!in.is_open()) return;
    std::string tag;
    while (in >> tag) {
        if (tag == "CLEARED") in >> clearedMask;
        else if (tag == "SETS") in >> unlockedSets;
    }
}

void Game::saveProgress() const {
    std::ofstream out(progressPath(), std::ios::trunc);
    if (!out.is_open()) return;
    out << "MOONSTRUCK_PROGRESS_V1\n";
    out << "CLEARED " << clearedMask << "\n";
    out << "SETS " << unlockedSets << "\n";
}

// A clear: record the road walked, and keep the run that walked it so the
// harder roads can be started with that deck.
void Game::recordClear() {
    clearedMask |= (runMode == Mode::HARD) ? 2 : (runMode == Mode::RANDOM_HARD) ? 4 : 1;
    // What you take off the thing you just put down. Finishing the fifty is
    // the Shadow Knight's own plate and blade; the moon's comes off the hard
    // road, which is the only place it was ever going to come from.
    if (runMode == Mode::HARD || runMode == Mode::RANDOM_HARD) unlockedSets |= 2;
    else                                                       unlockedSets |= 1;
    saveProgress();
    writeWinSave();
}

void Game::writeWinSave() const {
    std::ofstream out(winSavePath(), std::ios::trunc);
    if (out.is_open()) writeSaveTo(out);
}

bool Game::loadWinSave() {
    std::ifstream in(winSavePath());
    return in.is_open() && loadSaveFrom(in);
}

bool Game::hasWinSave() const {
    std::error_code ec;
    return std::filesystem::exists(winSavePath(), ec);
}

std::string Game::savePath(int slot) const {
    return Audio::saveDir() + "save" + std::to_string(slot) + ".dat";
}

bool Game::saveExists(int slot) const {
    return std::filesystem::exists(savePath(slot));
}

bool Game::anySaveExists() const {
    for (int i = 1; i <= SAVE_SLOTS; i++)
        if (saveExists(i)) return true;
    return false;
}

// The single save.dat from before save slots. Moved into slot 1 rather than
// abandoned, so an in-progress run survives the update.
void Game::migrateLegacySave() const {
    const std::string legacy = Audio::saveDir() + "save.dat";
    if (!std::filesystem::exists(legacy) || saveExists(1)) return;
    std::error_code ec;
    std::filesystem::rename(legacy, savePath(1), ec);
}

// Enough of the file to tell the slots apart, read without disturbing the run.
std::string Game::saveSummary(int slot) const {
    std::ifstream in(savePath(slot));
    if (!in.is_open()) return "";
    std::string line;
    if (!std::getline(in, line) || line != "SAVE_V1") return "damaged save";
    int encounter = 0, won = 0;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        if (tag == "ENCOUNTER") iss >> encounter;
        else if (tag == "WON")  iss >> won;
        else if (line.rfind("CARD|", 0) == 0) break;   // the header is all above the deck
    }
    if (encounter <= 0) return "damaged save";
    return "Encounter " + std::to_string(encounter) + ", "
         + std::to_string(won) + (won == 1 ? " win" : " wins");
}

// Shared by saving and loading. Saving offers every slot (an occupied one is
// overwritten, after a confirmation); loading offers only the filled ones.
int Game::chooseSaveSlot(const std::string& title, bool forSaving) {
    while (true) {
        // Rows, not card widgets: a slot is a line of text.
        std::vector<CardBar::Action> acts;
        std::vector<int> slotOf;
        for (int i = 1; i <= SAVE_SLOTS; i++) {
            const std::string summary = saveSummary(i);
            const bool empty = summary.empty();
            acts.push_back(CardBar::Action{ "Slot " + std::to_string(i),
                                            empty ? "empty" : summary,
                                            /*disabled*/!forSaving && empty });
            slotOf.push_back(i);
        }
        acts.push_back(CardBar::Action{ forSaving ? "Don't save" : "Back",
                                        forSaving ? "carry on without saving" : "back to the menu", false });
        // Said on the screen where it matters. A death clears the slot the
        // run was saved into, and finding that out afterwards is no good.
        const std::string heading = forSaving
            ? title + "        dying deletes the save"
            : title;
        int choice = CardBar::pick(heading, {}, acts, 0);
        if (choice < 0 || choice >= (int)slotOf.size()) return 0;
        const int slot = slotOf[choice];
        if (!forSaving && saveSummary(slot).empty()) continue;   // an empty slot has nothing to load
        if (forSaving && !saveSummary(slot).empty()
            && !confirm("Overwrite slot " + std::to_string(slot) + "?")) continue;
        return slot;
    }
}

void Game::saveGame(int slot) {
    currentSaveSlot = slot;
    std::ofstream out(savePath(slot), std::ios::trunc);
    if (!out.is_open()) {
        std::cout << Color::YELLOW << "Warning: couldn't write the save file." << Color::RESET << "\n";
        return;
    }
    writeSaveTo(out);
}

// The save file itself. Split out so the permanent win snapshot is written by
// the same code that writes a slot, and can never drift from it.
void Game::writeSaveTo(std::ostream& out) const {
    out << "SAVE_V1\n";
    out << "MODE " << (int)runMode << "\n";
    out << "RANDSEED " << randomSeed << "\n";
    out << "ROADBONUS " << roadBonus << " " << roadGearPct << "\n";
    out << "ENCOUNTER " << currentRun.getCurrentEncounter() << "\n";
    out << "WON " << currentRun.getEncountersWon() << "\n";
    out << "MAXHP " << maxPlayerHealth << "\n";
    out << "HP " << playerHealth << "\n";
    out << "MAXENERGY " << maxEnergy << "\n";
    // Written for readability only - both are rebuilt from the tiers on load.
    out << "EQUIPDMG " << equipDamagePercent << "\n";
    out << "EQUIPARM " << equipArmorPercent << "\n";
    out << "LUCK " << runLuck << "\n";
    out << "HANDBONUS " << handSizeBonus << "\n";
    out << "REWARDBONUS " << rewardChoiceBonus << "\n";
    out << "ATTUNE " << attunementBoons << "\n";
    out << "GEARINT " << gearInterval << "\n";
    out << "SEALS " << sealsBroken << "\n";
    out << "RELICS " << relicsOwned << " " << (redThreadUsed ? 1 : 0) << "\n";
    out << "MOONSEEN " << moonZonesSeen << "\n";
    out << "MOONMET " << moonstruckMet << "\n";
    out << "SAT " << satCount << "\n";
    out << "WORN " << wornWeapon << " " << wornArmor << "\n";
    out << "WEAPONTIER " << weaponTier << "\n";
    out << "ARMORTIER " << armorTier << "\n";
    for (int i = 0; i < 5; i++)
        out << "UPGRADE " << i << " " << (upgrades.isUnlocked(i) ? 1 : 0) << " " << (upgrades.isActive(i) ? 1 : 0) << "\n";

    for (const Card& c : playerDeck.getAllCardsOrdered()) {
        out << "CARD|" << c.getName() << "|" << c.getDescription() << "|" << cardTypeToStr(c.getType())
            << "|" << c.getCost() << "|" << c.getValue() << "|" << effectToStr(c.getEffect())
            << "|" << (c.isRare() ? 1 : 0) << "|" << (c.isSuperRare() ? 1 : 0) << "|" << (c.isLegendary() ? 1 : 0)
            << "|" << dmgToStr(c.getPhysType()) << "|" << dmgToStr(c.getPhysType2()) << "|" << dmgToStr(c.getElemType())
            << "|" << c.getUpgradeCount() << "\n";
    }
}

void Game::deleteSave(int slot) const {
    std::error_code ec;
    std::filesystem::remove(savePath(slot), ec);
}

// Dying only costs the run that was being played.
void Game::deleteCurrentSave() {
    if (currentSaveSlot <= 0) return;
    deleteSave(currentSaveSlot);
    currentSaveSlot = 0;
}

bool Game::loadGame(int slot) {
    std::ifstream in(savePath(slot));
    if (!in.is_open()) return false;
    currentSaveSlot = slot;
    return loadSaveFrom(in);
}

bool Game::loadSaveFrom(std::istream& in) {
    std::string line;
    if (!std::getline(in, line) || line != "SAVE_V1") return false;

    int savedEncounter = 1, savedWon = 0, savedMaxHp = 100, savedHp = 100, savedMaxEnergy = 3;
    int savedEquipDmg = 0, savedEquipArm = 0, savedWeaponTier = 0, savedArmorTier = 0;
    // Boons. Default 0, so a save written before they existed loads as a run that
    // simply never took one rather than failing to parse.
    int savedLuck = 0, savedHandBonus = 0, savedRewardBonus = 0;
    int savedAttune = 0, savedGearInt = 3;
    int savedSeals = 0, savedMoonSeen = 0, savedWornW = -1, savedWornA = -1;
    int savedRelics = 0, savedThread = 0, savedMoonMet = 0, savedSat = 0;
    int savedMode = 0;
    unsigned savedSeed = 0;
    int savedRoadBonus = 0, savedRoadGear = 0;
    std::vector<bool> unlockedFlags(5, false), activeFlags(5, false);
    std::vector<Card> loadedCards;

    while (std::getline(in, line)) {
        if (line.empty()) continue;

        if (line.rfind("CARD|", 0) == 0) {
            std::vector<std::string> f;
            size_t pos = 5;
            while (pos <= line.size()) {
                size_t next = line.find('|', pos);
                if (next == std::string::npos) { f.push_back(line.substr(pos)); break; }
                f.push_back(line.substr(pos, next - pos));
                pos = next + 1;
            }
            if (f.size() != 13) continue; // corrupted line - skip rather than abort the whole load
            try {
                loadedCards.push_back(Card(
                    f[0], f[1], strToCardType(f[2]), std::stoi(f[3]), std::stoi(f[4]),
                    strToEffect(f[5]), f[6] == "1", strToDmg(f[9]), strToDmg(f[11]),
                    f[7] == "1", strToDmg(f[10]), f[8] == "1", std::stoi(f[12])));
            } catch (...) { continue; }
            continue;
        }

        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        if (tag == "ENCOUNTER")     iss >> savedEncounter;
        else if (tag == "WON")      iss >> savedWon;
        else if (tag == "MAXHP")    iss >> savedMaxHp;
        else if (tag == "HP")       iss >> savedHp;
        else if (tag == "MAXENERGY") iss >> savedMaxEnergy;
        else if (tag == "EQUIPDMG") iss >> savedEquipDmg;
        else if (tag == "EQUIPARM") iss >> savedEquipArm;
        else if (tag == "LUCK")        iss >> savedLuck;
        else if (tag == "HANDBONUS")   iss >> savedHandBonus;
        else if (tag == "REWARDBONUS") iss >> savedRewardBonus;
        else if (tag == "ATTUNE")      iss >> savedAttune;
        else if (tag == "GEARINT")     iss >> savedGearInt;
        else if (tag == "SEALS")       iss >> savedSeals;
        else if (tag == "RELICS")      iss >> savedRelics >> savedThread;
        else if (tag == "MOONSEEN")    iss >> savedMoonSeen;
        else if (tag == "MOONMET")     iss >> savedMoonMet;
        else if (tag == "SAT")         iss >> savedSat;
        else if (tag == "MODE")        iss >> savedMode;
        else if (tag == "RANDSEED")    iss >> savedSeed;
        else if (tag == "ROADBONUS")   iss >> savedRoadBonus >> savedRoadGear;
        else if (tag == "WORN")        iss >> savedWornW >> savedWornA;
        else if (tag == "WEAPONTIER") iss >> savedWeaponTier;
        else if (tag == "ARMORTIER") iss >> savedArmorTier;
        else if (tag == "UPGRADE") {
            int idx, unl, act;
            iss >> idx >> unl >> act;
            if (idx >= 0 && idx < 5) { unlockedFlags[idx] = unl != 0; activeFlags[idx] = act != 0; }
        }
    }

    if (loadedCards.empty()) return false; // no usable deck - treat as a failed load

    playerDeck = Deck();
    for (const Card& c : loadedCards) playerDeck.addCard(c);
    playerDeck.shuffle();

    currentRun = Run();
    currentRun.loadState(savedEncounter, savedWon);

    maxPlayerHealth  = savedMaxHp;
    playerHealth     = std::min(savedHp, savedMaxHp);
    maxEnergy        = savedMaxEnergy;
    playerEnergy     = maxEnergy;
    weaponTier       = savedWeaponTier;
    armorTier        = savedArmorTier;
    runLuck           = savedLuck;
    handSizeBonus     = savedHandBonus;
    rewardChoiceBonus = savedRewardBonus;
    attunementBoons   = savedAttune;
    gearInterval      = std::max(2, savedGearInt);
    sealsBroken       = std::max(0, savedSeals);
    relicsOwned       = savedRelics;
    redThreadUsed     = savedThread != 0;
    moonZonesSeen     = savedMoonSeen;
    moonstruckMet     = std::max(0, savedMoonMet);
    satCount          = std::max(0, savedSat);
    runMode           = (savedMode >= 0 && savedMode <= 3) ? (Mode)savedMode : Mode::NORMAL;
    randomSeed        = savedSeed;
    roadBonus         = std::max(0, savedRoadBonus);
    roadGearPct       = std::max(0, savedRoadGear);
    currentRun.setDifficulty(difficultyFor(runMode));
    buildRandomOrder();
    // A save from before the equipment tab wears the newest of everything.
    // Clamped to what the save found, which runs past six with a trophy set.
    const int cap = maxGearTier();
    wornWeapon = std::min(std::min(cap, savedWeaponTier), savedWornW < 0 ? cap : savedWornW);
    wornArmor  = std::min(std::min(cap, savedArmorTier),  savedWornA < 0 ? cap : savedWornA);
    // Recomputed rather than restored, so a save written before gear became a
    // percentage loads as the right percentage instead of a stale flat number.
    (void)savedEquipDmg; (void)savedEquipArm;
    equipDamagePercent = gearPercentFor(weaponTier, true);
    equipArmorPercent  = gearPercentFor(armorTier, false);
    for (int i = 0; i < 5; i++) upgrades.setUpgradeState(i, unlockedFlags[i], activeFlags[i]);

    turnNumber = 1;
    playerTurnActive = true;
    inEncounter = false; // not in combat yet - offerContinueOrEndRun() starts the next fight if Continue is chosen
    running = true;

    return true;
}

void Game::showHowToPlay() {
    // Four pages. One screen could not hold it any more, and a wall of text
    // nobody scrolls teaches nothing.
    auto page = [](const char* title) {
        UIHelper::clearScreen();
        std::cout << "\n" << Color::BOLD << Color::CYAN << title << Color::RESET << "\n\n";
    };
    auto head = [](const char* h) { std::cout << Color::BOLD << h << Color::RESET << "\n"; };
    auto more = []() { UIHelper::waitForKey("  (press any key)"); };

    page("HOW TO PLAY   1 of 4: the fight");
    head("GOAL");
    std::cout << "  Fifty encounters through five areas, with a boss every tenth fight.\n";
    std::cout << "  Build a deck as you go, and take what the road offers you.\n\n";

    head("YOUR TURN");
    std::cout << "  Each turn you draw a hand and get a pool of energy. Every card's cost\n";
    std::cout << "  comes out of that pool. When you are done, End Turn to let the enemy\n";
    std::cout << "  act, then a new turn begins.\n\n";

    head("CARD TYPES");
    std::cout << "  " << Color::CARD_ATTACK  << "ATTACK " << Color::RESET << " deals damage to the enemy\n";
    std::cout << "  " << Color::CARD_DEFEND  << "DEFEND " << Color::RESET << " grants you armor, which absorbs incoming damage\n";
    std::cout << "  " << Color::CARD_SPECIAL << "SPECIAL" << Color::RESET << " buffs, debuffs, and unique effects\n\n";

    head("CARDS THAT COST YOU");
    std::cout << "  A card ringed in " << Color::YELLOW << "gold" << Color::RESET << " buys its power with something: your health,\n";
    std::cout << "  your armor, your next turn's cards. The face says what it takes, and\n";
    std::cout << "  \"+\" on any card opens the full text.\n\n";

    head("TWO SCREENS IN A FIGHT");
    std::cout << "  " << Color::CYAN << "View Enemy" << Color::RESET << "  every move it has, the odds, what it hits for, and what\n";
    std::cout << "              kind of blow it lands\n";
    std::cout << "  " << Color::CYAN << "View Player" << Color::RESET << " your gear, boons, relics, vigils and anything a card is\n";
    std::cout << "              still charging you\n\n";
    more();

    page("HOW TO PLAY   2 of 4: damage");
    head("YOUR DAMAGE TYPES");
    std::cout << "  Some cards carry a tag: " << Color::YELLOW << "Smash, Pierce" << Color::RESET << " (physical) or "
              << Color::YELLOW << "Fire, Poison,\n  Wind" << Color::RESET << " (elemental). Every enemy is weak to one type (+50% damage)\n";
    std::cout << "  and some resist another (-50%). View Enemy shows which.\n";
    std::cout << "  An elemental attack also has a 10% chance on hit to leave its status\n";
    std::cout << "  behind. The Attunement boon raises that chance.\n\n";

    head("WHAT HITS YOU");
    std::cout << "  Enemy blows have a type too. Your armor resists one or two of them\n";
    std::cout << "  (25% less damage) and is weak to one (25% more). No piece is simply\n";
    std::cout << "  better than another, so change armor at a rest site to suit what is\n";
    std::cout << "  ahead. The combat log says " << Color::GREEN << "[Resisted]" << Color::RESET << " or " << Color::RED << "[Weak to]" << Color::RESET << " when it matters.\n\n";

    head("STATUS EFFECTS");
    std::cout << "  " << Color::POISON_CLR << "Poison" << Color::RESET
              << "        half the card value each turn, for 6 turns. Wins long fights.\n";
    std::cout << "  " << Color::BURN_CLR << "Burn" << Color::RESET
              << "          half again the card value each turn, for 2 turns. Hits now.\n";
    std::cout << "  " << Color::REND_CLR << "Rend" << Color::RESET
              << "          the full value, but only when the target ATTACKS. Wind leaves it.\n";
    std::cout << "  " << Color::STUN_CLR << "Stun" << Color::RESET << "          skips the target's next turn entirely\n";
    std::cout << "  " << Color::WEAK_CLR << "Weak" << Color::RESET << "          the target deals 1.5-2x less damage for a few turns\n";
    std::cout << "  " << Color::STRENGTH_CLR << "Strength" << Color::RESET << "      damage dealt is multiplied for a number of turns\n\n";
    more();

    page("HOW TO PLAY   3 of 4: between fights");
    head("REST SITES");
    std::cout << "  " << Color::HEAL << "Rest" << Color::RESET << "        heal to full\n";
    std::cout << "  " << Color::YELLOW << "Forge" << Color::RESET << "       upgrade a card, and every copy of it with it\n";
    std::cout << "  " << Color::CYAN << "Equipment" << Color::RESET << "   choose which weapon and armor to wear\n";
    std::cout << "  " << Color::CYAN << "View Deck" << Color::RESET << "   browse your deck and discard what you do not want\n";
    std::cout << "  " << Color::DIM << "Skip" << Color::RESET << "        move on\n";
    std::cout << "  Equipment and View Deck are free; Rest, Forge and Skip move you on.\n\n";

    head("CARD RARITY AND THE FORGE");
    std::cout << "  Starter < " << Color::COMMON_TINT << "Uncommon" << Color::RESET << " < "
              << Color::RARE_TINT << "Rare" << Color::RESET << " < " << Color::SUPER_RARE_TINT << "Super Rare" << Color::RESET
              << " < " << Color::LEGENDARY_TINT << "Legendary" << Color::RESET << ". Legendaries come from bosses.\n";
    std::cout << "  Forging adds value by rarity, and the FIRST upgrade also takes 1 off\n";
    std::cout << "  the cost. Uncommon and rare cards take 2 upgrades, super rare and\n";
    std::cout << "  legendary 3. Starters cannot be forged.\n\n";

    head("GEAR");
    std::cout << "  Weapons and armor drop every 3rd encounter. Their percentages add up\n";
    std::cout << "  across everything you have claimed, whatever you are wearing; what you\n";
    std::cout << "  wear decides your armor's resistances and your weapon's own trick, like\n";
    std::cout << "  the Ebon Blade's healing or the Mythril Edge's cheaper opening attack.\n\n";
    more();

    page("HOW TO PLAY   4 of 4: the long game");
    head("BOONS");
    std::cout << "  Every 12th encounter, pick one of five, kept for the run: Fortune (all\n";
    std::cout << "  odds), Endurance (+1 card a turn), Foresight (+1 card on rewards),\n";
    std::cout << "  Attunement (elemental statuses land far more often) and Scavenger (gear\n";
    std::cout << "  more often).\n\n";

    head("RELICS");
    std::cout << "  From the 6th encounter, then every 12th, one of three relics. They do\n";
    std::cout << "  not cost energy and they last the run: poison that lingers, a free\n";
    std::cout << "  reroll on rewards, armor at the start of every fight. One of them is\n";
    std::cout << "  " << Color::YELLOW << "cursed" << Color::RESET << ", and wears the same gold ring the risky cards do.\n\n";

    head("VIGILS");
    std::cout << "  Each area's boss has one burning in its chest. They are what keeps the\n";
    std::cout << "  thing on the peak awake, and putting one out costs it a night it cannot\n";
    std::cout << "  spare, so it answers by pouring more of itself into everything left:\n";
    std::cout << "  +15% health and +10% attack each, and they stack. Nothing forces you to\n";
    std::cout << "  touch them, and putting out all four is the only way to see what is\n";
    std::cout << "  actually wearing your face.\n\n";

    head("THE ROADS");
    std::cout << "  A run is fifty encounters, and it ends there. Finishing it opens\n";
    std::cout << "  " << Color::CYAN << "Random" << Color::RESET << " (the same fifty in an order you\n";
    std::cout << "  have not fought) and " << Color::CYAN << "Hard" << Color::RESET << " (every enemy as strong as fifty\n";
    std::cout << "  fights further on, and redder for it). Finishing Hard opens\n";
    std::cout << "  " << Color::CYAN << "Random Hard" << Color::RESET << ", the hard fifty with nothing where you left it.\n";
    std::cout << "  Pick a road on Start Game, with the deck that earned it or a fresh\n";
    std::cout << "  one. What you have cleared is kept in its own file, so dying can\n";
    std::cout << "  never take a road away from you.\n\n";

    head("BETWEEN RUNS");
    std::cout << "  Encounters won and cards collected unlock permanent upgrades you can\n";
    std::cout << "  switch on for the next run. There are three save slots, and dying only\n";
    std::cout << "  clears the slot you were playing.\n\n";

    UIHelper::waitForKey("  (press any key to continue)");

    std::vector<CardBar::Action> helpActs{
        CardBar::Action{ "Tutorial", false },
        CardBar::Action{ "Return", false },
    };
    if (CardBar::pick("How to play", {}, helpActs, 0) == 0) showTutorial();
}

namespace {
// Every in-fight tip goes out through here, so they all break the same way
// and none of them runs off the edge of the log box.
void tutorialFact(const std::string& body) {
    std::cout << "\n";
    UIHelper::printWrapped(std::string(Color::DIM) + "Fact: " + Color::RESET + body, 2, 6);
    std::cout << "\n";
}
} // namespace

void Game::showTutorial() {
    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << Color::CYAN << "TUTORIAL" << Color::RESET << "\n\n";
    std::cout << "  Fight the Slime and learn the mechanics.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << "YOUR HAND" << Color::RESET << "\n\n";
    std::cout << "  Each turn you draw a hand of cards and get a pool of energy,\n";
    std::cout << "  shown at the top. Every card costs some of that energy.\n";
    std::cout << "  Arrow keys highlight a card, Enter plays it.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << "READING A CARD" << Color::RESET << "\n\n";
    std::cout << "  A card face shows its name, its cost, and the number it will actually\n";
    std::cout << "  land for - your gear is already counted in it. Tags like " << Color::YELLOW << "[Smash]"
              << Color::RESET << " say\n";
    std::cout << "  what kind of blow it is. \"+\" on a card opens its full text.\n\n";
    std::cout << "  A card ringed in " << Color::YELLOW << "gold" << Color::RESET << " pays for its power with something of\n";
    std::cout << "  yours. Your " << Color::BOLD << "Scrap Shield" << Color::RESET << " is one: 5 armor, and it nicks you for 1.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << "ATTACK, DEFEND, SPECIAL" << Color::RESET << "\n\n";
    std::cout << "  " << Color::CARD_ATTACK << "ATTACK" << Color::RESET << " cards deal damage. " << Color::CARD_DEFEND << "DEFEND" << Color::RESET
              << " cards grant armor that\n";
    std::cout << "  absorbs incoming damage. " << Color::CARD_SPECIAL << "SPECIAL" << Color::RESET << " cards do everything else -\n";
    std::cout << "  status effects, counters, heals, and more.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << "DAMAGE TYPES, BOTH WAYS" << Color::RESET << "\n\n";
    std::cout << "  Some attacks carry a tag, like " << Color::BOLD << "Bash" << Color::RESET << " (" << Color::YELLOW << "Smash" << Color::RESET
              << ") and " << Color::BOLD << "Lunge" << Color::RESET << " (" << Color::YELLOW << "Pierce" << Color::RESET << ")\n";
    std::cout << "  in your hand. Every enemy is weak to one type for +50% damage.\n\n";
    std::cout << "  Their blows have a type too, and your armor resists some kinds and\n";
    std::cout << "  fears one. Check View Enemy: it names both sides of that.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << "ENDING YOUR TURN" << Color::RESET << "\n\n";
    std::cout << "  Done playing cards? Pick \"End Turn\" and the enemy acts.\n";
    std::cout << "  \"View Enemy\" shows what it can do and what it is weak to;\n";
    std::cout << "  \"View Player\" shows your own gear, boons, relics and effects.\n\n";
    std::cout << "  Your turn.\n\n";
    UIHelper::waitForKey("  (press any key to begin)");

    // Snapshot everything the tutorial touches, so a real run starts clean afterward
    // regardless of how this practice fight goes.
    int savedHealth        = playerHealth;
    int savedArmor         = playerArmor;
    int savedArmorPersist  = playerArmorPersistTurns;
    int savedEnergy        = playerEnergy;
    int savedTurn          = turnNumber;
    bool savedTurnActive   = playerTurnActive;
    bool savedInEncounter  = inEncounter;

    // 20 HP: enough turns for the tips to come up, not so many that it is a
    // fight. The tutorial is here to show how a turn works.
    enemy = Enemy("Slime", 20, 4, 2, EnemyType::MELEE);
    EnemyArt::setEnemyVariant(enemy.getName());
    EnemyArt::setTutorialBackdrop(); // day forest - a gentler scene than the run's dungeon opener
    playerHealth = maxPlayerHealth;
    playerArmor = 0;
    playerArmorPersistTurns = 0;
    playerStatus.reset();
    counterAttackActive = false;
    parryActive = false;
    turnNumber = 1;
    playerTurnActive = true;
    inEncounter = true;
    resetEnergy();
    playerDeck.resetDeck();
    int drawCount = BASE_HAND_SIZE + handSizeBonus + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    UIHelper::clearScreen();
    std::cout << "\n" << Color::DIM << "Fact: " << Color::RESET << Color::ENERGY_CLR << playerEnergy << "/" << maxEnergy
              << " energy" << Color::RESET << " up top is your budget for the turn - every card's cost comes\n";
    std::cout << "  out of that pool, so you can't play more than you can afford.\n\n";
    UIHelper::waitForKey("  (press any key to start turn 1)");

    // Capped by turnNumber (bumped only when a turn actually ends via End Turn),
    // not by call count - freely checking View Enemy/Status shouldn't burn a "turn".
    const int startTurn = turnNumber;
    const int maxTutorialTurns = 6;
    bool slimeDefeated = false;
    bool attackTipShown = false, defendTipShown = false, specialTipShown = false, damageTypeTipShown = false;
    bool riskTipShown = false, hitTipShown = false;
    while (playerHealth > 0 && turnNumber - startTurn < maxTutorialTurns) {
        const int hpBeforeTurn = playerHealth;
        handleInput();

        // The first blow you take is the moment the armour's resistances mean
        // something, so the lesson lands there rather than in a menu.
        if (!hitTipShown && playerHealth < hpBeforeTurn) {
            hitTipShown = true;
            const DamageType bt = enemyAttackType();
            const int mod = armourTypeMod(bt);
            std::string how = mod < 0 ? std::string(Color::GREEN) + "resists that, so it landed 25% softer" + Color::RESET
                            : mod > 0 ? std::string(Color::RED) + "is weak to that, so it landed 25% harder" + Color::RESET
                                      : std::string("neither resists it nor fears it");
            tutorialFact("the slime's blows are " + std::string(Color::BOLD) + typeWord(bt) + Color::RESET
                         + ". Your armor " + how + ". Rest sites let you change armor to suit what is ahead.");
            UIHelper::waitForKey();
        }

        // Read back what was actually played, via state playCardFromHand() sets -
        // this survives even if the same call also auto-ended the turn and reset
        // the hand (which would otherwise wipe any "used" flags we'd diffed against).
        if (lastActionWasCardPlay) {
            if (!attackTipShown && lastPlayedCardType == CardType::ATTACK) {
                attackTipShown = true;
                tutorialFact("the slime's " + std::string(Color::BLUE) + "DEF" + Color::RESET
                             + " (defense) soaks up part of your attack's DMG value. That is why the damage"
                               " dealt came in lower than the card's number.");
                UIHelper::waitForKey();
            } else if (!defendTipShown && lastPlayedCardType == CardType::DEFEND) {
                defendTipShown = true;
                tutorialFact("when the enemy hits you, their attack's damage comes off your armor first."
                             " That is what DEFEND cards are for.");
                UIHelper::waitForKey();
            } else if (!specialTipShown && lastPlayedCardType == CardType::SPECIAL) {
                specialTipShown = true;
                tutorialFact("SPECIAL cards often only do something under a condition. Parry, for example,"
                             " only blocks and ripostes if the enemy actually attacks this turn. If they do"
                             " not, it does nothing.");
                UIHelper::waitForKey();
            }

            if (!riskTipShown && lastPlayedCardWasRisky) {
                riskTipShown = true;
                tutorialFact("that card was ringed in " + std::string(Color::YELLOW) + "gold" + Color::RESET
                             + ": it bought what it did with something of yours. Plenty of the best cards in"
                               " the game do, and the ring is there so you never spend it by accident.");
                UIHelper::waitForKey();
            }

            if (!damageTypeTipShown && lastPlayedCardType == CardType::ATTACK
                && (lastPlayedPhysType != DamageType::NONE || lastPlayedPhysType2 != DamageType::NONE)) {
                damageTypeTipShown = true;
                bool matched = lastPlayedPhysType == enemy.getWeakness() || lastPlayedPhysType2 == enemy.getWeakness();
                if (matched) {
                    tutorialFact("that card's damage-type tag matched the slime's weakness, so it hit"
                                 " for +50% bonus damage.");
                } else {
                    tutorialFact("that card's damage-type tag did not match the slime's weakness this time,"
                                 " so no bonus. Check View Enemy to see what an enemy IS weak to before"
                                 " picking your attacks.");
                }
                UIHelper::waitForKey();
            }
        }

        if (!enemy.isAlive()) { slimeDefeated = true; break; }
    }

    UIHelper::clearScreen();
    if (slimeDefeated) {
        std::cout << "\n" << Color::BOLD << Color::GREEN << "Slime defeated!" << Color::RESET << "\n\n";
    } else {
        std::cout << "\n" << Color::BOLD << Color::CYAN << "Tutorial complete." << Color::RESET << "\n\n";
    }
    std::cout << "  That's the core loop. Good luck out there.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << Color::CYAN << "AFTER A REAL WIN" << Color::RESET << "\n\n";
    std::cout << "  Winning a real fight takes you to a rest site with:\n\n";
    std::cout << "  " << Color::HEAL << "Rest" << Color::RESET << "        heal to full HP\n";
    std::cout << "  " << Color::YELLOW << "Forge" << Color::RESET << "       upgrade a card - all copies of it upgrade together\n";
    std::cout << "  " << Color::CYAN << "Equipment" << Color::RESET << "   choose which weapon and armor to wear\n";
    std::cout << "  " << Color::CYAN << "View Deck" << Color::RESET << "   browse your deck, discard cards you don't want\n";
    std::cout << "  " << Color::DIM << "Skip" << Color::RESET << "        move on without doing any of the above\n\n";
    std::cout << "  Equipment and View Deck are free. Rest, Forge and Skip move you on.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << Color::CYAN << "WHAT THE ROAD GIVES YOU" << Color::RESET << "\n\n";
    std::cout << "  " << Color::YELLOW << "Cards" << Color::RESET << "      one to pick after every fight\n";
    std::cout << "  " << Color::YELLOW << "Gear" << Color::RESET << "       every 3rd encounter: a weapon, armor, or +30 max HP.\n";
    std::cout << "             Each weapon has its own trick and each armor its own\n";
    std::cout << "             resistances, so newer is not always what you want to wear.\n";
    std::cout << "  " << Color::YELLOW << "Relics" << Color::RESET << "     from the 6th encounter, then every 12th. They cost no\n";
    std::cout << "             energy and last the run. One kind is cursed, and says so.\n";
    std::cout << "  " << Color::YELLOW << "Boons" << Color::RESET << "      every 12th encounter, one of five lasting blessings\n";
    std::cout << "  " << Color::YELLOW << "Vigils" << Color::RESET << "     one burns in each boss. Put it out and everything left\n";
    std::cout << "             gets tougher, because more of the moon is in it.\n\n";
    UIHelper::waitForKey("  (press any key to return to the menu)");

    // Restore pre-tutorial state so the real run starts clean
    Hud::setActive(false);   // the tutorial fought a real fight; end its panel
    playerHealth = savedHealth;
    playerArmor = savedArmor;
    playerArmorPersistTurns = savedArmorPersist;
    playerStatus.reset();
    counterAttackActive = false;
    parryActive = false;
    turnNumber = savedTurn;
    playerTurnActive = savedTurnActive;
    inEncounter = savedInEncounter;
    playerEnergy = savedEnergy;
    playerDeck.resetDeck();
}

// The main menu, lifted out of run() so finishRun() can come back to it.
// Returns false if the player chose to quit.
bool Game::mainMenuFlow() {
    int menuChoice = 0;
    int loadSlot = 0;
    while (true) {
        menuChoice = showMainMenu();
        if (menuChoice == 2) return false;
        if (menuChoice != 1) break;
        // Backing out of the slot list returns to the menu.
        loadSlot = chooseSaveSlot("Load a run", /*forSaving*/false);
        if (loadSlot > 0) break;
        UIHelper::clearScreen();
        Hud::setActive(false);
        UIHelper::printTitle();
    }

    if (menuChoice == 1 && loadSlot > 0 && loadGame(loadSlot)) {
        upgrades.displayUpgradeInfo();
        notice("Save loaded. Encounter " + std::to_string(currentRun.getCurrentEncounter())
               + ", " + std::to_string(currentRun.getEncountersWon()) + " enemies defeated so far.");
        offerContinueOrEndRun(false);
        // If the player picked End Run right away without fighting anything, there's
        // no live combat for the loop below to detect via checkGameOver() - wrap up here instead.
        if (!inEncounter) finishRun();
    } else {
        if (menuChoice == 1 && loadSlot > 0) {
            std::cout << "\n" << Color::YELLOW << "Warning: that save could not be loaded - starting a new game instead." << Color::RESET << "\n";
            UIHelper::waitForKey();
        }
        // A fresh run owns no slot until it is saved, so dying cannot take one.
        currentSaveSlot = 0;
        upgrades.checkAndUnlockUpgrades(0, 0);
        upgrades.displayUpgradeInfo();

        Mode picked = Mode::NORMAL;
        bool carry = false;
        if (!chooseMode(picked, carry)) {
            // Backed out of the road picker: back to the title, not into a run.
            UIHelper::clearScreen();
            Hud::setActive(false);
            UIHelper::printTitle();
            return mainMenuFlow();
        }
        // The story belongs to the first road. The harder ones are walked by
        // someone who has already heard it.
        if (picked == Mode::NORMAL || picked == Mode::RANDOM) showIntro();
        startRunInMode(picked, carry);
        startEncounter();
    }
    return true;
}

void Game::run() {
    // Gear cards draw their sword or shield through the art layer.
    CardBar::setIconRenderer(&EnemyArt::drawItemIcon);
    loadProgress();   // which roads this player has already earned
    loadSettings();   // and how they like it read to them
    init();
    migrateLegacySave();   // an older save.dat becomes slot 1

    UIHelper::printTitle();

    if (!mainMenuFlow()) return;

    while (running) {
        handleInput();

        if (checkGameOver()) {
            if (enemy.isAlive()) {
                // The ??? fight cannot end a run.
                if (inSecretEncounter) { handleSecretDefeat(); continue; }
                displayGameOver();
                currentRun.displayRunStats();
                currentRun.loseRun();
                inEncounter = false;
    Hud::setActive(false);   // the panel belongs to the fight
                deleteCurrentSave(); // no reloading out of a loss, but other slots are safe
            } else {
                handleEncounterWin();
                if (inEncounter) {
                    continue;
                }
            }
            finishRun();
        }
    }

    std::cout << "Thanks for playing!\n";
}

void Game::finishRun() {
    int encounters = currentRun.getEncountersWon();
    runStats.completeRun(encounters);
    runStats.displayRunSummary(encounters);

    if (handleGameOverInput()) {
        selectUpgrades();

        Card carryOverCard("", "", CardType::ATTACK, 0, 0);
        bool hasCarryOver = selectCardToCarryOver(carryOverCard);

        maxPlayerHealth = 100;
        playerHealth = maxPlayerHealth;
        playerArmor = 0;
        playerEnergy = 3;
        maxEnergy = 3;
        equipDamagePercent = 0;
        equipArmorPercent  = 0;
        handSizeBonus      = 0;
        runLuck            = 0;
        rewardChoiceBonus  = 0;
        weaponTier = 0;
        armorTier  = 0;
        wornWeapon = wornArmor = 0;
        sealsBroken = 0;
        relicsOwned = 0;
        redThreadUsed = false;
        moonstruckMet = 0;
        satCount = 0;
        runMode = Mode::NORMAL;      // the road is chosen again at the menu
        roadBonus = 0;
        roadGearPct = 0;
        randomSeed = 0;
        randomOrder.clear();
        currentRun.setDifficulty(0);
        // These two are boons, and boons belong to the run. Neither was reset,
        // so Attunement and Scavenger carried into every run after the first.
        attunementBoons = 0;
        gearInterval = 3;
        turnNumber = 1;
        playerTurnActive = true;
        playerDeck = Deck();
        init();
        if (hasCarryOver) {

            if (carryOverCard.isStarter())
                playerDeck.removeCardByName(carryOverCard.getBaseName());
            playerDeck.addCard(carryOverCard);
        }

        runStats.resetRunStats();
        currentRun = Run();
        Console::clearHistory();   // a new run starts a fresh log
        moonZonesSeen = 0;

        // Back to the menu, so a new run or a save can be picked without quitting.
        // The title banner has to go back up first: the menu draws its options
        // through it, and without it the menu is live but invisible.
        UIHelper::clearScreen();
        Hud::setActive(false);
        UIHelper::printTitle();
        if (!mainMenuFlow()) {
            running = false;
            runStats.displayCumulativeStats();
        }
    } else {
        running = false;
        runStats.displayCumulativeStats();
    }
}
