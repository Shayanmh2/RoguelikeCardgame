#include "Card.h"
#include <vector>
#include <sstream>

// Legendary is checked first because Bloodlust carries BOTH superRare and
// legendary; the old ladder never tested legendary at all, so it was quietly
// getting the super-rare number.
//
// Rare and Super Rare both sit at 2.0 on purpose. No rare-tier Strength card
// exists today, so nothing is indistinguishable in practice, but a new one
// would need its own step here.
double Card::strengthMultiplier() const {
    if (legendary) return 4.0;
    if (superRare) return 2.0;
    if (rare)      return 2.0;
    return 1.2;
}

CardEffect Card::effectFromString(const std::string& s) {
    if (s == "POISON")       return CardEffect::POISON;
    if (s == "BURN")         return CardEffect::BURN;
    if (s == "REND")         return CardEffect::REND;
    if (s == "STUN")         return CardEffect::STUN;
    if (s == "WEAK")         return CardEffect::WEAK;
    if (s == "COUNTER")      return CardEffect::COUNTER;
    if (s == "PARRY")        return CardEffect::PARRY;
    if (s == "PIERCE")       return CardEffect::PIERCE;
    if (s == "FORTIFY")      return CardEffect::FORTIFY;
    if (s == "STRENGTH")     return CardEffect::STRENGTH;
    if (s == "DOUBLE_HIT")   return CardEffect::DOUBLE_HIT;
    if (s == "IMPAIR")       return CardEffect::IMPAIR;
    if (s == "CHIP")         return CardEffect::CHIP;
    if (s == "HEAL")         return CardEffect::HEAL;
    if (s == "WARD")         return CardEffect::WARD;
    if (s == "TAUNT")        return CardEffect::TAUNT;
    if (s == "FEAR")         return CardEffect::FEAR;
    if (s == "TRUESTRIKE")   return CardEffect::TRUESTRIKE;
    return CardEffect::NONE;
}

DamageType Card::damageTypeFromString(const std::string& s) {
    if (s == "SMASH")  return DamageType::SMASH;
    if (s == "PIERCE") return DamageType::PIERCE;
    if (s == "FIRE")   return DamageType::FIRE;
    if (s == "POISON") return DamageType::POISON;
    if (s == "WIND")   return DamageType::WIND;
    return DamageType::NONE;
}

Card::Card(std::string n, std::string desc, CardType t, int c, int v, CardEffect e, bool isRare,
           DamageType physT, DamageType elemT, bool isSuperRare, DamageType physT2, bool isLegendary, int savedUpgradeCount)
    : name(n), description(desc), type(t), effect(e), cost(c), value(v), upgradeCount(savedUpgradeCount), rare(isRare),
      superRare(isSuperRare), legendary(isLegendary), physType(physT), physType2(physT2), elemType(elemT) {}

std::string Card::getName() const {
    return name;
}

std::string Card::getDescription() const {
    return description;
}

CardType Card::getType() const {
    return type;
}

std::string Card::getTypeString() const {
    switch(type) {
        case CardType::ATTACK: return "ATTACK";
        case CardType::DEFEND: return "DEFEND";
        case CardType::SPECIAL: return "SPECIAL";
        default: return "UNKNOWN";
    }
}

int Card::getCost() const {
    return cost;
}

int Card::getValue() const {
    return value;
}

CardEffect Card::getEffect() const {
    return effect;
}

int Card::getUpgradeCount() const {
    return upgradeCount;
}

bool Card::isRare() const {
    return rare;
}

bool Card::isSuperRare() const {
    return superRare;
}

bool Card::isLegendary() const {
    return legendary;
}

static const std::vector<std::string>& starterCardNames() {
    // Name-keyed, so a rename here is not cosmetic: isStarter() drives the
    // upgrade cap and the white name tint.
    static const std::vector<std::string> names = {"Quick Jab", "Slash", "Bash", "Lunge", "Defend", "Brace", "Parry"};
    return names;
}

bool Card::isStarter() const {
    std::string base = getBaseName();
    for (const auto& n : starterCardNames())
        if (base == n) return true;
    return false;
}

DamageType Card::getPhysType() const {
    return physType;
}

DamageType Card::getPhysType2() const {
    return physType2;
}

DamageType Card::getElemType() const {
    return elemType;
}

std::string Card::getTypeTag() const {
    std::string tag;
    if (physType  != DamageType::NONE) tag += std::string("[") + damageTypeName(physType) + "]";
    if (physType2 != DamageType::NONE) tag += std::string("[") + damageTypeName(physType2) + "]";
    if (elemType  != DamageType::NONE) tag += std::string("[") + damageTypeName(elemType) + "]";
    return tag;
}

std::string Card::getBaseName() const {
    std::string base = name;
    while (!base.empty() && base.back() == '+') base.pop_back();
    return base;
}

int Card::getMaxUpgrades() const {
    // Weaken/Stun cards aren't upgradable at all - Weak's duration is already
    // generous, and Stun is fixed at 1 turn regardless of value, so there'd be
    // nothing upgrading them actually improves. Same logic for a pure-buff
    // SPECIAL Strength card (its multiplier is fixed by rarity, not value) -
    // but NOT for an ATTACK card like Bloodlust, where value still raises damage.
    if (effect == CardEffect::WEAK || effect == CardEffect::STUN
        || effect == CardEffect::TAUNT || effect == CardEffect::FEAR) return 0;
    if (effect == CardEffect::STRENGTH && type == CardType::SPECIAL) return 0;
    // Common (starter) 1; Uncommon 2; Rare 3; Super Rare 4; Legendary 5
    if (isStarter()) return 1;
    if (legendary)   return 5;
    if (superRare)   return 4;
    if (rare)        return 3;
    return 2;
}

