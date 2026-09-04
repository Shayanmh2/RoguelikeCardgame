#include "Game.h"
#include "Audio.h"
#include "Colors.h"
#include "UIHelper.h"
#include "EnemyArt.h"
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

Game::Game() : playerDeck(), enemy("Enemy", 50, 8, 4, EnemyType::MELEE), currentRun(), playerHealth(100), maxPlayerHealth(100), playerArmor(0), playerArmorPersistTurns(0), playerEnergy(3), maxEnergy(3), turnNumber(1), playerTurnActive(true), running(false), inEncounter(false), equipDamageBonus(0), equipArmorBonus(0), weaponTier(0), armorTier(0), counterAttackActive(false), parryActive(false), counterBonusValue(0), parryBonusValue(0) {}

// Elemental effects get a matching sound; everything else uses "special".
static const char* effectSoundName(CardEffect effect) {
    switch (effect) {
        case CardEffect::POISON: return "poison";
        case CardEffect::BURN:   return "fire";
        case CardEffect::STUN:   return "volt";
        case CardEffect::HEAL:   return "heal";
        default:                 return "special";
    }
}

// Card name tint by rarity; legendary gets bold gold instead of a pastel.
// One short line for a card face. The full sentence lives in the details
// panel; this is what has to read at a glance while choosing.
static std::string cardFaceLine(const Card& c, int shownValue) {
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
        case CardEffect::HEAL:       base  = "heal " + std::to_string(shownValue); break;
        case CardEffect::WARD:       extra = "ward";     break;
        case CardEffect::TAUNT:      extra = "taunt";    break;
        // Six characters is what fits beside a three-digit damage figure;
        // "unstoppable" was rendering as "unstop".
        case CardEffect::TRUESTRIKE: extra = "true";    break;
        default: break;
    }
    if (extra) base += "  " + std::string(extra);
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
    w.rare      = c.isRare() || c.isSuperRare() || c.isLegendary();
    w.disabled  = disabled;
    w.nameColor = Console::xterm256Public(
                      c.isLegendary() ? 220
                    : c.isSuperRare() ? 218
                    : c.isRare()      ? 153
                    : c.isStarter()   ?   7
                                      : 120);
    w.tint = Console::xterm256Public(
                 (c.getType() == CardType::ATTACK) ? 9
               : (c.getType() == CardType::DEFEND) ? 12
                                                   : 13);
    return w;
}

// What one upgrade would do to this card. Card::upgrade() adds 3 to the value
// and takes 1 off the cost with a floor of 1, so both are predictable without
// having to actually apply it.
static int upgradedValue(const Card& c) { return c.getValue() + 3; }
static int upgradedCost(const Card& c)  { return c.getCost() > 1 ? c.getCost() - 1 : c.getCost(); }

// Compact "6 -> 9 dmg" for the card face. The face clips at about fifteen
// characters, so this cannot spell out the whole change.
// `bonus` is the flat damage or armour the player already adds to every card
// of that type. The in-battle hand shows values with it folded in, so the
// forge has to as well - otherwise a Strike reads "5 -> 8" here and hits for
// 61 in the fight. Specials get no bonus, so callers pass 0 for those.
static std::string upgradeFaceLine(const Card& c, int bonus) {
    const int v = c.getValue() + bonus, u = upgradedValue(c) + bonus;
    if (c.getEffect() == CardEffect::HEAL)
        return "heal " + std::to_string(v) + "->" + std::to_string(u);
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

static const char* rarityTint(const Card& c) {
    if (c.isLegendary()) return Color::LEGENDARY_TINT;
    if (c.isStarter())   return Color::CARD_NAME;
    if (c.isSuperRare()) return Color::SUPER_RARE_TINT;
    if (c.isRare())      return Color::RARE_TINT;
    return Color::COMMON_TINT;
}

// Higher = rarer; used to sort card lists highest-rarity-first (Forge, View Deck).
static int rarityRank(const Card& c) {
    if (c.isLegendary()) return 4;
    if (c.isSuperRare()) return 3;
    if (c.isRare())      return 2;
    if (c.isStarter())   return 0;
    return 1; // uncommon reward-tier
}

// String round-trips for the save file - mirrors the identifier names cards.json
// already uses, kept separate from ConfigLoader's parsing since these need both directions.
// A boss falling is the beat the run has been building to, so it does not
// share a cue with the forty regular enemies before it. Same for its blows.
static const char* deathSfx(bool isBoss) { return isBoss ? "boss_death" : "dead"; }

static const char* effectToStr(CardEffect e) {
    switch (e) {
        case CardEffect::POISON:     return "POISON";
        case CardEffect::BURN:       return "BURN";
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
        case CardEffect::TRUESTRIKE: return "TRUESTRIKE";
        default:                     return "NONE";
    }
}
static CardEffect strToEffect(const std::string& s) {
    if (s == "POISON")     return CardEffect::POISON;
    if (s == "BURN")       return CardEffect::BURN;
    if (s == "STUN")       return CardEffect::STUN;
    if (s == "WEAK")       return CardEffect::WEAK;
    if (s == "COUNTER")    return CardEffect::COUNTER;
    if (s == "PARRY")      return CardEffect::PARRY;
    if (s == "PIERCE")     return CardEffect::PIERCE;
    if (s == "FORTIFY")    return CardEffect::FORTIFY;
    if (s == "STRENGTH")   return CardEffect::STRENGTH;
    if (s == "DOUBLE_HIT") return CardEffect::DOUBLE_HIT;
    if (s == "IMPAIR")     return CardEffect::IMPAIR;
    if (s == "CHIP")       return CardEffect::CHIP;
    if (s == "HEAL")       return CardEffect::HEAL;
    if (s == "WARD")       return CardEffect::WARD;
    if (s == "TAUNT")      return CardEffect::TAUNT;
    if (s == "TRUESTRIKE") return CardEffect::TRUESTRIKE;
    return CardEffect::NONE;
}
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
static DamageType strToDmg(const std::string& s) {
    if (s == "SMASH")  return DamageType::SMASH;
    if (s == "PIERCE") return DamageType::PIERCE;
    if (s == "FIRE")   return DamageType::FIRE;
    if (s == "POISON") return DamageType::POISON;
    if (s == "WIND")   return DamageType::WIND;
    return DamageType::NONE;
}
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

// Gear name/bonus escalates per tier claimed; the last tier repeats after that.
// Gear name colour, on the same rarity ladder the cards use. Both slots were
// hardcoded to 153 - the rare tint - so a Legendary Blade came up the same
// blue as a Rusty Blade and the last tier read as nothing special.
static int equipTintFor(int tier) {
    if (tier >= 5) return 220;   // legendary: neon gold
    if (tier == 4) return 218;   // super rare: pale pink
    if (tier >= 2) return 153;   // rare: sky blue
    return 120;                  // common: pale green
}

static EquipTier weaponTierAt(int tier) {
    static const std::vector<EquipTier> tiers = {
        {"Rusty Blade", 3}, {"Iron Sword", 4}, {"Steel Blade", 5},
        {"War Axe", 6}, {"Mythril Edge", 8}, {"Legendary Blade", 10}
    };
    int idx = std::min(tier, (int)tiers.size() - 1);
    return tiers[idx];
}
static EquipTier armorTierAt(int tier) {
    static const std::vector<EquipTier> tiers = {
        {"Iron Plating", 3}, {"Steel Plating", 4}, {"Chainmail", 5},
        {"Plate Armor", 6}, {"Mythril Plating", 8}, {"Legendary Aegis", 10}
    };
    int idx = std::min(tier, (int)tiers.size() - 1);
    return tiers[idx];
}

void Game::init() {
    // One of each starter card; duplicates only ever come from later card rewards.
    playerDeck.addCard(Card("Quick Jab", "Deal 3 damage.", CardType::ATTACK, 0, 3));
    playerDeck.addCard(Card("Slash", "Deal 4 damage.", CardType::ATTACK, 1, 4));
    playerDeck.addCard(Card("Bash", "Deal 6 damage. Counts as a Smash attack, so it hits harder against enemies weak to Smash and lands softer against those that resist it.", CardType::ATTACK, 2, 6, CardEffect::NONE, false, DamageType::SMASH));
    playerDeck.addCard(Card("Lunge", "Deal 6 damage. Counts as a Pierce attack, so it hits harder against enemies weak to Pierce and lands softer against those that resist it.", CardType::ATTACK, 2, 6, CardEffect::NONE, false, DamageType::PIERCE));
    playerDeck.addCard(Card("Defend", "Gain 8 armor.", CardType::DEFEND, 1, 8));
    playerDeck.addCard(Card("Brace", "Gain 8 armor.", CardType::DEFEND, 1, 8));
    playerDeck.addCard(Card("Parry",
        "Block the enemy's next attack and riposte for 1.5x their attack "
        "plus 3, ignoring their defense, with a chance to stun them. "
        "How big a blow you can catch is your armor plus 9: too heavy a hit "
        "breaks the guard. Ranged enemies are blocked but stand too far away "
        "to riposte.",
        CardType::SPECIAL, 3, 3, CardEffect::PARRY));

    applyUpgrades();
    
    playerDeck.shuffle();
    
    running = true;
}

// One-line flavor text per regular enemy, matched the same way as the move
// hints below (substring on the base name, so "Greater "/"Eternal " prefixes
// from later cycles still match). Grouped by zone/theme, not by type.
static std::string enemyFlavorText(const std::string& enemyName) {
    auto has = [&](const char* k){ return enemyName.find(k) != std::string::npos; };
    // Bosses first: generateBossEnemy() prefixes them with "Ancient "/"Eternal " on
    // later cycles, so substring matching still catches them - but "Shadow Knight"
    // also contains "Knight", so these have to be tested before the regular roster
    // or the finale would inherit the Dark Dungeon knight's line.
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
    else if (has("Moonstruck"))return "It was an ordinary animal until the red moon found it. It has not eaten since.";
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

        // Wheel or arrows. waitKey() would block on a key and never see the
        // wheel, so this polls both and draws frames in between.
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

void Game::displayEnemyInfo() const {
    int atk = enemy.getBaseAttack();
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

    std::cout << "\n" << Color::DIM << "Possible moves:" << Color::RESET << "\n";

    if (enemy.isBoss()) {
        switch (enemy.getBossType()) {
            case BossType::STONE_COLOSSUS:
                std::cout << "  " << Color::RED    << "Earthquake Slam" << Color::RESET << " (rare)    - hits you twice, ignores armor\n";
                std::cout << "  " << Color::ARMOR_CLR << "Fortify"      << Color::RESET << " (common)  - gains a lot of armor\n";
                std::cout << "  " << Color::RED    << "Crush"           << Color::RESET << " (common)  - heavy melee strike\n";
                break;
            case BossType::VILE_WITCH:
                std::cout << "  " << Color::CARD_SPECIAL << "Poison Cloud" << Color::RESET << " (common)  - poisons you heavily\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Hex"          << Color::RESET << " (common)  - burns you\n";
                std::cout << "  " << Color::RED         << "Strike"       << Color::RESET << " (uncommon)- direct attack\n";
                break;
            case BossType::WARLORD:
                std::cout << "  " << Color::STUN_CLR << "Thunderstrike" << Color::RESET << " (rare)    - stuns you, then attacks\n";
                std::cout << "  " << Color::CARD_SPECIAL << "War Cry"   << Color::RESET << " (uncommon)- weakens you, then attacks\n";
                std::cout << "  " << Color::RED      << "Heavy Strike"  << Color::RESET << " (common)  - attacks, grows stronger each turn\n";
                break;
            case BossType::HYDRA:
                std::cout << "  " << Color::CARD_SPECIAL << "Venomous Bite" << Color::RESET << " (common)  - poisons you heavily\n";
                std::cout << "  " << Color::RED         << "Twin Strike"   << Color::RESET << " (common)  - hits you twice\n";
                std::cout << "  " << Color::HEAL        << "Regrowth"      << Color::RESET << " (uncommon)- regrows a head, heals HP\n";
                std::cout << "  " << Color::RED         << "Bite"          << Color::RESET << " (uncommon)- direct attack\n";
                break;
            case BossType::DRAGON:
                std::cout << "  " << Color::CARD_SPECIAL << "Fire Breath" << Color::RESET << " (common)  - burns you heavily\n";
                std::cout << "  " << Color::RED       << "Claw Rake"   << Color::RESET << " (uncommon)- heavy strike, ignores armor\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Wing Buffet" << Color::RESET << " (uncommon)- knocks you off balance, weakens you\n";
                std::cout << "  " << Color::RED         << "Claw"        << Color::RESET << " (uncommon)- direct attack\n";
                break;
            case BossType::SHADOW_KNIGHT:
                std::cout << "  " << Color::CARD_SPECIAL << "Dark Mirror" << Color::RESET << " (always)  - plays a shadow copy of a random card from YOUR deck\n";
                std::cout << "  " << Color::DIM << "  Your attacks become its strikes, your armor its guard, your potions its mending." << Color::RESET << "\n";
                std::cout << "  " << Color::DIM << "  The bigger your deck's numbers, the harder it hits back." << Color::RESET << "\n";
                break;
            default: break;
        }
    } else {
        auto nameHas = [&](const char* k){ return enemy.getName().find(k) != std::string::npos; };
        // tag shown in parens right after the move name: exact hit chance and/or damage,
        // pulled from the same roll thresholds/formulas enemyTurn() actually uses.
        auto line = [&](const char* clr, const char* mv, const std::string& tag, const std::string& desc){
            std::cout << "  " << clr << mv << Color::RESET;
            if (!tag.empty()) std::cout << Color::DIM << " (" << tag << ")" << Color::RESET;
            std::cout << " - " << desc << "\n";
        };
        auto pct     = [](int p){ return std::to_string(p) + "%"; };
        auto justDmg = [](int d){ return std::to_string(d) + " dmg"; };
        auto pctDmg  = [](int p, int d){ return std::to_string(p) + "%, " + std::to_string(d) + " dmg"; };
        auto pctTag  = [](int p, const std::string& s){ return std::to_string(p) + "%, " + s; };
        bool named = true;
        // MELEE
        if      (nameHas("Goblin"))    line(Color::RED, "Jab", justDmg(atk), "a quick strike.");
        else if (nameHas("Bandit"))    line(Color::RED, "Dagger Throw", justDmg(atk), "a hurled blade.");
        else if (nameHas("Raider"))    line(Color::RED, "Bash", justDmg(atk), "a heavy smash.");
        else if (nameHas("Warrior"))   line(Color::RED, "Pierce", justDmg(atk), "a lunge that bypasses half your armor.");
        else if (nameHas("Knight"))    line(Color::ARMOR_CLR, "Shield Bash", justDmg(std::max(1, atk / 2)), "raises armor, then chips you.");
        else if (nameHas("Berserker")) line(Color::RED, "Frenzy", pctDmg(55, atk), "45% chance to grow +2 attack instead.");
        else if (nameHas("Gladiator")) line(Color::RED, "Uppercut", justDmg(atk + 3), "a brutal armor-piercing blow.");
        else if (nameHas("Enforcer"))  line(Color::RED, "Combo Strike", justDmg(atk) + " x2", "hits you twice.");
        // TANK
        else if (nameHas("Guardian"))  line(Color::RED, "Whirlwind", pctDmg(60, atk), "a piercing sweep, or braces.");
        else if (nameHas("Barbarian")) line(Color::ARMOR_CLR, "Iron Skin", pctTag(60, "Armor +" + std::to_string(def + 6)), "big armor (may also weaken you); else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Sentinel"))  line(Color::ARMOR_CLR, "Fortify", pctTag(60, "Armor +" + std::to_string(def + 4)), "stacks armor; else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Warden"))    line(Color::RED, "Smackdown", pctDmg(60, atk + 1), "a solid hit, or braces.");
        else if (nameHas("Paladin"))   line(Color::RED, "Cleave", pctDmg(60, atk + 2), "a piercing strike, or braces.");
        else if (nameHas("Bastion"))   line(Color::ARMOR_CLR, "Wall / Challenge", "45% / 30%", "a wall of +" + std::to_string(def + 8) + " armor, or forces attack-only; else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Fortress"))  line(Color::ARMOR_CLR, "Shield Bash", justDmg(std::max(1, atk / 2)), "armor, then bashes you.");
        else if (nameHas("Orc"))       line(Color::RED, "Body Slam", pctDmg(60, atk + 2), "a crushing blow, or braces.");
        // CASTER
        else if (nameHas("Sage"))      line(Color::BURN_CLR, "Torch", pctTag(70, "Burn 5"), "may heal when low; else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Archon"))    line(Color::BURN_CLR, "Hellfire", pctTag(70, "Burn 12"), "may heal when low; else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Spellmaster"))line(Color::POISON_CLR, "Virulent Plague", pctTag(70, "Poison 14"), "may heal when low; else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Enchanter")) line(Color::CARD_SPECIAL, "Tempt", pct(60), "shrinks your next hand to 3 cards; else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Sorcerer"))  line(Color::BLUE, "Ice Blast", pct(60), "weakens you and thins your next hand; else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Vampire"))   line(Color::MAGENTA, "Vampiric Drain", pctDmg(65, 10), "hits you, heals herself +6, weakens you 2.");
        else if (nameHas("Mystic"))    line(Color::CYAN, "Illusion", pct(50), "becomes untargetable for a turn; else attacks for " + justDmg(atk) + ".");
        // RANGED
        else if (nameHas("Deadeye"))   line(Color::RED, "Dead Shot", justDmg(atk), "a shot that pierces your armor.");
        else if (nameHas("Wyvern"))    line(Color::RED, "Flying Gnash", justDmg(atk + 2), "a piercing dive.");
        else if (nameHas("Omneye"))    line(Color::RED, "Eye-Beam", pctDmg(70, atk + 2), "a piercing beam; else a 30% weakening gaze.");
        else if (nameHas("Assassin"))  line(Color::RED, "Ambush", "45%, " + justDmg(atk), "strikes mid-turn on a random card you play (armor-piercing).");
        else if (nameHas("Falcon"))    line(Color::CYAN, "Gouge", pctDmg(65, atk + 1), "a wind-borne dive that rakes straight past your armor.");
        // BEAST
        else if (nameHas("Wolf"))      line(Color::RED, "Bite", pctDmg(70, atk), "a lunging bite, or braces.");
        else if (nameHas("Spider"))    line(Color::CARD_SPECIAL, "Web Trap", pctTag(50, "Weaken 2"), "else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Serpent"))   line(Color::CARD_SPECIAL, "Entangle", pctTag(50, "Weaken 3"), "else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Basilisk"))  line(Color::MAGENTA, "Curse", pct(40), "lose the run if it isn't dead in 5 turns! Else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Cockatrice"))line(Color::MAGENTA, "Petrifying Bite", pctDmg(35, atk), "bites, then 3 turns to kill it or you turn to stone!");
        else if (nameHas("Manticore")) line(Color::RED, "Twin Maw", justDmg(atk) + " x2", "both heads bite in the same lunge.");
        else if (nameHas("Fleshmass")) line(Color::MAGENTA, "Bind", justDmg(atk), "a landed lash limits you to 1 card next turn.");
        // UNDEAD
        else if (nameHas("Ghoul"))     line(Color::CARD_SPECIAL, "Chomp", justDmg(atk), "bites, heals itself +8, poisons you 3.");
        else if (nameHas("Banshee"))   line(Color::CARD_SPECIAL, "Wailing Scream", "Weaken 2", "weakens you, strengthens herself +2 attack.");
        else if (nameHas("Specter") || nameHas("Wraith")) line(Color::CYAN, "Ghost", pct(50), "becomes untargetable for a turn; else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Moonstruck"))line(Color::STRENGTH_CLR, "Moon Scent", "45%", "works itself into a frenzy: its attacks hit x1.6 harder for 3 turns; else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Revenant"))  line(Color::CYAN, "Parry", "50%", "catches your next blow, halves it and ripostes; else attacks for " + justDmg(atk) + ".");
        else if (nameHas("Lich"))      line(Color::MAGENTA, "Raise Undead", pctTag(45, "Summon (6 atk)"), "summons a skeleton that guards it; else attacks for " + justDmg(atk) + ".");
        else named = false;

        if (named) {
            line(Color::RED, "Basic attack", justDmg(atk), "a plain strike otherwise.");
        } else {
            switch (enemy.getType()) {
                case EnemyType::MELEE:
                    std::cout << "  " << Color::RED     << "Attack"  << Color::RESET << " (70%, " << atk << " dmg) - reduced by your armor\n";
                    std::cout << "  " << Color::ARMOR_CLR << "Defend" << Color::RESET << " (30%)         - gains " << def << " armor\n";
                    break;
                case EnemyType::RANGED:
                    std::cout << "  " << Color::RED      << "Pierce attack"   << Color::RESET << " (60%, " << atk << " dmg) - bypasses half your armor\n";
                    std::cout << "  " << Color::ARMOR_CLR << "Defend"         << Color::RESET << " (20%)         - gains " << std::max(1, def - 1) << " armor\n";
                    std::cout << "  " << Color::CARD_SPECIAL << "Crippling shot" << Color::RESET << " (20%)         - weakens you (-2 dmg for 2 turns)\n";
                    break;
                case EnemyType::TANK:
                    std::cout << "  " << Color::ARMOR_CLR << "Defend" << Color::RESET << " (65%)         - gains " << def << " armor\n";
                    std::cout << "  " << Color::RED       << "Attack" << Color::RESET << " (35%, " << std::max(1, atk - 2) << " dmg) - reduced by your armor\n";
                    break;
                case EnemyType::CASTER:
                    if (enemy.getHealth() < enemy.getMaxHealth() / 3) {
                        std::cout << "  " << Color::HEAL    << "Heal"   << Color::RESET << " (60%)         - recovers ~" << (8 + def / 2) << " HP (low HP)\n";
                        std::cout << "  " << Color::RED      << "Attack" << Color::RESET << " (40%, " << (atk + 1) << " dmg)\n";
                    } else {
                        std::cout << "  " << Color::CARD_SPECIAL << "Poison Bolt" << Color::RESET << " (40%)         - poisons you (3 stacks)\n";
                        std::cout << "  " << Color::CARD_SPECIAL << "Fireball"    << Color::RESET << " (20%)         - burns you (2 turns)\n";
                        std::cout << "  " << Color::RED         << "Attack"      << Color::RESET << " (40%, " << (atk + 1) << " dmg)\n";
                        std::cout << "  " << Color::DIM     << "[Casts Heal instead if HP drops below 33%]" << Color::RESET << "\n";
                    }
                    break;
                case EnemyType::BEAST:
                    std::cout << "  " << Color::RED       << "Attack"         << Color::RESET << " (60%, " << atk << " dmg) - reduced by your armor\n";
                    std::cout << "  " << Color::CARD_SPECIAL << "Venomous bite"  << Color::RESET << " (25%)         - poisons you (3 stacks)\n";
                    std::cout << "  " << Color::ARMOR_CLR  << "Defend"         << Color::RESET << " (15%)         - gains " << std::max(1, def - 1) << " armor\n";
                    break;
                case EnemyType::UNDEAD:
                    std::cout << "  " << Color::RED      << "Attack"         << Color::RESET << " (75%, " << atk << " dmg) - reduced by your armor\n";
                    std::cout << "  " << Color::CARD_SPECIAL << "Chilling touch" << Color::RESET << " (25%)         - weakens you (-2 dmg for 2 turns)\n";
                    break;
            }
        }
    }
    std::cout << "\n";
}

