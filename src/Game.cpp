#include "Game.h"
#include "Achievements.h"
#include "Audio.h"
#include "Colors.h"
#include "UIHelper.h"
#include "EnemyArt.h"
#include "ProjectileTable.h"
#include "Console.h"
#include "CardBar.h"
#include "Hud.h"
#include "Platform.h"
#include "GameShared.h"
#include <SDL.h>
#include <algorithm>
#include <iostream>
#include <random>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept> // std::out_of_range, caught below

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

// A special card's cue. The ones that would otherwise share "special" are
// pitched apart, so each reads as its own: the dead coming up out of the
// ground, a whole ailment going off at once, a guard or a blow turned.
static void playCardCue(const Card& c) {
    switch (c.getEffect()) {
        case CardEffect::RAISE:     Audio::playSFXPitched("dead", 0.75f);    return;
        case CardEffect::UNLEASH: {
            const DamageType el = c.getElemType();
            Audio::playSFXPitched(el == DamageType::FIRE ? "fire" : el == DamageType::WIND ? "wind" : "poison", 0.7f);
            return;
        }
        case CardEffect::TURNABOUT: Audio::playSFXPitched("special", 1.3f);  return;
        case CardEffect::FEINT:     Audio::playSFXPitched("special", 1.2f);  return;
        default:                    Audio::playSFX(effectSoundName(c.getEffect())); return;
    }
}

// The cards that leave your deck for the rest of the fight once played: one
// use each, handed back when the fight ends (endEncounterEffects()).
static bool onceAFight(CardEffect e) {
    return e == CardEffect::SACRIFICE || e == CardEffect::LASTSTAND || e == CardEffect::UNLEASH
        || e == CardEffect::TURNABOUT || e == CardEffect::FEINT;
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

// Card -> widget. Every screen that shows cards wants the same conversion, so
// it lives here rather than being rebuilt per screen. elemChance is the live
// chance an elemental attack lands its status, which Attunement raises above
// the 10% the card's own text quotes, and luck is Fortune's points, which make
// Impair, Taunt and Fear surer.
static CardBar::Card toWidget(const Card& c, int shownValue, int elemChance, bool disabled = false,
                              int luck = 0) {
    CardBar::Card w;
    w.name      = c.getName();
    w.effect    = c.brief(shownValue, elemChance, false, luck);
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

// What one upgrade would do to this card, read off a copy that has had it.
// Card::upgrade() alone decides the step, which goes by rarity, and the cost,
// which only the first upgrade trims; a guess kept here fell out of step.
static Card upgradedCopy(const Card& c) { Card u = c; u.upgrade(); return u; }
static int upgradedCost(const Card& c)  { return upgradedCopy(c).getCost(); }
static int upgradedValue(const Card& c) { return upgradedCopy(c).getValue(); }

// The forge's face: the card's brief with every number the upgrade moves
// written "6->8", so it says what the card does and what the forge changes.
// Both briefs are the same sentence with different numbers, so their words
// pair up; if they ever do not, the upgraded one stands alone.
static std::string upgradeFaceLine(const std::string& now, const std::string& next) {
    auto words = [](const std::string& t) {
        std::vector<std::string> out;
        std::istringstream in(t);
        for (std::string w; in >> w; ) out.push_back(w);
        return out;
    };
    const std::vector<std::string> a = words(now), b = words(next);
    if (a.size() != b.size()) return next;
    const char* digits = "0123456789";
    std::string out;
    for (size_t i = 0; i < a.size(); i++) {
        std::string w = b[i];
        if (a[i] != b[i]) {
            // Only the digits may differ: "+5," and "+10," share the "+" and the ",".
            const size_t s0 = a[i].find_first_of(digits), s1 = b[i].find_first_of(digits);
            if (s0 == std::string::npos || s1 == std::string::npos) return next;
            const size_t e0 = a[i].find_last_of(digits), e1 = b[i].find_last_of(digits);
            if (a[i].substr(0, s0) != b[i].substr(0, s1) || a[i].substr(e0 + 1) != b[i].substr(e1 + 1))
                return next;
            w = b[i].substr(0, s1) + a[i].substr(s0, e0 - s0 + 1) + "->" + b[i].substr(s1);
        }
        if (!out.empty()) out += ' ';
        out += w;
    }
    return out;
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

// The first ten fights are where a run is learned, on a deck of starters, and
// Weaken there turned a fight into a slow loss: nothing in them inflicts it,
// the Moonstruck included. A move that only weakens gives its turns to the
// enemy's other moves; one that does something else as well just loses it.
static const int NO_WEAKEN_UNTIL = 10;

// A roll from 0 to 99 spread over the buckets either side of [lo, hi), so that
// bucket never comes up and the others keep their proportions.
static int foldAway(int r, int lo, int hi) {
    const int x = r * (100 - (hi - lo)) / 100;
    return x < lo ? x : x + (hi - lo);
}

// A boon every this many encounters, for the whole run.
static const int BOON_INTERVAL = 11;
// Quick hands out the same four boons and four relics as the full road in
// half the fights (boons on 6/12/18/24, relics on 3/9/15/21), and gear every
// second fight instead of every third, so each boss meets about the same
// knight. What it does not make up is card picks and rest stops.
static const int QUICK_BOON_INTERVAL  = 6;
static const int QUICK_RELIC_FIRST    = 3;
static const int QUICK_RELIC_INTERVAL = 6;
static const int GEAR_INTERVAL        = 3;
static const int QUICK_GEAR_INTERVAL  = 2;
// What one broken seal adds to every enemy, as a percentage of its base. They
// stack: three seals is +45% health and +30% attack.
static const int SEAL_HP_PCT  = 15;
static const int SEAL_ATK_PCT = 10;

// ---- attack types ----------------------------------------------------------
// What a resisted or exposed type does to a blow: gentler than the 1.5x the
// player gets, because the player chooses their cards but not what hits them.
static const int ARMOUR_TYPE_PCT = 25;

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

    // Even the Aegis has a gap. Without one, armour stops being a choice: you
    // take it and never open the Equipment tab again.
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
    "an attack a turn costs 1 less",
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
    "Light in the hand: the first attack you pay for each turn costs 1 less energy. A free one "
    "does not use it up.",
    "Every attack card deals 6 more damage, added before your gear bonus.",
    "Taken off the thing that was wearing your face: your attacks slip past the "
    "enemy's defense entirely.",
    "It answers to the moon: the first attack you play each turn lands twice, the "
    "second time from its reflection, just as hard.",
};

// ---- relics ----------------------------------------------------------------
// One of three every 12 encounters from the 6th, so they fall between the
// boons.
static const int RELIC_FIRST = 6, RELIC_INTERVAL = 12;
int Game::boonInterval() const     { return onQuick() ? QUICK_BOON_INTERVAL : BOON_INTERVAL; }
int Game::relicFirst() const       { return onQuick() ? QUICK_RELIC_FIRST : RELIC_FIRST; }
int Game::relicInterval() const    { return onQuick() ? QUICK_RELIC_INTERVAL : RELIC_INTERVAL; }
int Game::baseGearInterval() const { return onQuick() ? QUICK_GEAR_INTERVAL : GEAR_INTERVAL; }
// Relic cards: a yellow stripe and a purple name, so they never read as gear
// (green) or a card. The cursed one keeps a red name. Icons follow the pouch
// in items.png, in Relic::Id order.
static const SDL_Color RELIC_STRIPE{ 240, 200, 70, 255 };
static const SDL_Color RELIC_NAME{ 190, 130, 255, 255 };
static const int POUCH_ICON = 14, RELIC_ICON0 = 15;

static int signatureChanceFor(const std::string& name) {
    auto has = [&](const char* k) { return name.find(k) != std::string::npos; };
    // The secret fight should play like ITS fight, not like any other beast:
    // its own moves outnumber every kit move put together.
    if (has("Moon Shade")) return 60;
    // Two full hits in one turn; at the usual 40% they come round constantly.
    if (has("Enforcer") || has("Manticore")) return 30;
    // A summon that then stays on the field and attacks every turn.
    if (has("Lich")) return 25;
    // Stone, then the drop, turn and turn about: every turn it takes is its own.
    if (has("Gargoyle")) return 100;
    return kSignatureChance;
}

// The church against a five-legendary deck: its nine and the False Moon.
static const int CHURCH_HP_PCT = 200, CHURCH_ATK_PCT = 133, CHURCH_DEF_BONUS = 4;
static const int FALSE_MOON_HP_BASE = 500;

// Higher = rarer; sorts card lists highest-rarity-first (Forge, View Deck).
static int rarityRank(const Card& c) {
    if (c.isLegendary()) return 4;
    if (c.isSuperRare()) return 3;
    if (c.isRare())      return 2;
    if (c.isStarter())   return 0;
    return 1; // uncommon reward-tier
}

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

void Game::init() {
    // One of each starter card; duplicates only ever come from later card rewards.
    playerDeck.addCard(Card("Quick Jab", "Deal 3 damage.", CardType::ATTACK, 0, 3));
    // Slash is the plain one: a point under Bash and Lunge, but no type for an
    // enemy to resist.
    playerDeck.addCard(Card("Slash", "Deal 5 damage.", CardType::ATTACK, 1, 5));
    playerDeck.addCard(Card("Bash", "Deal 6 damage. Counts as a Smash attack, so it hits harder against enemies weak to Smash and lands softer against those that resist it.", CardType::ATTACK, 1, 6, CardEffect::NONE, false, DamageType::SMASH));
    playerDeck.addCard(Card("Lunge", "Deal 6 damage. Counts as a Pierce attack, so it hits harder against enemies weak to Pierce and lands softer against those that resist it.", CardType::ATTACK, 1, 6, CardEffect::NONE, false, DamageType::PIERCE));
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
    if (has("Moonstruck Shadow Knight")) return "It has stopped trying to look like you. It is only fighting off the sleep now, and every stance you ever dropped, it kept.";
    if (has("False Moon"))      return "No knight, no shape, no shadow: only the moon itself, with nothing left to wear but every move you have ever made.";
    // The ruined church's nine, the faithful and the things they made.
    if (has("Bellringer"))      return "It rang the hours for the moon every night the church stood. The bell took its hearing first, and the moon never answered.";
    if (has("Confessor"))       return "It heard every prayer the faithful made to the moon, and every wrong they owned up to. It still keeps count.";
    if (has("Glass Templar"))   return "The church's last guard. When the windows came down, it took up the glass and wore it.";
    if (has("Gargoyle"))        return "Carved to watch the moon from the roof. The faithful prayed beneath it so long that the moon's light woke it.";
    if (has("Exhumed Saint"))   return "The faithful wanted a saint of their own, so they dug one out of the crypt and stood it back up. It does not know it died.";
    if (has("Mirror Nun"))      return "The moon came to the faithful the way it came to you, as a reflection. She keeps one in a hand mirror.";
    if (has("Sexton"))          return "He buried the faithful the moon never chose, and he has kept digging. There is a hole out back that fits you.";
    if (has("Inquisitor"))      return "The faithful sent it after anyone who said the moon was false. You are on its list, near the top.";
    if (has("Fleshmass"))       return "A body the faithful made for the moon to wear. It could not hold the moon, and it crumbled into this. Whatever it lashes, it holds.";
    if      (has("Stone Colossus")) return "The dungeon's foundation stood up one day. Everything since has been rubble it walked through.";
    else if (has("Vile Witch"))     return "She bought every guard in these halls, and still does her own poisoning.";
    else if (has("Thunder Beast"))  return "The storm over this forest is not weather. It goes where the beast goes, and it gets louder the longer you fight.";
    else if (has("Hydra"))          return "Take a head and the lake hands it another. The lake has always been generous with it.";
    else if (has("Undead Dragon"))  return "Died on this peak an age ago and has never once accepted the terms.";
    else if (has("Shadow Knight"))  return "It has watched you the whole way up. Whatever you play, it answers with the same.";
    // The Dungeon
    else if (has("Goblin"))    return "A scrawny dungeon-scavenger, quick with a blade and quicker to flee.";
    else if (has("Orc"))       return "A dull-eyed brute muscled into the front ranks by sheer size.";
    else if (has("Wizard"))    return "A washed-up spellcaster who never left the ruins he once studied in.";
    else if (has("Skeleton"))  return "Old bones stirred back to motion by whatever still lingers down here.";
    else if (has("Spider"))    return "Fat and pale from years in the dark, spinning webs across forgotten halls.";
    else if (has("Archer"))    return "A hooded scavenger who would rather put an arrow in you from across the room.";
    else if (has("Bandit"))    return "A squatter who knows every blind corner down here, and keeps a knife ready for each of them.";
    else if (has("Warden"))    return "It used to keep these halls locked. Now it keeps in anything that still moves.";
    else if (has("Sage"))      return "An old hermit who mistook the ruins for a place to be left alone, and throws fire at anyone who proves him wrong.";
    // The Dark Dungeon
    else if (has("Ghoul"))     return "Something that used to be a person, now driven by hunger alone. Every bite it takes out of you goes back into it.";
    else if (has("Basilisk"))  return "A pale-eyed reptile whose stare has turned half this dungeon to statues.";
    else if (has("Assassin"))  return "A shape that waits in the dark until you have already committed to a move.";
    else if (has("Knight"))    return "A knight who took the witch's coin and never looked back.";
    else if (has("Sentinel"))  return "Corrupted long ago, and still guarding a door with nothing behind it.";
    else if (has("Enchanter")) return "A young witch searching these halls for her sister, who came down here and never came back. She would rather empty your hand than fight you for it.";
    else if (has("Wraith"))    return "A grudge with no body left to carry it, drifting through the witch's halls.";
    else if (has("Serpent"))   return "Something long and patient, coiling where the torchlight gives out.";
    else if (has("Omneye"))    return "A single vast eye the witch keeps chained to watch her domain.";
    // The Wicked Forest
    else if (has("Raider"))    return "Strikes from the treeline and is gone before the thunder fades.";
    else if (has("Barbarian")) return "A tribal warrior who calls this storm-choked wood home, and whose skin has gone hard as bark.";
    else if (has("Mystic"))    return "A veiled seer who fades from sight whenever the lightning does.";
    else if (has("Banshee"))   return "Her scream carries farther than the thunder, and cuts twice as deep.";
    else if (has("Wolf"))      return "It runs with a pack, though nobody has ever seen more than one of them at a time.";
    else if (has("Falcon"))    return "A hunter and her trained falcon, moving as a single predator.";
    else if (has("Berserker")) return "Feral even by the forest's standards, and getting worse the longer this drags on.";
    else if (has("Guardian"))  return "An eagle-headed knight that has nested in this canopy longer than anyone can say.";
    else if (has("Vampire"))   return "Elegant, patient, and never quite as far away as she seems.";
    // The Dark Lake
    else if (has("Specter"))   return "A shape the fog keeps almost showing you, and never quite does.";
    else if (has("Cockatrice"))return "Half bird, half serpent, all of it eager to turn you to stone.";
    else if (has("Deadeye"))   return "A marksman who has had nothing to do out here but perfect one shot.";
    else if (has("Warrior"))   return "Shipwrecked here years ago and never found a way back to shore.";
    else if (has("Bastion"))   return "A drowned wall of a man, still holding a line no one else remembers.";
    else if (has("Spellmaster"))return "Brews plague in the lake's stagnant shallows, and drinks it like water.";
    // Two of the moon's shapes have their own stories. The Beguiler wears the
    // Enchanter's lost sister (both are met in the Dark Dungeon); the Templar
    // wears someone the mountain's heretics once held above everyone.
    else if (has("Moon Shade Beguiler")) return "The dark moon, in the shape of a great mage lost in these halls, the sister someone down here is still looking for. It is learning her magic by wearing her.";
    else if (has("Moon Shade Templar"))  return "The dark moon, in the shape of a templar who once meant everything to the heretics of this mountain.";
    else if (has("Moon Shade"))return "The dark moon, in the shape of something it struck before you, worn a little wrong. It has followed you since the first gate, learning how you move.";
    else if (has("Revenant"))  return "Rose from the lakebed still furious about how it got there. It waits for your swing so it can give it back.";
    else if (has("Wyvern"))    return "Nests in the reeds at the lake's edge, half-drowned and twice as vicious for it.";
    // The Mountain
    else if (has("Gladiator")) return "Fought his way up from the lowlands and never stopped climbing.";
    else if (has("Paladin"))   return "A holy knight sent from above to hunt whatever desecrates this mountain. He found a piece of you first, and means to destroy it. Nothing you put on him stays for long.";
    else if (has("Unchosen"))  return "One of this mountain's heretics. It begged the moon to strike it, but the moon chose you instead, and it has been trying to become you ever since.";
    else if (has("Sorcerer"))  return "Never seemed to notice the mountain froze around him. Maybe he did that.";
    else if (has("Lich"))      return "The oldest thing on this mountain, and the only one still keeping score. It never fights alone for long.";
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
    if (runLuck > 0)           { any = true; row("Fortune   ", "x" + std::to_string(runLuck) + ": +" + std::to_string(luckBonus())
                                     + (onQuick() ? "% to rewards and chances in a fight"
                                                  : "% to rewards, chances in a fight and the Moon Shades' odds")); }
    if (handSizeBonus > 0)     { any = true; row("Endurance ", "+" + std::to_string(handSizeBonus) + " card(s) every turn"); }
    if (rewardChoiceBonus > 0) { any = true; row("Foresight ", "+" + std::to_string(rewardChoiceBonus) + " card(s) on every reward"); }
    if (attunementBoons > 0)   { any = true; row("Attunement", "elemental attacks land their status " + std::to_string(attunementChance()) + "% of the time"); }
    if (gearInterval < baseGearInterval()) { any = true; row("Scavenger ", "gear every " + std::to_string(gearInterval) + " encounters"); }
    if (!any) std::cout << "    " << Color::DIM << "none yet: one every " << boonInterval() << " encounters" << Color::RESET << "\n";

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
    if (!any) std::cout << "    " << Color::DIM << "none yet: the first on encounter " << relicFirst() << Color::RESET << "\n";

    head("THE ROAD");
    row("Road      ", std::string(modeName()) + (currentRun.getDifficulty() > 0
        ? ": enemies scale as if " + std::to_string(currentRun.getDifficulty() * 50) + " fights had come before"
        : onQuick() ? std::string(": every area at half the length") : std::string()));
    if (currentRun.inChurch())
        row("Encounter ", std::to_string(currentRun.shownEncounter()) + ", in the ruined church, "
            + std::to_string(currentRun.getEncountersWon()) + " won");
    else
        row("Encounter ", std::to_string(currentRun.getCurrentEncounter()) + " of " + std::to_string(currentRun.getLength())
            + ", " + std::to_string(currentRun.getEncountersWon()) + " won");
    if (!onQuick())   // the quick road has no vigils
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
    if (!onQuick())   // nothing follows you on the quick road
        row("???       ", std::to_string(moonstruckMet) + " met this run");
    // Which of the dead Raise Undead would call up, for a deck that holds it.
    for (const Card& c : playerDeck.getAllCardsOrdered())
        if (c.getEffect() == CardEffect::RAISE) {
            row("The dead  ", raisedDead(lastUndead) ? "Raise Undead calls up your " + lastUndead
                                                    : std::string("no undead killed yet for Raise Undead to call up"));
            break;
        }

    head("ACHIEVEMENTS");
    row("Earned    ", std::to_string(Achievements::earnedCount()) + " of "
        + std::to_string(Achievements::knownCount()) + ":  "
        + std::to_string(Achievements::earnedCount(Achievements::BRONZE)) + " bronze, "
        + std::to_string(Achievements::earnedCount(Achievements::SILVER)) + " silver, "
        + std::to_string(Achievements::earnedCount(Achievements::GOLD)) + " gold"
        // The crimson moon stays a secret until it is earned.
        + (Achievements::earnedCount(Achievements::CRIMSON) > 0
           ? ", " + std::to_string(Achievements::earnedCount(Achievements::CRIMSON)) + " crimson"
           : std::string()));

    std::string now;
    auto add = [&](const std::string& t) { now += "    " + t + "\n"; };
    if (vulnerableTurns > 0)   add("Exposed: you take " + std::to_string((int)(vulnerableMult * 100)) + "% damage until your next turn.");
    if (cardDamagePenalty > 0) add("Your cards deal " + std::to_string(cardDamagePenalty) + " less this turn.");
    if (pendingDamagePenalty > 0) add("Your cards will deal " + std::to_string(pendingDamagePenalty) + " less next turn.");
    if (cardSoftenPct > 0)     add("Your attacks deal " + std::to_string(cardSoftenPct) + "% less this turn.");
    if (energyDebt > 0)        add("You start next turn " + std::to_string(energyDebt) + " energy short.");
    if (cardLimitThisTurn > 0) add("Only " + std::to_string(std::max(0, cardLimitThisTurn - cardsPlayedThisTurn)) + " more card(s) this turn.");
    if (pactOfRuinActive)      add("The Pact of Ruin: every card costs 2 HP, every attack festers.");
    if (noHealThisEncounter)   add("You cannot heal until this fight ends.");
    if (extraTurnsPending > 0) add(std::to_string(extraTurnsPending) + " borrowed turn(s) to come, and a stun after each.");
    if (statusWardTurns > 0)   add("Status Guard: ailments blocked for " + std::to_string(statusWardTurns) + " more turn(s).");
    if (armorReversed)         add("Your armor is turned: it fears what it resisted and resists what it feared, until this fight ends.");
    if (moonClock > 0)         add("Moonstruck: " + std::to_string(moonClock) + " turn(s) until you are its vessel."
                                   + (moonTaken().empty() ? std::string() : " It has taken back: " + moonTaken() + "."));
    if (raisedAlive)           add("Your " + raisedName + " stands beside you: " + std::to_string(raisedHp) + "/"
                                   + std::to_string(raisedMaxHp) + " HP, striking for "
                                   + std::to_string(raisedStrikeFor(raisedName, raisedValue))
                                   + " at the end of your turns.");
    if (!now.empty()) { head("RIGHT NOW"); std::cout << now; }
}

void Game::displayEnemyInfo() const {
    // Includes any self-buff, so the numbers on this screen are the numbers
    // the enemy will actually hit for right now.
    int atk = enemy.getBaseAttack() + enemy.getBonusAttack();
    int def = enemy.getBaseDefense(); // used below for move-estimate formulas, not the live display

    EnemyArt::print(EnemyArt::getWalkFrame(enemy.getType(), enemy.getBossType()));

    std::cout << "\n" << Color::BOLD << Color::RED << enemy.getName() << Color::RESET << "\n";
    // The true form keeps the Shadow Knight's name, so its lore is asked for by
    // the key the lore table knows it by.
    std::string flavor = enemyFlavorText(trueFormPhase ? std::string("Moonstruck Shadow Knight") : enemy.getName());
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
    if (enemy.typesAreReversed())
        std::cout << "  " << Color::CYAN << "Turned: those two have swapped for the rest of the fight."
                  << Color::RESET << "\n";
    enemy.displayStatusEffects("  ");
    // The rule it fights by, and where it stands: the church's nine, and the
    // Unchosen on the mountain.
    {
        const std::string rule = churchRule();
        if (!rule.empty())
            for (const std::string& row : UIHelper::wrapRows(rule))
                std::cout << "  " << Color::MAGENTA << row << Color::RESET << "\n";
    }

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
                std::cout << "  " << Color::RED << "Strike" << Color::RESET << " (30%, " << atk << " dmg) - she comes in and strikes with her staff\n";
                std::cout << "  " << Color::HEAL << "Life Siphon" << Color::RESET << " (15%, heals 20) - drains your health into herself\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Toxic Eruption" << Color::RESET << " (15%, Poison 6) - the ground itself festers\n";
                break;
            case BossType::WARLORD:
                std::cout << "  " << Color::RED << "Heavy Strike" << Color::RESET << " (73%, " << atk << " dmg) - each one also gives it +1 attack, up to +10\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Battlecry" << Color::RESET << " (15%, Weaken 3) - no attack that turn\n";
                std::cout << "  " << Color::STUN_CLR << "Thunderstrike" << Color::RESET << " (12%, stun) - you lose your next turn\n";
                break;
            case BossType::HYDRA:
                std::cout << "  " << Color::CARD_SPECIAL << "Venomous Bite" << Color::RESET << " (30%, Poison 5) - venom in the wound\n";
                std::cout << "  " << Color::RED << "Many Heads" << Color::RESET << " (25%, " << std::max(1, atk / 2)
                          << " dmg x" << hydraHeads << ") - a half-weight bite per head it has\n";
                std::cout << "  " << Color::RED << "Bite" << Color::RESET << " (25%, " << atk << " dmg) - a direct attack\n";
                std::cout << "  " << Color::HEAL << "Regrowth" << Color::RESET << " (20%, heals 18) - two heads from every open stump,"
                          << " else one new head; up to " << HYDRA_HEADS_MAX << "\n";
                std::cout << "  " << Color::DIM << "Your Pierce hits cut a head off, and a Rend tearing as it"
                          << " swings does half the time." << Color::RESET << "\n";
                std::cout << "  " << Color::DIM << "A Fire hit or a Burn on it sears the stumps shut, so"
                          << " nothing grows back from them." << Color::RESET << "\n";
                break;
            case BossType::DRAGON:
                std::cout << "  " << Color::RED << "Cursed Bite" << Color::RESET << " (30%, " << atk
                          << " dmg + Rend 3) - the wound opens again each time it strikes\n";
                std::cout << "  " << Color::RED << "Claw Rake" << Color::RESET << " (25%, " << (atk + 5) << " dmg) - ignores your armor\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Wing Buffet" << Color::RESET << " (25%, Weaken 3) - knocks you off balance\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Fire Breath" << Color::RESET << " (20%, Burn 8) - a wall of flame\n";
                break;
            case BossType::SHADOW_KNIGHT:
                std::cout << "  " << Color::CARD_SPECIAL << "Dark Mirror" << Color::RESET
                          << " (each card you play, up to 3 a round) - answers with a shadow copy of a random card"
                          << " from YOUR deck\n";
                std::cout << "  " << Color::RED << "Swing" << Color::RESET << " ("
                          << std::max(1, atk * KNIGHT_TURN_END_PCT / 100)
                          << " dmg) - a lighter blow, when you end a turn without playing a card\n";
                if (trueFormPhase) {
                    std::cout << "  " << Color::MAGENTA << "  True form: every card comes back whole. Stances are real,"
                              << " drawbacks are paid, extra moves are taken." << Color::RESET << "\n";
                    std::cout << "  " << Color::MAGENTA << "  Its Dodge Reversal or Parry is taken up before your card"
                              << " lands, so the blow walks into it." << Color::RESET << "\n";
                    std::cout << "  " << Color::DIM << "  What your cards cost you, they cost it: health, armor, moves"
                              << " next round, its ability to heal." << Color::RESET << "\n";
                }
                std::cout << "  " << Color::DIM << "  Your attacks become its strikes, your armor its guard, your healing its mending." << Color::RESET << "\n";
                std::cout << "  " << Color::DIM << "  The bigger your deck's numbers, the harder it hits back." << Color::RESET << "\n";
                break;
            case BossType::FALSE_MOON:
                std::cout << "  " << Color::RED << "Moonstruck" << Color::RESET
                          << " (before your first turn) - ten turns until you are its vessel. Every two, it takes back"
                             " a piece of you: your strength (a quarter softer), your wits (a card fewer), your speed"
                             " (an energy fewer), the way you move (your guard half as strong), and last, your soul.\n";
                std::cout << "  " << Color::DIM << "  Every quarter of its health you take while you have fewer than"
                             " five turns left wins a turn back. Sacrifice and Last Stand each win a turn"
                             " back, and if the Red Thread saves you, you have at least five turns left."
                          << (moonClock > 0 ? " Turns left: " + std::to_string(moonClock) + "." : std::string())
                          << Color::RESET << "\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Dark Mirror" << Color::RESET
                          << " (each card you play: 3 a round, 4 below two thirds of its health, 5 below a third)"
                             " - answers with one of YOUR cards, played whole, price and all\n";
                std::cout << "  " << Color::RED << "Swing" << Color::RESET << " ("
                          << std::max(1, atk * KNIGHT_TURN_END_PCT / 100)
                          << " dmg) - when you end a turn without playing a card\n";
                std::cout << "  " << Color::MAGENTA << "A shape it wore" << Color::RESET
                          << " (in place of the swing, half the time) - one of the Moon Shades' own moves:"
                             " Grave Silk (Weaken 2, Poison 4), Lunar Mirage (2 fewer cards, and armor),"
                             " the Howl (+2 attack, up to +6), the Gaze (a turn off the clock) or"
                             " Eclipse Ward (heavy armor)\n";
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
            if (enemy.blowTypeOverride() != DamageType::NONE)
                std::cout << Color::CYAN << " (feinted, for the fight)" << Color::RESET;
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
        // The Moonstruck has no kit: its three moves are every turn it takes.
        const bool moon = nameHas("Moon Shade");
        int S = ownMoveChance();
        if (moon) S = 100;
        // Neither it nor the Gargoyle has a kit: every turn is one of its own.
        const bool ownOnly = moon || S >= 100;
        const bool weakens = enemyWeakens();
        const int kit = 100 - S;
        const std::string all = pct(S);
        auto sig    = [&](int share) { return pct(S * share / 100); };
        auto kitPct = [&](int share) { return pct(share * kit / 100); };

        bool named = true;
        // THE RUINED CHURCH: their own moves. Their rules are listed above.
        if      (nameHas("Sexton"))        line(Color::RED, "Spade", tag(all, dmg(atk + 2)), "a swing that throws earth in your face.");
        else if (nameHas("Mirror Nun"))    line(Color::WEAK_CLR, "Moon in the Glass", tag(all, "Weaken 2"), "it also makes your next hand a card smaller.");
        else if (nameHas("Gargoyle")) {
            line(Color::RED, "Drop", tag("50%, every waking turn", dmg(atk + 4)), "it drops on you from the roof.");
            line(Color::CYAN, "Stone", "50%, the turn after", "back to its perch: nothing hurts it on your next turn.");
        }
        else if (nameHas("Bellringer"))    line(Color::CARD_SPECIAL, "Peal", all, "the bell goes on ringing in your head: your next hand is a card smaller.");
        else if (nameHas("Inquisitor"))    line(Color::RED, "Quarrel", tag(all, dmg(atk + 3)), "a heavy bolt that bypasses half your armor.");
        else if (nameHas("Exhumed Saint")) line(Color::RED, "Crosier", tag(all, dmg(atk + 3)), "a blow with its crosier.");
        else if (nameHas("Confessor"))     line(Color::RED, "Penance", tag(all, dmg(atk + 2)), "bypasses half your armor.");
        else if (nameHas("Glass Templar")) line(Color::RED, "Leaded Shield", tag(all, dmg(atk + 2)), "a bash with its glass shield.");
        else if (nameHas("Unchosen"))  line(Color::RED, "Borrowed Cut", tag(all, dmg(atk + 2)), "your own swing, copied badly.");
        // MELEE
        else if (nameHas("Goblin"))    line(Color::RED, "Jab", tag(all, dmg(atk)), "a quick strike.");
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
        else if (nameHas("Barbarian")) line(Color::ARMOR_CLR, "Iron Skin", tag(all, armor(def + 6)),
                                            weakens ? "hardens up; half the time it also leaves you Weakened 2." : "hardens up.");
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
        else if (nameHas("Sorcerer"))  line(Color::BLUE, "Ice Blast", weakens ? tag(all, "Weaken 2") : all,
                                            weakens ? "it also makes your next hand a card smaller." : "your next hand is a card smaller.");
        else if (nameHas("Vampire"))   line(Color::MAGENTA, "Vampiric Drain", tag(all, dmg(10)),
                                            weakens ? "a bite: she heals +6, gains +1 attack, and leaves you Weakened 2."
                                                    : "a bite: she heals +6 and gains +1 attack.");
        else if (nameHas("Mystic"))    line(Color::CYAN, "Illusion", all, "takes no damage on your next turn.");
        // RANGED
        else if (nameHas("Deadeye"))   line(Color::RED, "Dead Shot", tag(all, dmg(atk)), "a shot that bypasses half your armor.");
        else if (nameHas("Wyvern"))    line(Color::RED, "Flying Gnash", tag(all, dmg(atk + 2)), "a diving bite that bypasses half your armor.");
        else if (nameHas("Omneye")) {
            line(Color::RED, "Eye-Beam", tag(sig(weakens ? 70 : 100), dmg(atk + 2)), "a beam that bypasses half your armor.");
            if (weakens) line(Color::WEAK_CLR, "Gaze", tag(sig(30), "Weaken 2"), "an unsettling stare.");
        }
        else if (nameHas("Assassin")) {
            // Not a turn move: it fires during YOUR turn, so its turns use the
            // generic ranged moves listed with it.
            line(Color::RED, "Ambush", std::to_string(ASSASSIN_AMBUSH_PCT) + "% per card you play, " + dmg(atk),
                 "once per turn, strikes from the shadows.");
            named = false;
        }
        else if (nameHas("Falcon"))    line(Color::CYAN, "Gouge", tag(all, dmg(atk + 1)), "a wind-borne dive that rakes past half your armor.");
        // BEAST
        else if (nameHas("Wolf"))      line(Color::RED, "Bite", tag(all, dmg(atk)), "a lunging bite.");
        // Early on these two have no move of their own: their only one weakens.
        else if (nameHas("Spider"))    { if (weakens) line(Color::CARD_SPECIAL, "Web Trap", tag(all, "Weaken 2"), "a sticky snare."); }
        else if (nameHas("Serpent"))   { if (weakens) line(Color::CARD_SPECIAL, "Entangle", tag(all, "Weaken 3"), "coils around you."); }
        else if (nameHas("Basilisk"))  line(Color::MAGENTA, "Curse", tag(all, "once"), "lose the run if it isn't dead in 7 turns. After that, those turns go to its other moves.");
        else if (nameHas("Cockatrice"))line(Color::MAGENTA, "Petrifying Bite", tag(all, dmg(atk)), "bites, then 5 turns to kill it or you turn to stone. Once per fight.");
        else if (nameHas("Manticore")) line(Color::RED, "Twin Maw", tag(all, dmg(atk) + " x2"), "both heads bite in the same lunge.");
        else if (nameHas("Fleshmass")) line(Color::MAGENTA, "Bind", tag(all, dmg(atk)), "a lash that draws blood also limits you to 1 card next turn.");
        // UNDEAD
        else if (nameHas("Ghoul"))     line(Color::CARD_SPECIAL, "Chomp", tag(all, dmg(atk)), "heals itself +8 and gives you Poison 3.");
        else if (nameHas("Banshee"))   line(Color::CARD_SPECIAL, "Wailing Scream", weakens ? tag(all, "Weaken 2") : all,
                                            weakens ? "she also gains +2 attack, up to +6." : "she gains +2 attack, up to +6.");
        else if (nameHas("Specter") || nameHas("Wraith")) line(Color::CYAN, "Ghost", all, "takes no damage on your next turn.");
        else if (nameHas("Moon Shade")) {
            const int z = std::max(0, std::min(4, moonZone));
            const MoonMove& own = MOON_MOVES[z];
            const std::string frenzy = timesText(MOON_FRENZY[z]);
            // Straight percentages, not the shared sig() wording: this is the
            // one enemy whose moves are not gated behind a signature roll.
            std::string info = own.info;
            if (z == 0 && !weakens) info = "Poison 3: bone-white thread that rots what it holds.";
            if (z == 2) info = "heals " + std::to_string(howlHeal(enemy.getMaxHealth())) + " and gains +2 attack, up to +6.";
            if (z <= MOON_LEARNING_LAST) {
                // Still learning: the scent lasts one blow, and opens it up until then.
                line(Color::STRENGTH_CLR, "Moon Scent", "40% when calm", "its next turn is a Moonlit Maul at " + frenzy
                     + ", and until then it is exposed: your attacks deal x1.5.");
                line(Color::RED, "Moonlit Maul", tag("32% when calm, always right after Moon Scent", dmg(atk + 3)),
                     "a savage blow, " + frenzy + " straight after the scent.");
                line(Color::MAGENTA, own.name, "28% when calm", info);
            } else {
                line(Color::STRENGTH_CLR, "Moon Scent", "40% when calm", "works itself into a frenzy: its blows hit " + frenzy + " for 3 turns.");
                line(Color::RED, "Moonlit Maul", tag("32% calm, 78% frenzied", dmg(atk + 3)), "a savage blow, " + frenzy + " while the scent lasts.");
                line(Color::MAGENTA, own.name, "28% calm, 22% frenzied", info);
            }
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
                    line(Color::RED, "Pierce attack", tag(sig(weakens ? 60 : 75), dmg(atk)), "bypasses half your armor.");
                    line(Color::ARMOR_CLR, "Defend", tag(sig(weakens ? 20 : 25), armor(std::max(1, def - 1))), "braces.");
                    if (weakens) line(Color::CARD_SPECIAL, "Crippling shot", tag(sig(20), "Weaken 2"), "you deal less damage for 2 turns.");
                    break;
                case EnemyType::TANK:
                    line(Color::ARMOR_CLR, "Defend", tag(sig(65), armor(def)), "braces.");
                    line(Color::RED, "Attack", tag(sig(35), dmg(std::max(1, atk - 2))), "reduced by your armor.");
                    break;
                case EnemyType::CASTER:
                    if (enemy.getHealth() < enemy.getMaxHealth() / 3) {
                        const int mend = nameHas("Wizard") ? 3 : 8 + def / 2;
                        line(Color::HEAL, "Heal", tag(sig(60), "~" + std::to_string(mend) + " HP"), "while it is below a third of its HP.");
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
                    line(Color::RED, "Attack", tag(sig(weakens ? 75 : 100), dmg(atk)), "reduced by your armor.");
                    if (weakens) line(Color::CARD_SPECIAL, "Chilling touch", tag(sig(25), "Weaken 2"), "you deal less damage for 2 turns.");
                    break;
                default: break;
            }
        }
        flush(ownOnly ? std::string("  Its moves:") : "  Its own moves (" + pct(S) + " of turns):");

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
                    // Early on the Weaken slice goes to the other two, in proportion.
                    if (enemyIsFlyer()) {
                        line(Color::RED, "Dive", tag(kitPct(weakens ? 60 : 80), dmg(atk)), "comes in fast and is gone before Parry can answer.");
                        if (weakens) line(Color::WEAK_CLR, "Wingbeat", tag(kitPct(25), "Weaken 2"), "a gust in your face.");
                        line(Color::RED, "Double rake", tag(kitPct(weakens ? 15 : 20), dmg(std::max(1, atk / 2)) + " x2"), "two passes on a wingtip.");
                        break;
                    }
                    if (nameHas("Assassin")) {   // thrown blades: armour stops them
                        line(Color::RED, "Throw", tag(kitPct(weakens ? 60 : 80), dmg(atk)), "a blade from the dark.");
                        if (weakens) line(Color::WEAK_CLR, "Weakening shot", tag(kitPct(25), "Weaken 2"), "clips your arm.");
                        line(Color::RED, "Double throw", tag(kitPct(weakens ? 15 : 20), dmg(std::max(1, atk / 2)) + " x2"), "two blades, one after the other.");
                        break;
                    }
                    line(Color::RED, "Shot", tag(kitPct(weakens ? 60 : 80), dmg(atk)), "bypasses half your armor.");
                    if (weakens) line(Color::WEAK_CLR, "Weakening shot", tag(kitPct(25), "Weaken 2"), "clips your arm.");
                    line(Color::RED, "Double shot", tag(kitPct(weakens ? 15 : 20), dmg(std::max(1, atk / 2)) + " x2"), "both bypass half your armor.");
                    break;
                case EnemyType::CASTER:
                    if (nameHas("Wizard")) {   // no hex, and a small mend
                        line(Color::RED, "Force bolt", tag(kitPct(45), dmg(atk)), "a plain cast.");
                        line(Color::HEAL, "Mend / brace", tag(kitPct(55), "~3 HP"), "heals when below half HP, else braces for " + armor(def) + ".");
                        break;
                    }
                    line(Color::RED, "Force bolt", tag(kitPct(weakens ? 45 : 64), dmg(atk)), "a plain cast.");
                    if (weakens) line(Color::WEAK_CLR, "Hex", tag(kitPct(30), "Weaken 2"), "a curse that settles over you.");
                    line(Color::HEAL, "Mend / brace", tag(kitPct(weakens ? 25 : 36), "~" + std::to_string(6 + def) + " HP"), "heals when below half HP, else braces for " + armor(def) + ".");
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
        if (!ownOnly) printKit();
    }
    std::cout << "\n";
}

// Mirrors the attack path in playCardFromHand. Kept deliberately close to it:
// if one changes and the other does not, the preview starts lying.
int Game::previewDamage(const Card& c) const {
    // Setting an ailment off lands on the enemy itself, whatever stands in front.
    if (c.getEffect() == CardEffect::UNLEASH) return std::min(enemy.getHealth(), unleashDamage(c));
    if (c.getType() != CardType::ATTACK) return 0;
    if (lichAddAlive) return 0;                       // the skeleton soaks it all
    // The card the Confessor named heals it instead.
    if (!confessedCard.empty() && c.getName() == confessedCard && enemyIs("Confessor")) return 0;
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
    // A Pierce hit goes round the Glass Templar's glass.
    const bool roundGlass = glassUp && enemyIs("Glass Templar")
        && (c.getPhysType() == DamageType::PIERCE || c.getPhysType2() == DamageType::PIERCE);
    int armor = roundGlass ? 0 : enemy.getArmor(), lost = 0;
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
    if (hasRelic(Relic::SEAL_FRAGMENT) && sealsBroken > 0) v = v * (100 + 8 * sealsBroken) / 100;
    if (hasRelic(Relic::GLASS_MOON)) v = v * 125 / 100;
    // The False Moon has your strength: every blow a quarter softer.
    if (moonTook(8)) v = v * 3 / 4;
    // What the drawback cards charge: a flat cut from Reckless Swing, a
    // percentage from Heavy Guard. Both run through here so the hand, the
    // preview and the swing itself all quote the same number.
    if (cardSoftenPct > 0) v = v * (100 - cardSoftenPct) / 100;
    v -= cardDamagePenalty;
    return std::max(1, v);
}

// What the raised dead strike for, before their own weight: the card's number
// through the weapon and the relics that lift every blow, but not the blade's
// own heft or what your cards are paying this turn. It is not your sword.
int Game::raisedBase(int value) const {
    int v = applyPct(value + upgrades.getDamageBonus() + roadBonus, weaponPct());
    if (hasRelic(Relic::SEAL_FRAGMENT) && sealsBroken > 0) v = v * (100 + 8 * sealsBroken) / 100;
    if (hasRelic(Relic::GLASS_MOON)) v = v * 125 / 100;
    return std::max(1, v);
}

int Game::raisedStrikeFor(const std::string& name, int value) const {
    const RaisedDead* d = raisedDead(name);
    return d ? std::max(1, raisedBase(value) * d->strikePct / 100) : 0;
}

int Game::attunementChance() const {
    // 10% base, and each Attunement boon adds 12 points to it.
    return std::min(100, 10 + attunementBoons * 12);
}

int Game::defWithGear(int rawValue) const {
    const int v = applyPct(rawValue + upgrades.getArmorBonus() + roadBonus, armorPct());
    // The False Moon has the way you move: your guard comes up half as strong.
    return moonTook(2) ? v / 2 : v;
}

// SPECIAL cards (heals, buffs, taunts) are deliberately untouched by gear.
int Game::gearedValue(const Card& c, int rawValue) const {
    if (c.getType() == CardType::ATTACK) return atkWithGear(rawValue);
    if (c.getType() == CardType::DEFEND) return defWithGear(rawValue);
    // Raise Undead's number is a strike, through the weapon like an attack's.
    if (c.getEffect() == CardEffect::RAISE) return raisedBase(rawValue);
    // And a poison, burn or rend card lands half again as strong under its relic.
    if (c.getType() == CardType::SPECIAL) {
        if (c.getEffect() == CardEffect::POISON) return dotPower(StatusType::POISON, rawValue);
        if (c.getEffect() == CardEffect::BURN)   return dotPower(StatusType::BURN, rawValue);
        if (c.getEffect() == CardEffect::REND)   return dotPower(StatusType::REND, rawValue);
    }
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
        // What setting the ailment off would take off the enemy right now.
        case CardEffect::UNLEASH:   return unleashDamage(c);
        default:                    return gearedValue(c, c.getValue());
    }
}

