#include "Card.h"
#include <vector>

Card::Card(std::string n, std::string desc, CardType t, int c, int v, CardEffect e, bool isRare,
           DamageType physT, DamageType elemT, bool isSuperRare, DamageType physT2, bool isLegendary)
    : name(n), description(desc), type(t), effect(e), cost(c), value(v), upgradeCount(0), rare(isRare),
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
        else if (effect == CardEffect::STRENGTH)
            description = "Deal " + std::to_string(value) + " damage. Gain +3 attack for 2 turns.";
        else
            description = "Deal " + std::to_string(value) + " damage";
    } else if (type == CardType::DEFEND) {
        if (effect == CardEffect::FORTIFY)
            description = "Gain " + std::to_string(value) + " armor that persists for 3 turns (until broken).";
        else if (effect == CardEffect::IMPAIR)
            description = "Gain " + std::to_string(value) + " armor. 50% chance to Weaken the enemy.";
        else if (effect == CardEffect::CHIP)
            description = "Gain " + std::to_string(value) + " armor. Deal 3 damage.";
        else
            description = "Gain " + std::to_string(value) + " armor";
    } else if (type == CardType::SPECIAL) {
        if (effect == CardEffect::COUNTER)
            description = "Counter: if the enemy attacks, hit back for double their damage +" + std::to_string(value)
                        + ". Fizzles if they don't. Always fires before Parry.";
        else if (effect == CardEffect::PARRY)
            description = "Parries the attack, then riposte for 1.5x damage.";
        // other SPECIAL descriptions are left as-is (stack counts in wording, not raw value)
    }
}