// Mirrors the attack path in playCardFromHand. Kept deliberately close to it:
// if one changes and the other does not, the preview starts lying.
int Game::previewDamage(const Card& c) const {
    if (c.getType() != CardType::ATTACK) return 0;
    if (lichAddAlive) return 0;                       // the skeleton soaks it all
    const bool trueStrike = (c.getEffect() == CardEffect::TRUESTRIKE);
    if (enemyInvulnerable && !trueStrike) return 0;

    const bool pierce = trueStrike || (c.getEffect() == CardEffect::PIERCE);
    const int  hits   = (c.getEffect() == CardEffect::DOUBLE_HIT) ? 2 : 1;

    DamageType weakness = enemy.getWeakness();
    bool hitsWeakness = weakness != DamageType::NONE &&
                        (c.getPhysType() == weakness || c.getPhysType2() == weakness
                         || c.getElemType() == weakness);
    DamageType resistance = enemy.getResistance();
    bool hitsResistance = !trueStrike && resistance != DamageType::NONE &&
                          (c.getPhysType() == resistance || c.getPhysType2() == resistance
                           || c.getElemType() == resistance);

    int dmg = std::max(0, c.getValue() + upgrades.getDamageBonus() + equipDamageBonus);
    dmg = (int)(dmg * playerStatus.getWeakMultiplier() * playerStatus.getStrengthMultiplier());
    if (hitsWeakness)   dmg = (int)(dmg * 1.5);
    if (hitsResistance) dmg = (int)(dmg * 0.5);
    if (enemyParryStance && !trueStrike) dmg = (int)(dmg * 0.5);

    // Armour soaks per hit and is spent as it soaks, same as Enemy::takeDamage.
    int armor = enemy.getArmor(), lost = 0;
    for (int i = 0; i < hits; i++) {
        int dealt = calculateDamage(dmg, pierce ? 0 : enemy.getBaseDefense());
        lost  += std::max(0, dealt - armor);
        armor  = std::max(0, armor - dealt);
    }
    return std::min(lost, enemy.getHealth());
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
    playerEnergy = maxEnergy;
}

void Game::applyCardEffect(const Card& card) {
    int val = card.getValue();
    switch (card.getEffect()) {
        case CardEffect::POISON:
            EnemyArt::printBattleCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::POISON);
            if (applyEnemyStatus(StatusType::POISON, val)) {
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::POISON, true);
                std::cout << "  " << Color::POISON_CLR << "Applied " << val << " Poison to enemy! (" << val << " dmg/turn for 3 turns)" << Color::RESET << "\n";
            }
            break;
        case CardEffect::BURN:
            EnemyArt::printBattleCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::BURN);
            if (applyEnemyStatus(StatusType::BURN, val)) {
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::BURN, true);
                std::cout << "  " << Color::BURN_CLR << "Applied " << val << " Burn to enemy! (" << val << " dmg/turn for 3 turns)" << Color::RESET << "\n";
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
        case CardEffect::HEAL: {
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::HEAL);
            int before = playerHealth;
            {
                int before = playerHealth;
                playerHealth = std::min(maxPlayerHealth, playerHealth + val);
                if (playerHealth > before)
                    EnemyArt::popNumber(playerHealth - before, false, EnemyArt::PopKind::HEAL);
            }
            std::cout << "  " << Color::HEAL << "Recovered " << (playerHealth - before) << " HP!" << Color::RESET
                      << " (" << playerHealth << "/" << maxPlayerHealth << ")\n";
            break;
        }
        case CardEffect::STRENGTH: {
            // Same rarity scaling as Bloodlust's inline version (Uncommon 1.2x, Rare 2x,
            // Super Rare 3x) - this path is for pure-buff SPECIAL cards like Strengthen.
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::STRENGTH);
            double buff = card.isSuperRare() ? 3.0 : card.isRare() ? 2.0 : 1.2;
            playerStatus.apply(StatusType::STRENGTH, 2, 1.5, buff);
            std::cout << "  " << Color::STRENGTH_CLR << "Strength surges! x" << buff << " damage for 2 turns!" << Color::RESET << "\n";
            break;
        }
        case CardEffect::TAUNT:
            enemyTauntTurns = 2;
            std::cout << "  " << Color::RED << "You taunt the enemy! They're much more likely to attack for their next 2 turns." << Color::RESET << "\n";
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
    if (enemyStatusWardActive) {
        enemyStatusWardActive = false;
        std::cout << "  " << Color::CYAN << "The shadow's guard blocks the ailment!" << Color::RESET << "\n";
        return false;
    }
    enemy.applyStatus(type, amount, weakMultiplier);
    return true;
}

