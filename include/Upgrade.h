#ifndef UPGRADE_H
#define UPGRADE_H

#include <string>

enum class UpgradeType {
    HEALTH_BOOST,      // +20 max health
    DAMAGE_UP,         // +2 damage per attack card
    ARMOR_UP,          // +2 armor per defend card
    CARD_DRAW,         // Draw 1 extra card at start
    RARITY_BOOST       // Higher chance of rare cards
};

class Upgrade {
private:
    std::string name;
    std::string description;
    UpgradeType type;
    int unlockRequirement;  // e.g., "win 3 encounters"
    
public:
    Upgrade(std::string n, std::string desc, UpgradeType t, int req);
    
    std::string getName() const;
    std::string getDescription() const;
    UpgradeType getType() const;
    int getUnlockRequirement() const;
    
    void display() const;
};

#endif
