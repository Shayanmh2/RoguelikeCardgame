#include "UpgradeSystem.h"
#include "UIHelper.h"
#include <iostream>

UpgradeSystem::UpgradeSystem() {
    initializeUpgrades();
}

void UpgradeSystem::initializeUpgrades() {
    allUpgrades.push_back(Upgrade("Health Boost",   "Gain +20 max health",                       UpgradeType::HEALTH_BOOST,    2));
    allUpgrades.push_back(Upgrade("Sharp Blades",   "All attacks deal +2 damage",                UpgradeType::DAMAGE_UP,       3));
    allUpgrades.push_back(Upgrade("Iron Resolve",   "All defends grant +2 armor",                UpgradeType::ARMOR_UP,        3));
    allUpgrades.push_back(Upgrade("Keen Sense",     "Draw 1 extra card at start",                UpgradeType::CARD_DRAW,       6));
    allUpgrades.push_back(Upgrade("Fortunate Soul", "Higher chance of rare reward cards",        UpgradeType::RARITY_BOOST,    8));
    unlockedUpgrades.resize(allUpgrades.size(), false);
    activeUpgrades.resize(allUpgrades.size(), false);
}

void UpgradeSystem::checkAndUnlockUpgrades(int totalEncounters, int totalCards) {
    if (totalEncounters >= 2) unlockedUpgrades[0] = true;
    if (totalEncounters >= 3) unlockedUpgrades[1] = true;
    if (totalEncounters >= 3) unlockedUpgrades[2] = true;
    if (totalCards >= 6)      unlockedUpgrades[3] = true;
    if (totalEncounters >= 10) unlockedUpgrades[4] = true;
}

void UpgradeSystem::selectActiveUpgrades() {
    bool anyUnlocked = false;
    for (bool u : unlockedUpgrades) if (u) { anyUnlocked = true; break; }
    if (!anyUnlocked) {
        std::cout << "\nNo upgrades unlocked yet.\n";
        return;
    }

    while (true) {
        // Build index mapping: menu position → allUpgrades index
        std::vector<int> idxMap;
        for (int i = 0; i < (int)allUpgrades.size(); i++)
            if (unlockedUpgrades[i]) idxMap.push_back(i);

        std::vector<std::string> options;
        for (int i : idxMap) {
            std::string status = activeUpgrades[i] ? "[ON]  " : "[   ] ";
            options.push_back(status + allUpgrades[i].getName() + " - " + allUpgrades[i].getDescription());
        }
        options.push_back("Done");

        UIHelper::clearScreen();
        std::cout << "\nUpgrades - select to toggle:\n";
        int choice = UIHelper::menuSelect(options);

        if (choice < 0 || choice >= (int)idxMap.size()) break;  // Done or ESC

        int realIdx = idxMap[choice];
        activeUpgrades[realIdx] = !activeUpgrades[realIdx];
    }
}

int UpgradeSystem::getHealthBonus() const {
    return activeUpgrades[0] ? 20 : 0;
}

int UpgradeSystem::getDamageBonus() const {
    return activeUpgrades[1] ? 2 : 0;
}

int UpgradeSystem::getArmorBonus() const {
    return activeUpgrades[2] ? 2 : 0;
}

int UpgradeSystem::getDrawBonus() const {
    return activeUpgrades[3] ? 1 : 0;
}

bool UpgradeSystem::isRarityBoostActive() const {
    return activeUpgrades[4];
}

bool UpgradeSystem::isUnlocked(int index) const {
    if (index < 0 || index >= (int)unlockedUpgrades.size()) return false;
    return unlockedUpgrades[index];
}

bool UpgradeSystem::isActive(int index) const {
    if (index < 0 || index >= (int)activeUpgrades.size()) return false;
    return activeUpgrades[index];
}

void UpgradeSystem::setUpgradeState(int index, bool unlocked, bool active) {
    if (index < 0 || index >= (int)allUpgrades.size()) return;
    unlockedUpgrades[index] = unlocked;
    activeUpgrades[index] = active;
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