// Silent when warded - callers already have their own "resisted" fallback message.
bool Game::tryStunEnemy() {
    if (enemyStatusWardActive) {
        enemyStatusWardActive = false;
        return false;
    }
    return enemy.tryApplyStun();
}

void Game::refreshBattleAuras() {
    EnemyArt::AuraFlags knight, foe;
    knight.strength = playerStatus.hasStrength();
    knight.weak     = playerStatus.hasWeak();
    knight.poison   = playerStatus.hasPoison();
    knight.burn     = playerStatus.hasBurn();
    knight.stun     = playerStatus.hasStun();
    foe.strength = enemy.hasStrength();   // Moon Scent: glows red, as the knight does
    foe.weak   = enemy.hasWeak();
    foe.poison = enemy.hasPoison();
    foe.burn   = enemy.hasBurn();
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
        if (!spendEnergy(card.getCost())) return;

        Card playedCard = playerDeck.playCard(index - 1);

        cardsPlayedThisTurn++;
        lastActionWasCardPlay = true;
        lastPlayedCardType = playedCard.getType();
        lastPlayedPhysType = playedCard.getPhysType();
        lastPlayedPhysType2 = playedCard.getPhysType2();

        std::cout << "Played: [" << playedCard.getName() << "] (Cost: " << playedCard.getCost() << ")\n";
        // Legendaries announce themselves. Stance cards are the exception:
        // playing one only arms it, and the cue belongs on the payoff, so
        // those fire it when they actually resolve instead.
        if (playedCard.isLegendary() && playedCard.getEffect() != CardEffect::COUNTER
                                     && playedCard.getEffect() != CardEffect::PARRY)
            Audio::playSFX("legendary");

        if (playedCard.getType() == CardType::ATTACK) {
            // Reckoning: ignores defense like PIERCE, and additionally refuses
            // every reduction the enemy can put in the way.
            bool trueStrike     = (playedCard.getEffect() == CardEffect::TRUESTRIKE);
            bool pierce         = trueStrike || (playedCard.getEffect() == CardEffect::PIERCE);
            bool doubleHit      = (playedCard.getEffect() == CardEffect::DOUBLE_HIT);
            int  hits           = doubleHit ? 2 : 1;
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

            // Revenant parry: this blow is half-deflected, and it ripostes afterward.
            bool revenantParried = enemyParryStance && !trueStrike;
            if (enemyParryStance && !trueStrike) {
                enemyParryStance = false;
                std::cout << "  " << Color::MAGENTA << "The Revenant parries, catching your blow!" << Color::RESET << "\n";
            }
            double parryFactor = revenantParried ? 0.5 : 1.0;

            for (int hitNum = 1; hitNum <= hits && (lichAddAlive || enemy.isAlive()); hitNum++) {
                int bonusDamage  = std::max(0, playedCard.getValue() + upgrades.getDamageBonus() + equipDamageBonus);
                bonusDamage = (int)(bonusDamage * weakMult * strengthMult);
                if (hitsWeakness) bonusDamage = (int)(bonusDamage * 1.5);
                if (hitsResistance) bonusDamage = (int)(bonusDamage * 0.5);
                bonusDamage = (int)(bonusDamage * parryFactor);

                std::string hitLabel = (hits > 1 ? ("Hit " + std::to_string(hitNum) + ": dealt ") : "Dealt ");
                auto printTags = [&]() {
                    if (hitsWeakness)      std::cout << " " << Color::YELLOW << "[Weakness! x1.5]" << Color::RESET;
                    if (hitsResistance)    std::cout << " " << Color::DIM << "[Resisted x0.5]" << Color::RESET;
                    if (weakMult < 1.0)    std::cout << " " << Color::WEAK_CLR << "[Weakened]" << Color::RESET;
                    if (strengthMult > 1.0)std::cout << " " << Color::STRENGTH_CLR << "[Strength x" << strengthMult << "]" << Color::RESET;
                    if (trueStrike)        std::cout << " " << Color::MAGENTA << "[Unstoppable]" << Color::RESET;
                    else if (pierce)       std::cout << " " << Color::MAGENTA << "[Armor-Piercing]" << Color::RESET;
                    if (revenantParried)   std::cout << " " << Color::MAGENTA << "[Parried x0.5]" << Color::RESET;
                };

                if (lichAddAlive) {
                    // The summoned skeleton bodyguards the Lich - it soaks direct hits (no armor) until cut down.
                    int before = lichAddHp;
                    lichAddHp = std::max(0, lichAddHp - std::max(0, bonusDamage));
                    int lost = before - lichAddHp;
                    EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), playedCard.getElemType(), lost > 0);
                    EnemyArt::popNumber(lost, true, EnemyArt::PopKind::DAMAGE);
                    Audio::playSFX(lichAddHp <= 0 ? "dead" : "attack");
                    std::cout << "  " << Color::PLAYER_ATTACK << hitLabel << lost << " damage to the summoned skeleton!"
                              << Color::RESET << " (Skeleton HP: " << lichAddHp << "/" << lichAddMaxHp << ")";
                    printTags();
                    std::cout << "\n";
                    if (lichAddHp <= 0) { lichAddAlive = false; EnemyArt::setCompanion(""); std::cout << "  " << Color::MAGENTA << "The summoned skeleton crumbles to dust!" << Color::RESET << "\n"; }
                } else if (enemyInvulnerable && !trueStrike) {
                    EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), playedCard.getElemType(), false);
                    std::cout << "  " << Color::DIM << "Your attack passes through the phased form. No damage!" << Color::RESET << "\n";
                } else {
                    int defenseValue = pierce ? 0 : enemy.getBaseDefense();
                    int damageDealt  = calculateDamage(bonusDamage, defenseValue);
                    int hpBefore = enemy.getHealth();
                    enemy.takeDamage(damageDealt);
                    int hpLost = hpBefore - enemy.getHealth();
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

                    // 10% chance per hit to also inflict the matching ailment.
                    if (enemy.isAlive() && (playedCard.getElemType() == DamageType::POISON || playedCard.getElemType() == DamageType::FIRE)) {
                        std::random_device rd;
                        std::mt19937 gen(rd());
                        if (std::uniform_int_distribution<>(1, 100)(gen) <= 10) {
                            if (playedCard.getElemType() == DamageType::POISON) {
                                if (applyEnemyStatus(StatusType::POISON, 3)) {
                                    EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::POISON, true);
                                    std::cout << "  " << Color::POISON_CLR << "The venom seeps in! Poisoned for 3 dmg/turn." << Color::RESET << "\n";
                                }
                            } else {
                                if (applyEnemyStatus(StatusType::BURN, 3)) {
                                    EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::BURN, true);
                                    std::cout << "  " << Color::BURN_CLR << "The flames catch! Burning for 3 dmg/turn." << Color::RESET << "\n";
                                }
                            }
                        }
                    }
                }
                // let each hit's sound finish before the next one cuts in
                if (doubleHit && hitNum < hits) UIHelper::pause(300);
            }

            if (revenantParried && playerHealth > 0 && enemy.isAlive()) {
                std::cout << "  " << Color::MAGENTA << "The Revenant ripostes!" << Color::RESET << "\n";
                UIHelper::pause(150);
                enemyStrikePlayer(std::max(1, enemy.getBaseAttack() / 2), false, enemy.getWeakMultiplier());
            }

            if (playedCard.getEffect() == CardEffect::STRENGTH) {
                // Scales with rarity, same idea as Weaken/Shatter/Sunder: Uncommon 1.2x,
                // Rare 2x, Super Rare 3x (only Strengthen and Bloodlust use this today).
                EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::STRENGTH);
                double strengthBuff = playedCard.isSuperRare() ? 3.0 : playedCard.isRare() ? 2.0 : 1.2;
                playerStatus.apply(StatusType::STRENGTH, 2, 1.5, strengthBuff);
                std::cout << "  " << Color::STRENGTH_CLR << "Strength surges! x" << strengthBuff << " damage for 2 turns!" << Color::RESET << "\n";
            }
        } else if (playedCard.getType() == CardType::DEFEND) {
            int bonusArmor = playedCard.getValue() + upgrades.getArmorBonus() + equipArmorBonus;
            playerArmor += bonusArmor;
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
                    std::cout << "  " << Color::DIM << "The shield's edge passes through the phased form. No damage." << Color::RESET << "\n";
                } else {
                int chipDmg = 3;
                int hpBefore = enemy.getHealth();
                enemy.takeDamageRaw(chipDmg);
                int hpLost = hpBefore - enemy.getHealth();
                Audio::playSFX(!enemy.isAlive() ? deathSfx(enemy.isBoss()) : "hit");
                std::cout << "  " << Color::PLAYER_ATTACK << "The shield's edge bites, dealing " << hpLost << " damage!"
                          << Color::RESET << " (Enemy HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                }
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

// One regular-enemy attack resolved against the player: armor, Dodge Reversal /
// Parry interception, then damage. weakMult is passed in (read once per enemy
// turn, before processWeak ticks) rather than re-read here. No boss second-wind -
// that's bossStrikesPlayer's job.
void Game::enemyStrikePlayer(int atk, bool pierceHalfArmor, double weakMult) {
    // Weak scales it down, Strength scales it up - the mirror of what the
    // player's own two buffs do to their attacks.
    atk = (int)(atk * weakMult * enemy.getStrengthMultiplier());
    EnemyArt::printBattleAttack(enemy.getType(), enemy.getBossType(), playerArmor > 0);
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
            bool tooFarToRiposte = !enemy.isBoss() && enemy.getType() == EnemyType::RANGED;
            if (tooFarToRiposte) {
                Audio::playSFX("special");
                std::cout << Color::CYAN << "Parry! You block the shot. No damage taken, but they're too far away to riposte."
                          << Color::RESET << "\n";
                UIHelper::pause(300);
                return;
            }
            int riposteDmg = (int)((atk * 1.5 + parryBonusValue) * playerStatus.getStrengthMultiplier());
            int hpBefore = enemy.getHealth();
            enemy.takeDamage(riposteDmg); // ignores defense - takeDamage only accounts for armor
            int hpLost = hpBefore - enemy.getHealth();
            EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), DamageType::NONE, hpLost > 0);
            EnemyArt::popNumber(hpLost, true, EnemyArt::PopKind::DAMAGE);
            bool stunned = tryStunEnemy();
            if (stunned && enemy.isAlive())
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::STUN, true);
            Audio::playSFX(!enemy.isAlive() ? deathSfx(enemy.isBoss()) : "special");
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
    int effectiveArmor = pierceHalfArmor ? (playerArmor / 2) : playerArmor;
    int actualDamage = atk - effectiveArmor;
    if (actualDamage < 0) actualDamage = 0;
    playerArmor -= (pierceHalfArmor ? atk / 2 : atk);
    if (playerArmor < 0) playerArmor = 0;
    playerHealth -= actualDamage;
    if (playerHealth < 0) playerHealth = 0;
    if (actualDamage > 0) {
        EnemyArt::printBattleKnightHit(enemy.getType(), enemy.getBossType());
        EnemyArt::popNumber(actualDamage, false, EnemyArt::PopKind::DAMAGE);
    }
    Audio::playSFX("hit");
    std::cout << Color::DAMAGE << "Enemy attacks for " << actualDamage << " damage!" << Color::RESET;
    if (weakMult < 1.0)
        std::cout << " " << Color::WEAK_CLR << "[Weakened]" << Color::RESET;
    std::cout << "  HP: " << hpColor(playerHealth, maxPlayerHealth)
              << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
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
    enemyStrikePlayer(enemy.getBaseAttack(), true, enemy.getWeakMultiplier());
    refreshBattleAuras();
}

// Re-arms per-turn enemy mechanics at the start of each player turn.
void Game::armPerTurnEnemyMechanics() {
    assassinAmbushArmed = enemy.isAlive() && enemy.getName().find("Assassin") != std::string::npos;
}

