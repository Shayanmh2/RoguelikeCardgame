#include "Card.h"
#include <iostream>
#include <vector>

Card::Card(std::string n, std::string desc, CardType t, int c, int v, CardEffect e, bool isRare)
    : name(n), description(desc), type(t), effect(e), cost(c), value(v), upgradeCount(0), rare(isRare) {}

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

std::string Card::getBaseName() const {
    std::string base = name;
    while (!base.empty() && base.back() == '+') base.pop_back();
    return base;
}

int Card::getMaxUpgrades() const {
    // Starter cards (guaranteed by name — the reward pool filters out any card
    // whose base name the player already owns, so these names never duplicate
    // via rewards) get one upgrade. Common cards get three. Rare cards get five,
    // so a fully-forged rare card ends up strictly ahead of anything common.
    static const std::vector<std::string> starterNames = {"Quick Jab", "Strike", "Bash", "Defend"};
    std::string base = getBaseName();
    for (const auto& n : starterNames)
        if (base == n) return 1;
    return rare ? 5 : 3;
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
        else
            description = "Gain " + std::to_string(value) + " armor";
    }
    // SPECIAL descriptions are left as-is (stack counts in wording, not raw value)
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
