#ifndef UPGRADESYSTEM_H
#define UPGRADESYSTEM_H

#include "Upgrade.h"
#include <vector>

class UpgradeSystem {
private:
    std::vector<Upgrade> allUpgrades;
    std::vector<bool> unlockedUpgrades;  // parallel to allUpgrades
    std::vector<bool> activeUpgrades;    // parallel to allUpgrades
    
    void initializeUpgrades();
    
public:
    UpgradeSystem();
    
    // Unlock upgrades based on achievements
    void checkAndUnlockUpgrades(int totalEncounters, int totalCards);
    
    // Selection screen
    void selectActiveUpgrades();
    
    // Apply upgrades to game state
    int getHealthBonus() const;
    int getDamageBonus() const;
    int getArmorBonus() const;
    int getDrawBonus() const;
    
    // Check if upgrade is unlocked
    bool isUnlocked(int index) const;
    bool isActive(int index) const;

    void setUpgradeState(int index, bool unlocked, bool active); // restore from a save file
    
    void displayUpgradeInfo() const;
};

#endif