void Game::enemyTurn() {
    if (!enemy.isAlive()) return;

    refreshBattleAuras();

    // Wipe armor from 2 turns ago (i.e. left over from the enemy's own last turn) - not
    // from this one. A Defend action below can grant fresh armor that then survives
    // through the player's entire next turn, which is the whole point of Defending.
    enemy.resetArmor();

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
    int roll = rollDist(gen);

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
    auto doAttack = [&](int atk, bool pierceHalfArmor) {
        if (enemy.hasStun()) {
            if (!volleyBroken) {
                volleyBroken = true;
                std::cout << "  " << Color::CYAN
                          << "The stun lands mid-swing. The rest of the assault never comes."
                          << Color::RESET << "\n";
            }
            return;
        }
        enemyStrikePlayer(atk, pierceHalfArmor, weakMult);
    };

    auto doDefend = [&](int amt) {
        enemy.gainArmor(amt);
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
    int atk = enemy.getBaseAttack();
    int def = enemy.getBaseDefense();

    // --- Per-name signature moves. Reuse the doAttack/doDefend/applyPlayerStatus
    // helpers in scope; return when handled, else fall through to the generic
    // type behavior below. Enemies without a signature (Wizard, Skeleton, Archer,
    // Falcon, and the unchanged beasts) simply aren't matched here.
    auto nameHas = [&](const char* k){ return enemy.getName().find(k) != std::string::npos; };
    auto themed  = [&](const char* msg){ UIHelper::typeWrite(std::string(Color::MAGENTA) + msg + Color::RESET + "\n"); UIHelper::pause(150); };

    // MELEE
    if (nameHas("Goblin"))    { themed("Goblin jabs at you!"); doAttack(atk, false); return; }
    if (nameHas("Bandit"))    { themed("Bandit hurls a dagger!"); doAttack(atk, false); return; }
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
        if (!taunted && roll < 45) {
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

    // TANK
    if (nameHas("Guardian"))  { if (taunted || roll < 60) { themed("Guardian sweeps a WHIRLWIND!"); doAttack(atk, true); } else doDefend(def); return; }
    if (nameHas("Barbarian")) {
        if (taunted) { doAttack(atk, false); return; }
        if (roll < 60) {
            enemy.gainArmor(def + 6);
            std::cout << Color::ARMOR_CLR << "Barbarian hardens its IRON SKIN (+" << (def + 6) << " armor)!" << Color::RESET << "\n";
            UIHelper::pause(150);
            if (roll < 30) applyPlayerStatus(StatusType::WEAK, 2);
            return;
        }
        doAttack(atk, false); return;
    }
    if (nameHas("Sentinel")) {
        if (taunted) { doAttack(atk, false); return; }
        if (roll < 60) { enemy.gainArmor(def + 4); std::cout << Color::ARMOR_CLR << "Sentinel FORTIFIES (+" << (def + 4) << " armor)!" << Color::RESET << "\n"; UIHelper::pause(150); }
        else doAttack(atk, false);
        return;
    }
    if (nameHas("Warden"))    { if (taunted || roll < 60) { themed("Warden delivers a SMACKDOWN!"); doAttack(atk + 1, false); } else doDefend(def); return; }
    if (nameHas("Paladin"))   { if (taunted || roll < 60) { themed("Paladin CLEAVES through your guard!"); doAttack(atk + 2, true); } else doDefend(def); return; }
    if (nameHas("Bastion")) {
        if (taunted) { doAttack(atk, false); return; }
        if (roll < 45) {
            enemy.gainArmor(def + 8);
            std::cout << Color::ARMOR_CLR << "Bastion raises an impenetrable wall (+" << (def + 8)
                      << " armor)!" << Color::RESET << "\n";
            UIHelper::pause(150);
        } else if (roll < 75) {
            // The challenge is the point of the armor: it stops you waiting
            // the wall out behind your own guard.
            playerAttackOnly = true;
            Audio::playSFX("special");
            std::cout << Color::RED << "Bastion HAMMERS its shield and dares you to break it!" << Color::RESET
                      << " Next turn you can only play " << Color::CARD_ATTACK << "ATTACK" << Color::RESET << " cards.\n";
            UIHelper::pause(250);
        } else doAttack(atk, false);
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
    if (nameHas("Orc"))       { if (taunted || roll < 60) { themed("Orc hurls a crushing BODY SLAM!"); doAttack(atk + 2, false); } else doDefend(def); return; }

    // CASTER (still leans on the low-HP heal instinct)
    if (nameHas("Sage")) {
        if (enemy.getHealth() < enemy.getMaxHealth() / 3 && roll < 40) { int h = 8 + def / 2; enemy.heal(h); std::cout << Color::HEAL << "Sage channels a healing light (+" << h << " HP)." << Color::RESET << "\n"; UIHelper::pause(200); }
        else if (!taunted && roll < 70) { applyPlayerStatus(StatusType::BURN, 5); Audio::playSFX("fire"); std::cout << Color::BURN_CLR << "Sage hurls a TORCH! Burn 5." << Color::RESET << "\n"; UIHelper::pause(250); }
        else doAttack(atk, false);
        return;
    }
    if (nameHas("Archon")) {
        if (enemy.getHealth() < enemy.getMaxHealth() / 3 && roll < 35) { int h = 10 + def / 2; enemy.heal(h); std::cout << Color::HEAL << "Archon mends itself (+" << h << " HP)." << Color::RESET << "\n"; UIHelper::pause(200); }
        else if (!taunted && roll < 70) { applyPlayerStatus(StatusType::BURN, 12); Audio::playSFX("fire"); std::cout << Color::BURN_CLR << "Archon calls down HELLFIRE! Burn 12." << Color::RESET << "\n"; UIHelper::pause(250); }
        else doAttack(atk, false);
        return;
    }
    if (nameHas("Spellmaster")) {
        if (enemy.getHealth() < enemy.getMaxHealth() / 3 && roll < 35) { int h = 10 + def / 2; enemy.heal(h); std::cout << Color::HEAL << "Spellmaster mends itself (+" << h << " HP)." << Color::RESET << "\n"; UIHelper::pause(200); }
        else if (!taunted && roll < 70) { applyPlayerStatus(StatusType::POISON, 14); Audio::playSFX("poison"); std::cout << Color::POISON_CLR << "Spellmaster spreads a VIRULENT PLAGUE! Poison 14." << Color::RESET << "\n"; UIHelper::pause(250); }
        else doAttack(atk, false);
        return;
    }
    if (nameHas("Enchanter")) {
        if (!taunted && roll < 60) {
            nextHandPenalty = 2;
            Audio::playSFX("special");
            std::cout << Color::MAGENTA << "Enchanter TEMPTS you into hesitation." << Color::RESET
                      << " Your next hand is only " << Color::CYAN << "3 cards" << Color::RESET << ".\n";
            UIHelper::pause(250);
        } else doAttack(atk, false);
        return;
    }
    if (nameHas("Sorcerer")) {
        if (!taunted && roll < 60) {
            applyPlayerStatus(StatusType::WEAK, 2);
            nextHandPenalty = std::max(nextHandPenalty, 1);
            Audio::playSFX("special");
            std::cout << Color::BLUE << "Sorcerer hurls an ICE BLAST!" << Color::RESET
                      << " You are " << Color::WEAK_CLR << "Weakened 2" << Color::RESET
                      << " and your next hand loses a card.\n";
            UIHelper::pause(250);
        } else doAttack(atk, false);
        return;
    }
    if (nameHas("Vampire")) {
        if (taunted) { doAttack(atk, false); return; }
        if (roll < 65) {
            std::cout << Color::MAGENTA << "Vampire sinks in a VAMPIRIC DRAIN!" << Color::RESET << "\n";
            UIHelper::pause(150);
            doAttack(10, false);
            if (enemy.isAlive()) {
                enemy.heal(6); enemy.addBonusAttack(1);
                std::cout << Color::HEAL << "She drinks deep, mending herself (+6 HP) and growing stronger (+1 attack)." << Color::RESET << "\n";
            }
            if (playerHealth > 0) applyPlayerStatus(StatusType::WEAK, 2);
            UIHelper::pause(200);
        } else doAttack(atk, false);
        return;
    }
    if (nameHas("Mystic")) {
        if (!taunted && roll < 50 && !enemyInvulnerable) {
            enemyInvulnerable = true;
            Audio::playSFX("special");
            std::cout << Color::CYAN << "Mystic weaves an ILLUSION, splitting into fading copies." << Color::RESET
                      << " It takes no damage next turn.\n";
            UIHelper::pause(250);
        } else doAttack(atk, false);
        return;
    }

    // RANGED
    if (nameHas("Deadeye")) { themed("Deadeye lines up a DEAD SHOT!"); doAttack(atk, true); return; }
    if (nameHas("Wyvern"))  { themed("Wyvern dives with a FLYING GNASH!"); doAttack(atk + 2, true); return; }
    if (nameHas("Omneye")) {
        if (!taunted && roll < 30) { applyPlayerStatus(StatusType::WEAK, 2); std::cout << Color::WEAK_CLR << "Omneye's gaze unsettles you. Weakened 2." << Color::RESET << "\n"; UIHelper::pause(200); return; }
        themed("Omneye fires a searing eye-beam!"); doAttack(atk + 2, true); return;
    }

    if (nameHas("Falcon")) {
        // Gouge rides the dive. Enemy attacks carry no elemental tag in this game,
        // so the wind shows up where it would actually matter: the gust opens your
        // guard and the talons get in past armor.
        if (taunted || roll < 65) { themed("The falcon stoops and GOUGES on a howling gust!"); doAttack(atk + 1, true); }
        else doAttack(atk, false);
        return;
    }

    // BEAST
    if (nameHas("Wolf"))    { if (roll < 70) { themed("Wolf lunges with a BITE!"); doAttack(atk, false); } else doDefend(std::max(1, def - 1)); return; }
    if (nameHas("Spider"))  { if (!taunted && roll < 50) { applyPlayerStatus(StatusType::WEAK, 2); std::cout << Color::WEAK_CLR << "Spider snares you in a WEB TRAP! Weakened 2." << Color::RESET << "\n"; UIHelper::pause(200); } else doAttack(atk, false); return; }
    if (nameHas("Serpent")) { if (!taunted && roll < 50) { applyPlayerStatus(StatusType::WEAK, 3); std::cout << Color::WEAK_CLR << "Serpent ENTANGLES you! Weakened 3." << Color::RESET << "\n"; UIHelper::pause(200); } else doAttack(atk, false); return; }
    if (nameHas("Basilisk")) {
        if (!taunted && curseTurnsLeft == 0 && roll < 40) {
            curseTurnsLeft = 5;
            Audio::playSFX("special");
            std::cout << Color::BOLD << Color::MAGENTA << "Basilisk fixes you with a petrifying CURSE!" << Color::RESET << "\n"
                      << "  " << Color::RED << "Defeat it within 5 turns or turn to stone." << Color::RESET << "\n";
            UIHelper::pause(300);
        } else doAttack(atk, false);
        return;
    }
    if (nameHas("Cockatrice")) {
        // Petrifying Bite draws blood and starts the clock. It reuses the Basilisk
        // curse slot, so a countdown can never be stacked twice, and the == 0 guard
        // means one cockatrice only ever casts it once per fight.
        if (!taunted && curseTurnsLeft == 0 && roll < 35) {
            themed("Cockatrice sinks in a PETRIFYING BITE!");
            doAttack(atk, false);
            if (playerHealth > 0) {
                curseTurnsLeft = 3;
                Audio::playSFX("special");
                std::cout << "  " << Color::BOLD << Color::MAGENTA << "Stone creeps out from the wound!" << Color::RESET << "\n"
                          << "  " << Color::RED << "Defeat it within 3 turns or turn to stone." << Color::RESET << "\n";
                UIHelper::pause(300);
            }
        } else doAttack(atk, false);
        return;
    }
    if (nameHas("Manticore")) {
        // Two heads, two bites. enemyStrikePlayer already closes on a 350ms beat,
        // so the hits read as separate without an extra pause between them.
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
            Audio::playSFX("special");
            std::cout << Color::BOLD << Color::MAGENTA << "Tentacles coil around you! BOUND:" << Color::RESET
                      << " you can only play one card next turn.\n";
            UIHelper::pause(250);
        }
        return;
    }

    // UNDEAD
    if (nameHas("Ghoul")) {
        if (taunted) { doAttack(atk, false); return; }
        themed("Ghoul CHOMPS down, feeding on you!");
        doAttack(atk, false);
        if (enemy.isAlive()) { int h = 8; enemy.heal(h); std::cout << Color::HEAL << "Ghoul heals " << h << " HP from the bite." << Color::RESET << "\n"; }
        if (playerHealth > 0) applyPlayerStatus(StatusType::POISON, 3);
        UIHelper::pause(200);
        return;
    }
    if (nameHas("Banshee")) {
        if (taunted) { doAttack(atk, false); return; }
        applyPlayerStatus(StatusType::WEAK, 2);
        enemy.addBonusAttack(2);
        std::cout << Color::MAGENTA << "Banshee looses a WAILING SCREAM!" << Color::RESET
                  << " You are " << Color::WEAK_CLR << "Weakened 2" << Color::RESET
                  << " and she grows stronger (+2 attack).\n";
        UIHelper::pause(250);
        return;
    }
    if (nameHas("Specter") || nameHas("Wraith")) {
        if (!taunted && roll < 50 && !enemyInvulnerable) {
            enemyInvulnerable = true;
            Audio::playSFX("special");
            std::cout << Color::CYAN << "The spirit turns GHOSTLY, fading half out of sight." << Color::RESET
                      << " It takes no damage next turn.\n";
            UIHelper::pause(250);
        } else doAttack(atk, false);
        return;
    }
    // One trick, used often. The taunt it used to share this slot with now
    // belongs to the Bastion, where it plays off that wall of armor.
    if (nameHas("Revenant")) {
        if (taunted) { doAttack(atk, false); return; }
        if (roll < 50) {
            enemyParryStance = true;
            Audio::playSFX("special");
            std::cout << Color::CYAN << "Revenant raises a PARRY stance, ready to catch your next blow." << Color::RESET << "\n";
            UIHelper::pause(250);
        } else doAttack(atk, false);
        return;
    }
    // Moon Scent. It works itself up rather than reaching for you: the same
    // effect the player's Strengthen grants, on the other side of the field.
    // Only cast when the last one has lapsed, so it tops out at x1.6 instead
    // of riding a permanent buff.
    if (nameHas("Moonstruck")) {
        if (!taunted && !enemy.hasStrength() && roll < 45) {
            enemy.applyStatus(StatusType::STRENGTH, 3, 1.5, 1.6);
            Audio::playSFX("special");
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(),
                                          EnemyArt::SelfGlow::STRENGTH);
            std::cout << Color::BOLD << Color::MAGENTA << "The Moonstruck takes your MOON SCENT!"
                      << Color::RESET << " Its blows hit " << Color::STRENGTH_CLR << "x1.6"
                      << Color::RESET << " harder for 3 turns.\n";
            UIHelper::pause(300);
        } else doAttack(atk, false);
        return;
    }
    if (nameHas("Lich")) {
        bool skeletonActs = lichAddAlive; // a just-summoned skeleton waits a turn before it swings
        if (!taunted && !lichAddAlive && roll < 45) {
            lichAddMaxHp = 24; lichAddHp = 24; lichAddAtk = 6;
            lichAddAlive = true;
            EnemyArt::setCompanion("Skeleton");
            Audio::playSFX("special");
            std::cout << Color::BOLD << Color::MAGENTA << "Lich RAISES an undead skeleton to fight at its side!" << Color::RESET
                      << " (Skeleton HP: " << lichAddHp << "/" << lichAddMaxHp << ")\n";
            UIHelper::pause(300);
        } else {
            doAttack(atk, false);
        }
        if (skeletonActs && lichAddAlive && playerHealth > 0 && enemy.isAlive()) {
            std::cout << Color::MAGENTA << "The summoned skeleton claws at you!" << Color::RESET << "\n";
            UIHelper::pause(150);
            enemyStrikePlayer(lichAddAtk, false, 1.0);
        }
        return;
    }

    // Everything above is named. Everything below is the fallback, and three
    // roster entries still land here: Wizard, Skeleton and Archer have no
    // nameHas branch of their own. TODO: give them one - they are the flattest
    // fights in the game.
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
                applyPlayerStatus(StatusType::WEAK, 2);
                Audio::playSFX("special");
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
                applyPlayerStatus(StatusType::POISON, 3);
                Audio::playSFX("poison");
                std::cout << Color::POISON_CLR << "Enemy casts Poison Bolt! You are poisoned for 3 stacks." << Color::RESET << "\n";
            } else if (roll < 60) {
                applyPlayerStatus(StatusType::BURN, 2);
                Audio::playSFX("fire");
                std::cout << Color::BURN_CLR << "Enemy casts Fireball! You are burning for 2 turns." << Color::RESET << "\n";
            } else {
                doAttack(atk + 1, false);
            }
            break;
        case EnemyType::BEAST:
            if (roll < 60) {
                doAttack(atk, false);
            } else if (roll < 85) {
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
                applyPlayerStatus(StatusType::WEAK, 2);
                Audio::playSFX("special");
                std::cout << Color::WEAK_CLR << "Enemy's chilling touch saps your strength! Weakened for 2 turns." << Color::RESET << "\n";
            }
            break;
        default:
            doAttack(atk, false);
    }
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
}