bool Game::hasRelic(int id) const { return (relicsOwned >> id) & 1; }

// What a card costs to play right now. The Mythril Edge takes 1 off the first
// attack you pay for each turn; everything else costs what it says. A free
// attack played first does not use the discount up.
int Game::effectiveCost(const Card& c) const {
    int cost = c.getCost();
    if (wornWeapon == 5 && c.getType() == CardType::ATTACK && !mythrilSpent && cost > 0)
        cost -= 1;
    return cost;
}

bool Game::freeCardPlayable() const {
    if (playerBoundTurn && cardsPlayedThisTurn >= 1) return false;
    if (cardLimitThisTurn > 0 && cardsPlayedThisTurn >= cardLimitThisTurn) return false;
    for (int i = 0; i < playerDeck.handSize(); ++i) {
        if (playerDeck.isCardUsed(i)) continue;
        const Card& c = playerDeck.getCardFromHand(i);
        if (playerAttackOnly && c.getType() != CardType::ATTACK) continue;
        if (isGraveDirt(c)) continue;   // free, and cannot be played at all
        if (effectiveCost(c) == 0) return true;
    }
    return false;
}

// The kind of blow each enemy deals, for the armour: by name, with its
// archetype as the fallback. Bosses and Moonstruck forms first, since
// "Shadow Knight" contains "Knight".
DamageType Game::enemyAttackType() const {
    using D = DamageType;
    // Feint: it was drawn into another kind of attack.
    if (enemy.blowTypeOverride() != D::NONE) return enemy.blowTypeOverride();
    static const std::pair<const char*, D> TABLE[] = {
        {"Moon Shade Weaver", D::POISON}, {"Moon Shade Beguiler", D::WIND},
        {"Moon Shade Gorgon", D::POISON}, {"Moon Shade Templar", D::SMASH}, {"Moon Shade", D::PIERCE},
        {"Colossus", D::SMASH}, {"Witch", D::POISON}, {"Thunder", D::WIND}, {"Hydra", D::POISON},
        {"Dragon", D::FIRE},    {"Shadow Knight", D::PIERCE}, {"False Moon", D::PIERCE},
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
        // The Ruined Church
        {"Sexton", D::SMASH}, {"Mirror Nun", D::WIND}, {"Gargoyle", D::SMASH}, {"Bellringer", D::SMASH},
        {"Inquisitor", D::PIERCE}, {"Exhumed Saint", D::SMASH}, {"Confessor", D::FIRE},
        {"Glass Templar", D::PIERCE}, {"Unchosen", D::PIERCE},
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
    // The true form's Turnabout turns it inside out for the rest of the fight.
    const int sign = armorReversed ? -1 : 1;
    if (t == p.resist1 || t == p.resist2) return -ARMOUR_TYPE_PCT * sign;
    if (t == p.weak) return ARMOUR_TYPE_PCT * sign;
    return 0;
}

// Feint turns its blows to the first type your armor resists.
DamageType Game::feintType() const {
    for (DamageType t : { DamageType::SMASH, DamageType::PIERCE, DamageType::FIRE,
                          DamageType::POISON, DamageType::WIND })
        if (armourTypeMod(t) < 0) return t;
    return DamageType::NONE;
}

// Only worth playing when its blows are not already ones your armor resists.
bool Game::feintHelps() const {
    return feintType() != DamageType::NONE && armourTypeMod(enemyAttackType()) >= 0;
}

// In the hand they say what they would do to this enemy.
std::string Game::typesFace(const Card& c) const {
    if (c.getEffect() == CardEffect::TURNABOUT)
        return std::string("For the fight it is weak to ") + typeWord(enemy.getResistance())
             + " and resists " + typeWord(enemy.getWeakness()) + ". Once a fight.";
    const DamageType from = enemyAttackType(), to = feintType();
    if (to == DamageType::NONE) return "Your armor resists nothing, so there is no type to turn their blows into.";
    if (armourTypeMod(from) < 0) return std::string("Your armor already resists their ") + typeWord(from) + " blows.";
    return std::string("Their ") + typeWord(from) + " blows come as " + typeWord(to)
         + ", which your armor resists, for the fight. Once a fight.";
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
    // The Hydra: a Pierce hit cuts a head off, down to one, and leaves the stump
    // open; a Fire hit sears every open stump shut. Emberlance does both.
    if (enemy.getBossType() == BossType::HYDRA) {
        const bool cuts = card.getPhysType() == DamageType::PIERCE || card.getPhysType2() == DamageType::PIERCE;
        if (cuts && hydraHeads > 1) {
            hydraHeads--;
            hydraStumps++;
            Audio::playSFXPitched("attack", 0.7f);
            std::cout << "  " << Color::CYAN << "You cut one of its heads off. It has " << hydraHeads
                      << " left." << Color::RESET << "\n";
            if (hydraHeads == 1) earn(Achievements::ONE_HEAD);
        }
        if (card.getElemType() == DamageType::FIRE && hydraStumps > 0) {
            earn(Achievements::SEAR);
            Audio::playSFXPitched("fire", 1.35f);
            std::cout << "  " << Color::BURN_CLR
                      << (hydraStumps == 1 ? "The fire sears the stump shut. Nothing will grow back from it."
                                           : "The fire sears the stumps shut. Nothing will grow back from them.")
                      << Color::RESET << "\n";
            hydraStumps = 0;
        }
    }
    // The chance rolls here lean your way with Fortune, 5 points a pick.
    if (wornWeapon == 1 && d100(gen) <= 15 + luckBonus() && applyEnemyStatus(StatusType::POISON, 2))
        std::cout << "  " << Color::POISON_CLR << "Rust gets into the wound. Poison "
                  << dotPower(StatusType::POISON, 2) << "." << Color::RESET << "\n";
    const bool smash = card.getPhysType() == DamageType::SMASH || card.getPhysType2() == DamageType::SMASH;
    if (smash && hasRelic(Relic::WEIGHTED_POMMEL) && d100(gen) <= 35 + luckBonus()
        && applyEnemyStatus(StatusType::WEAK, 2))
        std::cout << "  " << Color::WEAK_CLR << "The pommel rings its skull. Weakened." << Color::RESET << "\n";
}

// The relics that act once at the start of every fight.
void Game::applyFightStartRelics() {
    attacksPlayedThisTurn = 0; mythrilSpent = false;
    openingAttack = false;
    if (hasRelic(Relic::WARDEN_LANTERN)) playerArmor += lanternArmor();
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
// Nothing weakens you in the first ten fights (Quick's first five): see
// NO_WEAKEN_UNTIL.
bool Game::enemyWeakens() const { return currentRun.getRoadPosition() > NO_WEAKEN_UNTIL; }

// How often this enemy uses its own moves right now. The Spider's and the
// Serpent's one move is a Weaken, so early on they have none and every turn
// goes to their kit.
int Game::ownMoveChance() const {
    const std::string n = enemy.getName();
    if (!enemyWeakens() && (n.find("Spider") != std::string::npos || n.find("Serpent") != std::string::npos))
        return 0;
    return signatureChanceFor(n);
}

// The kit's roll as the kit reads it. Early on its Weaken bucket is folded
// away and the moves either side keep their proportions. The lens reads the
// same roll through here, so what it shows is what comes.
int Game::kitRoll(int r) const {
    if (enemyWeakens()) return r;
    if (enemy.getType() == EnemyType::RANGED) return foldAway(r, 60, 85);
    if (enemy.getType() == EnemyType::CASTER && enemy.getName().find("Wizard") == std::string::npos)
        return foldAway(r, 45, 75);
    return r;
}

// Mirrors the signature branches of the enemy's turn (enemyTurn()), in the
// same order and on the same roll, so the lens never names a move that does
// not come. A change to one of those branches belongs here too.
std::string Game::ownMoveName(int roll, bool taunted) const {
    auto has = [&](const char* k) { return enemy.getName().find(k) != std::string::npos; };
    // What a taunted enemy does when its own move is not an attack.
    const std::string plain = "Attack";
    const bool lowHp = enemy.getHealth() < enemy.getMaxHealth() / 3;
    // THE RUINED CHURCH. Their rule turns are read in lensIntent(), ahead of this.
    if (has("Gargoyle"))      return gargoyleStone ? "Drop" : taunted ? "Lunge" : "Stone";
    if (has("Sexton"))        return "Spade";
    if (has("Mirror Nun"))    return taunted ? plain : "Moon in the Glass";
    if (has("Bellringer"))    return taunted ? plain : "Peal";
    if (has("Inquisitor"))    return "Quarrel";
    if (has("Exhumed Saint")) return "Crosier";
    if (has("Confessor"))     return "Penance";
    if (has("Glass Templar")) return "Leaded Shield";
    if (has("Unchosen"))  return "Borrowed Cut";
    // MELEE
    if (has("Goblin"))      return "Jab";
    if (has("Bandit"))      return "Dagger Throw";
    if (has("Raider"))      return "Bash";
    if (has("Warrior"))     return "Pierce";
    if (has("Knight"))      return taunted ? plain : "Shield Bash";
    if (has("Berserker"))   return (!taunted && roll < 45 && enemy.getBonusAttack() < 6) ? "Roar" : "Frenzy";
    if (has("Gladiator"))   return "Uppercut";
    if (has("Enforcer"))    return "Combo Strike";
    // TANK
    if (has("Guardian"))    return "Whirlwind";
    if (has("Barbarian"))   return taunted ? plain : "Iron Skin";
    if (has("Sentinel"))    return taunted ? plain : "Fortify";
    if (has("Warden"))      return "Smackdown";
    if (has("Paladin"))     return (!taunted && roll < 40) ? "Absolution" : "Judgement";
    if (has("Bastion"))     return taunted ? plain : roll < 60 ? "Wall" : "Challenge";
    if (has("Fortress"))    return taunted ? plain : "Shield Bash";
    if (has("Orc"))         return "Body Slam";
    // CASTER
    if (has("Sage"))        return taunted ? plain : (lowHp && roll < 40) ? "Heal" : "Torch";
    if (has("Archon"))      return taunted ? plain : (lowHp && roll < 35) ? "Mend" : "Hellfire";
    if (has("Spellmaster")) return taunted ? plain : (lowHp && roll < 35) ? "Mend" : "Virulent Plague";
    if (has("Enchanter"))   return taunted ? plain : "Tempt";
    if (has("Sorcerer"))    return taunted ? plain : "Ice Blast";
    if (has("Vampire"))     return taunted ? plain : "Vampiric Drain";
    if (has("Mystic"))      return taunted ? plain : enemyInvulnerable ? "" : "Illusion";
    // RANGED
    if (has("Deadeye"))     return "Dead Shot";
    if (has("Wyvern"))      return "Flying Gnash";
    if (has("Omneye"))      return (!taunted && roll < 30 && enemyWeakens()) ? "Gaze" : "Eye-Beam";
    if (has("Falcon"))      return "Gouge";
    // BEAST
    if (has("Wolf"))        return "Bite";
    if (has("Spider"))      return taunted ? plain : "Web Trap";
    if (has("Serpent"))     return taunted ? plain : "Entangle";
    if (has("Basilisk"))    return taunted ? plain : curseTurnsLeft != 0 ? "" : "Curse";
    if (has("Cockatrice"))  return taunted ? plain : curseTurnsLeft != 0 ? "" : "Petrifying Bite";
    if (has("Manticore"))   return "Twin Maw";
    if (has("Fleshmass"))   return "Bind";
    // UNDEAD
    if (has("Ghoul"))       return taunted ? plain : "Chomp";
    if (has("Banshee"))     return taunted ? plain : "Wailing Scream";
    if (has("Specter") || has("Wraith")) return taunted ? plain : enemyInvulnerable ? "" : "Ghost";
    if (has("Moon Shade")) {
        const int z = std::max(0, std::min(4, moonZone));
        const bool calm = !enemy.hasStrength();
        const bool learning = z <= MOON_LEARNING_LAST;
        const bool own = !taunted && (calm ? roll >= 72 : (!learning && roll >= 78));
        if (!taunted && calm && !own && roll < 40) return "Moon Scent";
        // The gaze does not stack: with the stone already creeping, it mauls.
        if (own) return (z == 3 && curseTurnsLeft != 0) ? "Moonlit Maul" : MOON_MOVES[z].name;
        return "Moonlit Maul";
    }
    if (has("Revenant"))    return taunted ? plain : "Parry";
    if (has("Lich"))        return taunted ? plain : lichAddAlive ? "" : "Raise Undead";
    return fallbackMoveName(roll);
}

// The turn an enemy with no move of its own takes when its signature roll
// comes up (the fallback at the end of enemyTurn()).
std::string Game::fallbackMoveName(int roll) const {
    switch (enemy.getType()) {
        case EnemyType::MELEE:  return roll < 70 ? "Attack" : "Brace";
        case EnemyType::RANGED: {
            const int r = enemyWeakens() ? roll : roll * 80 / 100;
            return r < 60 ? "Shot" : r < 80 ? "Brace" : "Crippling shot";
        }
        case EnemyType::TANK:   return roll < 65 ? "Brace" : "Attack";
        case EnemyType::CASTER:
            if (enemy.getHealth() < enemy.getMaxHealth() / 3 && roll < 60) return "Heal";
            return roll < 40 ? "Poison bolt" : roll < 60 ? "Fireball" : "Attack";
        case EnemyType::BEAST:  return roll < 60 ? "Attack" : roll < 85 ? "Venomous bite" : "Brace";
        case EnemyType::UNDEAD: return (roll < 75 || !enemyWeakens()) ? "Attack" : "Chilling touch";
        default:                return "Attack";
    }
}

std::string Game::lensIntent() const {
    if (lensRoll < 0 || enemy.isBoss() || !enemy.isAlive()) return "";
    // A taunt forces the roll into the attack bucket and skips the signature
    // gate (enemyTurn()), so a taunted turn is always its own move, on that roll.
    const bool taunted = enemyTauntTurns > 0;
    int roll = lensRoll;
    if (taunted) {
        switch (enemy.getType()) {
            case EnemyType::TANK: case EnemyType::CASTER: roll = 99; break;
            default:                                      roll = 0;  break;
        }
    }
    // The church's rule turns come before any roll.
    if (!taunted && enemyIs("Bellringer") && bellTolls >= 2) return "Great Toll";
    if (enemyIs("Inquisitor") && (taunted || inquisitorMarks >= 3)) return taunted ? "Stock" : "Sentence";
    // The Moonstruck has no kit, so every one of its turns is its own.
    if (taunted || enemy.getName().find("Moon Shade") != std::string::npos || lensSigRoll < ownMoveChance()) {
        const std::string own = ownMoveName(roll, taunted);
        if (!own.empty()) return own;   // empty: it cannot use it now, and the kit takes the turn
    }
    const int r = kitRoll(roll);
    const bool canBuff = enemy.getBonusAttack() < 6;
    switch (enemy.getType()) {
        case EnemyType::MELEE:  return r < 60 ? "Swing" : (r < 85 && canBuff) ? "Wind up" : "Heavy swing";
        case EnemyType::TANK:   return r < 45 ? "Brace" : r < 85 ? "Heavy blow" : "Shove";
        case EnemyType::RANGED:
            if (enemyIsFlyer()) return r < 60 ? "Dive" : r < 85 ? "Wingbeat" : "Double rake";
            return r < 60 ? "Shot" : r < 85 ? "Weakening shot" : "Double shot";
        case EnemyType::CASTER:
            if (r < 45) return "Force bolt";
            if (r < 75 && enemy.getName().find("Wizard") == std::string::npos) return "Hex";
            return enemy.getHealth() < enemy.getMaxHealth() / 2 ? "Mend" : "Brace";
        case EnemyType::BEAST:  return r < 55 ? "Lunge" : (r < 85 && canBuff) ? "Frenzy" : "Brace";
        case EnemyType::UNDEAD: return r < 50 ? "Claw" : r < 80 ? "Grave rot" : "Knit";
        default:                return "";
    }
}

// A relic, one of three, every 12 encounters from the 6th. One may be cursed:
// it wears the same gold outline as the cards that cost you something.
void Game::offerRelic() {
    std::vector<int> pool;
    for (int i = 0; i < Relic::COUNT; i++) {
        if (hasRelic(i)) continue;
        // The quick road has no vigils to put out and nothing following you.
        if (onQuick() && (i == Relic::SEAL_FRAGMENT || i == Relic::MOON_LOCKET)) continue;
        pool.push_back(i);
    }
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
        earn(Achievements::RELIC);
        if (r.cursed) earn(Achievements::CURSED_RELIC);
        int held = 0;
        for (int i = 0; i < Relic::COUNT; i++) held += hasRelic(i) ? 1 : 0;
        if (held >= 5) earn(Achievements::COLLECTOR);
        Audio::playSFXPitched("upgrade", 0.8f);   // rewards share one sound: a relic is the deepest
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
    // The False Moon has your speed: one energy fewer, every turn it holds it.
    if (moonTook(4)) playerEnergy = std::max(0, playerEnergy - 1);
    if (energyDebt > 0) {
        std::cout << "  " << Color::DIM << "Last turn's rush leaves you " << energyDebt
                  << " energy short." << Color::RESET << "\n";
        energyDebt = 0;
    }
}

void Game::applyCardEffect(const Card& card) {
    int val = card.getValue();
    switch (card.getEffect()) {
        // What goes on is what its relic makes of the card (dotPower()).
        case CardEffect::POISON: {
            const int p = dotPower(StatusType::POISON, val);
            EnemyArt::printBattleCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::POISON);
            if (applyEnemyStatus(StatusType::POISON, val)) {
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::POISON, true);
                std::cout << "  " << Color::POISON_CLR << "Applied " << p << " Poison to the enemy! ("
                          << (p + 1) / 2 << " dmg/turn for 6 turns)" << Color::RESET << "\n";
            }
            break;
        }
        case CardEffect::BURN: {
            const int p = dotPower(StatusType::BURN, val);
            EnemyArt::printBattleCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::BURN);
            if (applyEnemyStatus(StatusType::BURN, val)) {
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::BURN, true);
                std::cout << "  " << Color::BURN_CLR << "Applied " << p << " Burn to the enemy! ("
                          << p + p / 2 << " dmg/turn for 2 turns)" << Color::RESET << "\n";
            }
            break;
        }
        case CardEffect::REND: {
            const int p = dotPower(StatusType::REND, val);
            EnemyArt::printBattleCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::REND);
            if (applyEnemyStatus(StatusType::REND, val)) {
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::REND, true);
                std::cout << "  " << Color::REND_CLR << "Applied " << p << " Rend to the enemy! ("
                          << p << " damage the next 3 times it attacks)" << Color::RESET << "\n";
            }
            break;
        }
        case CardEffect::STUN:
            EnemyArt::printBattleCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::STUN);
            if (tryStunEnemy()) {
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::STUN, true);
                std::cout << "  " << Color::STUN_CLR << "The enemy is STUNNED and will lose its next turn!" << Color::RESET << "\n";
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
                std::cout << "  " << Color::WEAK_CLR << "The enemy is Weakened for 3 turns! (its attacks deal "
                          << (int)((1.0 - 1.0 / weakMult) * 100 + 0.5) << "% less damage)" << Color::RESET << "\n";
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
            unchosenCopies(0, playerHealth - before, 0, 0);
            break;
        }
        case CardEffect::STRENGTH: {
            // This path is for pure-buff SPECIAL cards like Strengthen; Bloodlust
            // takes the ATTACK path below. Both read the same ladder.
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::STRENGTH);
            double buff = card.strengthMultiplier();
            playerStatus.apply(StatusType::STRENGTH, 2, 1.5, buff);
            std::cout << "  " << Color::STRENGTH_CLR << "Strength surges! x" << buff << " damage for 2 turns!" << Color::RESET << "\n";
            unchosenCopies(0, 0, 2, buff);
            break;
        }
        case CardEffect::BLOODPRICE:
            playerEnergy += val;
            playerHealth = std::max(0, playerHealth - 6);
            if (playerHealth <= 0) earn(Achievements::BUYERS_REMORSE);
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
            unchosenCopies(0, 0, 2, 1.5);
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
            unchosenCopies(0, 0, 3, 2.0);
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
        case CardEffect::RAISE: {
            const RaisedDead* d = raisedDead(lastUndead);
            if (!d) {   // playCardFromHand() stops this before any energy is spent
                std::cout << "  " << Color::DIM << "Nothing you have killed will rise." << Color::RESET << "\n";
                break;
            }
            // Paid first, every time, out of your own health. Like Blood Price it
            // can take the last of it, and then nothing gets up.
            playerHealth = std::max(0, playerHealth - RAISE_PRICE);
            std::cout << "  " << Color::DAMAGE << "Raising it takes " << RAISE_PRICE << " of your HP."
                      << Color::RESET << " (" << playerHealth << "/" << maxPlayerHealth << ")\n";
            if (playerHealth <= 0) { earn(Achievements::GRAVE_MISTAKE); break; }
            // Raised again while it stands, it is put back together whole.
            const bool again = raisedAlive && raisedName == d->name;
            raisedName  = d->name;
            raisedMaxHp = raisedBody(*d, maxPlayerHealth);
            raisedHp    = raisedMaxHp;
            raisedValue = again ? std::max(raisedValue, val) : val;
            raisedAlive = true;
            EnemyArt::setAlly(raisedName);
            EnemyArt::printAllyRise(enemy.getType(), enemy.getBossType());
            std::cout << "  " << Color::BOLD << Color::CYAN
                      << (again ? "Your " + raisedName + " pulls itself back together."
                                : "Your " + raisedName + " rises to fight beside you!")
                      << Color::RESET << " (" << raisedName << " HP: " << raisedHp << "/" << raisedMaxHp
                      << ", strikes for " << raisedStrikeFor(raisedName, raisedValue) << ")\n";
            break;
        }
        case CardEffect::UNLEASH: {
            const DamageType el = card.getElemType();
            const bool rend = el == DamageType::WIND;
            const StatusType st = el == DamageType::FIRE ? StatusType::BURN
                                : rend ? StatusType::REND : StatusType::POISON;
            const char* word = el == DamageType::FIRE ? "Burn" : rend ? "Rend" : "Poison";
            const char* ink  = el == DamageType::FIRE ? Color::BURN_CLR : rend ? Color::REND_CLR : Color::POISON_CLR;
            const EnemyArt::CastGlow glow = el == DamageType::FIRE ? EnemyArt::CastGlow::BURN
                                          : rend ? EnemyArt::CastGlow::REND : EnemyArt::CastGlow::POISON;
            const int total = unleashTotal(card);
            if (total <= 0) {
                std::cout << "  " << Color::DIM << "There is no " << word << " on it to set off."
                          << Color::RESET << "\n";
                break;
            }
            EnemyArt::printBattleCast(enemy.getType(), enemy.getBossType(), glow);
            // Every charge of Rend is a tear, and each can take a Hydra head.
            const int tears = rend ? enemy.statusRemaining(StatusType::REND) : 0;
            enemy.clearStatus(st);
            const int before = enemy.getHealth();
            // Rend tears through armour, as its charges always have; the other two never touch it.
            enemy.takeDamageOverTime(total, rend);
            if (!enemy.isAlive() && el == DamageType::FIRE) fireKill = true;
            const int lost = before - enemy.getHealth();
            EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), glow, true);
            EnemyArt::popNumber(lost, true, EnemyArt::PopKind::DAMAGE);
            std::cout << "  " << ink << "Every " << (rend ? "charge" : "tick") << " of its " << word
                      << (rend ? " tears" : " lands") << " at once: " << lost << " damage!" << Color::RESET
                      << " (Enemy HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
            if (st == StatusType::BURN && enemy.isAlive()) searStumps("burn");
            for (int i = 0; i < tears && enemy.isAlive(); ++i) rendCutsHead();
            // Its price: the same ailment on you, past any ward, as your own
            // drawbacks are. Nothing to pay once the fight is already won.
            if (enemy.isAlive()) {
                playerStatus.apply(st, UNLEASH_PRICE);
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), glow, false);
                std::cout << "  " << ink
                          << (rend ? "It tears at you as well." : el == DamageType::FIRE ? "The flare catches you too."
                                                                                       : "You breathe some of it in.")
                          << " " << word << " " << UNLEASH_PRICE << "." << Color::RESET << "\n";
            }
            if (!enemy.isAlive()) {
                if (!rend) earn(Achievements::SLOW_BURN);
                Audio::playSFX(deathSfx(enemy.isBoss()));
            }
            refreshBattleAuras();
            break;
        }
        case CardEffect::TURNABOUT:
            EnemyArt::printBattleCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::WEAK);
            enemy.reverseTypes();
            std::cout << "  " << Color::CYAN << "You turn its guard inside out. For the rest of the fight it is weak to "
                      << typeWord(enemy.getWeakness()) << " and resists " << typeWord(enemy.getResistance()) << "."
                      << Color::RESET << "\n";
            break;
        case CardEffect::FEINT: {
            const DamageType from = enemyAttackType(), to = feintType();
            enemy.setBlowType(to);
            std::cout << "  " << Color::CYAN << "You draw it into a different kind of attack: its "
                      << typeWord(from) << " blows come as " << typeWord(to)
                      << " now, which your armor resists (-25%)." << Color::RESET << "\n";
            break;
        }
        // Taunt and Fear are one card pointed in opposite directions, and both
        // are a read rather than a guarantee: one time in five it does not land.
        case CardEffect::TAUNT:
            if (provokeFizzles(luckBonus())) {
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
            if (provokeFizzles(luckBonus())) {
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
bool Game::applyPlayerStatus(StatusType type, int amount, double weakMultiplier) {
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
        return false;
    }
    if (statusWardTurns > 0) {
        // Holds for its whole duration rather than popping on the first
        // ailment, so it answers a caster that throws two in a turn.
        std::cout << "  " << Color::CYAN << "Status Guard blocks the ailment!" << Color::RESET
                  << " " << Color::DIM << "(" << statusWardTurns
                  << (statusWardTurns == 1 ? " turn" : " turns") << " left)" << Color::RESET << "\n";
        return false;
    }
    playerStatus.apply(type, amount, weakMultiplier);
    if (glowFor(type, glow))
        EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), glow, false);
    return true;
}

