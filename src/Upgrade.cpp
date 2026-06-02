#include "Upgrade.h"
#include <iostream>

Upgrade::Upgrade(std::string n, std::string desc, UpgradeType t, int req)
    : name(n), description(desc), type(t), unlockRequirement(req) {}

std::string Upgrade::getName() const {
    return name;
}

std::string Upgrade::getDescription() const {
    return description;
}

UpgradeType Upgrade::getType() const {
    return type;
}

int Upgrade::getUnlockRequirement() const {
    return unlockRequirement;
}

void Upgrade::display() const {
    std::cout << "[" << name << "]\n"
              << "  " << description << "\n";
}