void Game::endPlayerTurn() {
    playerTurnActive = false;

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

        // Tick player status effects (start of player's new turn)
        // Second wind has to be offered here too. It used to be checked only in
        // bossStrikesPlayer(), so a poison or burn tick that reduced you to 0
        // killed you outright with the save still unspent - which is exactly
        // the case a player at 1 HP hits, since any tick at all is lethal
        // there. That read as "last stand doesn't work".
        int playerPoisonDmg = playerStatus.processPoison();
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
        // WEAK/STRENGTH tick at end of player turn (after all attacks are resolved)
        playerStatus.processWeak();
        playerStatus.processStrength();

        // Discard remaining hand cards and draw a fresh hand for next turn
        // (Tempt/Ice Blast shrink the next hand via nextHandPenalty).
        playerDeck.resetDeck();
        int drawCount = std::max(1, 5 + upgrades.getDrawBonus() - nextHandPenalty);
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
    playerBoundTurn = fleshmassBindPending; // Fleshmass Bind lands on the turn after the lash
    fleshmassBindPending = false;
    cardsPlayedThisTurn = 0;
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
        std::cout << "You were defeated! Better luck next time.\n";
    } else if (!enemy.isAlive()) {
        UIHelper::printGameOverScreen(true, currentRun.getEncountersWon(), runStats.getTotalCardsCollected());
        std::cout << "Enemy defeated! Onward to the next encounter!\n";
    }
}

bool Game::handleGameOverInput() {
    // Clear first: the picker draws over the console rather than replacing
    // it, so the run summary printed above would show through the buttons.
    UIHelper::clearScreen();
    std::vector<CardBar::Action> overActs{
        CardBar::Action{ "Play again", false },
        CardBar::Action{ "Quit", false },
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
    for (const Card& c : allCards) widgets.push_back(toWidget(c, c.getValue()));
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

// Centred yes/no, drawn like every other choice in the game. These used to
// print into the top left of whatever screen happened to be up, which looked
// like a stray fragment of the old terminal UI sitting over the new one.
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
        h.encounter = inSecretEncounter ? std::string("???")
            : currentRun.isBossEncounter() ? std::string("BOSS")
            : "Encounter " + std::to_string(currentRun.getCurrentEncounter());

        h.playerHp = playerHealth; h.playerMax = maxPlayerHealth;
        h.playerArmor = playerArmor;
        int totalDmg = upgrades.getDamageBonus() + equipDamageBonus;
        int totalArm = upgrades.getArmorBonus()  + equipArmorBonus;
        // Shown as a permanent readout beside the bar, the way the enemy's
        // ATK/DEF are - they used to appear only as tags once non-zero.
        h.playerAtk = totalDmg;
        h.playerDef = totalArm;
        // Colour is kept, not stripped: the panel renders the escapes, so an
        // ailment reads in its own colour exactly as it does in the log.
        std::string ptags;
        if (playerArmorPersistTurns > 0)
            ptags += std::string(Color::CYAN) + "[Fortified "
                   + std::to_string(playerArmorPersistTurns) + "] " + Color::RESET;
        ptags += playerStatus.summary();
        h.playerTags = ptags;

        h.enemyName = enemy.getName();
        h.enemyHp = enemy.getHealth(); h.enemyMax = enemy.getMaxHealth();
        h.enemyAtk = enemy.getBaseAttack();
        h.enemyDef = enemy.getBaseDefense() + enemy.getArmor();
        std::string etags = enemy.statusSummary();
        if (enemyInvulnerable) etags += std::string(" ") + Color::CYAN + "[Phased: immune]" + Color::RESET;
        h.enemyTags = etags;

        if (curseTurnsLeft > 0)
            h.notice = "CURSED - turn to stone in " + std::to_string(curseTurnsLeft)
                     + (curseTurnsLeft == 1 ? " turn" : " turns")
                     + " unless the " + enemy.getName() + " falls";
        else if (playerAttackOnly) h.notice = "TAUNTED - attack cards only this turn";
        else if (playerBoundTurn)  h.notice = std::string("BOUND - one card this turn")
                                            + (cardsPlayedThisTurn >= 1 ? " (spent)" : "");

        h.addActive = lichAddAlive;
        h.addName   = "Skeleton";
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
    // Deliberately NOT clearing: the panel and hand are drawn every frame now,
    // so the text region is purely the combat log. Wiping it each turn left the
    // log empty while you chose a card - the last thing that happened is
    // exactly what you want to see at that moment.
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
    int dmgBonus  = upgrades.getDamageBonus() + equipDamageBonus;
    int armBonus  = upgrades.getArmorBonus()  + equipArmorBonus;
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
            cantAfford = ci.getCost() > playerEnergy;
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
            int dispVal = c.getValue();
            if      (c.getType() == CardType::ATTACK) dispVal = (int)(std::max(0, dispVal + dmgBonus) * weakMult * strengthMult);
            else if (c.getType() == CardType::DEFEND) dispVal = dispVal + armBonus;

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
                + " cost:" + Color::ENERGY_CLR + std::to_string(c.getCost()) + Color::RESET
                + "  " + valLabel + ":" + Color::GREEN + std::to_string(dispVal) + Color::RESET;

            CardBar::Card w;
            w.name     = c.getName();
            w.effect   = cardFaceLine(c, dispVal);
            w.typeLabel = c.getTypeString();
            w.elemTag   = c.getTypeTag();   // [Smash] / [Pierce][Wind] / ...
            w.cost     = c.getCost();
            w.rare     = c.isRare() || c.isSuperRare() || c.isLegendary();
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
                                c.isSuperRare() ? "SUPER RARE" : c.isRare() ? "RARE" : "",
                                c.getUpgradeCount());
        }
        return;   // redraw the turn cleanly rather than resuming a stale layout
    }
    if (choice < 0) return;

    // Capture wraps only the branches where something actually happens. It used
    // to be switched on here and left on, so the rest site, the forge and every
    // reward menu poured their card lists into the log.
    if (choice < handCount) {
        Console::setHistoryCapture(true);
        playCardFromHand(choice + 1);
        if (checkGameOver()) { Console::setHistoryCapture(false); return; }
        UIHelper::pause(600);  // let the card result stay visible before redraw
        // A stun can only be pending here if something that just happened
        // applied it: endPlayerTurn() consumes any stun from the enemy
        // turn before handing control back. Spending it now means losing
        // the rest of this turn and no more than that.
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
    } else {
        displayActionLog();
    }
}

Enemy Game::generateBossEnemy() {
    int bossHealth  = currentRun.getEnemyHealth() * 2;
    int bossAttack  = currentRun.getEnemyAttack() + 4;
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

    if (btype == BossType::SHADOW_KNIGHT) bossHealth = bossHealth * 5 / 4;

    int cycle = currentRun.getCycle();
    if (cycle == 1) name = "Ancient " + name;
    else if (cycle >= 2) name = "Eternal " + name;

    Enemy boss(name, bossHealth, bossAttack, bossDefense, etype);
    boss.setBossType(btype);
    if (btype == BossType::STONE_COLOSSUS) boss.gainArmor(10);
    return boss;
}

// Shared by bossAction() and the Shadow Knight's mirrored attacks (can't be a lambda - those resolve outside bossAction()).
void Game::bossStrikesPlayer(int damage, bool raw) {
    double weakMult = enemy.getWeakMultiplier() * enemy.getStrengthMultiplier();
    EnemyArt::printBattleAttack(enemy.getType(), enemy.getBossType(), playerArmor > 0);
    // Dodge Reversal fires before Parry when both are active (uncapped, higher priority)
    if (counterAttackActive) {
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
    if (parryActive) {
        int parryCap = playerArmor + parryBonusValue * 3; // current armor + Parry's own bonus - stack armor first to parry bigger hits
        parryActive = false;
        if (damage <= parryCap) {
            int riposteDmg = (int)((damage * 1.5 + parryBonusValue) * playerStatus.getStrengthMultiplier());
            int hpBefore = enemy.getHealth();
            enemy.takeDamage(riposteDmg); // ignores defense - takeDamage only accounts for armor
            int hpLost = hpBefore - enemy.getHealth();
            EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), DamageType::NONE, hpLost > 0);
            EnemyArt::popNumber(hpLost, true, EnemyArt::PopKind::DAMAGE);
            bool stunned = tryStunEnemy();
            if (stunned && enemy.isAlive())
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::STUN, true);
            Audio::playSFX(!enemy.isAlive() ? deathSfx(enemy.isBoss()) : "special");
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
    if (raw) {
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
    if (playerHealth > 0 || !bossSecondWindAvailable) return false;
    bossSecondWindAvailable = false;
    playerHealth = 1;
    return true;
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

    bool bossVolleyBroken = false;
    auto doAttack = [&](int damage, bool raw) {
        if (enemy.hasStun()) {   // see the note on the regular doAttack
            if (!bossVolleyBroken) {
                bossVolleyBroken = true;
                std::cout << "  " << Color::CYAN
                          << "The stun lands mid-swing. The rest of the assault never comes."
                          << Color::RESET << "\n";
            }
            return;
        }
        bossStrikesPlayer(damage, raw);
    };

    switch (enemy.getBossType()) {
        case BossType::STONE_COLOSSUS:
            if (roll < 15) {
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Stone Colossus uses EARTHQUAKE SLAM!" + Color::RESET + "\n");
                UIHelper::pause(300);
                doAttack(15, true);
            } else if (roll < 45) {
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
                applyPlayerStatus(StatusType::POISON, 4);
                applyPlayerStatus(StatusType::BURN, 2);
                Audio::playSFX("poison");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Vile Witch casts PLAGUE!" + Color::RESET
                    + " You gain " + Color::POISON_CLR + "Poison 4" + Color::RESET
                    + " and " + Color::BURN_CLR + "Burn 2" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 85) {
                int healAmt = 20;
                enemy.heal(healAmt);
                std::cout << Color::MAGENTA << "Vile Witch siphons life, healing " << Color::HEAL
                          << healAmt << " HP!" << Color::RESET << " ("
                          << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                UIHelper::pause(250);
            } else {
                applyPlayerStatus(StatusType::POISON, 6);
                Audio::playSFX("poison");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Vile Witch casts TOXIC ERUPTION!" + Color::RESET
                    + " You gain " + Color::POISON_CLR + "Poison 6" + Color::RESET + "!\n");
                UIHelper::pause(350);
            }
            break;

        case BossType::WARLORD:
            if (roll < 12) {
                applyPlayerStatus(StatusType::STUN, 1);
                Audio::playSFX("volt");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Thunder Beast unleashes a THUNDERSTRIKE!" + Color::RESET
                    + " You are " + Color::STUN_CLR + "STUNNED" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 27) {
                applyPlayerStatus(StatusType::WEAK, 3);
                Audio::playSFX("special");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Thunder Beast roars a BATTLECRY!" + Color::RESET
                    + " You are " + Color::WEAK_CLR + "Weakened 3" + Color::RESET + "!\n");
                UIHelper::pause(350);
            }
            UIHelper::typeWrite(std::string(Color::MAGENTA) + "Thunder Beast attacks!" + Color::RESET + "\n");
            UIHelper::pause(200);
            doAttack(atk, false);
            if (enemy.getBonusAttack() < 6) {
                enemy.addBonusAttack(1);
                std::cout << Color::MAGENTA << "Thunder Beast grows stronger!" << Color::RESET
                          << " (total bonus +" << Color::RED << enemy.getBonusAttack() << Color::RESET << " attack)\n";
                UIHelper::pause(200);
            }
            break;

        case BossType::HYDRA:
            if (roll < 20) {
                int healAmt = 18;
                enemy.heal(healAmt);
                std::cout << Color::MAGENTA << "Hydra regrows a severed head, healing " << Color::HEAL
                          << healAmt << " HP!" << Color::RESET << " ("
                          << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                UIHelper::pause(250);
            } else if (roll < 50) {
                applyPlayerStatus(StatusType::POISON, 5);
                Audio::playSFX("poison");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Hydra sinks its fangs in with a VENOMOUS BITE!" + Color::RESET
                    + " You gain " + Color::POISON_CLR + "Poison 5" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 75) {
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Hydra lashes out with TWIN STRIKE!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
                if (enemy.isAlive()) doAttack(atk, false);
            } else {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Hydra bites!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
            }
            break;

        case BossType::DRAGON:
            if (roll < 20) {
                applyPlayerStatus(StatusType::BURN, 4);
                Audio::playSFX("fire");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Dragon unleashes FIRE BREATH!" + Color::RESET
                    + " You gain " + Color::BURN_CLR + "Burn 4" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 45) {
                applyPlayerStatus(StatusType::WEAK, 3);
                Audio::playSFX("special");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Dragon's WING BUFFET knocks you off balance!" + Color::RESET
                    + " You are " + Color::WEAK_CLR + "Weakened 3" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 70) {
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Dragon rakes with CLAW RAKE!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk + 5, true);
            } else {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Dragon claws at you!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
            }
            break;

        case BossType::SHADOW_KNIGHT: {
            // Leftover prepared moves play out here; Taunt forces one guaranteed strike instead.
            if (roll < 0 || knightPreparedMoves.empty()) {
                knightPreparedMoves.clear();
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Shadow Knight strikes!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
                break;
            }
            while (!knightPreparedMoves.empty() && enemy.isAlive() && playerHealth > 0) {
                std::uniform_int_distribution<> pick(0, (int)knightPreparedMoves.size() - 1);
                int idx = pick(gen);
                Card mirrored = knightPreparedMoves[idx];
                knightPreparedMoves.erase(knightPreparedMoves.begin() + idx);
                executeShadowKnightMirror(mirrored);
            }
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
    if (deckCards.empty()) return;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(deckCards.begin(), deckCards.end(), gen);
    size_t count = std::min((size_t)3, deckCards.size());
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
    double weakMult = enemy.getWeakMultiplier();
    int atk = (int)(std::max(0, enemy.getBaseAttack() + enemy.getBonusAttack()) * weakMult);
    int v = std::max(1, mirrored.getValue());
    UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Shadow Knight mirrors your "
        + mirrored.getName() + "!" + Color::RESET + "\n");
    UIHelper::pause(300);

    if (mirrored.getType() == CardType::ATTACK) {
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
                int healAmt = v * 2;
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
            default: // reactive cards (Dodge Reversal/Parry/Taunt) have no mirror
                std::cout << Color::MAGENTA << "The mirrored stance dissolves, and the shadow lunges!" << Color::RESET << "\n";
                UIHelper::pause(200);
                bossStrikesPlayer(atk, false);
                break;
        }
    }
}

