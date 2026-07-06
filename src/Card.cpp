#include "Card.h"
#include <iostream>
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

bool Card::isUpgraded() const {
    return upgradeCount > 0;
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
    // Starter cards (guaranteed by name — the reward pool filters out any card
    // whose base name the player already owns, so these names never duplicate
    // via rewards) get one upgrade. Common cards get three, rare cards four,
    // super rare cards five — so each tier ends up strictly ahead once maxed.
    if (isStarter()) return 1;
    if (legendary || superRare) return 5;
    return rare ? 4 : 3;
}

void Card::upgrade() {
    value += 3;
    if (cost > 0) cost--;
    name += "+";
    upgradeCount++;
    // keep description in sync so the hand always shows the correct number,
    // preserving each effect's flavor text rather than falling back to generic wording
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
            description = "Parries hits up to " + std::to_string(value * 3) + " dmg: block, riposte 1.5x +"
                        + std::to_string(value) + " (ignores defense), stun. Bigger hits break your guard.";
        // other SPECIAL descriptions are left as-is (stack counts in wording, not raw value)
    }
}

void Card::display() const {
    std::string typeStr;
    switch(type) {
        case CardType::ATTACK: typeStr = "ATTACK"; break;
        case CardType::DEFEND: typeStr = "DEFEND"; break;
        case CardType::SPECIAL: typeStr = "SPECIAL"; break;
    }
    
    std::cout << "[" << name << "] (" << typeStr << ")\n"
              << "  Cost: " << cost << " | Value: " << value << "\n"
              << "  " << description << "\n";
}