void Card::upgrade() {
    value += 3;
    // Cost floors at 1, not 0 - a card should never become fully free from upgrades
    // alone, so extra plays (from beating bosses) stay worth taking.
    if (cost > 1) cost--;
    name += "+";
    upgradeCount++;
    // keep description text in sync with the new value
    // Any ATTACK carrying an elemental tag has a 10% on-hit chance to leave its
    // matching status behind (Game::playCardFromHand). The rebuild below flattens
    // attack text to "Deal N damage.", so without re-appending this here the note
    // would survive in cards.json and then vanish on the first upgrade.
    auto elementalNote = [](DamageType e) -> const char* {
        switch (e) {
            case DamageType::FIRE:   return " 10% chance on hit to also apply Burn 3: 4 damage a turn for 2 turns.";
            case DamageType::POISON: return " 10% chance on hit to also apply Poison 3: 2 damage a turn for 6 turns.";
            case DamageType::WIND:   return " 10% chance on hit to also apply Rend 3: 3 damage the next 3 times it attacks.";
            default:                 return "";
        }
    };

    if (type == CardType::ATTACK) {
        if (effect == CardEffect::DOUBLE_HIT)
            description = "Deal " + std::to_string(value) + " damage twice (" + std::to_string(value * 2) + " total).";
        else if (effect == CardEffect::PIERCE)
            description = "Deal " + std::to_string(value) + " damage: ignoring enemy defense.";
        else if (effect == CardEffect::TRUESTRIKE)
            description = "Deal " + std::to_string(value) + " damage that nothing reduces. Ignores armor, "
                          "defense, resistance, and any stance or phase the enemy is hiding behind.";
        else if (effect == CardEffect::STRENGTH) {
            double buff = strengthMultiplier();
            std::ostringstream buffStr;
            buffStr << buff;
            description = "Deal " + std::to_string(value) + " damage. Gain x" + buffStr.str() + " damage for 2 turns.";
        }
        else
            description = "Deal " + std::to_string(value) + " damage.";
        description += elementalNote(elemType);
    } else if (type == CardType::DEFEND) {
        if (effect == CardEffect::FORTIFY)
            description = "Gain " + std::to_string(value) + " armor that persists for 3 turns (until broken).";
        else if (effect == CardEffect::IMPAIR)
            description = "Gain " + std::to_string(value) + " armor. 50% chance to Weaken the enemy.";
        else if (effect == CardEffect::CHIP)
            description = "Gain " + std::to_string(value) + " armor. Deal 3 damage.";
        else if (effect == CardEffect::WARD)
            description = "Gain " + std::to_string(value) + " armor and ward yourself for 2 turns. "
                          "Every ailment the enemy would inflict (Poison, Burn, Weak or Stun) is blocked while it holds.";
        else
            description = "Gain " + std::to_string(value) + " armor.";
    } else if (type == CardType::SPECIAL) {
        if (effect == CardEffect::COUNTER)
            description = "Counter: reverses the enemy's next attack or ailment back at them, doubled, +" + std::to_string(value)
                        + ". Fizzles if they do neither.";
        else if (effect == CardEffect::PARRY) {
            // This used to collapse to one line on upgrade and silently drop
            // the stun, the armour threshold and the ranged caveat - so Parry+
            // told you less than Parry did. Both now say the same things, with
            // the value-derived numbers filled in.
            description = "Block the enemy's next attack and riposte for 1.5x their attack "
                          "plus " + std::to_string(value) + ", ignoring their defense, with a chance to stun them. "
                          "How big a blow you can catch is your armor plus " + std::to_string(value * 3) + ": "
                          "too heavy a hit breaks the guard. Ranged enemies are blocked but "
                          "stand too far away to riposte.";
        }
        // The tick is not the card value: poison pays half of it over six turns,
        // burn half again over two. Both formulas mirror StatusEffects::apply().
        else if (effect == CardEffect::POISON)
            description = "Apply " + std::to_string(value) + " Poison ("
                        + std::to_string((value + 1) / 2) + " dmg/turn for 6 turns).";
        else if (effect == CardEffect::BURN)
            description = "Apply " + std::to_string(value) + " Burn ("
                        + std::to_string(value + value / 2) + " dmg/turn for 2 turns).";
        else if (effect == CardEffect::REND)
            description = "Apply " + std::to_string(value) + " Rend ("
                        + std::to_string(value) + " damage the next 3 times the enemy attacks).";
        else if (effect == CardEffect::WEAK) {
            // Duration is always 3; the multiplier is set by rarity, not by value.
            const char* m = superRare ? "2" : rare ? "1.75" : "1.5";
            description = std::string("Weaken the enemy for 3 turns: their attacks land for ")
                        + m + "x less damage.";
        }
        else if (effect == CardEffect::HEAL)
            description = "Heal " + std::to_string(value) + " HP.";
        else if (effect == CardEffect::STRENGTH) {
            double buff = strengthMultiplier();
            std::ostringstream buffStr;
            buffStr << buff;
            description = "Gain x" + buffStr.str() + " damage for 2 turns.";
        }
        // STUN is left as-is - duration no longer scales with value, only cost drops
    }
}