// The relics that deepen what you inflict: what goes on is half again as
// strong, rounded up so a small one still grows. Every poison, burn and rend
// you put on the enemy comes through applyEnemyStatus(), so all of it does.
int Game::dotPower(StatusType type, int amount) const {
    const bool deeper = (type == StatusType::POISON && hasRelic(Relic::VENOM_VIAL))
                     || (type == StatusType::BURN   && hasRelic(Relic::EMBER_HEART))
                     || (type == StatusType::REND   && hasRelic(Relic::GALE_FEATHER));
    return deeper ? (amount * 3 + 1) / 2 : amount;
}

// Same idea in reverse; false means warded (caller skips its own "applied" message).
bool Game::applyEnemyStatus(StatusType type, int amount, double weakMultiplier) {
    amount = dotPower(type, amount);
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
            noteUndeadKill(lichAddName);
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
    return true;
}

// Fear needs something to work on, so it refuses against an enemy with no
// self-protective move. "Guard" is wider than armor: phasing out, a parry
// stance, a summon and a self-heal all count. test_fear re-derives this
// from enemyTurn().
bool Game::enemyCanDefend() const {
    // A boss can be frightened too: bossAction() gives it a smaller chance to
    // flinch and brace.
    if (enemy.isBoss()) return true;

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

    // Two exceptions. The Knight is MELEE but its signature is a shield bash.
    // The Archer has no move of its own, so its signature turns come from the
    // plain ranged kit, which braces. Both raise a guard, so both can be scared.
    const std::string& n = enemy.getName();
    return n.find("Knight") != std::string::npos || n.find("Archer") != std::string::npos;
}