void Game::offerBossReward() {
    std::vector<Card> rewards = rewardPool.generateRareRewards(3, maxEnergy, playerDeck.getAllCardNames());

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
    for (const Card& c : rewards) bossWidgets.push_back(toWidget(c, c.getValue()));

    while (true) {
        std::vector<CardBar::Action> bossActs{ CardBar::Action{ "Skip", false } };
        int choice = CardBar::pick("Boss reward, choose one", bossWidgets, bossActs,
                                   (int)bossWidgets.size());
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
    const int MAX_ENERGY_CAP = 5;
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

// Narrative beats, three per zone: "enter" plays at the zone's first fight
// (no boss spoilers - the boss is still 8-9 fights away), "approach" plays
// right before that zone's boss (this is where it's named), "outro" plays
// after the boss falls (the "soul fragment" recovered). Only plays on the
// first run through (cycle 0) - by the time an endless cycle repeats, the
// knight is already whole again.
namespace {
    using Lines = std::vector<std::string>;
    struct ZoneStory { Lines enter, approach, outro; };
    const ZoneStory ZONE_STORY[5] = {
        { // The Dungeon -> Stone Colossus
          { "The knight passes through a rusted iron gate into a dungeon of wet stone, his "
            "footsteps the only sound in corridors that swallow torchlight before it can catch.",
            "Something is missing in him, has been missing longer than he can remember, and "
            "the emptiness sits behind his ribs like a held breath.",
            "He grips a notched sword he isn't sure he ever learned to use, and presses deeper "
            "into the gloom." },
          { "The passage finally opens into a vast chamber, and the ground itself seems to wake.",
            "A stone colossus rises from the rubble, older than the dungeon around it, and "
            "plants itself squarely in the only way forward." },
          { "Among the rubble, the knight kneels and closes a gauntleted hand around the first "
            "fragment, a small point of light resting in the dust.",
            "It settles into the hollow behind his ribs, and for a moment his armor doesn't "
            "feel quite so heavy." } },
        { // The Dark Dungeon -> Vile Witch
          { "The passage narrows and darkens, stone giving way to something less honest:",
            "runes scored into the floor, a green light that flickers without a source, "
            "whispers that stop the instant he turns toward them." },
          { "The corridor opens on a chamber ringed with shattered cauldrons.",
            "A vile witch waits at its heart, with the patience of something that has already "
            "decided how this ends." },
          { "The cauldrons lie in pieces, and the wrongness in the air finally lifts.",
            "He takes the second fragment from her ruined altar.",
            "It settles in quietly, and with it comes a clarity he hadn't known he'd lost." } },
        { // The Wicked Forest -> Thunder Beast
          { "Trees older than the dungeon close overhead, branches woven so tight no daylight "
            "reaches the forest floor.",
            "The air itself feels charged, hair lifting on his arms with every step, thunder "
            "answering thunder in a storm that never quite arrives." },
          { "The storm finally breaks over a clearing at the heart of the wood.",
            "A thunder beast commands the canopy there, lightning coiled and ready." },
          { "When the last peal fades and the ozone smell clears, the forest seems to exhale "
            "with him.",
            "The third fragment lies scorched into the earth where the beast fell.",
            "Something in his legs remembers how to move fast again." } },
        { // The Dark Lake -> Hydra
          { "The shoreline is black glass under a fog that swallows sound as readily as light.",
            "The lake gives back no stars, only his own pale reflection.",
            "He pushes a rotting raft out into the mist." },
          { "Out past the fog line the water answers with ripples that have nothing to do "
            "with the wind.",
            "The hydra wakes beneath the surface, unwilling to let anything cross unchallenged." },
          { "The lake stills once the last head falls silent, and the fourth fragment drifts "
            "to him on the tide.",
            "His movements feel less like effort now, more like current." } },
        { // The Mountain -> Undead Dragon
          { "Past the treeline the world turns to wind and ice, a narrow ledge of slate the "
            "only path between him and the drop.",
            "Frost climbs his plate faster than his own breath can melt it." },
          { "The ledge ends at a cave mouth colder than the wind outside.",
            "Something waits within: a dragon that died once and never quite left." },
          { "The dragon's frozen breath goes still.",
            "He draws the fifth fragment from its shell, and warmth spreads through him.",
            "Nearly whole now, he can feel the shape of who he used to be." } },
    };
    // The Peak: plays once, right before the Shadow Knight (encounter 50). No
    // matching outro - handleGameVictory() already covers that beat.
    const Lines PEAK_APPROACH = {
        "Above the clouds the sky turns a bruised purple, the air too thin to hold much of "
        "anything.",
        "At the summit's edge, something is already waiting: a knight in his own armor, "
        "carrying his own sword, wearing every piece of himself he's spent this whole climb "
        "trying to reclaim."
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
}

// Odds for the ??? encounter, rolled before each regular fight.
//
// 2% per eligible fight. Eligible means: first cycle, not a boss, past
// encounter 5 (a starter deck cannot fight this thing), and not already used.
// That is ~39 eligible fights a run, so a full run sees it a little over half
// the time - rare enough to be a surprise, common enough to be found.
static const int SECRET_CHANCE_PERCENT = 2;
static const int SECRET_EARLIEST       = 6;

bool Game::rollSecretEncounter() {
    if (secretUsedThisRun || inSecretEncounter) return false;
    // Any cycle: secretUsedThisRun is cleared when a new wave starts, so
    // each pass through the fifty gets its own chance at it.
    if (currentRun.isBossEncounter()) return false;
    if (currentRun.getCurrentEncounter() < SECRET_EARLIEST) return false;
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> d(1, 100);
    return d(gen) <= SECRET_CHANCE_PERCENT;
}

void Game::beginSecretEncounter() {
    secretUsedThisRun = true;
    inSecretEncounter = true;

    UIHelper::clearScreen();
    UIHelper::padToCenter(4);
    UIHelper::printCenteredWrapped(std::string(Color::DIM)
        + "The moon comes up wrong. Everything that was making noise stops at once."
        + Color::RESET, 68, true);
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::BOLD) + Color::RED
        + "Something has been following you." + Color::RESET);
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
    UIHelper::waitForKey("");

    UIHelper::clearScreen();
    EnemyArt::setSecretBackdrop();
    Audio::playBGM("bgm_secret");   // off the zone rotation entirely
    Audio::playSFX("boss");
    playerDeck.resetDeck();
    int drawCount = 5 + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    // Built off the fight it interrupts rather than a fixed statline, so it
    // stays a step above whatever the zone is currently throwing at you.
    int health  = (int)(currentRun.getEnemyHealth()  * 1.6) + 30;
    int attack  = (int)(currentRun.getEnemyAttack()  * 1.35) + 2;
    int defense = (int)(currentRun.getEnemyDefense() * 1.2) + 1;
    enemy = Enemy("Moonstruck (Beast)", health, attack, defense, EnemyType::BEAST);

    EnemyArt::setEnemyVariant("Moonstruck");
    Console::pushHistory("");
    playerHealth = std::max(1, playerHealth);
    playerArmor = 0;
    playerArmorPersistTurns = 0;
    playerStatus.reset();
    enemyParryStance = false;
    playerAttackOnly = false;
    playerBoundTurn = false;
    curseTurnsLeft = 0;
    lichAddAlive = false;
    EnemyArt::setCompanion("");
    turnNumber = 1;
    cardsPlayedThisTurn = 0;
    resetEnergy();
    playerTurnActive = true;
    inEncounter = true;
    running = true;
}

// Beating it: one guaranteed Super Rare, then straight on to the fight it
// interrupted. No rest site, no equipment roll - this was never on the map.
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
        + "It does not leave a body. Only what it was carrying." + Color::RESET);
    std::cout << "\n";
    UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
    UIHelper::waitForKey("");
    UIHelper::showHeadline("", 0, 0, 0);

    // One in four that the thing was carrying something legendary. This and
    // finishing a run are the only ways to get one now.
    std::vector<Card> prize;
    {
        static thread_local std::mt19937 lg(std::random_device{}());
        std::uniform_int_distribution<> d(1, 100);
        if (d(lg) <= 25) {
            std::vector<Card> legs = rewardPool.getUnownedLegendaries(playerDeck.getAllCardNames());
            if (!legs.empty()) {
                std::uniform_int_distribution<> pick(0, (int)legs.size() - 1);
                prize.push_back(legs[pick(lg)]);
            }
        }
    }
    if (prize.empty())
        prize = rewardPool.generateSuperRareReward(playerDeck.getAllCardNames());
    if (!prize.empty()) {
        std::vector<CardBar::Card> w{ toWidget(prize[0], prize[0].getValue()) };
        std::vector<CardBar::Action> acts{ CardBar::Action{ "Take it", false } };
        while (true) {
            int ch = CardBar::pick("The beast was carrying this", w, acts, 1);
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
    int drawCount = 5 + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    if (currentRun.isBossEncounter()) {
        enemy = generateBossEnemy();
        bossSecondWindAvailable = true;
    } else {
        int health  = currentRun.getEnemyHealth();
        int attack  = currentRun.getEnemyAttack();
        int defense = currentRun.getEnemyDefense();

        // The 44 unique regular enemies, ordered so no two consecutive fights
        // share a type. Reordered so each zone's lineup both escalates in
        // apparent power (weakest zone 1, strongest zone 5) and fits the
        // zone's own theme (e.g. Wolf/Falcon in the Wicked Forest, Sorcerer's
        // ice in the Mountain, Serpent/Fleshmass in the Dark Lake).
        struct RosterEntry { EnemyType type; const char* name; };
        static const RosterEntry ROSTER[44] = {
            // 1-9, The Dungeon, before the Stone Colossus
            {EnemyType::MELEE, "Goblin"},   {EnemyType::TANK, "Orc"},
            {EnemyType::CASTER, "Wizard"},  {EnemyType::UNDEAD, "Skeleton"},
            {EnemyType::BEAST, "Spider"},   {EnemyType::RANGED, "Archer"},
            {EnemyType::MELEE, "Bandit"},   {EnemyType::TANK, "Warden"},
            {EnemyType::CASTER, "Sage"},
            // 11-19, The Dark Dungeon, before the Vile Witch
            // Serpent <-> Cockatrice traded with the Dark Lake: both petrify
            // clocks (Basilisk 5 turns, Cockatrice 3) used to sit seven fights
            // apart in this one zone and then never reappear. Both are BEAST,
            // so the no-repeated-type rule is unaffected by the trade.
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
            // Gladiator <-> Deadeye and Sorcerer <-> Revenant swapped in from
            // the Mountain, Fortress <-> Fleshmass traded across as well.
            // Warrior and Spellmaster then trade places purely to keep the
            // no-repeated-type rule: the swaps had left Gladiator/Warrior both
            // MELEE and Spellmaster/Sorcerer both CASTER back to back.
            // Type run: UNDEAD BEAST MELEE TANK CASTER MELEE CASTER TANK RANGED
            {EnemyType::UNDEAD, "Specter"}, {EnemyType::BEAST, "Cockatrice"},
            {EnemyType::MELEE, "Gladiator"},{EnemyType::TANK, "Bastion"},
            {EnemyType::CASTER, "Spellmaster"}, {EnemyType::MELEE, "Warrior"},
            {EnemyType::CASTER, "Sorcerer"},{EnemyType::TANK, "Fortress"},
            {EnemyType::RANGED, "Wyvern"},
            // 41-48, The Mountain, before the Dragon + Shadow Knight finale
            // Lich and Manticore trade places for the same reason - Revenant
            // arriving here would otherwise sit directly before Lich, two
            // UNDEAD in a row.
            // Type run: RANGED MELEE UNDEAD BEAST UNDEAD TANK BEAST CASTER
            {EnemyType::RANGED, "Deadeye"}, {EnemyType::MELEE, "Enforcer"},
            {EnemyType::UNDEAD, "Revenant"},{EnemyType::BEAST, "Manticore"},
            {EnemyType::UNDEAD, "Lich"},    {EnemyType::TANK, "Paladin"},
            {EnemyType::BEAST, "Fleshmass"},{EnemyType::CASTER, "Archon"},
        };
        int r = currentRun.getRegularIndex() % 44;
        EnemyType etype = ROSTER[r].type;
        std::string name = ROSTER[r].name;
        int cycle = currentRun.getCycle();
        if (cycle == 1) name = "Greater " + name;
        else if (cycle >= 2) name = "Eternal " + name;
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
    enemyInvulnerable = false;
    enemyParryStance = false;
    nextHandPenalty = 0;
    curseTurnsLeft = 0;
    lichAddAlive = false;
    lichAddHp = lichAddMaxHp = lichAddAtk = 0;
    inSecretEncounter = false;
    EnemyArt::setCompanion("");
    fleshmassBindPending = false;
    playerBoundTurn = false;
    cardsPlayedThisTurn = 0;
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

    // No run-stats dump or encounter banner: the combat panel already shows the
    // encounter, its difficulty and both HP bars, and the log should hold
    // events rather than open with a header nobody reads twice.
    if (currentRun.isBossEncounter()) Audio::playSFX("boss");

    refreshBattleAuras();
    EnemyArt::printBattle(enemy.getType(), enemy.getBossType());

    // Panel up, then a short beat before the hand arrives. This used to be 600
    // (900ms after the pacing multiplier), from when the screen genuinely was
    // not finished yet and needed time to settle. The panel and scene now draw
    // immediately, so all that pause did was hold a complete screen with no
    // cards on it.
    syncHud();
    UIHelper::pause(120);
    // handleInput() will render the hand + menu side-by-side on first input
}

void Game::nextEncounter() {
    currentRun.nextEncounter();
    startEncounter();
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
        CardBar::Action{ "Forge",     "upgrade a card  (+3 value, -1 cost)", false },
        CardBar::Action{ "View Deck", "browse and discard", false },
        CardBar::Action{ "Skip",      "press on without resting", false },
    };
    int siteChoice = CardBar::pick("Rest site", {}, siteActs, 0);
    if (siteChoice < 0) siteChoice = 3;   // ESC leaves without resting

    if (siteChoice == 0) {
        const std::string confirmPrompt = "Rest and heal to full?  (" + std::to_string(playerHealth)
                                        + "/" + std::to_string(maxPlayerHealth) + " HP)";
        if (!confirm(confirmPrompt)) continue; // declined - back to the rest site menu

        playerHealth = maxPlayerHealth;
        Audio::playSFX("heal");
        notice("You rest and fully recover to " + std::to_string(maxPlayerHealth) + " HP.");
        break; // committed - progress as normal
    } else if (siteChoice == 1) {
        if (playerDeck.totalCards() == 0) {
            notice("Your deck is empty. Nothing to upgrade.");
            continue; // nothing happened - back to the rest site menu
        }

        std::vector<Card> allCards = playerDeck.getAllCardsOrdered();
        std::stable_sort(allCards.begin(), allCards.end(),
            [](const Card& a, const Card& b) { return rarityRank(a) > rarityRank(b); });

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

        // Paginated - a long collection previously produced tall enough menus to
        // reliably trigger the redraw-duplication glitch (see the How To Play /
        // Tutorial fixes for the same root cause).
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
                const int gearBonus =
                      (c.getType() == CardType::ATTACK) ? upgrades.getDamageBonus() + equipDamageBonus
                    : (c.getType() == CardType::DEFEND) ? upgrades.getArmorBonus()  + equipArmorBonus
                                                        : 0;
                CardBar::Card w = toWidget(c, c.getValue() + gearBonus, maxed);
                // On the forge the useful number is what it becomes, not what
                // it currently is - that is the decision being made here.
                if (!maxed) w.effect = upgradeFaceLine(c, gearBonus);
                if (groupCount[g] > 1) w.name += " x" + std::to_string(groupCount[g]);
                w.note = maxed ? "maxed"
                               : (std::to_string(upgradesLeft) + " upgrade"
                                  + (upgradesLeft != 1 ? "s" : "") + " left");
                widgets.push_back(w);
            }

            std::string title = "Forge   pick a card to upgrade";
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
                    const int gearBonus =
                          (c.getType() == CardType::ATTACK) ? upgrades.getDamageBonus() + equipDamageBonus
                        : (c.getType() == CardType::DEFEND) ? upgrades.getArmorBonus()  + equipArmorBonus
                                                            : 0;
                    int left = c.getMaxUpgrades() - c.getUpgradeCount();
                    std::string text = c.getDescription();
                    if (left > 0) {
                        // showDetail word-wraps and has no newline handling, so
                        // this has to read as another sentence rather than a row.
                        text += "   Upgrading takes it to " + std::to_string(upgradedValue(c) + gearBonus);
                        text += (c.getType() == CardType::DEFEND) ? " armor" 
                              : (c.getType() == CardType::ATTACK) ? " damage" : " value";
                        if (upgradedCost(c) < c.getCost())
                            text += " and drops the cost from " + std::to_string(c.getCost())
                                  + " to " + std::to_string(upgradedCost(c));
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
                notice(done);
            }
            committed = true;
            break;
        }

        if (committed) break; // progress as normal
        else continue;        // back to the rest site menu, no commitment made
    } else if (siteChoice == 2) {
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
            CardBar::Card w = toWidget(c, c.getValue());
            if (groupCount[first + i] > 1) w.name += " x" + std::to_string(groupCount[first + i]);
            widgets.push_back(w);
        }

        // Say what picking a card actually does - it is a destructive action
        // and the grid alone does not imply it.
        std::string title = "Your Deck   pick a card to discard one copy   "
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

        // Announce the regular-reward rarity gate lifting - Legendary is never
        // mentioned here, it stays a silent boss-reward-only rarity. Waits for a
        // keypress instead of a timed pause - the next screen used to blow right
        // past this before it could be read.
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
    } else {
        // No keypress here on purpose: 44 regular wins a run, and a prompt on
        // every one of them is 44 keys of friction. The banner just holds.
        UIHelper::pause(700);
        UIHelper::showHeadline("", 0, 0, 0);
        offerCardReward();
    }

    if (currentRun.getCurrentEncounter() % 3 == 0)
        offerEquipmentDrop();

    restSite();

    offerContinueOrEndRun();
}

