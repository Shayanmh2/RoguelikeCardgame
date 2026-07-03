#include "Card.h"
#include <iostream>
#include <vector>

Card::Card(std::string n, std::string desc, CardType t, int c, int v, CardEffect e)
    : name(n), description(desc), type(t), effect(e), cost(c), value(v), upgradeCount(0) {}

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

std::string Card::getBaseName() const {
    std::string base = name;
    while (!base.empty() && base.back() == '+') base.pop_back();
    return base;
}

int Card::getMaxUpgrades() const {
    // Starter cards (guaranteed by name — the reward pool filters out any card
    // whose base name the player already owns, so these names never duplicate
    // via rewards) get one upgrade. Unlockable cards get up to three, to keep
    // incentive on collecting new cards rather than just forging starters.
    static const std::vector<std::string> starterNames = {"Quick Jab", "Strike", "Bash", "Defend"};
    std::string base = getBaseName();
    for (const auto& n : starterNames)
        if (base == n) return 1;
    return 3;
}

void Card::upgrade() {
    value += 3;
    if (cost > 0) cost--;
    name += "+";
    upgradeCount++;
    // keep description in sync so the hand always shows the correct number
    if (type == CardType::ATTACK)
        description = "Deal " + std::to_string(value) + " damage";
    else if (type == CardType::DEFEND)
        description = "Gain " + std::to_string(value) + " armor";
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