// Silent when warded: callers print their own resisted message.
bool Game::tryStunEnemy() {
    if (enemyStatusWardActive) {
        enemyStatusWardActive = false;
        return false;
    }
    const bool stunned = enemy.tryApplyStun(luckBonus());   // a boss resists less with Fortune
    if (stunned) earn(Achievements::STUN);
    if (stunned && enemy.getBossType() == BossType::WARLORD) earn(Achievements::STORM_STOPS);
    return stunned;
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
    knight.moonstruck = moonClock > 0;   // the False Moon's red on him while its clock runs
    // Having a Bad Day: three ailments on you at once, the stone curse counted.
    // Every change to them redraws the scene, so this sees each one.
    const int ailments = (int)knight.weak + (int)knight.poison + (int)knight.burn + (int)knight.rend
                       + (int)knight.stun + (curseTurnsLeft > 0 ? 1 : 0);
    if (ailments >= 3) earn(Achievements::BAD_DAY);
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
        // Raise Undead has nothing to call up until an undead has fallen.
        if (card.getEffect() == CardEffect::RAISE && !raisedDead(lastUndead)) {
            std::cout << Color::DIM << "You have not killed anything that will rise yet." << Color::RESET << "\n";
            return;
        }
        // A payoff with nothing on the enemy to set off would only charge you.
        if (card.getEffect() == CardEffect::UNLEASH && unleashTotal(card) <= 0) {
            std::cout << Color::DIM << "There is nothing on the enemy for that to set off." << Color::RESET << "\n";
            return;
        }
        // Nor a Feint your armor could not use.
        if (card.getEffect() == CardEffect::FEINT && !feintHelps()) {
            std::cout << Color::DIM << typesFace(card) << Color::RESET << "\n";
            return;
        }
        // Grave Dirt does nothing, and cannot be played to do it.
        if (isGraveDirt(card)) {
            std::cout << Color::DIM << "Grave Dirt does nothing. It goes when the fight does." << Color::RESET << "\n";
            return;
        }
        const int paid = effectiveCost(card);
        if (!spendEnergy(paid)) return;
        if (paid < card.getCost()) mythrilSpent = true;

        Card playedCard = playerDeck.playCard(index - 1);

        cardsPlayedThisTurn++;
        // Magician: from encounter 10 on, every card played has to be poison,
        // burn or rend, the cards that set them off included.
        if (currentRun.getCurrentEncounter() >= 10) {
            const CardEffect pe = playedCard.getEffect();
            if (pe != CardEffect::POISON && pe != CardEffect::BURN && pe != CardEffect::REND
                && pe != CardEffect::UNLEASH)
                playedNonDot = true;
        }
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

        std::cout << "Played: [" << playedCard.getName() << "] (Cost: " << paid << ")\n";
        // Legendaries announce themselves. Stance cards are the exception:
        // playing one only arms it, and the cue belongs on the payoff, so
        // those fire it when they actually resolve instead.
        if (playedCard.isLegendary() && playedCard.getEffect() != CardEffect::COUNTER
                                     && playedCard.getEffect() != CardEffect::PARRY)
            Audio::playSFX("legendary");

        // The final boss answers this card with one of its own. A stance it
        // can hold goes first, so a Dodge Reversal or Parry is waiting for the
        // blow you just committed to; anything else answers after yours lands.
        Card answer("", "", CardType::ATTACK, 0, 0);
        const bool answers = drawKnightAnswer(answer);
        const bool answerFirst = answers && knightTakesStance(answer);
        if (answerFirst) {
            playKnightAnswer(answer, true);
            if (playerHealth <= 0 || !enemy.isAlive()) return;
        }

        // The once-a-fight cards leave the deck for the rest of the fight rather
        // than shuffling back in, so Last Stand cannot come round again and
        // stack your wounds onto the armour already there.
        if (onceAFight(playedCard.getEffect())) {
            exhausted.push_back(playedCard);
            playerDeck.removeCardByName(playedCard.getName());
        }

        // The card the Confessor named: its blow becomes the Confessor's healing.
        const bool confessed = playedCard.getType() == CardType::ATTACK && !confessedCard.empty()
                            && playedCard.getName() == confessedCard && enemyIs("Confessor") && enemy.isAlive();
        if (confessed) {
            const int hits = (playedCard.getEffect() == CardEffect::DOUBLE_HIT
                              || playedCard.getEffect() == CardEffect::TRUE_DOUBLE) ? 2 : 1;
            const int heal = hits * std::max(1, (int)(liveValue(playedCard) * playerStatus.getWeakMultiplier()
                                                      * playerStatus.getStrengthMultiplier()));
            const int before = enemy.getHealth();
            enemy.heal(heal);
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::HEAL);
            Audio::playSFX("heal");
            std::cout << "  " << Color::MAGENTA << "The Confessor takes it as a confession, and it heals "
                      << (enemy.getHealth() - before) << "." << Color::RESET << " (Enemy HP: "
                      << hpColor(enemy.getHealth(), enemy.getMaxHealth()) << enemy.getHealth() << "/"
                      << enemy.getMaxHealth() << Color::RESET << ")\n";
        } else if (playedCard.getType() == CardType::ATTACK) {
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
            // The Mirror Nun hands back half of the turn's first attack.
            int mirrorBlow = 0;

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
                    if (lichAddHp <= 0) { lichAddAlive = false; EnemyArt::setCompanion(""); noteUndeadKill(lichAddName);
                        std::cout << "  " << Color::MAGENTA << "The summoned " << lichAddName
                                  << " crumbles to dust!" << Color::RESET << "\n"; }
                } else if (enemyInvulnerable && !trueStrike) {
                    EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), playedCard.getElemType(), false);
                    std::cout << "  " << Color::DIM << (enemyIs("Gargoyle") ? "Your blow rings off the stone. No damage!"
                                                                            : "Your attack passes through the phased form. No damage!")
                              << Color::RESET << "\n";
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
                        earn(Achievements::OWN_BLOW);
                        if (held)
                            std::cout << "  " << Color::BOLD << Color::YELLOW
                                      << savedLine("You stay on your feet at 1 HP.") << Color::RESET << "\n";
                        continue;
                    }
                    int hpBefore = enemy.getHealth();
                    const int armorBefore = enemy.getArmor();
                    // The Glass Templar: a Pierce hit goes round the glass and leaves it
                    // standing; anything else that breaks it sets the shards flying.
                    const bool roundGlass = glassUp && enemyIs("Glass Templar")
                        && (playedCard.getPhysType() == DamageType::PIERCE
                            || playedCard.getPhysType2() == DamageType::PIERCE);
                    if (roundGlass) enemy.takeDamageRaw(damageDealt);
                    else            enemy.takeDamage(damageDealt);
                    const bool glassBroke = glassUp && !roundGlass && armorBefore > 0 && enemy.getArmor() == 0
                                         && enemyIs("Glass Templar");
                    if (glassBroke) glassUp = false;
                    mirrorBlow += damageDealt;
                    if (!enemy.isAlive() && playedCard.getElemType() == DamageType::FIRE) fireKill = true;
                    int hpLost = hpBefore - enemy.getHealth();
                    if (revenantParried) parriedDamage += hpLost;
                    EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), playedCard.getElemType(), hpLost > 0);
                    EnemyArt::popNumber(hpLost > 0 ? hpLost : (damageDealt - hpLost), true,
                                        hpLost <= 0    ? EnemyArt::PopKind::BLOCKED
                                        : hitsWeakness ? EnemyArt::PopKind::WEAK_HIT
                                                       : EnemyArt::PopKind::DAMAGE);
                    int armorBlocked = damageDealt - hpLost;
                    if (hpLost >= 200) earn(Achievements::BIG_HIT);
                    if (hpLost >= 500) earn(Achievements::HUGE_HIT);
                    if (hpLost >= 1000) earn(Achievements::THOUSAND);
                    if (hpLost <= 0 && enemy.isAlive()) earn(Achievements::TICKLE);
                    if (!enemy.isAlive() && damageDealt - armorBefore - hpBefore >= 100)
                        earn(Achievements::OVERKILL);
                    if (!enemy.isAlive() && enemy.isBoss() && playedCard.getBaseName() == "Quick Jab")
                        earn(Achievements::JAB_FINISH);
                    Audio::playSFX(!enemy.isAlive() ? deathSfx(enemy.isBoss()) : "attack");
                    std::cout << "  " << Color::PLAYER_ATTACK << hitLabel << hpLost << " damage to the enemy!"
                              << Color::RESET << " (Enemy HP: "
                              << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                              << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")";
                    printTags();
                    if (armorBlocked > 0)
                        std::cout << " " << Color::ARMOR_CLR << "[" << armorBlocked << " blocked by armor]" << Color::RESET;
                    if (roundGlass) std::cout << " " << Color::CYAN << "[Round the glass]" << Color::RESET;
                    std::cout << "\n";
                    // The killing blow sets nothing flying: the glass goes with it.
                    if (glassBroke && enemy.isAlive())
                        churchBackfire(std::max(1, glassArmor / 2), "The glass bursts, and the shards fly back at you:");

                    if (hpLost > 0) onPlayerHit(playedCard, hpLost);

                    // Under the pact every attack festers, element or not.
                    if (pactOfRuinActive && enemy.isAlive()) {
                        if (applyEnemyStatus(StatusType::BURN, 3))
                            std::cout << "  " << Color::BURN_CLR << "The pact sets the wound alight. Burn "
                                      << dotPower(StatusType::BURN, 3) << "." << Color::RESET << "\n";
                        if (applyEnemyStatus(StatusType::REND, 2))
                            std::cout << "  " << Color::REND_CLR << "And it will not close. Rend "
                                      << dotPower(StatusType::REND, 2) << "." << Color::RESET << "\n";
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
                            // The value applied is 3 (more under its relic), but the tick
                            // is not: poison pays half of it over six turns, burn half again
                            // over two. These mirror StatusEffects::apply() and must move with it.
                            if (elem == DamageType::POISON) {
                                if (applyEnemyStatus(StatusType::POISON, 3)) {
                                    EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::POISON, true);
                                    std::cout << "  " << Color::POISON_CLR << "The venom seeps in! Poisoned for "
                                              << (dotPower(StatusType::POISON, 3) + 1) / 2 << " dmg/turn."
                                              << Color::RESET << "\n";
                                }
                            } else if (elem == DamageType::FIRE) {
                                if (applyEnemyStatus(StatusType::BURN, 3)) {
                                    EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::BURN, true);
                                    std::cout << "  " << Color::BURN_CLR << "The flames catch! Burning for "
                                              << dotPower(StatusType::BURN, 3) + dotPower(StatusType::BURN, 3) / 2
                                              << " dmg/turn." << Color::RESET << "\n";
                                }
                            } else {
                                if (applyEnemyStatus(StatusType::REND, 3)) {
                                    EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::REND, true);
                                    std::cout << "  " << Color::REND_CLR << "The cut runs deep! "
                                              << dotPower(StatusType::REND, 3) << " damage each time it swings."
                                              << Color::RESET << "\n";
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
                // Your own blow, handed back whole. Half its own attack would
                // make the riposte a rounding error against anything you would
                // actually parry, and the stance not worth taking.
                enemyStrikePlayer(std::max(1, parriedDamage), false, enemy.getWeakMultiplier());
            }

            // The Mirror Nun: the first attack each turn comes back off her glass.
            if (openingAttack && mirrorBlow > 0 && enemy.isAlive() && playerHealth > 0 && enemyIs("Mirror Nun"))
                churchBackfire(std::min(mirrorBlow / 2, maxPlayerHealth / 3), "It comes back off her mirror at you:");

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
                        std::cout << "  " << Color::BURN_CLR << "The blade sets them alight! Burn "
                                  << dotPower(StatusType::BURN, 4) << "." << Color::RESET << "\n";
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
                unchosenCopies(0, 0, 2, strengthBuff);
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
            unchosenCopies(bonusArmor, 0, 0, 0);

            if (playedCard.getEffect() == CardEffect::FORTIFY) {
                playerArmorPersistTurns = 3;
                std::cout << "  " << Color::CYAN << "Fortified! This armor won't fade for 3 turns (until broken)." << Color::RESET << "\n";
            }
            if (playedCard.getEffect() == CardEffect::IMPAIR) {
                std::random_device rd;
                std::mt19937 gen(rd());
                // Even odds, and Fortune leans them your way.
                std::uniform_int_distribution<> d100(1, 100);
                bool triggered = d100(gen) <= 50 + luckBonus();
                if (triggered && applyEnemyStatus(StatusType::WEAK, 2)) {
                    EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::WEAK, true);
                    std::cout << "  " << Color::WEAK_CLR << "The impact staggers the enemy! Weakened for 3 turns!" << Color::RESET << "\n";
                } else if (!triggered) {
                    std::cout << "  " << Color::DIM << "(It keeps its footing this time.)" << Color::RESET << "\n";
                }
            }
            if (playedCard.getEffect() == CardEffect::CHIP) {
                if (enemyInvulnerable) {
                    EnemyArt::printBattleShieldBash(enemy.getType(), enemy.getBossType(), false);
                    std::cout << "  " << Color::DIM << (enemyIs("Gargoyle") ? "The shield's edge rings off the stone. No damage."
                                                                            : "The shield's edge passes through the phased form. No damage.")
                              << Color::RESET << "\n";
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
                    if (playerHealth <= 0) earn(Achievements::SCRAP_DEATH);
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
                    unchosenCopies(fromWounds, 0, 0, 0);
                    break;
                }
                default: break;
            }

            if (playedCard.getEffect() == CardEffect::WARD) {
                statusWardTurns = 2;
                std::cout << "  " << Color::CYAN << "Warded for 2 turns! Every ailment the enemy tries to put on you is blocked." << Color::RESET << "\n";
            }
        } else if (playedCard.getType() == CardType::SPECIAL) {
            playCardCue(playedCard);
            applyCardEffect(playedCard);
        }
        if (playerEnergy > 0)
            std::cout << Color::DIM << "  (" << playerEnergy << " energy left)" << Color::RESET << "\n";
        refreshBattleAuras();
        if (answers && !answerFirst) playKnightAnswer(answer, false);
        moonClockWinBack();          // every quarter of the False Moon you take wins a turn back
        moonClockOffering(playedCard);
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
    enemy.takeDamageOverTime(dmg, true);   // through armour, as before
    int lost = before - enemy.getHealth();
    EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(),
                                     EnemyArt::CastGlow::REND, true);
    std::cout << "  " << Color::REND_CLR << "The wound tears open as it swings: "
              << lost << " damage." << Color::RESET << "\n";
    if (enemy.isAlive()) rendCutsHead();
    refreshBattleAuras();
    return enemy.getHealth() <= 0;
}

// The Hydra: a Rend tearing as it swings can take a head off, the way a
// Pierce hit does, and leave the stump open. Half the tears do, so a DoT
// deck has a way to keep the heads down; more heads biting means more tears.
static const int REND_CUT_CHANCE = 50;
bool Game::rendCutsHead() {
    if (enemy.getBossType() != BossType::HYDRA || hydraHeads <= 1) return false;
    static thread_local std::mt19937 gen(std::random_device{}());
    if (std::uniform_int_distribution<>(1, 100)(gen) > REND_CUT_CHANCE + luckBonus()) return false;
    hydraHeads--;
    hydraStumps++;
    Audio::playSFXPitched("attack", 0.7f);   // a head coming off: a heavy chop
    std::cout << "  " << Color::REND_CLR << "The tear takes one of its heads off. It has " << hydraHeads
              << " left." << Color::RESET << "\n";
    if (hydraHeads == 1) earn(Achievements::ONE_HEAD);
    return true;
}

// Raise Undead remembers the last of the dead you put down, by the name it
// rises under: a "Greater" one rises as the same thing.
void Game::noteUndeadKill(const std::string& name) {
    for (const RaisedDead& d : RAISED_DEAD)
        if (name.find(d.name) != std::string::npos) { lastUndead = d.name; return; }
}

// The card in your hand says which of the dead would answer, with what it
// would rise with right now.
std::string Game::raiseFace(const Card& c) const {
    const RaisedDead* d = raisedDead(lastUndead);
    if (!d) return "Raise the last undead you killed. You have not killed one yet.";
    return "Raise your " + lastUndead + ": " + std::to_string(raisedBody(*d, maxPlayerHealth))
         + " HP, striking for " + std::to_string(raisedStrikeFor(lastUndead, c.getValue()))
         + " each turn. It takes their attacks for you. Lose " + std::to_string(RAISE_PRICE) + " HP.";
}

// The raised dead take their turn as yours ends: one blow at the enemy, past
// its defense, or at whatever the Lich has standing in front of it.
void Game::raisedStrikes() {
    if (!raisedAlive || playerHealth <= 0 || !enemy.isAlive()) return;
    const RaisedDead* d = raisedDead(raisedName);
    if (!d) return;
    const int dmg = raisedStrikeFor(raisedName, raisedValue);
    if (lichAddAlive) {
        const int before = lichAddHp;
        lichAddHp = std::max(0, lichAddHp - dmg);
        const int lost = before - lichAddHp;
        EnemyArt::printAllyAttack(enemy.getType(), enemy.getBossType(), lost > 0, /*onCompanion*/true);
        EnemyArt::popNumberAdd(lost, EnemyArt::PopKind::DAMAGE);
        if (lichAddHp <= 0) Audio::playSFX("dead");
        else                Audio::playSFXPitched("attack", 1.15f);   // a lighter blow than yours
        std::cout << "  " << Color::CYAN << "Your " << raisedName << " " << d->verb << " the summoned "
                  << lichAddName << " for " << lost << "." << Color::RESET
                  << " (" << lichAddName << " HP: " << lichAddHp << "/" << lichAddMaxHp << ")\n";
        if (lichAddHp <= 0) {
            lichAddAlive = false;
            EnemyArt::setCompanion("");
            noteUndeadKill(lichAddName);
            std::cout << "  " << Color::MAGENTA << "The summoned " << lichAddName
                      << " crumbles to dust!" << Color::RESET << "\n";
        }
    } else if (enemyInvulnerable) {
        EnemyArt::printAllyAttack(enemy.getType(), enemy.getBossType(), false, false);
        std::cout << "  " << Color::DIM << "Your " << raisedName
                  << (enemyIs("Gargoyle") ? "'s blow rings off the stone." : "'s blow passes through the phased form.")
                  << Color::RESET << "\n";
    } else {
        const int hpBefore = enemy.getHealth();
        enemy.takeDamage(dmg);   // past its defense; armour still soaks it
        const int lost = hpBefore - enemy.getHealth();
        EnemyArt::printAllyAttack(enemy.getType(), enemy.getBossType(), lost > 0, false);
        EnemyArt::popNumber(lost > 0 ? lost : dmg, true,
                            lost > 0 ? EnemyArt::PopKind::DAMAGE : EnemyArt::PopKind::BLOCKED);
        if (!enemy.isAlive()) Audio::playSFX(deathSfx(enemy.isBoss()));
        else                  Audio::playSFXPitched("attack", 1.15f);
        std::cout << "  " << Color::CYAN << "Your " << raisedName << " " << d->verb << " it for " << lost << "."
                  << Color::RESET << " (Enemy HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")";
        if (dmg > lost && enemy.isAlive())
            std::cout << " " << Color::ARMOR_CLR << "[" << (dmg - lost) << " blocked by armor]" << Color::RESET;
        std::cout << "\n";
        if (!enemy.isAlive() && enemy.isBoss()) earn(Achievements::NECROMANCER);
    }
    refreshBattleAuras();
    UIHelper::pause(200);
}

void Game::addStrikes() {
    std::cout << Color::MAGENTA << "The summoned " << lichAddName << " strikes at you!"
              << Color::RESET << "\n";
    UIHelper::pause(150);
    enemyStrikePlayer(lichAddAtk, false, 1.0, /*ranged*/false, /*useFrames*/true,
                      /*projectile*/-1, /*closeIn*/false, /*fromCompanion*/true);
}

// A blow the raised dead take for you, whole: what stands in front is what
// gets hit, and the blow that fells it is spent on it. Your armour and what
// only you are weak to have nothing to do with it.
void Game::raisedTakes(int blow) {
    const int before = raisedHp;
    raisedHp = std::max(0, raisedHp - std::max(0, blow));
    const int took = before - raisedHp;
    EnemyArt::printAllyHit(enemy.getType(), enemy.getBossType());
    EnemyArt::popNumberAlly(took, EnemyArt::PopKind::DAMAGE);
    Audio::playSFXPitched(raisedHp <= 0 ? "dead" : "hit", 1.2f);   // a smaller body than yours
    std::cout << Color::DAMAGE << "Your " << raisedName << " takes the blow for you: " << took << " damage."
              << Color::RESET << " (" << raisedName << " HP: " << raisedHp << "/" << raisedMaxHp << ")\n";
    if (raisedHp <= 0) {
        raisedAlive = false;
        EnemyArt::printAllyFall(enemy.getType(), enemy.getBossType());
        EnemyArt::setAlly("");
        std::cout << "  " << Color::CYAN << "Your " << raisedName << " falls apart." << Color::RESET << "\n";
    }
    UIHelper::pause(200);
}

// The payoff cards: every tick of one ailment at once, x1.5. Burn and Poison
// take the weakness and resistance their ticks would have; Rend's tears go
// through armour, as they always have.
int Game::unleashTotal(const Card& c) const {
    const DamageType el = c.getElemType();
    const bool rend = el == DamageType::WIND;
    const StatusType st = el == DamageType::FIRE ? StatusType::BURN
                        : rend ? StatusType::REND : StatusType::POISON;
    int total = enemy.pendingStatus(st);
    if (total <= 0) return 0;
    if (!rend) {
        if (enemy.getWeakness() == el)   total = total * 3 / 2;
        if (enemy.getResistance() == el) total = total / 2;
    }
    return total * 3 / 2;
}

// What it would take off the enemy, which is what the face and the preview quote.
int Game::unleashDamage(const Card& c) const {
    const int total = unleashTotal(c);
    return c.getElemType() == DamageType::WIND ? std::max(0, total - enemy.getArmor()) : total;
}

// The Hydra's open stumps, seared shut so nothing grows back from them: by a
// Burn ticking on it, or set off all at once. A Fire hit does the same in
// onPlayerHit().
void Game::searStumps(const char* by) {
    if (enemy.getBossType() != BossType::HYDRA || hydraStumps <= 0) return;
    earn(Achievements::SEAR);
    Audio::playSFXPitched("fire", 1.35f);   // a hiss as the stump closes
    std::cout << "  " << Color::BURN_CLR << "The " << by
              << (hydraStumps == 1 ? " sears the stump shut. Nothing will grow back from it."
                                   : " sears the stumps shut. Nothing will grow back from them.")
              << Color::RESET << "\n";
    hydraStumps = 0;
}

void Game::payPactOfRuin() {
    if (!pactOfRuinActive) return;
    playerHealth = std::max(0, playerHealth - 2);
    std::cout << "  " << Color::DAMAGE << "The pact takes its 2 HP." << Color::RESET
              << " (" << playerHealth << "/" << maxPlayerHealth << ")\n";
    if (playerHealth <= 0 && trySecondWind())
        std::cout << "  " << Color::BOLD << Color::YELLOW
                  << savedLine("You refuse to fall, and cling on at 1 HP.") << Color::RESET << "\n";
}