void Game::handleGameVictory() {
    // The ending is written for the first time through. Later waves get the
    // legendary and the choice to continue, without replaying the story.
    // Two waves, then the run is over. Only the first ending offers to go on.
    const bool firstTime = currentRun.getCycle() == 0;
    UIHelper::waitForKey();
    UIHelper::clearScreen();
    UIHelper::padToCenter(2);
    UIHelper::printCenteredWrapped(std::string(Color::BOLD) + Color::MAGENTA
        + (firstTime
           ? "The Shadow Knight staggers... and your dark reflection scatters like smoke."
           : "The shadow falls again. It is wearing a older face this time.")
        + Color::RESET, 68, true);
    UIHelper::pause(1100);

    // The ending gets the display face, same as the victory banner. The rule
    // lines that used to frame it were the last of the terminal chrome.
    UIHelper::clearScreen();
    UIHelper::showHeadline("VICTORY ETERNAL", 240, 200, 60);
    for (int i = 0, pad = Console::rows() * 44 / 100; i < pad; i++) std::cout << "\n";
    UIHelper::printCenteredWrapped(firstTime
        ? "All 50 encounters conquered. Every boss lies broken, even the shadow "
          "that wore your own face and fought with your own cards. The realm is "
          "free. Your legend is complete."
        : "One hundred encounters. You went back down into it knowing exactly "
          "what was waiting, and it still was not enough to stop you. There is "
          "nothing left down there that has not already lost to you.", 64);
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

    // The realm is saved; the run does not have to stop. Everything needed for
    // a second pass already exists - Run::getCycle() drives the "Greater"
    // prefix on every enemy, the boss schedule repeats on the same positions,
    // and enemy scaling is a pure function of the encounter number, so 51-100
    // is the same fifty fights at continued scaling with the deck intact.
    inEncounter = false;
    Hud::setActive(false);   // the panel belongs to the fight

    // Offered after the first wave only. Clearing the second is the end of
    // the run - there is no third.
    bool onward = false;
    if (firstTime) {
        std::vector<CardBar::Action> onwardActs{
            CardBar::Action{ "Continue", "encounters 51-100, everything scales on, deck intact", false },
            CardBar::Action{ "End the run", "stop here with the victory", false },
        };
        onward = CardBar::pick("Continue to a higher difficulty?", {}, onwardActs, 0) == 0;
    }
    if (onward) {
        currentRun.nextEncounter();
        secretUsedThisRun = false;   // the ??? encounter gets another chance
        notice("The road does not end. Something older is stirring past the peak.");
        startEncounter();
        return;
    }
    deleteSave();
    currentRun.loseRun(); // main loop routes to finishRun()
}

void Game::offerContinueOrEndRun(bool justWonEncounter) {
    UIHelper::clearScreen();
    const std::string title = std::string(justWonEncounter ? "Round complete" : "Resume run")
        + "        " + std::to_string(currentRun.getEncountersWon()) + " cleared"
        + "        " + std::to_string(playerHealth) + "/" + std::to_string(maxPlayerHealth) + " HP";
    // Saving used to hide behind a yes/no prompt after choosing End run, so it
    // was easy to finish a run without realising you could keep it. All three
    // outcomes are on the screen now.
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
            // Saving skips nextEncounter(), so advance the counter here or the
            // save would point at the fight just won and replay it on load.
            if (justWonEncounter) currentRun.nextEncounter();
            saveGame();
            notice("Progress saved. Pick Load Save on the main menu to carry on.");
        }
        inEncounter = false;
    Hud::setActive(false);   // the panel belongs to the fight
        currentRun.loseRun();
    }
}

