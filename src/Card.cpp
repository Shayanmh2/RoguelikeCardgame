#include "Card.h"
#include <vector>
#include <sstream>

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
    static const std::vector<std::string> names = {"Quick Jab", "Jab", "Bash", "Lunge", "Defend", "Brace", "Parry"};
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
    if (effect == CardEffect::WEAK || effect == CardEffect::STUN || effect == CardEffect::TAUNT) return 0;
    if (effect == CardEffect::STRENGTH && type == CardType::SPECIAL) return 0;
    // Starter caps at 1; common 3; rare 4; super rare/legendary 5
    if (isStarter()) return 1;
    if (legendary || superRare) return 5;
    return rare ? 4 : 3;
}

void Card::upgrade() {
    value += 3;
    // Cost floors at 1, not 0 - a card should never become fully free from upgrades
    // alone, so extra plays (from beating bosses) stay worth taking.
    if (cost > 1) cost--;
    name += "+";
    upgradeCount++;
    // keep description text in sync with the new value
    if (type == CardType::ATTACK) {
        if (effect == CardEffect::DOUBLE_HIT)
            description = "Deal " + std::to_string(value) + " damage twice (" + std::to_string(value * 2) + " total)";
        else if (effect == CardEffect::PIERCE)
            description = "Deal " + std::to_string(value) + " damage, ignoring enemy defense.";
        else if (effect == CardEffect::STRENGTH) {
            double buff = superRare ? 3.0 : rare ? 2.0 : 1.2;
            std::ostringstream buffStr;
            buffStr << buff;
            description = "Deal " + std::to_string(value) + " damage. Gain x" + buffStr.str() + " damage for 2 turns.";
        }
        else
            description = "Deal " + std::to_string(value) + " damage";
    } else if (type == CardType::DEFEND) {
        if (effect == CardEffect::FORTIFY)
            description = "Gain " + std::to_string(value) + " armor that persists for 3 turns (until broken).";
        else if (effect == CardEffect::IMPAIR)
            description = "Gain " + std::to_string(value) + " armor. 50% chance to Weaken the enemy.";
        else if (effect == CardEffect::CHIP)
            description = "Gain " + std::to_string(value) + " armor. Deal 3 damage.";
        else if (effect == CardEffect::WARD)
            description = "Gain " + std::to_string(value) + " armor. Blocks the next ailment (Poison/Burn/Weak/Stun) the enemy inflicts on you.";
        else
            description = "Gain " + std::to_string(value) + " armor";
    } else if (type == CardType::SPECIAL) {
        if (effect == CardEffect::COUNTER)
            description = "Counter: reverses the enemy's next attack or ailment back at them, doubled, +" + std::to_string(value)
                        + ". Fizzles if they do neither.";
        else if (effect == CardEffect::PARRY)
            description = "Parries the attack, then riposte for 1.5x damage.";
        else if (effect == CardEffect::POISON)
            description = "Apply " + std::to_string(value) + " Poison (" + std::to_string(value) + " dmg/turn for 3 turns).";
        else if (effect == CardEffect::BURN)
            description = "Apply " + std::to_string(value) + " Burn (" + std::to_string(value) + " dmg/turn for 3 turns).";
        else if (effect == CardEffect::WEAK)
            description = "Apply Weak for " + std::to_string(value) + " turns (deals 1.5x less damage).";
        else if (effect == CardEffect::HEAL)
            description = "Heal " + std::to_string(value) + " HP.";
        else if (effect == CardEffect::STRENGTH) {
            double buff = superRare ? 3.0 : rare ? 2.0 : 1.2;
            std::ostringstream buffStr;
            buffStr << buff;
            description = "Gain x" + buffStr.str() + " damage for 2 turns.";
        }
        // STUN is left as-is - duration no longer scales with value, only cost drops
    }
}