// Everything a drawback card took for the length of one fight comes back here.
void Game::endEncounterEffects() {
    enemyReflectNext = false;
    for (const Card& c : exhausted) playerDeck.addCard(c);
    exhausted.clear();
    clearGraveDirt();
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
    knightMoveDebt = 0; knightChainDepth = 0; knightSacrificeSpent = false; knightLastStandSpent = false;
    knightUnleashSpent = 0;
    playerSkippedTurn = false;
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
    // A turn ended without a card: the final boss swings for it (bossAction()).
    playerSkippedTurn = cardsPlayedThisTurn == 0;
    // Raise Undead: your dead take their turn as yours ends. A blow that
    // finishes the fight ends it here.
    raisedStrikes();
    if (!enemy.isAlive() && !saintRises()) { playerTurnActive = true; return; }
    // It Was Right There: the hand this turn ends on, read now, because the
    // enemy's turn deals the next one before anything checks whether you fell.
    bool heldSacrifice = false;
    for (int i = 0; i < playerDeck.handSize(); ++i)
        if (!playerDeck.isCardUsed(i) && playerDeck.getCardFromHand(i).getEffect() == CardEffect::SACRIFICE)
            heldSacrifice = true;
    // Wide Open: Berserk Stance's exposure, read the same way. It wears off at
    // the start of your next turn, before the check for a fall.
    bool exposedByBerserk = vulnerableTurns > 0;
    cardSoftenPct = 0;        // Heavy Guard only softens the turn it was played

    // Borrowed Time: another turn before the enemy gets one, a whole one. Its
    // stun waits for that turn to end: put on at its start, the check after
    // each card spent it there, and the borrowed turn ended after one card.
    if (extraTurnsPending > 0) {
        extraTurnsPending--;
        cardsPlayedThisTurn = 0; attacksPlayedThisTurn = 0; mythrilSpent = false;
        cardLimitThisTurn = 0;
        resetEnergy();
        playerDeck.resetDeck();
        const int again = std::max(1, BASE_HAND_SIZE + handSizeBonus + upgrades.getDrawBonus()
                                      - (moonTook(6) ? 1 : 0));   // the False Moon has your wits
        for (int i = 0; i < again; ++i) { try { playerDeck.drawCard(); } catch (...) { break; } }
        playerTurnActive = true;
        UIHelper::typeWrite(std::string(Color::BOLD) + Color::CYAN
            + "Time folds back on itself. You move again." + Color::RESET + "\n");
        confessorNames();   // a new hand, so a new confession
        UIHelper::pause(250);
        return;
    }
    // The borrowed turn is over, and now it is paid for: stunned, you lose the
    // turn after the enemy's, so the enemy gets the next one free.
    if (borrowedStunsPending > 0) {
        borrowedStunsPending--;
        playerStatus.apply(StatusType::STUN, 1);
    }

    // Per-turn enemy debuffs/stances on the player expire as the turn they hit ends.
    playerAttackOnly = false;
    enemyInvulnerable = false;
    enemyParryStance = false;
    playerBoundTurn = false;
    EnemyArt::setEnemyGhost(false); // drop the fade before the enemy's own turn redraws
    EnemyArt::setEnemyStone(false); // and the stone: the Gargoyle wakes on its turn
    int curseAtStart = curseTurnsLeft; // only tick down on turns the curse was already active (not the cast turn)

    // Loops instead of running once: if the player's new turn opens stunned, that
    // turn is skipped entirely (no hand shown, straight to another enemy turn) -
    // same as how a stunned enemy loses its turn in enemyTurn().
    bool playerStunned;
    do {
        UIHelper::typeWrite(std::string("\n") + Color::BOLD + "--- Enemy's Turn ---" + Color::RESET + "\n");
        UIHelper::pause(180);
        enemyTurn();
        // The False Moon's clock: a turn nearer, and every two a piece of you.
        moonClockTick();
        // The final boss swings for a turn you let go on this enemy turn only:
        // a turn of yours a stun takes is not one you chose to skip.
        playerSkippedTurn = false;
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
        // Warden's Lantern: every turn starts with some armor, not only the first.
        if (hasRelic(Relic::WARDEN_LANTERN)) playerArmor += lanternArmor();
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
                          << savedLine("You refuse to fall! Clinging to 1 HP, you survive the poison!")
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
                          << savedLine("You refuse to fall! Clinging to 1 HP, you survive the flames!")
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
                                    + upgrades.getDrawBonus() - nextHandPenalty
                                    - (moonTook(6) ? 1 : 0));   // the False Moon has your wits
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

        // The Exhumed Saint gets back up from whatever felled it on its turn.
        if (!enemy.isAlive() && playerHealth > 0) saintRises();
        if (playerHealth <= 0 && heldSacrifice) earn(Achievements::HELD_SACRIFICE);
        if (playerHealth <= 0 && exposedByBerserk) earn(Achievements::WIDE_OPEN);
        exposedByBerserk = false;   // a turn lost to a stun comes after it wore off
        if (playerHealth <= 0 || !enemy.isAlive()) { playerTurnActive = true; return; }

        playerStunned = playerStatus.processStun();
        if (playerStunned) {
            UIHelper::typeWrite(std::string(Color::STUN_CLR) + "You are STUNNED and lose your turn!" + Color::RESET + "\n");
            UIHelper::pause(400);
        }
    } while (playerStunned);

    confessorNames();           // the Confessor names this turn's confession
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
        if (trySecondWind())
            std::cout << "  " << Color::BOLD << Color::YELLOW
                      << savedLine("You refuse to fall, and cling on at 1 HP.") << Color::RESET << "\n";
        UIHelper::pause(200);
    }
    cardsPlayedThisTurn = 0; attacksPlayedThisTurn = 0; mythrilSpent = false;
    playerTurnActive = true;
    // A beat to see what it did, rather than a key, if the prompt is skipped.
    if (optConfirm >= 1) UIHelper::pause(450);
    else UIHelper::waitForKey("  (press any key for your turn)");
    // handleInput() will clear and redraw the full state for the new turn
}

bool Game::checkGameOver() {
    if (playerHealth <= 0) {
        return true;
    }
    if (!enemy.isAlive()) {
        // The Exhumed Saint gets back up once, and the fight goes on.
        if (saintRises()) return false;
        return true;
    }
    return false;
}

