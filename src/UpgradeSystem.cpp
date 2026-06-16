#include "UpgradeSystem.h"
#include <iostream>

UpgradeSystem::UpgradeSystem() {
    initializeUpgrades();
}

void UpgradeSystem::initializeUpgrades() {
    allUpgrades.push_back(Upgrade("Health Boost",   "Gain +20 max health",                       UpgradeType::HEALTH_BOOST,    2));
    allUpgrades.push_back(Upgrade("Extra Strike",   "Start with 2 bonus Strike cards",           UpgradeType::STARTING_STRIKE, 5));
    allUpgrades.push_back(Upgrade("Sharp Blades",   "All attacks deal +2 damage",                UpgradeType::DAMAGE_UP,       3));
    allUpgrades.push_back(Upgrade("Iron Resolve",   "All defends grant +2 armor",                UpgradeType::ARMOR_UP,        3));
    allUpgrades.push_back(Upgrade("Adrenaline",     "Start with +1 energy per turn",             UpgradeType::EXTRA_ENERGY,    4));
    allUpgrades.push_back(Upgrade("Keen Sense",     "Draw 1 extra card at start",                UpgradeType::CARD_DRAW,       6));
    allUpgrades.push_back(Upgrade("Fortunate Soul", "Higher chance of rare reward cards",        UpgradeType::RARITY_BOOST,    8));
    unlockedUpgrades.resize(allUpgrades.size(), false);
    activeUpgrades.resize(allUpgrades.size(), false);
}

void UpgradeSystem::checkAndUnlockUpgrades(int totalEncounters, int totalCards) {
    if (totalEncounters >= 2) unlockedUpgrades[0] = true;
    if (totalEncounters >= 5) unlockedUpgrades[1] = true;
    if (totalEncounters >= 3) unlockedUpgrades[2] = true;
    if (totalEncounters >= 3) unlockedUpgrades[3] = true;
    if (totalEncounters >= 4) unlockedUpgrades[4] = true;
    if (totalCards >= 6)      unlockedUpgrades[5] = true;
    if (totalEncounters >= 8) unlockedUpgrades[6] = true;
}

void UpgradeSystem::displayUnlockedUpgrades() {
    std::cout << "\nUpgrades (toggle on/off for next run):\n\n";
    
    int unlockedCount = 0;
    for (size_t i = 0; i < allUpgrades.size(); ++i) {
        if (unlockedUpgrades[i]) {
            unlockedCount++;
            std::string status = activeUpgrades[i] ? "[ACTIVE]" : "[ ]";
            std::cout << (i + 1) << ". " << status << " " << allUpgrades[i].getName() << "\n";
            std::cout << "   " << allUpgrades[i].getDescription() << "\n\n";
        }
    }
    
    if (unlockedCount == 0) {
        std::cout << "No upgrades unlocked yet.\n";
    }
}

void UpgradeSystem::selectActiveUpgrades() {
    displayUnlockedUpgrades();
    
    std::string input;
    while (true) {
        std::cout << "Toggle upgrade (1-" << allUpgrades.size() << ") or press Enter to continue:\n";
        std::cout << "> ";
        std::getline(std::cin, input);
        
        if (input.empty()) break;
        
        try {
            int choice = std::stoi(input) - 1;
            if (choice >= 0 && choice < (int)allUpgrades.size() && unlockedUpgrades[choice]) {
                activeUpgrades[choice] = !activeUpgrades[choice];
                displayUnlockedUpgrades();
            } else {
                std::cout << "Invalid choice. Try again.\n";
            }
        } catch (...) {
            std::cout << "Invalid input. Try again.\n";
        }
    }
}

int UpgradeSystem::getHealthBonus() const {
    return activeUpgrades[0] ? 20 : 0;
}

int UpgradeSystem::getDamageBonus() const {
    return activeUpgrades[2] ? 2 : 0;
}

int UpgradeSystem::getArmorBonus() const {
    return activeUpgrades[3] ? 2 : 0;
}

int UpgradeSystem::getEnergyBonus() const {
    return activeUpgrades[4] ? 1 : 0;
}

int UpgradeSystem::getDrawBonus() const {
    return activeUpgrades[5] ? 1 : 0;
}

bool UpgradeSystem::isRarityBoostActive() const {
    return activeUpgrades[6];
}

bool UpgradeSystem::isUnlocked(int index) const {
    if (index < 0 || index >= (int)unlockedUpgrades.size()) return false;
    return unlockedUpgrades[index];
}

bool UpgradeSystem::isActive(int index) const {
    if (index < 0 || index >= (int)activeUpgrades.size()) return false;
    return activeUpgrades[index];
}

void UpgradeSystem::displayUpgradeInfo() const {
    int activeCount = 0;
    for (size_t i = 0; i < allUpgrades.size(); ++i) {
        if (activeUpgrades[i]) {
            if (activeCount == 0) std::cout << "\nActive upgrades:\n";
            activeCount++;
            allUpgrades[i].display();
        }
    }
}