void Game::offerCardReward() {
    UIHelper::clearScreen();
    bool rarityBoost = upgrades.isActive(4);
    // Gated by bosses defeated: Uncommon only, +Rare after 1st boss, +Super Rare after 2nd.
    int maxRarityUnlocked = std::min(2, currentRun.getCurrentEncounter() / 10);
    std::vector<Card> rewards = rewardPool.generateWeightedRewards(3, rarityBoost, maxEnergy, playerDeck.getAllCardNames(), maxRarityUnlocked);

    if (rewards.empty()) {
        notice("No new cards left to offer. You already own everything"
               " available at this cost.");
        return;
    }

    std::vector<CardBar::Card> widgets;
    for (const Card& c : rewards) widgets.push_back(toWidget(c, c.getValue()));

    while (true) {
        std::vector<CardBar::Action> acts{ CardBar::Action{ "Skip", false } };
        int choice = CardBar::pick("Pick a card to add to your deck",
                                   widgets, acts, (int)widgets.size());

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
            const std::string confirmPrompt = "Skip this reward, take none of the three?";
            if (!confirm(confirmPrompt)) continue;
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

void Game::offerEquipmentDrop() {
    EquipTier weapon = weaponTierAt(weaponTier);
    EquipTier armor  = armorTierAt(armorTier);
    int hpBoost = 30;

    // Same widgets as the card screens: this is a three way pick, so it reads
    // better as three panels than as a text list with a menu beside it.
    std::vector<CardBar::Card> widgets;
    {
        CardBar::Card w;
        w.name = weapon.name; w.elemTag = "[WEAPON]";
        w.effect = "+" + std::to_string(weapon.bonus) + " dmg";
        w.note = "on every attack";
        w.tint = Console::xterm256Public(9);
        w.nameColor = Console::xterm256Public(equipTintFor(weaponTier));
        widgets.push_back(w);

        CardBar::Card a2;
        a2.name = armor.name; a2.elemTag = "[ARMOR]";
        a2.effect = "+" + std::to_string(armor.bonus) + " armor";
        a2.note = "on every defend";
        a2.tint = Console::xterm256Public(12);
        a2.nameColor = Console::xterm256Public(equipTintFor(armorTier));
        widgets.push_back(a2);

        CardBar::Card h;
        h.name = "Health Pouch"; h.elemTag = "[VIGOR]";
        h.effect = "+" + std::to_string(hpBoost) + " max HP";
        h.note = std::to_string(maxPlayerHealth) + " to " + std::to_string(maxPlayerHealth + hpBoost);
        h.tint = Console::xterm256Public(10);
        h.nameColor = Console::xterm256Public(120);
        widgets.push_back(h);
    }

    while (true) {
        std::vector<CardBar::Action> acts{ CardBar::Action{ "Leave it behind", false } };
        int choice = CardBar::pick("You spot some gear on the ground", widgets, acts, 3);
        if (choice <= -2) continue;   // the "+" button has nothing extra to show here

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
            equipDamageBonus += weapon.bonus;
            weaponTier++;
            Audio::playSFX("upgrade");
            result = "You equip the " + weapon.name + ". +"
                   + std::to_string(weapon.bonus) + " damage.";
        } else if (choice == 1) {
            equipArmorBonus += armor.bonus;
            armorTier++;
            Audio::playSFX("upgrade");
            result = "You equip the " + armor.name + ". +"
                   + std::to_string(armor.bonus) + " armor per defend.";
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
        bool hasSave = saveExists();
        std::cout << "\n";
        std::vector<std::string> opts = {"Start Game"};
        if (hasSave) opts.push_back("Load Save");
        opts.push_back("How to Play");
        opts.push_back("Quit");

        int choice = UIHelper::titleMenu(opts);
        if (choice == 0) { UIHelper::showTitleBanner(false); return 0; } // Start Game
        if (hasSave && choice == 1) { UIHelper::showTitleBanner(false); return 1; } // Load Save

        int howToPlayIdx = hasSave ? 2 : 1;
        if (choice == howToPlayIdx) {
            UIHelper::showTitleBanner(false);
            showHowToPlay();
            UIHelper::clearScreen();
            Hud::setActive(false);   // nothing from in there belongs on the title
            UIHelper::printTitle();
            continue;
        }
        UIHelper::showTitleBanner(false);
        return 2; // Quit or ESC
    }
}

std::string Game::savePath() const {
    return Audio::exeDir() + "save.dat";
}

bool Game::saveExists() const {
    return std::filesystem::exists(savePath());
}

void Game::saveGame() const {
    std::ofstream out(savePath(), std::ios::trunc);
    if (!out.is_open()) {
        std::cout << Color::YELLOW << "Warning: couldn't write the save file." << Color::RESET << "\n";
        return;
    }
    out << "SAVE_V1\n";
    out << "ENCOUNTER " << currentRun.getCurrentEncounter() << "\n";
    out << "WON " << currentRun.getEncountersWon() << "\n";
    out << "MAXHP " << maxPlayerHealth << "\n";
    out << "HP " << playerHealth << "\n";
    out << "MAXENERGY " << maxEnergy << "\n";
    out << "EQUIPDMG " << equipDamageBonus << "\n";
    out << "EQUIPARM " << equipArmorBonus << "\n";
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

void Game::deleteSave() const {
    std::error_code ec;
    std::filesystem::remove(savePath(), ec);
}

bool Game::loadGame() {
    std::ifstream in(savePath());
    if (!in.is_open()) return false;

    std::string line;
    if (!std::getline(in, line) || line != "SAVE_V1") return false;

    int savedEncounter = 1, savedWon = 0, savedMaxHp = 100, savedHp = 100, savedMaxEnergy = 3;
    int savedEquipDmg = 0, savedEquipArm = 0, savedWeaponTier = 0, savedArmorTier = 0;
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
    equipDamageBonus = savedEquipDmg;
    equipArmorBonus  = savedEquipArm;
    weaponTier       = savedWeaponTier;
    armorTier        = savedArmorTier;
    for (int i = 0; i < 5; i++) upgrades.setUpgradeState(i, unlockedFlags[i], activeFlags[i]);

    turnNumber = 1;
    playerTurnActive = true;
    inEncounter = false; // not in combat yet - offerContinueOrEndRun() starts the next fight if Continue is chosen
    running = true;

    return true;
}

void Game::showHowToPlay() {
    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << Color::CYAN << "HOW TO PLAY" << Color::RESET << "\n\n";

    std::cout << Color::BOLD << "GOAL" << Color::RESET << "\n";
    std::cout << "  Survive as many encounters as you can. Build your deck, upgrade your\n";
    std::cout << "  best cards, and defeat the bosses that appear every 10th fight.\n\n";

    std::cout << Color::BOLD << "YOUR TURN" << Color::RESET << "\n";
    std::cout << "  Each turn you draw a hand and get a pool of energy. Every card's\n";
    std::cout << "  cost comes out of that energy. When you're done, End Turn to let\n";
    std::cout << "  the enemy act, then a new turn begins.\n\n";

    std::cout << Color::BOLD << "CARD TYPES" << Color::RESET << "\n";
    std::cout << "  " << Color::CARD_ATTACK  << "ATTACK " << Color::RESET << " deals damage to the enemy\n";
    std::cout << "  " << Color::CARD_DEFEND  << "DEFEND " << Color::RESET << " grants you armor, which absorbs incoming damage\n";
    std::cout << "  " << Color::CARD_SPECIAL << "SPECIAL" << Color::RESET << " buffs, debuffs, and unique effects\n\n";

    std::cout << Color::BOLD << "DAMAGE TYPES & WEAKNESSES" << Color::RESET << "\n";
    std::cout << "  Some cards carry a damage-type tag: " << Color::YELLOW << "Smash, Pierce" << Color::RESET
              << " (physical) or\n";
    std::cout << "  " << Color::YELLOW << "Fire, Poison, Wind" << Color::RESET << " (elemental). Every enemy is WEAK to one type\n";
    std::cout << "  (+50% damage) and some RESIST another (-50% damage). Check \"View\n";
    std::cout << "  Enemy\" in combat to plan your attacks.\n\n";

    std::cout << Color::BOLD << "STATUS EFFECTS" << Color::RESET << "\n";
    std::cout << "  " << Color::POISON_CLR << "Poison" << Color::RESET << " / " << Color::BURN_CLR << "Burn" << Color::RESET
              << "     damage over time, ticks down each turn\n";
    std::cout << "  " << Color::STUN_CLR << "Stun" << Color::RESET << "          skip the target's next turn entirely\n";
    std::cout << "  " << Color::WEAK_CLR << "Weak" << Color::RESET << "          target deals 1.5-2x less damage for a few turns\n";
    std::cout << "  " << Color::STRENGTH_CLR << "Strength" << Color::RESET << "      damage dealt is multiplied for a number of turns (buff)\n\n";

    std::cout << Color::BOLD << "CARD RARITY" << Color::RESET << "\n";
    std::cout << "  Common (starter) < " << Color::COMMON_TINT << "Uncommon" << Color::RESET << " < "
              << Color::RARE_TINT << "Rare" << Color::RESET << " < " << Color::SUPER_RARE_TINT << "Super Rare" << Color::RESET
              << " < " << Color::LEGENDARY_TINT << "Legendary" << Color::RESET << "\n";
    std::cout << "  Higher rarity cards hit harder and can be upgraded more times at the\n";
    std::cout << "  Forge. Legendary cards only ever drop from boss rewards.\n\n";

    std::cout << Color::BOLD << "REST SITES" << Color::RESET << "\n";
    std::cout << "  After winning a fight: Rest (heal to full), Forge (upgrade a card),\n";
    std::cout << "  View Deck (browse and discard cards you don't want), or Skip. Rest,\n";
    std::cout << "  Forge, and Skip all move you on to the next fight; View Deck lets\n";
    std::cout << "  you keep browsing until you hit Return.\n\n";

    std::cout << Color::BOLD << "REWARDS" << Color::RESET << "\n";
    std::cout << "  Win a fight -> pick a new card. Every 3rd encounter -> equipment (a\n";
    std::cout << "  permanent weapon, armor, or a Health Pouch for +max HP). Beat a boss\n";
    std::cout << "  -> pick from 3 rare-or-better cards (a rare shot at the Legendary\n";
    std::cout << "  Dodge Reversal). Every 2nd boss also offers +1 max energy.\n\n";

    std::cout << Color::BOLD << "BETWEEN RUNS" << Color::RESET << "\n";
    std::cout << "  Winning encounters and collecting cards unlocks permanent upgrades\n";
    std::cout << "  (more HP, more damage, more energy...) you can toggle on for your next run.\n\n";

    UIHelper::waitForKey("  (press any key to continue)");

    std::vector<CardBar::Action> helpActs{
        CardBar::Action{ "Tutorial", false },
        CardBar::Action{ "Return", false },
    };
    if (CardBar::pick("How to play", {}, helpActs, 0) == 0) showTutorial();
}

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
    std::cout << "\n" << Color::BOLD << "ATTACK, DEFEND, SPECIAL" << Color::RESET << "\n\n";
    std::cout << "  " << Color::CARD_ATTACK << "ATTACK" << Color::RESET << " cards deal damage. " << Color::CARD_DEFEND << "DEFEND" << Color::RESET
              << " cards grant armor that\n";
    std::cout << "  absorbs incoming damage. " << Color::CARD_SPECIAL << "SPECIAL" << Color::RESET << " cards do everything else -\n";
    std::cout << "  status effects, counters, heals, and more.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << "DAMAGE TYPES" << Color::RESET << "\n\n";
    std::cout << "  Some attacks carry a damage-type tag, like " << Color::BOLD << "Bash" << Color::RESET << " (" << Color::YELLOW << "Smash" << Color::RESET
              << ") and " << Color::BOLD << "Lunge" << Color::RESET << " (" << Color::YELLOW << "Pierce" << Color::RESET << ")\n";
    std::cout << "  in your hand. Every enemy is weak to one type for +50% damage -\n";
    std::cout << "  check View Enemy to see which.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << "ENDING YOUR TURN" << Color::RESET << "\n\n";
    std::cout << "  Done playing cards? Pick \"End Turn\" and the enemy acts. \"View\n";
    std::cout << "  Enemy\" shows their weakness/resistance; \"Status\" shows your own\n";
    std::cout << "  HP, armor, and active effects in detail.\n\n";
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

    // 15 HP: the tutorial is here to show how a turn works, not to be a fight.
    enemy = Enemy("Slime", 15, 4, 2, EnemyType::MELEE);
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
    int drawCount = 5 + upgrades.getDrawBonus();
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
    while (playerHealth > 0 && turnNumber - startTurn < maxTutorialTurns) {
        handleInput();

        // Read back what was actually played, via state playCardFromHand() sets -
        // this survives even if the same call also auto-ended the turn and reset
        // the hand (which would otherwise wipe any "used" flags we'd diffed against).
        if (lastActionWasCardPlay) {
            if (!attackTipShown && lastPlayedCardType == CardType::ATTACK) {
                attackTipShown = true;
                std::cout << "\n" << Color::DIM << "Fact: " << Color::RESET << "the slime's " << Color::BLUE << "DEF" << Color::RESET
                          << " (defense) soaks up part of your attack's DMG\n";
                std::cout << "  value - that's why the damage dealt came in lower than the card's number.\n\n";
                UIHelper::waitForKey();
            } else if (!defendTipShown && lastPlayedCardType == CardType::DEFEND) {
                defendTipShown = true;
                std::cout << "\n" << Color::DIM << "Fact: " << Color::RESET << "when the enemy hits you, their attack's damage\n";
                std::cout << "  is subtracted by your armor first - that's what DEFEND cards are for.\n\n";
                UIHelper::waitForKey();
            } else if (!specialTipShown && lastPlayedCardType == CardType::SPECIAL) {
                specialTipShown = true;
                std::cout << "\n" << Color::DIM << "Fact: " << Color::RESET << "SPECIAL cards often only do something under a\n";
                std::cout << "  condition. Parry, for example, only blocks and ripostes if the enemy actually\n";
                std::cout << "  attacks this turn - if they don't, it does nothing.\n\n";
                UIHelper::waitForKey();
            }

            if (!damageTypeTipShown && lastPlayedCardType == CardType::ATTACK
                && (lastPlayedPhysType != DamageType::NONE || lastPlayedPhysType2 != DamageType::NONE)) {
                damageTypeTipShown = true;
                bool matched = lastPlayedPhysType == enemy.getWeakness() || lastPlayedPhysType2 == enemy.getWeakness();
                if (matched) {
                    std::cout << "\n" << Color::DIM << "Fact: " << Color::RESET << "that card's damage-type tag matched the slime's\n";
                    std::cout << "  weakness, so it hit for +50% bonus damage.\n\n";
                } else {
                    std::cout << "\n" << Color::DIM << "Fact: " << Color::RESET << "that card's damage-type tag didn't match the\n";
                    std::cout << "  slime's weakness this time, so no bonus. Check View Enemy to see what an\n";
                    std::cout << "  enemy IS weak to before picking your attacks.\n\n";
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
    std::cout << "  " << Color::HEAL << "Rest" << Color::RESET << "       heal to full HP\n";
    std::cout << "  " << Color::YELLOW << "Forge" << Color::RESET << "      upgrade a card - all copies of it upgrade together\n";
    std::cout << "  " << Color::CYAN << "View Deck" << Color::RESET << "  browse your deck, discard cards you don't want\n";
    std::cout << "  " << Color::DIM << "Skip" << Color::RESET << "       move on without doing any of the above\n\n";
    std::cout << "  Rest, Forge, and Skip all move you to the next fight; View Deck\n";
    std::cout << "  lets you keep browsing until you choose Return.\n\n";
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

void Game::run() {
    init();

    UIHelper::printTitle();

    int menuChoice = showMainMenu();
    if (menuChoice == 2) {
        return;
    }

    if (menuChoice == 1 && loadGame()) {
        upgrades.displayUpgradeInfo();
        notice("Save loaded. Encounter " + std::to_string(currentRun.getCurrentEncounter())
               + ", " + std::to_string(currentRun.getEncountersWon()) + " enemies defeated so far.");
        offerContinueOrEndRun(false);
        // If the player picked End Run right away without fighting anything, there's
        // no live combat for the loop below to detect via checkGameOver() - wrap up here instead.
        if (!inEncounter) finishRun();
    } else {
        if (menuChoice == 1) {
            std::cout << "\n" << Color::YELLOW << "Warning: the save file could not be loaded - starting a new game instead." << Color::RESET << "\n";
            UIHelper::waitForKey();
        }
        upgrades.checkAndUnlockUpgrades(0, 0);
        upgrades.displayUpgradeInfo();

        currentRun.startRun();
        secretUsedThisRun = false;   // once per RUN, not once per launch
        startEncounter();
    }

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
                deleteSave(); // dying invalidates any existing save - no reloading out of a loss
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
        equipDamageBonus = 0;
        equipArmorBonus  = 0;
        weaponTier = 0;
        armorTier  = 0;
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
        currentRun.startRun();
        secretUsedThisRun = false;

        upgrades.displayUpgradeInfo();
        startEncounter();
    } else {
        running = false;
        runStats.displayCumulativeStats();
    }
}