void Game::displayGameOver() {
    UIHelper::clearScreen();
    if (playerHealth <= 0) {
        // In the False Moon's fight he does not fall: it takes him for its vessel.
        if (enemy.isBoss() && enemy.getBossType() == BossType::FALSE_MOON)
            EnemyArt::printBattleVessel(enemy.getType(), enemy.getBossType());
        else
            EnemyArt::printBattleKnightDeath(enemy.getType(), enemy.getBossType());
        Audio::playSFX("lose");
        UIHelper::pause(300);
        UIHelper::printGameOverScreen(false, currentRun.getEncountersWon(), runStats.getTotalCardsCollected());
        // Dying to the thing on the peak is not the same as dying on the road,
        // because it is the one enemy that wanted the rest of him.
        const bool toTheMoon = enemy.isBoss() && enemy.getBossType() == BossType::SHADOW_KNIGHT;
        const bool vessel = enemy.isBoss() && enemy.getBossType() == BossType::FALSE_MOON;
        UIHelper::printCenteredWrapped(std::string(Color::DIM) + (vessel
            ? "It comes down into what is left of you and settles in. It has its vessel at last."
            : toTheMoon
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

// 0 plays again, 1 goes back to the main menu, 2 quits. The main menu is
// offered after a save, where the run is not over and Load Save is how to
// get back to it.
int Game::handleGameOverInput(bool saved) {
    // Clear first: the picker draws over the console rather than replacing
    // it, so the run summary printed above would show through the buttons.
    UIHelper::clearScreen();
    // Two columns, matching the Rest site, the Continue/End run screen and the
    // upgrade list this leads into. Bare labels get centred instead, which made
    // the last three screens of a run each look laid out differently.
    std::vector<CardBar::Action> overActs{
        CardBar::Action{ "Play again", "keep your unlocks, carry one card into a fresh run", false },
    };
    if (saved)
        overActs.push_back(CardBar::Action{ "Return to main menu", "load a save or start a run from the title", false });
    overActs.push_back(CardBar::Action{ "Quit", "close the game", false });
    const std::string title = std::string(saved ? "Run saved" : "Run over") + "        "
        + std::to_string(currentRun.getEncountersWon()) + " encounters won        "
        + std::to_string(runStats.getTotalCardsCollected()) + " cards collected";
    const int choice = CardBar::pick(title, {}, overActs, 0);
    if (choice == 0) return 0;
    if (saved && choice == 1) return 1;
    return 2;
}

// Called right before the deck resets to starters on a new run - lets the
// player rescue exactly one card (upgrades and all) from the run that just ended.
bool Game::selectCardToCarryOver(Card& outCard) {
    std::vector<Card> allCards = playerDeck.getAllCardsOrdered();
    if (allCards.empty()) return false;

    std::vector<CardBar::Card> widgets;
    for (const Card& c : allCards) widgets.push_back(toWidget(c, gearedValue(c, c.getValue()), attunementChance(), false, luckBonus()));
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
    // A starter replaces its own copy, so it carries nothing: a lucky charm.
    if (outCard.isStarter()) earn(Achievements::LUCKY_CHARM);
    notice("You'll start your new run with " + outCard.getName() + ".");
    return true;
}

// Centred yes/no, drawn like every other choice in the game.
void Game::notice(const std::string& text) {
    UIHelper::clearScreen();
    UIHelper::padToCenter(3);
    UIHelper::printCenteredWrapped(text, 70);
    // With every prompt skipped, the result still shows, just long enough to read.
    if (optConfirm >= 2) { UIHelper::pause(900); return; }
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
    UIHelper::waitForKey("");
}

bool Game::confirm(const std::string& prompt, bool always) {
    if (!always && optConfirm >= 2) return true;
    std::vector<CardBar::Action> acts{ CardBar::Action{ "Yes", false },
                                       CardBar::Action{ "No",  false } };
    return CardBar::pick(prompt, {}, acts, 0) == 0;
}

void Game::syncHud() {
    Hud::set(hudState());
    Hud::setActive(true);
}

Hud::State Game::hudState() const {
        Hud::State h;
        h.turn = turnNumber;
        h.energy = playerEnergy; h.maxEnergy = maxEnergy;
        // Nothing at all in the true form's phase: it is not encounter
        // fifty-one, and it is not a numbered fight. The counter going is
        // part of what says the road has run out.
        h.encounter = trueFormPhase ? std::string()
            : inSecretEncounter ? std::string("???")
            : currentRun.isBossEncounter() ? std::string("BOSS")
            : "Encounter " + std::to_string(currentRun.shownEncounter());
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
        if (armorReversed) ptags += std::string(" ") + Color::MAGENTA + "[Armor turned]" + Color::RESET;
        // What the False Moon has taken back, while it holds it.
        if (moonClock > 0 && !moonTaken().empty())
            ptags += std::string(" ") + Color::RED + "[Lost: " + moonTaken() + "]" + Color::RESET;
        // The Sexton's Grave Dirt, counted only while he is the fight.
        if (enemyIs("Sexton")) {
            int dirt = 0;
            for (const Card& c : playerDeck.getAllCardsOrdered()) dirt += isGraveDirt(c) ? 1 : 0;
            if (dirt > 0)
                ptags += std::string(" ") + Color::DIM + "[Dirt " + std::to_string(dirt) + "/"
                       + std::to_string(GRAVE_DIRT_MAX) + "]" + Color::RESET;
        }
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
        if (enemyInvulnerable) etags += std::string(" ") + Color::CYAN
                                      + (enemyIs("Gargoyle") ? "[Stone: immune]" : "[Phased: immune]") + Color::RESET;
        if (enemyVulnerableTurns > 0) etags += std::string(" ") + Color::YELLOW + "[Exposed x1.5]" + Color::RESET;
        if (enemyReflectNext) etags += std::string(" ") + Color::MAGENTA + "[Reversal set]" + Color::RESET;
        if (enemyParryStance && enemy.isBoss()) etags += std::string(" ") + Color::MAGENTA + "[Parry stance]" + Color::RESET;
        if (enemy.typesAreReversed()) etags += std::string(" ") + Color::CYAN + "[Reversed]" + Color::RESET;
        if (enemy.blowTypeOverride() != DamageType::NONE)
            etags += std::string(" ") + Color::CYAN + "[Blows: " + typeWord(enemy.blowTypeOverride()) + "]" + Color::RESET;
        if (enemy.getBossType() == BossType::HYDRA) {
            etags += std::string(" ") + Color::MAGENTA + "[" + std::to_string(hydraHeads) + " heads]" + Color::RESET;
            if (hydraStumps > 0)
                etags += std::string(" ") + Color::RED + "[" + std::to_string(hydraStumps)
                       + (hydraStumps == 1 ? " open stump]" : " open stumps]") + Color::RESET;
        }
        // The church's counts: the tolls, the marks, the glass, the rising.
        if (enemyIs("Bellringer"))
            etags += std::string(" ") + Color::MAGENTA + "[Toll " + std::to_string(bellTolls) + "/3]" + Color::RESET;
        if (enemyIs("Inquisitor"))
            etags += std::string(" ") + Color::MAGENTA + "[Marked " + std::to_string(inquisitorMarks) + "/3]" + Color::RESET;
        if (glassUp && enemyIs("Glass Templar"))
            etags += std::string(" ") + Color::CYAN + "[Glass]" + Color::RESET;
        if (enemyIs("Exhumed Saint"))
            etags += std::string(" ") + Color::MAGENTA + (saintRisen ? "[Risen]" : "[Rises once]") + Color::RESET;
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
        else h.notice = churchNotice();

        h.addActive = lichAddAlive;
        h.addName   = lichAddName;
        h.addHp     = lichAddHp;
        h.addMax    = lichAddMaxHp;

        h.allyActive = raisedAlive;
        h.allyName   = raisedName;
        h.allyHp     = raisedHp;
        h.allyMax    = raisedMaxHp;
        h.allyStrike = raisedAlive ? raisedStrikeFor(raisedName, raisedValue) : 0;
        return h;
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
    // A phased enemy fades for the idle scene and its ticks; the Gargoyle's
    // stone goes grey instead.
    EnemyArt::setEnemyGhost(enemyInvulnerable && !enemyIs("Gargoyle"));
    EnemyArt::setEnemyStone(enemyInvulnerable && enemyIs("Gargoyle"));
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
    // The church's rules, while one is in play.
    {
        const std::string c = churchNotice();
        if (!c.empty()) std::cout << Color::BOLD << Color::MAGENTA << c << Color::RESET << "\n";
        if (moonClock > 0 && !moonTaken().empty())
            std::cout << "  " << Color::RED << "It has taken back: " << moonTaken() << "." << Color::RESET << "\n";
    }

    
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
        bool cantAfford = false, restricted = false, noDead = false, nothingToSet = false, feintNoUse = false;
        bool dirt = false;
        if (!used) {
            const Card& ci = playerDeck.getCardFromHand(i);
            cantAfford = effectiveCost(ci) > playerEnergy;
            restricted = playerAttackOnly && ci.getType() != CardType::ATTACK; // Revenant taunt
            // Raise Undead has nothing to call up until an undead has fallen.
            noDead = ci.getEffect() == CardEffect::RAISE && !raisedDead(lastUndead);
            // Nor a payoff, with none of its ailment on the enemy.
            nothingToSet = ci.getEffect() == CardEffect::UNLEASH && unleashTotal(ci) <= 0;
            // Nor a Feint your armor cannot use against these blows.
            feintNoUse = ci.getEffect() == CardEffect::FEINT && !feintHelps();
            // Nor Grave Dirt, which does nothing at all.
            dirt = isGraveDirt(ci);
        }
        bool bound = !used && playerBoundTurn && cardsPlayedThisTurn >= 1; // Fleshmass Bind: one play spent

        std::string optLabel = "Select Card " + std::to_string(i + 1);
        if (used) optLabel += " (used)";
        else if (cantAfford) optLabel += " (no energy)";
        else if (bound) optLabel += " (bound)";
        else if (restricted) optLabel += " (taunted)";
        else if (noDead) optLabel += " (no dead)";
        else if (nothingToSet) optLabel += " (nothing to set off)";
        else if (feintNoUse) optLabel += " (no use)";
        else if (dirt) optLabel += " (dirt)";
        options.push_back(optLabel);
        disabled.push_back(used || cantAfford || restricted || bound || noDead || nothingToSet || feintNoUse || dirt);
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
            w.effect   = c.brief(dispVal, attunementChance(), true, luckBonus());
            if (c.getEffect() == CardEffect::RAISE) w.effect = raiseFace(c);
            if (c.getEffect() == CardEffect::TURNABOUT || c.getEffect() == CardEffect::FEINT)
                w.effect = typesFace(c);
            if (isGraveDirt(c)) w.effect = "Does nothing. It goes when the fight does.";
            // The card the Confessor named: playing it heals the Confessor.
            if (!confessedCard.empty() && c.getName() == confessedCard && enemyIs("Confessor")) {
                w.note = "named: heals the Confessor";
                w.noteColor = SDL_Color{ 236, 104, 92, 255 };
            }
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
            w.disabled = cantAfford || restricted || bound || noDead || nothingToSet || feintNoUse || dirt;
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
            CardBar::showDetail(widgets[ci],
                                c.getDescription() + (c.getEffect() == CardEffect::RAISE
                                                      ? " " + raiseFace(c) : std::string()),
                                c.getTypeString(),
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
        } else if (playerEnergy <= 0 && playerTurnActive && !freeCardPlayable()) {
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
        showEnemyScreen();
    } else if (choice == handCount + 2) {
        showPlayerScreen();
    } else {
        displayActionLog();
    }
}

// Rows the two info screens give their buttons: one row of them, across the
// middle of the band, at the size every other screen's choices are.
static const int INFO_BUTTON_ROWS = 4;

// An info screen gets the window. In the middle of a fight the combat panel,
// the scene band and the hand band left its text about eight rows, so only the
// last lines showed and everything above them read as gone. The text is
// measured first; the portrait takes whatever rows it leaves, and none when
// that is too few to draw it.
void Game::infoScreen(void (Game::*print)() const) {
    Hud::setActive(false);
    CardBar::setEnergy(0, 0);   // no pips by the buttons; the battle menu sets them again
    std::ostringstream text;
    std::streambuf* shown = std::cout.rdbuf(text.rdbuf());
    (this->*print)();
    std::cout.rdbuf(shown);
    int lines = 0;
    const int width = std::max(1, Console::cols());
    std::istringstream in(text.str());
    for (std::string line; std::getline(in, line); )
        lines += std::max(1, (UIHelper::visibleLen(line) + width - 1) / width);
    const int room = Console::totalRows() - lines - INFO_BUTTON_ROWS - 1;
    EnemyArt::setPortraitRows(room >= 8 ? room : 0);
    UIHelper::clearScreen();
    (this->*print)();
}

// View Player: everything the run has given you, and the achievements a
// button away. Back comes here from them rather than straight to the fight.
void Game::showPlayerScreen() {
    const std::vector<CardBar::Action> acts{ CardBar::Action{ "Achievements", false },
                                             CardBar::Action{ "Back", false } };
    while (true) {
        infoScreen(&Game::displayPlayerInfo);
        CardBar::setNextHandRows(INFO_BUTTON_ROWS);
        if (CardBar::select({}, acts) != 0) break;
        Achievements::showScreen();
    }
    EnemyArt::setPortraitRows(-1);
    UIHelper::clearScreen();
    EnemyArt::printBattle(enemy.getType(), enemy.getBossType());
    syncHud();
}

// View Enemy: what it is and what it can do this fight.
void Game::showEnemyScreen() {
    infoScreen(&Game::displayEnemyInfo);
    CardBar::setNextHandRows(INFO_BUTTON_ROWS);
    CardBar::select({}, { CardBar::Action{ "Back", false } });
    EnemyArt::setPortraitRows(-1);
    UIHelper::clearScreen();
    EnemyArt::printBattle(enemy.getType(), enemy.getBossType());
    syncHud();
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
        case 6:
            name  = "False Moon";
            etype = EnemyType::CASTER;
            btype = BossType::FALSE_MOON;
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
    // So is the False Moon, which plays your deck back whole, and is fought
    // with five legendaries in it: 500 and half again your health, and the
    // church's third more attack, so it stands long enough for its clock to
    // matter.
    if (btype == BossType::FALSE_MOON) {
        bossHealth = FALSE_MOON_HP_BASE + maxPlayerHealth * 3 / 2;
        bossAttack = bossAttack * CHURCH_ATK_PCT / 100;
    }
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

void Game::offerBossReward() {
    std::vector<Card> rewards = rewardPool.generateRareRewards(3 + rewardChoiceBonus, maxEnergy,
                                    playerDeck.getAllCardNames(), currentRun.getBossIndex(),
                                    luckBonus());

    // Without this the loop below builds an empty option list and still puts up
    // a "choose one" screen with nothing on it but Skip. The rare pool runs dry
    // before the roster runs out of rare cards, so it is reachable in play.
    if (rewards.empty()) { offerExhaustedReward(); return; }
    showDiscardUpgrades(rewards);

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
    for (const Card& c : rewards) bossWidgets.push_back(toWidget(c, gearedValue(c, c.getValue()), attunementChance(), false, luckBonus()));
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
                showDiscardUpgrades(rewards);
                bossWidgets.clear();
                for (const Card& c : rewards) bossWidgets.push_back(toWidget(c, gearedValue(c, c.getValue()), attunementChance(), false, luckBonus()));
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

        playerDeck.addCard(takeBackDiscard(rewards[choice]));
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
        CardBar::Action{ "Skip", false },
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
          { "The knight passes through a rusted iron gate into a dungeon of wet stone. His "
            "footsteps are the only sound, and the torches barely light the walls.",
            "He knows exactly what is missing. It does not help. The gap behind his ribs sits "
            "there like a held breath.",
            "There is standing water in places, and after the second time he stops looking down "
            "into it. He keeps a tight grip on the wooden sword, though he is not sure he ever "
            "learned to use one." },
          { "The passage opens into a vast chamber, and the ground starts to move.",
            "A stone colossus pulls itself up out of the rubble, older than the dungeon around "
            "it, and plants itself in the only way forward." },
          { "In the rubble, a small point of light lies in the dust. It is a piece of him, "
            "picked up by the first thing that found it.",
            "It goes back into the hollow behind his ribs. His strength, or the start of it. "
            "The armor stops feeling like someone else's." } },
        { // The Dark Dungeon -> Vile Witch
          { "Past the gate the stone turns darker and colder. Someone has carved runes into the "
            "floor, and a green light flickers with nothing to cast it.",
            "Somewhere ahead, voices are whispering. Every time he turns toward them, they "
            "stop." },
          { "The corridor opens on a chamber ringed with shattered cauldrons.",
            "A vile witch waits at its heart, with the patience of something that has already "
            "decided how this ends." },
          { "The witch falls among her broken cauldrons, and the wrongness goes out of the air.",
            "The second piece was on her altar, being studied. She had no idea what she had.",
            "It goes back in quietly, and a fog he had stopped noticing clears from his head." } },
        { // The Wicked Forest -> Thunder Beast
          { "The trees close in overhead, their branches woven so tight that no moonlight "
            "reaches the ground.",
            "The air is charged, and the hair on his neck stands up with every step. Thunder "
            "answers thunder above the canopy, in a storm that never quite arrives." },
          { "The storm breaks at last over a clearing at the heart of the wood.",
            "A thunder beast waits there, with lightning crawling along its back." },
          { "The last thunder rolls away, and the forest breathes out with him.",
            "The third piece lies scorched into the earth where the beast fell. It had been "
            "carrying his speed around in its chest since the moon struck him.",
            "His legs remember. He had forgotten they had forgotten." } },
        { // The Dark Lake -> Hydra
          { "The shore is black glass, under a fog that swallows sound as easily as light.",
            "The lake gives back no stars. Only him, and a half second late.",
            "He pushes a rotting raft out into the mist and does not look over the side." },
          { "Out past the fog, ripples start across the water, and there is no wind to make "
            "them.",
            "The hydra comes up from below, and it lets nothing cross." },
          { "When the last head sinks, the lake goes still. The fourth piece drifts to him "
            "across the water, slowly, as if it had been waiting.",
            "Moving stops being work. His hands know the sword again, and his feet know where "
            "to go without being told." } },
        { // The Mountain -> Undead Dragon
          { "Past the treeline the world is wind and ice. A narrow ledge of slate is the only "
            "path, with the drop always at his side.",
            "Frost creeps over his plate as he climbs. Where the ice is clear he can see "
            "himself in it, holding the sword the way he used to." },
          { "The ledge ends at a cave mouth, colder than the wind outside.",
            "Something waits within: a dragon that died once and never quite left." },
          { "The dragon's frozen breath goes still.",
            "The last piece is in its chest, and it is warm. It is his soul, or the part of it "
            "that was torn loose, and it has been keeping something dead on its feet.",
            "He has all five pieces back, and still no shadow under him. The last of him is at "
            "the top, worn by something else." } },
    };
    // The Peak: plays once, right before the Shadow Knight (encounter 50). No
    // matching outro - handleGameVictory() already covers that beat.
    const Lines PEAK_APPROACH = {
        "Near the summit the cloud closes in, and the snow comes sideways on the wind. He "
        "cannot see the moon through it, but he can feel how close it is.",
        "At the edge of the summit stands a knight in his armor, holding his sword, both "
        "taken from him at the pond. It is the thing from the reflection, wearing the one "
        "shape it ever wanted. It has had the whole climb to practice."
    };
    // When the Shadow Knight falls with the true form earned: it drops his
    // shape and fights to escape the sleep.
    const Lines TRUE_FORM = {
        "Every one of its vigils is out. The long sleep is coming for it, and nothing it "
        "stole can keep it awake now.",
        "It stops pretending to be him. His face and his shadow are no use to it any more, "
        "so it tears free of them and fights to stay awake."
    };
    // The true form beaten with the church open (every shape it wore beaten
    // in some run, every vigil out in this one): it has nowhere to sleep and
    // nothing to hide in, so it runs, and something down the mountain is
    // still keeping it awake.
    const Lines TRUE_FORM_RUNS = {
        "It should go down into the long sleep now. It does not.",
        "With no shape left to hide in, it runs. It goes off the far side of the peak and "
        "down into the valley, toward a ruin where candles are still burning.",
        "Something down there is keeping it awake. He follows it down."
    };
    // The ruined church: on the way in, before the False Moon, and the
    // ending after it. The last line of the ending sits under the banner.
    const Lines CHURCH_ENTER = {
        "The church was built to look up at the moon. Most of the roof is gone now, and "
        "where the wall came down, the moon looks straight in.",
        "Its faithful are still here. Night after night they begged it to choose one of "
        "them. It never did. It chose him.",
        "They know exactly who he is."
    };
    const Lines CHURCH_APPROACH = {
        "The last of the faithful is down. The candles go out one by one along the nave, "
        "until only the moon is left lighting it.",
        "It has nothing left to wear. No knight, no shape, no shadow. Only the moon itself, "
        "come down into the ruin to keep him from finishing it."
    };
    const Lines ENDING_TRUE = {
        "The False Moon comes apart in the nave, and there is no one left in the valley to "
        "pray it back together.",
        "It does not go down into a long sleep this time. It is gone. Morning comes up over "
        "the valley, gray and then gold, through the hole in the wall where it used to hang.",
        "The bell in what is left of the tower rings once. Nobody is pulling the rope.",
        "Whole, and moonstruck no more."
    };
    // "Sit a while" at the rest site. Stage is how many pieces of him are back,
    // which is also where he is: 0 the dungeon, 4 the mountain, 5 the night
    // before the peak, and 6 the ruined church after it. Read in order, once
    // each.
    struct SitPassage { int stage; Lines lines; };
    const SitPassage SIT[] = {
        { 0, { "He gets the fire going on the third try. His hands know what they are for. They "
               "just keep arriving late.",
               "He tries to picture his own face and gets the helmet instead. That is probably "
               "fair. He wore it more.",
               "The wooden sword lies across his knees. He found it in the reeds by the pond, some "
               "child's, left behind from a game of knights. He means to give it back." } },
        { 0, { "Water drips somewhere behind him, steady as a clock. He shifts until the puddle is "
               "at his back.",
               "He remembers how it started, or the edge of it. A red moon over a still pond, and "
               "a moon in the water that was brighter than the one in the sky.",
               "He looked at it too long. That was all it needed from him." } },
        { 0, { "The first morning after the moon struck him, he found out the sun would not have "
               "him.",
               "When the light reached him his legs gave out, and when he looked down there was no "
               "shadow under him. He woke at dusk, where he fell.",
               "It has been nights ever since, and it will stay that way until he is whole. "
               "Everything he has fought, he has fought in the dark." } },

        { 1, { "His arms are his own again. He keeps closing his hand around a stick of firewood "
               "just to feel the grip hold.",
               "The colossus never knew what it had. It just got stronger one day and never asked "
               "why.",
               "He wonders how many things out here are like that, a little too strong and a "
               "little too awake, all because of him." } },
        { 1, { "The green light has got into the fire too. He feeds it until it burns orange "
               "again.",
               "The colossus had a small cold flame in its chest that had nothing to do with him. "
               "It was a vigil, lit there by the moon, and he can feel three more further on, each "
               "one in something worse.",
               "While any of them burns, the moon never has to sleep. Put them all out, and it "
               "will have to." } },
        { 1, { "Not all of him comes back in great pieces. The small things out here carry the "
               "rest: a step, a guard, the way he used to turn his wrist.",
               "Something he cut down tonight had one of his feints in it. It came back the moment "
               "the thing hit the ground, as if it had never left.",
               "He has stopped counting the fights. Each one is something of his to take back." } },

        { 2, { "With his wits back, he can think properly about the thing in the moon, and one "
               "question keeps coming back. Why would something that lives in a reflection want a "
               "body at all?",
               "A reflection only lasts while something is looking at it. Look away and it is "
               "gone. That is all the thing has ever been, something that lasts only as long as it "
               "is watched.",
               "A body would last on its own. That is what it wanted from him, and why it kept his "
               "face." } },
        { 2, { "Some nights he can feel something else out in the dark with the moon's light in "
               "it, the way he feels his own pieces.",
               "He is fairly sure they are the others it struck before him, people and beasts it "
               "tried on and let go. It can still put on their shapes whenever it likes.",
               "He wonders whether any of them are still in there." } },
        { 2, { "Something moves out past the firelight and stops when he looks. Not an animal. An "
               "animal would have run.",
               "It is pacing him. Not following, exactly. Walking alongside, a long way off, the "
               "way you copy someone's walk to learn it.",
               "He does not get up. Let it watch. He is getting himself back faster than it can "
               "learn him." } },

        { 3, { "No dry wood by the lake. He sits in the dark and lets his eyes adjust.",
               "He keeps his back to the water out of habit, but nothing is looking at him from it "
               "any more.",
               "This is where it lived, in the moon on the water. It has gone somewhere else now, "
               "further up, and wherever it went, he can feel its hold getting stronger." } },
        { 3, { "He is fast again. He catches a spark out of the air without thinking, then sits "
               "staring at his hand.",
               "He remembers the night now, most of it. The moon in the pond going dark. Something "
               "climbing out of the water to meet him. The sound of his own armor hitting the "
               "ground with nobody in it.",
               "He is fairly sure he was not afraid. He would like to be sure." } },
        { 3, { "The moon sits lower every night. He does not think that is the season.",
               "Whatever is up there is running out of time, and it knows exactly who is coming "
               "for it.",
               "For the first time since the moon struck him, he is not the one being hunted." } },

        { 4, { "The wind keeps trying to take the fire. He builds a wall of stones around it, and "
               "it holds.",
               "He has almost all of himself back. He expected that to feel like something. It "
               "feels like carrying a full pack instead of an empty one. Heavier, and better.",
               "When the cloud thins he can see the peak. Something up there is standing very "
               "still, in a shape he knows." } },
        { 4, { "He sits down without thinking about how. That came back on the lake, and he keeps "
               "noticing it: all the small things a body does on its own.",
               "The thing at the top has been copying those small things since it struck him, and "
               "never got one of them right. It makes a very good knight, standing still.",
               "The moment it has to move, it has to guess." } },
        { 4, { "Where the ledge bends around the mountain, he can see down into a valley on the "
               "far side, one he has never been to.",
               "There are lights down there, too few and too still for a town. Candles, a great "
               "many of them, all in one place, at an hour when nobody should be awake.",
               "Someone down there is keeping a vigil. He does not like to think who it is for." } },

        { 5, { "The last fire. He knows it the way he knows most things now, without being told.",
               "Before morning he will climb the rest of the way and take back what is left of "
               "him: his shadow, his face, and his name in someone's mouth.",
               "He puts the fire out himself. He wants to be the one who does it." } },

        { 6, { "Someone has kept count of the crimson nights on the wall by the door, one scratch "
               "for each. Most of the scratches are years apart. The newest are one a night, going "
               "back weeks.",
               "The thing in the moon draws its strength from a crimson night. He wonders whether "
               "it has been holding the moon red ever since it struck him, fighting the moon "
               "itself to keep it that way.",
               "Under the open roof he can feel it up there, latched onto the real moon. Next to "
               "the moon itself, it is nothing." } },
        { 6, { "The roof did not fall in. The faithful took it down themselves, a beam at a time, "
               "so that nothing would stand between them and the moon.",
               "When it rained they let the font fill, and knelt around it to pray to the moon in "
               "the water, the way he once stood looking into a pond.",
               "He does not go near it. He knows what lives in a reflection, and how much it likes "
               "to be looked at." } },
        { 6, { "The candles along the walls have never been let go out. Each new one was lit from "
               "the last, by someone sitting up with it through the night.",
               "It is a vigil, like the flames the moon set in the colossus and the others, except "
               "these were lit by people who wanted it to stay awake.",
               "That is what kept it awake after he put out the last of its own. While anyone in "
               "here is still praying to it, it cannot sleep." } },
        { 6, { "Out on the road it was a reflection, a borrowed shape, a suit of his armor. In "
               "here it was a god, with a house of its own and a whole valley looking up at it "
               "every night.",
               "Every prayer said in this place went to it, and nowhere is it stronger. At the "
               "pond it could only break him.",
               "In here, it could take him whole." } },
    };
    const int SIT_COUNT = (int)(sizeof(SIT) / sizeof(SIT[0]));
    // The road's memories, the ones Nothing Forgotten asks for. The church's
    // come after them, a bonus for whoever gets that far.
    int sitRoadCount() {
        int n = 0;
        for (int i = 0; i < SIT_COUNT; i++) n += SIT[i].stage <= 5 ? 1 : 0;
        return n;
    }

    // Plays once, at the start of every new run, before the first fight: one
    // line to each shot of the pond (tools/make_intro_scene.py).
    const Lines INTRO = {
        "Once in a long while the moon comes up crimson. On one of those nights, a knight "
        "stood by a still pond and watched it.",
        "Something lives in the moon's reflection. It has no shape of its own, and it has "
        "always wanted one. It had been watching him for some time.",
        "It had struck others before him, and none of them were worth keeping. This knight "
        "was worthy to be its vessel. It came up out of the water to take over his life, "
        "and it broke off far more of him than it meant to.",
        "Five great pieces tore loose: his strength, his wits, his speed, the sure way his "
        "hands knew a blade, and under all of it, his soul. Every move he had ever learned "
        "went with them in smaller pieces, out into the dark between here and the peak.",
        "There is an old word for someone the moon has been at. " + std::string(Color::BOLD)
        + Color::WHITE + "Moonstruck" + Color::RESET + Color::DIM + ". Nobody ever meant it "
        "like this.",
        "What is left of him stands up anyway and picks up a wooden sword. Every piece of "
        "him is in something else's keeping now, and he can still feel where each one is. "
        "He goes after them."
    };

    // The ending on the peak, a line to each shot (tools/make_ending_scene.py).
    // The last sits under the banner.
    const Lines ENDING = {
        "The false moon is put out. It goes back down into its long sleep, and it takes "
        "nothing of him with it.",
        "The knight stands on the peak wearing all of himself again: his strength, his wits, "
        "his speed, his hands, his soul, and a shadow nothing else is wearing any more.",
        "Beyond the hills the sun comes up, and he is still standing in it. The long night "
        "is over.",
        "Whole, and no longer a shadow of himself."
    };
    // The hard road's: the same morning, at the end of its dream.
    const Lines ENDING_HARD = {
        "It is put out again, on a road that was never meant to be walked twice.",
        "Fifty encounters on a road that hits like a hundred. He went back up knowing "
        "exactly what was waiting, and it still could not stop him. Everything on this "
        "mountain has lost to him twice now.",
        "Beyond the hills the sun comes up, and the dream lets go of him.",
        ""
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
    { "Moon Shade Weaver (Undead)", EnemyType::UNDEAD,
      "The torches gutter red, all at once, as if something breathed on them.",
      "Something it struck long before you is here, a spider and a skeleton at once, "
      "with the legs on wrong.",
      "Bone, then legs, then a shape you almost know, and then nothing at all." },
    { "Moon Shade Beguiler (Caster)", EnemyType::CASTER,
      "The runes in the floor go dark, then bleed red.",
      "Someone in a pointed hat, struck long before you, has been singing your name, "
      "and has nearly got it.",
      "Its song stops halfway through your name. It was closer this time." },
    { "Moon Shade (Beast)", EnemyType::BEAST,
      "The moon comes up wrong. Everything that was making noise stops at once.",
      "It struck a wolf once, and it has been wearing it for some time now. It is "
      "holding the shape.",
      "" },
    { "Moon Shade Gorgon (Beast)", EnemyType::BEAST,
      "The moon on the water turns red before the moon above it does.",
      "It struck a gorgon on this shore once. Whatever is left of it does not blink "
      "any more.",
      "Its stare goes out like a lamp, and the stiffness leaves your joints with it." },
    { "Moon Shade Templar (Tank)", EnemyType::TANK,
      "The cloud turns the color of a wound, and the wind stops dead.",
      "A templar it struck on this mountain long ago is climbing after you, and it "
      "climbs like a man now.",
      "The wind takes what is left of it up toward the peak. It has what it came for." },
};

bool Game::rollSecretEncounter() {
    if (inSecretEncounter) return false;
    if (onQuick()) return false;   // the Moonstruck are not on the quick road
    if (currentRun.inChurch()) return false;   // every shape is beaten by the time you get there
    if (currentRun.isBossEncounter()) return false;
    if (currentRun.getCurrentEncounter() < SECRET_EARLIEST) return false;
    // moonZonesSeen is cleared when a run starts, so every run gets its own
    // chance at all five.
    if (moonZonesSeen & (1 << moonZoneFor(currentRun.getCurrentEncounter()))) return false;
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> d(1, 100);
    // Luck applies here above all: a run that has not found this yet is exactly
    // the run that should get better odds for having invested in Fortune.
    const int chance = SECRET_CHANCE_PERCENT;
    // The Moonlit Locket makes it certain in every area, since finding the
    // Moonstruck is all it is for: one in however many regular fights the
    // area has left, so a miss raises
    // the next fight's odds and the last fight before its boss is a sure thing.
    int odds = chance;
    if (hasRelic(Relic::MOON_LOCKET)) {
        const int left = currentRun.regularFightsLeftInArea();
        odds = std::max(chance, (100 + left - 1) / left);
    }
    return d(gen) <= odds + luckBonus();
}

void Game::beginSecretEncounter() {
    moonZone = moonZoneFor(currentRun.getCurrentEncounter());
    moonZonesSeen |= 1 << moonZone;
    // Every shape met, in any run, kept with the progress.
    if (Achievements::seeForm(moonZone)) earn(Achievements::ALL_FORMS);
    saveProgress();
    moonstruckMet++;
    earn(Achievements::MEET_MOON);
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
    // It starts before startEncounter() could hand back what the last fight's
    // cards took: Sacrifice and Last Stand, the max HP, the pact. Back first.
    endEncounterEffects();
    playerDeck.resetDeck();
    int drawCount = BASE_HAND_SIZE + handSizeBonus + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    // Built off the fight it interrupts rather than a fixed statline, so it
    // stays a step above whatever the zone is currently throwing at you.
    int health  = sealScaled((int)(currentRun.getEnemyHealth()  * 1.6) + 30, SEAL_HP_PCT);
    int attack  = sealScaled((int)(currentRun.getEnemyAttack()  * MOON_ATTACK[moonZone]) + 2, SEAL_ATK_PCT);
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
    raisedAlive = false;      // your dead crumbled when the last fight ended
    EnemyArt::setAlly("");
    armorReversed = false;
    turnNumber = 1;
    cardsPlayedThisTurn = 0; attacksPlayedThisTurn = 0; mythrilSpent = false;
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
    EnemyArt::setBattleBackdrop(currentRun.getRoadPosition());
    // No setEnemyVariant here: the knight is still the knight, and the name
    // table would match it on "Knight" and hand back a roster melee sheet.
    // Bosses draw from their own art, which is already what is on screen.
    EnemyArt::printBattle(EnemyType::UNDEAD, BossType::SHADOW_KNIGHT);

    // The darkest loop for the second phase; without it, the Moonstruck's music.
    if (!Audio::playBGM("bgm_final")) Audio::playBGM("bgm_secret");
    Audio::playSFX("boss");

    // One fight, not two: a Turnabout or a Feint on the knight holds on what stands up.
    const bool turned = enemy.typesAreReversed();
    const DamageType feinted = enemy.blowTypeOverride();
    enemy = Enemy("Shadow Knight", hp, atk, def, EnemyType::UNDEAD);
    enemy.setBossType(BossType::SHADOW_KNIGHT);
    if (turned) enemy.reverseTypes();
    enemy.setBlowType(feinted);
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
    knightMoveDebt = 0; knightChainDepth = 0; knightSacrificeSpent = false; knightLastStandSpent = false;
    knightUnleashSpent = 0;
    // Its moves for this first round. Without them it stood up with the queue
    // the knight had emptied, and answered nothing on its first turn.
    prepareShadowKnightMoves();
    playerBoundTurn = false;
    enemyInvulnerable = false;
    lichAddAlive = false;
    EnemyArt::setCompanion("");
    lastMoveRoll = -1;
    turnNumber = 1;
    cardsPlayedThisTurn = 0; attacksPlayedThisTurn = 0; mythrilSpent = false;
    resetEnergy();
    // A second save for a second phase: the first was spent on the knight.
    bossSecondWindAvailable = true;
    Hud::setActive(true);
    playerTurnActive = true;
    inEncounter = true;
    running = true;
    Console::setHistoryCapture(false);
}

namespace {
// Waits out one line of a cutscene. A key moves on, but while the shot is
// still playing the first one only lets it finish, so reading fast never
// costs the picture. False means Esc: skip the rest.
bool waitCutsceneKey() {
    // Keys pressed while the line typed out are spent, except Esc.
    for (Platform::KeyEvent k = Platform::pollKey(); k.key != Platform::Key::NONE; k = Platform::pollKey())
        if (k.key == Platform::Key::ESCAPE) return false;
    int cx, cy;
    Platform::takeClick(cx, cy);
    while (true) {
        const Platform::KeyEvent k = Platform::pollKey();
        if (k.key == Platform::Key::ESCAPE) return false;
        if (k.key != Platform::Key::NONE || Platform::takeClick(cx, cy)) {
            if (!EnemyArt::cutsceneShotPlaying()) return true;
            EnemyArt::finishCutsceneShot();
        }
        Platform::frame();
    }
}

// One shot of a cutscene with its line under the picture. False when the
// player skips the rest with Esc.
bool cutsceneBeat(EnemyArt::Cutscene scene, int shot, const std::string& line) {
    UIHelper::clearScreen();
    Hud::setActive(false);
    EnemyArt::setCutsceneShot(scene, shot);
    const int below = EnemyArt::cutsceneBottom() - Console::textRegion().y;
    for (int r = 0, pad = below / Platform::cellH() + 2; r < pad; r++) std::cout << "\n";
    if (!line.empty()) {
        UIHelper::printCenteredWrapped(std::string(Color::DIM) + line + Color::RESET, 74, true);
        std::cout << "\n";
    }
    UIHelper::printCentered(std::string(Color::DIM) + "(any key to go on, Esc to skip)" + Color::RESET);
    return waitCutsceneKey();
}
} // namespace

// The night at the pond, a line at a time under the picture. Without its art
// it is the words alone, as it always was.
void Game::showIntro() {
    if (!EnemyArt::cutsceneReady(EnemyArt::Cutscene::INTRO)) { showStoryBeat(INTRO); return; }
    for (size_t i = 0; i < INTRO.size(); ++i)
        if (!cutsceneBeat(EnemyArt::Cutscene::INTRO, (int)i, INTRO[i])) break;
    EnemyArt::setCutsceneShot(EnemyArt::Cutscene::INTRO, -1);
}

// Beating it: one guaranteed Super Rare, then straight on to the fight it
// interrupted. No rest site, no equipment roll - this was never on the map.
void Game::handleSecretWin() {
    inSecretEncounter = false;
    endEncounterEffects();   // its prize is picked against your whole deck
    earn(Achievements::MOONSTRUCK);
    // Each shape beaten is kept across runs for the medals, and in this run
    // for the church: all five beaten in one run, with every vigil out, is
    // what makes the true form run to it.
    Achievements::beatForm(moonZone);
    moonZonesBeaten |= 1 << moonZone;
    saveProgress();
    if (curseTurnsLeft > 0) earn(Achievements::STONE_COLD);   // the Gorgon's gaze still spreading
    Hud::setActive(false);
    EnemyArt::printBattleDeath(enemy.getType(), enemy.getBossType());
    Audio::playSFX("win");
    UIHelper::pause(300);

    UIHelper::clearScreen();
    UIHelper::showHeadline("THE HUNT ENDS", 214, 66, 58);
    for (int i = 0, pad = Console::rows() * 42 / 100; i < pad; i++) std::cout << "\n";
    UIHelper::printCentered(std::string(Color::DIM)
        + "It does not leave a body. Only the move it had been practicing." + Color::RESET);
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
        std::vector<CardBar::Card> w{ toWidget(prize[0], gearedValue(prize[0], prize[0].getValue()), attunementChance(),
                                           false, luckBonus()) };
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
        prize[0] = takeBackDiscard(prize[0]);
        playerDeck.addCard(prize[0]);
        runStats.addCardToRun();
        Audio::playSFX("upgrade");
        checkDeckAchievements();
        notice("Added " + prize[0].getName() + " to your deck.");
    } else {
        notice("You already own every card it could have been carrying.");
    }
    // It sends you straight on into the fight it interrupted, with no rest in
    // between, so a win under half health lifts you to half first.
    const int half = (maxPlayerHealth + 1) / 2;
    if (playerHealth < half) {
        playerHealth = half;
        Audio::playSFX("heal");
        notice("You get your breath back before the next fight. You are back to half your health ("
               + std::to_string(playerHealth) + "/" + std::to_string(maxPlayerHealth) + ").");
    }
    startEncounter();   // the fight it interrupted, still at the same number
}

// Losing it does not end the run. It loses interest and moves off, and the
// fight it interrupted happens anyway - just with nothing gained and whatever
// health you crawled away with.
void Game::handleSecretDefeat() {
    inSecretEncounter = false;
    endEncounterEffects();
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

// The 44 regular enemies, ordered so no two fights in a row share a type,
// each zone escalating and fitting its theme. Quick draws four from each
// area's nine (see buildRandomOrder()). Then the ruined church's nine, which
// only the full road past the peak reaches, always in this order.
struct RosterEntry { EnemyType type; const char* name; };
static const int ROAD_REGULARS = 44;
static const RosterEntry ROSTER[53] = {
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
    // 41-48, The Mountain, before the Dragon + Shadow Knight finale. The
    // Unchosen, one of the mountain's heretics, is the last of them (48).
    // Type run: RANGED MELEE UNDEAD BEAST UNDEAD TANK CASTER MELEE
    {EnemyType::RANGED, "Deadeye"}, {EnemyType::MELEE, "Enforcer"},
    {EnemyType::UNDEAD, "Revenant"},{EnemyType::BEAST, "Manticore"},
    {EnemyType::UNDEAD, "Lich"},    {EnemyType::TANK, "Paladin"},
    {EnemyType::CASTER, "Archon"},  {EnemyType::MELEE, "The Unchosen"},
    // -9 to -1, The Ruined Church, before the False Moon. The Fleshmass, the
    // body the faithful made for the moon, is the last of them (-1).
    // Type run: MELEE CASTER BEAST TANK RANGED UNDEAD CASTER TANK BEAST
    {EnemyType::MELEE, "Sexton"},    {EnemyType::CASTER, "Mirror Nun"},
    {EnemyType::BEAST, "Gargoyle"},  {EnemyType::TANK, "Bellringer"},
    {EnemyType::RANGED, "Inquisitor"}, {EnemyType::UNDEAD, "Exhumed Saint"},
    {EnemyType::CASTER, "Confessor"}, {EnemyType::TANK, "Glass Templar"},
    {EnemyType::BEAST, "Fleshmass"},
};

void Game::startEncounter() {

    if (rollSecretEncounter()) { beginSecretEncounter(); return; }

    // The church's beats play on every road: it is new ground either way.
    if (currentRun.inChurch()) {
        if (currentRun.isBossEncounter()) showStoryBeat(CHURCH_APPROACH);
        else if (currentRun.isAreaStart()) showStoryBeat(CHURCH_ENTER);
    } else if (currentRun.getCycle() == 0) {
        if (currentRun.isBossEncounter()) {
            int bossIdx = currentRun.getBossIndex(); // 0..5
            if (bossIdx == 5) showStoryBeat(PEAK_APPROACH); // right before the Shadow Knight
            // TRUE_FORM is read in beginTrueForm(), when the knight gets back up.
            else if (bossIdx >= 0 && bossIdx < 5) showStoryBeat(ZONE_STORY[bossIdx].approach);
        } else if (currentRun.isAreaStart()) {
            int zone = currentRun.getAreaIndex(); // 0..4, one per zone
            if (zone >= 0 && zone < 5) showStoryBeat(ZONE_STORY[zone].enter);
        }
    }
    UIHelper::clearScreen();
    EnemyArt::setBattleBackdrop(currentRun.getRoadPosition());
    // The False Moon comes out of the eclipse in silence, and its music
    // starts as it takes shape (printBattleArrival).
    const bool arrives = currentRun.isBossEncounter() && currentRun.getBossIndex() == 6
        && EnemyArt::prepareArrival(BossType::FALSE_MOON);
    // BGM tracks the same areas as the backdrop, so the music turns over
    // right after each boss. Falls back to the base track until per-area
    // files (bgm2..bgm5) exist in sounds/.
    // A boss fights to its own area's track made heavier, a little more each
    // area (bgm_boss1 for the Colossus to bgm_boss5 for the Dragon), and the
    // Shadow Knight to the main song lowered; its true form has a track of
    // its own (beginTrueForm). Without the file, the area's music carries on.
    // The church plays the area theme with the made bell, and the False Moon
    // the same song with the church's own bell, pitched down.
    const int boss = currentRun.isBossEncounter() ? currentRun.getBossIndex() : -1;
    if (arrives) Audio::stopBGM();
    const bool bossTrack = boss >= 0 && (arrives ||
           Audio::playBGM(boss == 6 ? std::string("bgm_false_moon")
                          : boss == 5 ? std::string("bgm_knight") : "bgm_boss" + std::to_string(boss + 1)));
    if (!bossTrack) {
        if (!currentRun.inChurch()) Audio::playBGM(currentRun.getAreaIndex());
        else if (!Audio::playBGM(std::string("bgm_church"))) Audio::playBGM(4);
    }
    playerDeck.resetDeck();
    int drawCount = BASE_HAND_SIZE + handSizeBonus + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    trueFormPhase = false;   // whatever rose last wave does not carry over
    hydraHeads = 2;          // and the Hydra starts every fight with two
    hydraStumps = 0;         // and nothing cut yet
    hydraRegrew = false;
    thunderStuns = 0;
    paladinJudgements = 0;   // and the Paladin has not judged anyone yet
    // And the church's nine start every fight from nothing.
    bellTolls = 0; inquisitorMarks = 0; gargoyleStone = false;
    glassArmor = 0; glassUp = false; saintRisen = false; fireKill = false;
    confessedCard.clear();
    moonClock = 0; moonQuarters = 0;   // the False Moon casts it again when it stands up
    moonShadeLast = -1;
    // The 1 HP save belongs to boss fights. Set either way, so a regular fight
    // after a boss it was not spent on does not inherit it.
    bossSecondWindAvailable = currentRun.isBossEncounter();
    if (currentRun.isBossEncounter()) {
        enemy = generateBossEnemy();
    } else {
        int health  = sealScaled(currentRun.getEnemyHealth(), SEAL_HP_PCT);
        int attack  = sealScaled(currentRun.getEnemyAttack(), SEAL_ATK_PCT);
        int defense = currentRun.getEnemyDefense();
        // The church is past the peak, and the knight walks in with every
        // legendary the road gives, five by then. Its fights are built for
        // that deck: twice the road's health, a third more attack and four
        // more defense, before each one's own rule.
        if (currentRun.inChurch()) {
            health  = health * CHURCH_HP_PCT / 100;
            attack  = attack * CHURCH_ATK_PCT / 100;
            defense += CHURCH_DEF_BONUS;
        }

        int r = rosterIndexFor(currentRun.getRegularIndex());
        EnemyType etype = ROSTER[r].type;
        std::string name = ROSTER[r].name;
        int cycle = currentRun.getCycle();
        // "The Greater Unchosen", not "Greater The Unchosen".
        if (cycle == 1) name = name.rfind("The ", 0) == 0 ? "The Greater " + name.substr(4) : "Greater " + name;
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
            + std::to_string(currentRun.shownEncounter()))
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
    // Your raised dead crumble when a fight ends, and your armor comes back
    // the right way round.
    raisedAlive = false;
    EnemyArt::setAlly("");
    armorReversed = false;
    fleshmassBindPending = false;
    playerBoundTurn = false;
    cardsPlayedThisTurn = 0; attacksPlayedThisTurn = 0; mythrilSpent = false;
    endEncounterEffects();      // no drawback carries into the next fight
    armPerTurnEnemyMechanics(); // arm the Assassin ambush if this fight is the Assassin

    inEncounter = true;
    turnNumber = 1;
    playerTurnActive = true;
    playerArmor = 0;
    // and Fortify's hold with it: one played on a fight's last turn kept
    // counting into the next, holding that fight's first armor for turns.
    playerArmorPersistTurns = 0;
    playerEnergy = maxEnergy;
    playerStatus.reset();
    // Don't let an armed-but-unconsumed Status Guard carry into a new fight.
    statusWardTurns = 0;
    enemyStatusWardActive = false;
    applyFightStartRelics();
    churchFightStart();         // the Gargoyle on its perch, the Templar in its glass

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

    // The False Moon casts Moonstruck before your first turn, once it is up.
    if (enemy.getBossType() == BossType::FALSE_MOON) {
        if (arrives) EnemyArt::printBattleArrival(enemy.getType(), enemy.getBossType(), "bgm_false_moon");
        Console::setHistoryCapture(true);
        moonstruckCast();
        Console::setHistoryCapture(false);
    }
    // The Confessor reads its first sin out once it is on screen.
    if (enemyIs("Confessor")) {
        Console::setHistoryCapture(true);
        confessorNames();
        Console::setHistoryCapture(false);
    }

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
    if (currentRun.isBossEncounter() && bi >= 0 && bi < 4 && !onQuick()) {
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
            CardBar::Card w = toWidget(c, nowVal, attunementChance(), maxed, luckBonus());
            // On the forge the useful number is what it becomes, next to what
            // it is now: that is the decision being made here.
            if (!maxed) {
                const Card next = upgradedCopy(c);
                w.effect = upgradeFaceLine(w.effect, next.brief(gearedValue(next, next.getValue()),
                                                                attunementChance(), false, luckBonus()));
            }
            // Only a card's first upgrade takes 1 off its cost, and never below
            // its floor: past that the badge says so, rather than a new player
            // forging again and waiting for a drop that never comes.
            w.costMax = maxed || upgradedCost(c) >= c.getCost();
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
        acts.push_back(CardBar::Action{ "Back", false });

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
                    else
                        text += " (the cost stays at " + std::to_string(c.getCost())
                              + (c.getCost() <= c.minCost() ? ": this card will not go lower)"
                                                            : ": only a card's first upgrade lowers it)");
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
            earn(Achievements::FORGE);
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
        const int stage = currentRun.inChurch() ? 6   // the church's own
            : std::max(0, std::min(5, currentRun.areaBossesCleared() + (currentRun.isBossEncounter() ? 1 : 0)));
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
        earn(Achievements::SIT);
        if (SIT[sitNext].stage == 5) earn(Achievements::LAST_FIRE);   // the night before the peak
        // Heard in any run counts, so they are kept with the achievements.
        const bool heardAll = Achievements::hearSit(sitNext, sitRoadCount());
        saveProgress();
        if (heardAll) earn(Achievements::EVERY_SIT);
        showSitBeat(SIT[sitNext].lines, wornArmor);
        continue;
    } else if (siteChoice == 0) {
        const std::string confirmPrompt = "Rest and heal to full?  (" + std::to_string(playerHealth)
                                        + "/" + std::to_string(maxPlayerHealth) + " HP)";
        if (!confirm(confirmPrompt)) continue; // declined - back to the rest site menu

        if (playerHealth >= maxPlayerHealth) earn(Achievements::FULL_REST);
        playerHealth = maxPlayerHealth;
        restedThisRun = true;
        Audio::playSFX("heal");
        earn(Achievements::REST);
        notice("You rest and fully recover to " + std::to_string(maxPlayerHealth) + " HP.");
        break; // committed - progress as normal
    } else if (siteChoice == 1) {
        if (forgeMenu("Forge   pick a card to upgrade")) {    // committed - progress as normal
            if (playerHealth * 10 <= maxPlayerHealth) earn(Achievements::WALK_IT_OFF);
            break;
        }
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
        if (playerHealth * 10 <= maxPlayerHealth) earn(Achievements::WALK_IT_OFF);

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
        std::vector<CardBar::Action> back{ CardBar::Action{ "Back", false } };
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
        // on the same ladder, not a cupboard you always have access to.
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
            CardBar::Card w = toWidget(c, gearedValue(c, c.getValue()), attunementChance(), false, luckBonus());
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
        acts.push_back(CardBar::Action{ "Back", false });

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

        const Card gone = *groupCard[first + choice];   // before the deck lets go of it
        if (playerDeck.removeCardByName(name)) {
            rememberDiscard(gone);
            discardedThisRun = true;
            earn(Achievements::TRAVEL_LIGHT);
            if (playerDeck.totalCards() == 1) earn(Achievements::DOWN_TO_ONE);
            notice("Discarded one " + name + " from your deck.");
            // stay here: browsing and discarding never cost the rest site visit
        }
    }
}

// A card thrown out at a rest site with forge work on it is remembered, so
// when the same card turns up as a reward it comes back as it was, not fresh.
void Game::rememberDiscard(const Card& c) {
    if (c.getUpgradeCount() > 0) discardedCards.push_back(c);
}

// An offer shows each card the way you left it: the most worked copy of it you
// threw out, each copy on one card only.
void Game::showDiscardUpgrades(std::vector<Card>& offer) const {
    std::vector<bool> used(discardedCards.size(), false);
    for (Card& o : offer) {
        int best = -1;
        for (size_t i = 0; i < discardedCards.size(); ++i)
            if (!used[i] && discardedCards[i].getBaseName() == o.getBaseName()
                && (best < 0 || discardedCards[i].getUpgradeCount() > discardedCards[best].getUpgradeCount()))
                best = (int)i;
        if (best >= 0 && discardedCards[best].getUpgradeCount() > o.getUpgradeCount()) {
            o = discardedCards[best];
            used[best] = true;
        }
    }
}

// Taken back: the remembered copy, forgotten so it comes back once. A card
// handed out with no choice (a legendary) comes back as it was too.
Card Game::takeBackDiscard(const Card& c) {
    int best = -1;
    for (size_t i = 0; i < discardedCards.size(); ++i)
        if (discardedCards[i].getBaseName() == c.getBaseName()
            && (best < 0 || discardedCards[i].getUpgradeCount() > discardedCards[best].getUpgradeCount()))
            best = (int)i;
    if (best < 0 || discardedCards[best].getUpgradeCount() < c.getUpgradeCount()) return c;
    const Card back = discardedCards[best];
    discardedCards.erase(discardedCards.begin() + best);
    return back;
}

void Game::handleEncounterWin() {
    Audio::stopLoop();   // the heartbeat belongs to the fight
    clearGraveDirt();    // and the Sexton's earth to his
    // Raise Undead calls up the last of the dead you put down. The Undead
    // Dragon fights as a RANGED boss, so it is counted by what it is.
    if (enemy.getType() == EnemyType::UNDEAD || enemy.getBossType() == BossType::DRAGON)
        noteUndeadKill(enemy.getName());
    // Poison, burn and rend took every point: nothing you swung, turned back
    // or chipped ever landed.
    if (enemy.hitDamageTaken() == 0) earn(Achievements::DOT_ONLY);
    if (inSecretEncounter) { handleSecretWin(); return; }
    // The knight goes down and the moon stands up in it. Checked before the
    // encounter is counted as won, because it has not been.
    if (enemy.getBossType() == BossType::SHADOW_KNIGHT && !trueFormPhase && trueFormEarned()) {
        beginTrueForm();
        return;
    }
    // The fight is over, so what it took for its length comes back now: the
    // cards it set aside, the max HP it borrowed. Handed back here, they are
    // there for the rewards, the forge, the rest site and any save made
    // before the next fight.
    endEncounterEffects();
    Hud::setActive(false);   // the fight is over: no stale HP bars on the rewards
    currentRun.winEncounter();
    // With the church open the true form does not die at the peak: it runs,
    // so it gets no cracked death, only its escape.
    if (enemy.getBossType() == BossType::SHADOW_KNIGHT && trueFormPhase && churchEarned())
        EnemyArt::printBattleFlee(enemy.getType(), enemy.getBossType());
    else
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

    earn(Achievements::WIN_FIGHT);
    if (turnNumber == 1) earn(Achievements::FIRST_TURN);
    if (playerHealth * 10 <= maxPlayerHealth) earn(Achievements::BY_A_HAIR);
    if (curseTurnsLeft > 0) earn(Achievements::STONE_COLD);
    if (enemy.isBoss() && wornWeapon == 0 && wornArmor == 0 && (weaponTier > 0 || armorTier > 0))
        earn(Achievements::SENTIMENTAL);
    switch (enemy.getBossType()) {
        case BossType::STONE_COLOSSUS: earn(Achievements::COLOSSUS); break;
        case BossType::VILE_WITCH:     earn(Achievements::WITCH);    break;
        case BossType::WARLORD:        earn(Achievements::THUNDER);  break;
        case BossType::HYDRA:
            earn(Achievements::HYDRA);
            if (!hydraRegrew) earn(Achievements::CLEAN_CUTS);
            break;
        case BossType::DRAGON:         earn(Achievements::DRAGON);   break;
        case BossType::FALSE_MOON:
            // Half of the crimson one; the other half is every other achievement.
            Achievements::setFalseMoonBeaten(true);
            if (Achievements::crimsonDue()) Achievements::earn(Achievements::FALSE_MOON);
            saveProgress();
            break;
        case BossType::SHADOW_KNIGHT: {
            // The full road's clears are kept off the quick road by earn(), and
            // it has its own.
            earn(Achievements::CLEAR);
            if (onQuick()) earn(Achievements::SHORT_WAY);
            if (trueFormPhase) earn(Achievements::TRUE_FORM);
            if (!restedThisRun)    earn(Achievements::NO_REST);
            if (!discardedThisRun) earn(Achievements::NO_DISCARD);
            if (!playedNonDot)     earn(Achievements::MAGICIAN);
            // A card set aside this fight (Sacrifice, Last Stand) still counts.
            if (playerDeck.totalCards() + (int)exhausted.size() == 1) earn(Achievements::ONE_TRICK);
            // Nothing taken that lasts the run: no boon, and no relic.
            if (runLuck == 0 && handSizeBonus == 0 && rewardChoiceBonus == 0 && attunementBoons == 0
                && gearInterval == baseGearInterval()) earn(Achievements::NO_BOONS);
            if (relicsOwned == 0) earn(Achievements::NO_RELICS);
            // Checked before the Knight's own legendary is handed over.
            bool legend = false;
            for (const Card& c : playerDeck.getAllCardsOrdered()) legend = legend || c.isLegendary();
            if (!legend) earn(Achievements::NO_LEGENDS);
            for (int u = 0; u < 5; u++)
                if (upgrades.isActive(u)) { earn(Achievements::HEAD_START); break; }
            break;
        }
        default: break;
    }

    if (enemy.isBoss()) {
        int bossOccurrence = currentRun.getBossNumber(); // 1-indexed, including this one

        const int BOSS_HP_BOOST = 50; // permanent, every boss kill
        maxPlayerHealth += BOSS_HP_BOOST;
        playerHealth += BOSS_HP_BOOST;
        Audio::playSFX("heal");
        std::cout << "\n";
        UIHelper::printCentered(std::string(Color::HEAL) + "Victory strengthens you. Max HP increased by "
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
        if (enemy.getBossType() == BossType::FALSE_MOON) {
            UIHelper::showHeadline("", 0, 0, 0);
            handleTrueVictory();
            return;
        }
        if (enemy.getBossType() == BossType::SHADOW_KNIGHT) {
            UIHelper::showHeadline("", 0, 0, 0);
            // With the church open, the true form runs and the road goes on:
            // the rewards below are the peak's, as for every area's boss.
            if (!(trueFormPhase && churchEarned())) {
                handleGameVictory();
                return;
            }
            trueFormRuns();
        }

        // Announce the regular-reward rarity gate lifting. Legendary is never
        // mentioned here: it stays a silent boss-reward-only rarity. Waits for a
        // keypress so the next screen cannot blow past it.
        if (bossOccurrence == 1) {
            std::cout << "\n";
            UIHelper::printCentered(std::string(Color::BOLD) + Color::YELLOW
                                    + "Rare cards can now turn up in your rewards" + Color::RESET);
            UIHelper::waitForKey();
        } else if (bossOccurrence == 2) {
            std::cout << "\n";
            UIHelper::printCentered(std::string(Color::BOLD) + Color::YELLOW
                                    + "Super rare cards can now turn up in your rewards" + Color::RESET);
            UIHelper::waitForKey();
        }

        offerBossReward();
        // Every 2nd boss defeated (occurrence 2, 4, 6...) also grants a shot at +1 max energy.
        if (bossOccurrence % 2 == 0) offerExtraPlay();
        // Each area closes with a seal. The Dragon's would sit one fight before
        // the Shadow Knight's, so the mountain's comes after the peak instead.
        // The quick road has none.
        if (enemy.getBossType() != BossType::DRAGON && enemy.getBossType() != BossType::SHADOW_KNIGHT
            && !onQuick()) offerSeal();
    } else {
        // No keypress here on purpose: 44 regular wins a run, and a prompt on
        // every one of them is 44 keys of friction. The banner just holds.
        UIHelper::pause(700);
        UIHelper::showHeadline("", 0, 0, 0);
        offerCardReward();
    }

    if (currentRun.getCurrentEncounter() % gearInterval == 0)
        offerEquipmentDrop();

    // Every 11th encounter (11, 22, 33, 44), for as long as the run lasts. At
    // every 12th the last one came with only the Dragon and the knight left.
    // Quick: every 6th (6, 12, 18, 24).
    if (currentRun.getCurrentEncounter() % boonInterval() == 0)
        offerBoon();

    // Relics on the 6th and every 12th after, so they fall between the boons
    // (Quick: the 3rd and every 6th after).
    {
        const int enc = currentRun.getCurrentEncounter();
        if (enc >= relicFirst() && (enc - relicFirst()) % relicInterval() == 0) offerRelic();
    }

    restSite();

    checkDeckAchievements();
    offerContinueOrEndRun();
}

// Which road to walk: Normal and Quick from the first run, the others once
// the road that opens them is cleared. Returns false if the player backed out.
bool Game::chooseMode(Mode& out, bool& carryWinningRun) {
    carryWinningRun = false;
    while (true) {
        std::vector<CardBar::Action> acts;
        std::vector<Mode> modes;
        acts.push_back(CardBar::Action{ "Normal", "50 encounters at the intended difficulty", false });
        modes.push_back(Mode::NORMAL);
        acts.push_back(CardBar::Action{ "Quick", "25 encounters through all five areas, fewer achievements", false });
        modes.push_back(Mode::QUICK);
        if (clearedMask & 1) {
            acts.push_back(CardBar::Action{ "Randomized", "the same 50 encounters, in a random order", false });
            modes.push_back(Mode::RANDOM);
            acts.push_back(CardBar::Action{ "Hard", "50 encounters at hard difficulty", false });
            modes.push_back(Mode::HARD);
        }
        if (clearedMask & 2) {
            acts.push_back(CardBar::Action{ "Randomized Hard", "the hard 50 encounters, in a random order", false });
            modes.push_back(Mode::RANDOM_HARD);
        }
        acts.push_back(CardBar::Action{ "Back", false });

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
                CardBar::Action{ "Back", false },
            };
            UIHelper::clearScreen();
            const int c = CardBar::pick("You have a run that finished all 50 encounters.", {}, carry, 0);
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
        moonZonesBeaten = 0;
        moonstruckMet = 0;
        satCount = 0;
        redThreadUsed = false;
        randomSeed = 0;          // a new road gets its own order
        currentSaveSlot = 0;     // and owns no slot until it is saved
        roadBonus = 0;           // the carried run brings its own numbers
        roadGearPct = 0;
    }
    runMode = m;
    // Quick's gear comes every second fight from the start. A carried run
    // keeps the pace it earned.
    if (!carryWinningRun) gearInterval = baseGearInterval();
    // A new road has not rested or thrown anything away yet, whatever the
    // run it was carried out of did.
    restedThisRun = false;
    discardedThisRun = false;
    discardedCards.clear();
    playedNonDot = false;
    lastUndead.clear();       // nothing has fallen on this road yet
    currentRun.startRun();
    currentRun.setDifficulty(difficultyFor(m));
    currentRun.setQuick(m == Mode::QUICK);
    buildRandomOrder();
    moonZonesSeen = 0;
    moonZonesBeaten = 0;
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
// its order. Quick: four of each area's nine (three of the Mountain's
// eight) from the same seed, kept in road order so each area still climbs,
// and drawn again until no two fights in a row share a type.
void Game::buildRandomOrder() {
    randomOrder.clear();
    if (runMode != Mode::RANDOM && runMode != Mode::RANDOM_HARD && runMode != Mode::QUICK) return;
    if (randomSeed == 0) randomSeed = (unsigned)std::random_device{}();
    std::mt19937 gen(randomSeed);
    if (runMode == Mode::QUICK) {
        static const int AREA_FIRST[5] = { 0, 9, 18, 27, 36 }, AREA_SIZE[5] = { 9, 9, 9, 9, 8 };
        for (int a = 0; a < 5; ++a) {
            const int take = a < 4 ? 4 : 3;
            std::vector<int> pick;
            bool alternates = false;
            while (!alternates) {
                std::vector<int> pool;
                for (int i = 0; i < AREA_SIZE[a]; ++i) pool.push_back(AREA_FIRST[a] + i);
                std::shuffle(pool.begin(), pool.end(), gen);
                pick.assign(pool.begin(), pool.begin() + take);
                std::sort(pick.begin(), pick.end());
                alternates = true;
                for (int i = 0; i + 1 < take; ++i)
                    if (ROSTER[pick[i]].type == ROSTER[pick[i + 1]].type) alternates = false;
            }
            randomOrder.insert(randomOrder.end(), pick.begin(), pick.end());
        }
        return;
    }
    for (int i = 0; i < 44; i++) randomOrder.push_back(i);
    std::shuffle(randomOrder.begin(), randomOrder.end(), gen);
}

int Game::rosterIndexFor(int regularIndex) const {
    // The church's nine are not shuffled: they come in their own order.
    if (regularIndex >= ROAD_REGULARS) return std::min(regularIndex, 52);
    if (randomOrder.empty()) return regularIndex % 44;
    return randomOrder[regularIndex % (int)randomOrder.size()];
}

const char* Game::modeName() const {
    switch (runMode) {
        case Mode::RANDOM:      return "Randomized";
        case Mode::HARD:        return "Hard";
        case Mode::RANDOM_HARD: return "Randomized Hard";
        case Mode::QUICK:       return "Quick";
        default:                return "Normal";
    }
}

bool Game::trueFormEarned() const { return !onQuick() && sealsBroken >= 4 && moonstruckMet >= 1; }

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
    earn(Achievements::VIGIL);
    if (sealsBroken >= 4) earn(Achievements::EVERY_VIGIL);
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
    // Normal, Random and Quick get the first ending; Hard gets its own.
    const bool firstTime = runMode == Mode::NORMAL || runMode == Mode::RANDOM || onQuick();
    UIHelper::waitForKey();
    // The music lets go and the peak is left to the wind, until the sun
    // comes up over the hills and the wind dies under it.
    Audio::fadeOutBGM(1800);
    Audio::startLoop("peak_wind");
    bool sunUp = false;
    auto sunrise = [&sunUp]() {
        if (sunUp) return;
        sunUp = true;
        Audio::stopLoop(2500);
        Audio::playSFX("sunrise");
    };
    if (EnemyArt::cutsceneReady(EnemyArt::Cutscene::ENDING)) {
        // The morning on the peak, a line to each shot. Esc skips straight to
        // the last, so the banner is never missed.
        const Lines& lines = firstTime ? ENDING : ENDING_HARD;
        for (int i = 0; i < 3; i++) {
            if (i == 2) sunrise();   // the shot where the sun comes over the hills
            if (!cutsceneBeat(EnemyArt::Cutscene::ENDING, i, lines[i])) break;
        }
        sunrise();                   // skipped ahead: the banner still gets its sunrise
        UIHelper::showHeadline("VICTORY ETERNAL", 240, 200, 60, 10);   // high in the sky, clear of him
        cutsceneBeat(EnemyArt::Cutscene::ENDING, 3, lines[3]);
        UIHelper::showHeadline("", 0, 0, 0);
        EnemyArt::setCutsceneShot(EnemyArt::Cutscene::ENDING, -1);
    } else {
        // Without its art, the words alone, as it always was.
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
        sunrise();
        UIHelper::clearScreen();
        UIHelper::showHeadline("VICTORY ETERNAL", 240, 200, 60);
        for (int i = 0, pad = Console::rows() * 44 / 100; i < pad; i++) std::cout << "\n";
        UIHelper::printCenteredWrapped(firstTime
            ? "The knight stands on the peak wearing all of himself again: his strength, his "
              "wits, his speed, his hands, his soul, and a shadow nothing else is wearing any "
              "more. Whole, and no longer a shadow of himself."
            : "Fifty encounters on a road that hits like a hundred. He went back up knowing "
              "exactly what was waiting, and it still could not stop him. Everything on this "
              "mountain has lost to him twice now.", 64);
        std::cout << "\n";
        UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
        UIHelper::waitForKey("");
        UIHelper::showHeadline("", 0, 0, 0);
    }

    // The music comes back for what is left: the heart, the record, the menu.
    Audio::stopLoop();
    Audio::playBGM();

    std::vector<Card> legendaries = rewardPool.getUnownedLegendaries(playerDeck.getAllCardNames());
    if (!legendaries.empty()) {
        // Random rather than front(): with three legendaries, always handing
        // out the first in pool order meant the same card every run.
        static thread_local std::mt19937 lg2(std::random_device{}());
        std::uniform_int_distribution<> lpick(0, (int)legendaries.size() - 1);
        const Card leg = takeBackDiscard(legendaries[lpick(lg2)]);
        playerDeck.addCard(leg);
        checkDeckAchievements();
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

    // The quick road opens nothing and keeps no winning run on file: the
    // harder roads and the knight's own gear are what the full fifty is for.
    if (onQuick()) {
        UIHelper::clearScreen();
        UIHelper::padToCenter(4);
        UIHelper::printCenteredWrapped(std::string(Color::BOLD) + Color::YELLOW
            + "The quick road is yours." + Color::RESET, 68, true);
        std::cout << "\n";
        UIHelper::printCenteredWrapped(std::string(Color::DIM)
            + "Clearing the full fifty is what opens the harder roads." + Color::RESET, 68, true);
        std::cout << "\n";
        UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
        UIHelper::waitForKey("");
        deleteCurrentSave();
        currentRun.loseRun(); // main loop routes to finishRun()
        return;
    }

    // What a clear of the fifty buys is the harder roads, and a permanent
    // record of the run that cleared it: dying later empties a save slot,
    // but it can never take this away.
    const int before = clearedMask;
    const int beforeSets = unlockedSets;
    recordClear();
    announceClear(before, beforeSets);

    deleteCurrentSave();
    currentRun.loseRun(); // main loop routes to finishRun()
}

// What a clear opened: the harder roads, and the trophy set off the thing
// just put down. Shown at the peak, or on the way down from it when the true
// form runs to the church, since the clear is kept there either way.
void Game::announceClear(int before, int beforeSets, bool toTheChurch) {
    const bool opened = (!(before & 1) && (clearedMask & 1)) || (!(before & 2) && (clearedMask & 2))
                     || (!(beforeSets & 1) && (unlockedSets & 1)) || (!(beforeSets & 2) && (unlockedSets & 2));
    if (toTheChurch && !opened) return;
    UIHelper::clearScreen();
    UIHelper::padToCenter(4);
    if (!(before & 1) && (clearedMask & 1)) {
        UIHelper::printCenteredWrapped(std::string(Color::BOLD) + Color::YELLOW
            + "Two new roads open behind you." + Color::RESET, 68, true);
        std::cout << "\n";
        UIHelper::printCenteredWrapped(std::string(Color::DIM)
            + (toTheChurch ? "" : "It is asleep, not gone, and what it dreams about is the road. ")
            + "RANDOMIZED: the same 50 encounters, in an order you have never fought. "
              "HARD: the dream, where it remembers every step you took. Start either from the "
              "menu, with this run's deck or a fresh one." + Color::RESET, 68, true);
    } else if (!(before & 2) && (clearedMask & 2)) {
        UIHelper::printCenteredWrapped(std::string(Color::BOLD) + Color::YELLOW
            + "RANDOMIZED HARD opens." + Color::RESET, 68, true);
        std::cout << "\n";
        UIHelper::printCenteredWrapped(std::string(Color::DIM)
            + "The same hard 50 encounters, in an order you have never walked. Dreams do not keep "
              "things where you left them." + Color::RESET, 68, true);
    } else if (!toTheChurch) {
        UIHelper::printCenteredWrapped(std::string(Color::DIM)
            + "There is no harder road left. This one is yours." + Color::RESET, 68, true);
    }
    // The set is the part you keep: a rung above the Legendary gear on every
    // run's ladder from here on.
    std::cout << "\n";
    if (!(beforeSets & 1) && (unlockedSets & 1)) {
        UIHelper::printCenteredWrapped(std::string(Color::BOLD) + Color::MAGENTA
            + "You take back the plate and the blade it wore. They fit, which is the part "
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
}

// The true form beaten with the church open. The clear is kept here, so
// dying in the church cannot take it away, and the roads it opens are shown;
// then the rewards a boss gives, and the road goes on to the church.
void Game::trueFormRuns() {
    const int before = clearedMask;
    const int beforeSets = unlockedSets;
    recordClear();
    Audio::fadeOutBGM(1200);
    showStoryBeat(TRUE_FORM_RUNS);
    announceClear(before, beforeSets, /*toTheChurch*/true);
    UIHelper::clearScreen();
}

// The False Moon put out: the true ending. Its heart is the legendary the
// Shadow Knight's would have been, and the run ends here.
void Game::handleTrueVictory() {
    UIHelper::waitForKey();
    Audio::fadeOutBGM(1800);
    // The sunrise the peak never got in this run: the true form ran from it at
    // encounter 50, so the morning comes here, after the church. The peak's
    // own shots, under the true ending's words: the bell rings and the sun
    // comes over the hills on the third.
    if (EnemyArt::cutsceneReady(EnemyArt::Cutscene::ENDING)) {
        // No moon to go down: it went out in the nave.
        EnemyArt::setEndingMoonless(true);
        Audio::startLoop("peak_wind");
        bool sunUp = false;
        auto sunrise = [&sunUp]() {
            if (sunUp) return;
            sunUp = true;
            Audio::stopLoop(2500);
            Audio::playSFX("sunrise");
            Audio::playSFX("church_bell");
        };
        for (int i = 0; i < 3; i++) {
            if (i == 2) sunrise();
            if (!cutsceneBeat(EnemyArt::Cutscene::ENDING, i, ENDING_TRUE[i])) break;
        }
        sunrise();                   // skipped ahead: the banner still gets its morning
        UIHelper::showHeadline("THE LONG NIGHT ENDS", 214, 66, 58, 10);
        cutsceneBeat(EnemyArt::Cutscene::ENDING, 3, ENDING_TRUE[3]);
        UIHelper::showHeadline("", 0, 0, 0);
        EnemyArt::setCutsceneShot(EnemyArt::Cutscene::ENDING, -1);
        EnemyArt::setEndingMoonless(false);
        Audio::stopLoop();
    } else {
        // Without its art, the words alone, as it always was.
        for (size_t i = 0; i + 1 < ENDING_TRUE.size(); ++i) {
            UIHelper::clearScreen();
            UIHelper::padToCenter(4);
            // The last shot's bell, if the sound is there.
            if (i + 2 == ENDING_TRUE.size()) Audio::playSFX("church_bell");
            UIHelper::printCenteredWrapped(std::string(Color::BOLD) + Color::MAGENTA + ENDING_TRUE[i]
                                           + Color::RESET, 68, true);
            std::cout << "\n";
            UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
            UIHelper::waitForKey("");
        }
        Audio::playSFX("sunrise");
        UIHelper::clearScreen();
        UIHelper::showHeadline("THE LONG NIGHT ENDS", 214, 66, 58);
        for (int i = 0, pad = Console::rows() * 44 / 100; i < pad; i++) std::cout << "\n";
        UIHelper::printCenteredWrapped(ENDING_TRUE.back(), 64);
        std::cout << "\n";
        UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
        UIHelper::waitForKey("");
        UIHelper::showHeadline("", 0, 0, 0);
    }
    Audio::playBGM();

    std::vector<Card> legendaries = rewardPool.getUnownedLegendaries(playerDeck.getAllCardNames());
    if (!legendaries.empty()) {
        static thread_local std::mt19937 lg3(std::random_device{}());
        std::uniform_int_distribution<> lpick(0, (int)legendaries.size() - 1);
        const Card& leg = legendaries[lpick(lg3)];
        playerDeck.addCard(leg);
        checkDeckAchievements();
        runStats.addCardToRun();
        Audio::playSFX("upgrade");
        UIHelper::clearScreen();
        UIHelper::padToCenter(9);
        UIHelper::printCentered(std::string(Color::BOLD) + Color::YELLOW
                                + "Where the moon came apart, something is still glowing" + Color::RESET);
        std::cout << "\n";
        UIHelper::printCentered(std::string(Color::BOLD) + rarityTint(leg) + leg.getName()
                                + Color::RESET + "  " + Color::DIM + "(LEGENDARY)" + Color::RESET);
        UIHelper::printCenteredWrapped(std::string(Color::DIM) + leg.getDescription() + Color::RESET, 64);
        std::cout << "\n";
        UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
        UIHelper::waitForKey("");
    } else {
        UIHelper::clearScreen();
        notice("You already wield the legendary art. Claim a final trophy instead.");
        offerBossReward();
    }

    inEncounter = false;
    Hud::setActive(false);
    // The clear was kept at the peak; this keeps the run as it finished.
    recordClear();
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
            leftBySaving = true;
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
        std::min(2, currentRun.getRoadPosition() / 10), luckBonus());
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
                          ? 2 : std::min(2, currentRun.getRoadPosition() / 10);
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
    showDiscardUpgrades(rewards);
    std::vector<CardBar::Card> widgets;
    for (const Card& c : rewards) widgets.push_back(toWidget(c, gearedValue(c, c.getValue()), attunementChance(), false, luckBonus()));
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
                showDiscardUpgrades(rewards);
                widgets.clear();
                for (const Card& c : rewards) widgets.push_back(toWidget(c, gearedValue(c, c.getValue()), attunementChance(), false, luckBonus()));
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

        playerDeck.addCard(takeBackDiscard(rewards[choice]));
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
int Game::luckBonus() const { return runLuck * 5; }

// 5% of max HP, so it grows with the run instead of fading into nothing.
int Game::lanternArmor() const { return std::max(3, maxPlayerHealth * 5 / 100); }

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
    // Each offer is a card, the "+" text behind it, and the name it is taken by.
    std::vector<CardBar::Card> widgets;
    std::vector<std::string> details, names;
    auto offer = [&](const std::string& name, const std::string& face, const std::string& note,
                     const std::string& detail) {
        widgets.push_back(boonCard(name, face, note));
        details.push_back(detail);
        names.push_back(name);
    };
    // Nothing follows you on the quick road, so its Fortune does not say so.
    offer("Fortune", "+5% luck",
          onQuick() ? "rarer rewards and luckier fights"
                    : "rarer rewards, luckier fights, the Moon Shades more often",
          std::string("For the rest of this run, card rewards roll rarer, boss rewards roll a legendary "
          "more often, ") + (onQuick() ? "" : "a Moon Shade is likelier to find you, ")
          + "and the chances in a fight lean "
          "your way: Taunt and Fear take more often, bosses resist your stuns less, and "
          "Impair, the Weighted Pommel, the Rusty Blade and a Rend cutting a Hydra head all "
          "land more often. It adds 5 points each time you take it.");
    // Endurance and Scavenger are taken once: a second card a turn was the pick
    // that beat every other, and gear cannot come sooner than every 2.
    if (handSizeBonus == 0)
        offer("Endurance", "+1 card/turn", "every turn, for the rest of the run",
              "Draw one extra card at the start of every turn for the rest of this run. "
              "It can only be taken once.");
    offer("Foresight", "+1 reward", "one more card to choose from on every reward",
          "Every card reward screen offers one extra card to choose from for the rest of "
          "this run.");
    offer("Attunement", "+12% elements", "your elemental attacks land their status more often",
          "Your elemental attacks apply their status far more readily: Fire leaves Burn, "
          "Poison leaves Poison, Wind leaves Rend. The chance rises by 12 points each time "
          "you take it, from a base of 10%.");
    if (gearInterval > 2)
        offer("Scavenger", "gear sooner",
              "equipment every " + std::to_string(gearInterval - 1)
              + " encounters instead of " + std::to_string(gearInterval),
              "Equipment drops arrive every " + std::to_string(gearInterval - 1)
              + " encounters instead of every " + std::to_string(gearInterval)
              + " for the rest of the run.");
    std::vector<CardBar::Action> acts{ CardBar::Action{ "Take none", false } };
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
    earn(Achievements::BOON);

    const std::string& taken = names[choice];
    if (taken == "Attunement") {
        attunementBoons++;
        Audio::playSFXPitched("upgrade", 1.15f);   // a boon: the brightest of the rewards
        notice("Attunement. Your elemental attacks now land their status "
               + std::to_string(attunementChance()) + "% of the time.");
    } else if (taken == "Scavenger") {
        gearInterval = std::max(2, gearInterval - 1);
        Audio::playSFXPitched("upgrade", 1.15f);   // a boon: the brightest of the rewards
        notice("Scavenger. Equipment turns up every " + std::to_string(gearInterval)
               + " encounters from here on.");
    } else if (taken == "Endurance") {
        handSizeBonus++;
        Audio::playSFXPitched("upgrade", 1.15f);   // a boon: the brightest of the rewards
        notice("Endurance. You will draw " + std::to_string(BASE_HAND_SIZE + handSizeBonus)
               + " cards a turn from here on.");
    } else if (taken == "Foresight") {
        rewardChoiceBonus++;
        Audio::playSFXPitched("upgrade", 1.15f);   // a boon: the brightest of the rewards
        notice("Foresight. Reward screens will offer "
               + std::to_string(3 + rewardChoiceBonus) + " cards from here on.");
    } else {
        runLuck++;
        Audio::playSFXPitched("upgrade", 1.15f);   // a boon: the brightest of the rewards
        notice(std::string(onQuick() ? "Fortune. Rewards and your chances in a fight are "
                                     : "Fortune. Rewards, your chances in a fight and the Moon Shades' odds are ")
               + std::to_string(luckBonus()) + " points kinder this run.");
    }
}

void Game::offerEquipmentDrop() {
    EquipTier weapon = weaponTierAt(weaponTier);
    EquipTier armor  = armorTierAt(armorTier);
    int hpBoost = 30;
    // The trophies say which clear put them on the ladder.
    const int nextW = std::min(maxGearTier(), weaponTier + 1);
    const int nextA = std::min(maxGearTier(), armorTier + 1);
    // The Shadow set is the knight's own, taken back off the thing that wore
    // it (the user, 2026-10-08: "an armor once yours now corrupted by an
    // eldritch being").
    auto trophyWon = [](int tier, bool weapon) {
        return tier >= TIER_MOON   ? std::string("Yours for clearing the hard road. ")
             : tier >= TIER_SHADOW ? std::string("Yours for clearing all 50 encounters. ")
                                     + (weapon ? "A blade once yours, now corrupted by an eldritch being. "
                                               : "Armor once yours, now corrupted by an eldritch being. ")
             : std::string();
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
        trophyWon(nextW, true) + "Every attack card deals " + std::to_string(weapon.bonus)
        + "% more damage for the rest of this run. Your weapon bonus goes from +" + std::to_string(weaponPct())
        + "% to +" + std::to_string(gearPercentFor(weaponTier + 1, true) + roadGearPct) + "%, and the "
          "numbers on your cards update to match. " + WEAPON_PASSIVE_LONG[nextW],
        trophyWon(nextA, false) + "Every defend card gives " + std::to_string(armor.bonus)
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
            earn(Achievements::GEAR);
            if (weaponTier >= TIER_SHADOW) earn(Achievements::TROPHY);
            wornWeapon = weaponTier;   // new gear goes straight on
            equipDamagePercent = gearPercentFor(weaponTier, true);
            Audio::playSFXPitched("upgrade", 0.9f);   // gear: a shade under a card
            result = "You equip the " + weapon.name + ". +"
                   + std::to_string(weapon.bonus) + "% damage (now +"
                   + std::to_string(equipDamagePercent) + "%).";
        } else if (choice == 1) {
            armorTier = std::min(maxGearTier(), armorTier + 1);
            earn(Achievements::GEAR);
            if (armorTier >= TIER_SHADOW) earn(Achievements::TROPHY);
            wornArmor = armorTier;
            equipArmorPercent = gearPercentFor(armorTier, false);
            Audio::playSFXPitched("upgrade", 0.9f);   // gear: a shade under a card
            result = "You equip the " + armor.name + ". +"
                   + std::to_string(armor.bonus) + "% armor per defend (now +"
                   + std::to_string(equipArmorPercent) + "%).";
        } else {
            maxPlayerHealth += hpBoost;
            playerHealth += hpBoost;
            Audio::playSFX("heal");
            result = "You consume the Health Pouch. Max HP increased by "
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
        opts.push_back("Achievements");
        opts.push_back("Settings");
        opts.push_back("Quit");

        int choice = UIHelper::titleMenu(opts);
        // By label: "Load Save" only exists sometimes, so counting positions
        // would break.
        const std::string chosen = (choice >= 0 && choice < (int)opts.size()) ? opts[choice] : "Quit";
        UIHelper::showTitleBanner(false);
        if (chosen == "Start Game") return 0;
        if (chosen == "Load Save")  return 1;
        if (chosen == "How to Play" || chosen == "Settings" || chosen == "Achievements") {
            if (chosen == "Settings")          showSettings();
            else if (chosen == "Achievements") Achievements::showScreen();
            else                               showHowToPlay();
            UIHelper::clearScreen();
            Hud::setActive(false);   // nothing from in there belongs on the title
            UIHelper::printTitle();
            continue;
        }
        return 2; // Quit or ESC
    }
}

void Game::showHowToPlay() {
    // Four pages: one screen cannot hold it, and a wall of text nobody
    // scrolls teaches nothing.
    auto page = [](const char* title) {
        UIHelper::clearScreen();
        std::cout << "\n" << Color::BOLD << Color::CYAN << title << Color::RESET << "\n\n";
    };
    auto head = [](const char* h) { std::cout << Color::BOLD << h << Color::RESET << "\n"; };
    auto more = []() { UIHelper::waitForKey("  (press any key)"); };

    page("HOW TO PLAY   1 of 4: the fight");
    head("GOAL");
    std::cout << "  50 encounters through five areas, with a boss every tenth fight.\n";
    std::cout << "  Quick walks the same five areas in 25, with a boss every fifth.\n";
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
    std::cout << "  " << Color::WEAK_CLR << "Weak" << Color::RESET << "          the target deals 33-50% less damage for a few turns\n";
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
    std::cout << "  the cost. Uncommon and rare cards can be upgraded twice, super rare\n";
    std::cout << "  and legendary three times. Starters cannot be forged.\n\n";

    head("GEAR");
    std::cout << "  Weapons and armor drop every 3rd encounter. Their percentages add up\n";
    std::cout << "  across everything you have claimed, whatever you are wearing; what you\n";
    std::cout << "  wear decides your armor's resistances and your weapon's own trick, like\n";
    std::cout << "  the Ebon Blade's healing or the Mythril Edge's cheaper opening attack.\n\n";
    more();

    page("HOW TO PLAY   4 of 4: the long game");
    head("BOONS");
    std::cout << "  Every 11th encounter, pick one of five, kept for the run: Fortune (luck\n";
    std::cout << "  in rewards and fights), Endurance (+1 card a turn, once), Foresight (+1\n";
    std::cout << "  card on rewards), Attunement (elemental statuses land far more often) and\n";
    std::cout << "  Scavenger (gear more often).\n\n";

    head("RELICS");
    std::cout << "  From the 6th encounter, then every 12th, one of three relics. They do\n";
    std::cout << "  not cost energy and they last the run: stronger poison, a free\n";
    std::cout << "  reroll on rewards, armor at the start of every turn. One of them is\n";
    std::cout << "  " << Color::YELLOW << "cursed" << Color::RESET << ", and wears the same gold ring the risky cards do.\n\n";

    head("VIGILS");
    std::cout << "  The first four area bosses each have one burning in the chest. They\n";
    std::cout << "  are what keeps the thing on the peak awake, and putting one out costs\n";
    std::cout << "  it a night it cannot spare, so it answers by pouring more of itself\n";
    std::cout << "  into everything left: +15% health and +10% attack each, and they\n";
    std::cout << "  stack. Nothing forces you to touch them, and putting out all four is\n";
    std::cout << "  the only way to see what is actually wearing your face.\n\n";

    head("THE ROADS");
    std::cout << "  A Normal run is 50 encounters, and it ends there. Finishing it opens\n";
    std::cout << "  " << Color::CYAN << "Randomized" << Color::RESET << " (the same 50 in an order you\n";
    std::cout << "  have not fought) and " << Color::CYAN << "Hard" << Color::RESET << " (every enemy as strong as 50\n";
    std::cout << "  fights further on, and redder for it). Finishing Hard opens\n";
    std::cout << "  " << Color::CYAN << "Randomized Hard" << Color::RESET << ", the hard 50 with nothing where you left it.\n";
    std::cout << "  Pick a road on Start Game, with the deck that earned it or a fresh\n";
    std::cout << "  one. What you have cleared is kept in its own file, so dying can\n";
    std::cout << "  never take a road away from you.\n\n";
    std::cout << "  " << Color::CYAN << "Quick" << Color::RESET << " is open from the start: the same five areas in 25 encounters.\n";
    std::cout << "  Gear comes every 2nd encounter, relics on the 3rd and every 6th after,\n";
    std::cout << "  and boons every 6th. It has no vigils and fewer achievements, and\n";
    std::cout << "  clearing it opens nothing.\n\n";

    head("BETWEEN RUNS");
    std::cout << "  Encounters won and cards collected unlock permanent upgrades you can\n";
    std::cout << "  switch on for the next run. There are three save slots, and dying only\n";
    std::cout << "  clears the slot you were playing.\n\n";

    UIHelper::waitForKey("  (press any key to continue)");

    std::vector<CardBar::Action> helpActs{
        CardBar::Action{ "Tutorial", false },
        CardBar::Action{ "Back", false },
    };
    if (CardBar::pick("How to play", {}, helpActs, 0) == 0) showTutorial();
}

namespace {
// Every in-fight tip goes out through here, so they all break the same way
// and none of them runs off the edge of the log box.
void tutorialFact(const std::string& body) {
    std::cout << "\n";
    UIHelper::printWrapped(std::string(Color::DIM) + "Tip: " + Color::RESET + body, 2, 6);
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
    raisedAlive = false;
    EnemyArt::setAlly("");
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
    std::cout << "\n" << Color::DIM << "Tip: " << Color::RESET << Color::ENERGY_CLR << playerEnergy << "/" << maxEnergy
              << " energy" << Color::RESET << " up top is your budget for the turn - every card's cost comes\n";
    std::cout << "  out of that pool, so you can't play more than you can afford.\n\n";
    UIHelper::waitForKey("  (press any key to start turn 1)");

    // No turn limit: losing to the slime has its own achievement, and a 20 HP
    // slime only outlasts a player who lets it.
    bool slimeDefeated = false;
    bool attackTipShown = false, defendTipShown = false, specialTipShown = false, damageTypeTipShown = false;
    bool riskTipShown = false, hitTipShown = false;
    while (playerHealth > 0) {
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
        std::cout << "\n" << Color::BOLD << Color::RED << "The slime wins." << Color::RESET << "\n\n";
        earn(Achievements::TUTORIAL_SLIME);
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
    std::cout << "  " << Color::YELLOW << "Boons" << Color::RESET << "      every 11th encounter, one of five lasting blessings\n";
    std::cout << "  " << Color::YELLOW << "Vigils" << Color::RESET << "     one burns in each of the first four bosses. Putting one out\n";
    std::cout << "             makes everything left tougher: more of the moon is in it.\n";
    std::cout << "  " << Color::YELLOW << "Quick" << Color::RESET << "      gear every 2nd encounter, relics on the 3rd and every 6th\n";
    std::cout << "             after, boons every 6th, and no vigils.\n\n";
    UIHelper::waitForKey("  (press any key to go back to the menu)");

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
        notice("Save loaded. Encounter " + std::to_string(currentRun.shownEncounter())
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
        // The story belongs to the first road, and to the quick one a new
        // player may take instead. The harder ones are walked by someone who
        // has already heard it.
        if (picked == Mode::NORMAL || picked == Mode::RANDOM || picked == Mode::QUICK) showIntro();
        startRunInMode(picked, carry);
        startEncounter();
    }
    return true;
}

void Game::run() {
    // Gear cards draw their sword or shield through the art layer.
    CardBar::setIconRenderer(&EnemyArt::drawItemIcon);
    // The panel reads the fight live, so the bars move as blows land.
    Hud::setSource([this] { return hudState(); });
    Achievements::install();
    loadProgress();   // which roads this player has already earned
    loadSettings();   // and how they like it read to them
    init();
    migrateLegacySave();   // an older save.dat becomes slot 1

    UIHelper::printTitle();

    if (!mainMenuFlow()) return;

    while (running) {
        handleInput();
        updateHeartbeat();

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
    Audio::stopLoop();
    // A run lost mid-fight: the cards that fight set aside are offered to
    // carry over like the rest, and its Grave Dirt never is.
    endEncounterEffects();
    int encounters = currentRun.getEncountersWon();
    runStats.completeRun(encounters);
    runStats.displayRunSummary(encounters);

    const bool saved = leftBySaving;
    leftBySaving = false;
    const int next = handleGameOverInput(saved);
    if (next != 2) {
        // Going back to the menu skips both: the saved run is still to finish.
        Card carryOverCard("", "", CardType::ATTACK, 0, 0);
        bool hasCarryOver = false;
        if (next == 0) {
            selectUpgrades();
            hasCarryOver = selectCardToCarryOver(carryOverCard);
        }

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
        lastUndead.clear();
        raisedAlive = false;
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
        moonZonesBeaten = 0;

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
