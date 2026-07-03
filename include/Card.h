#ifndef CARD_H
#define CARD_H

#include <string>

enum class CardType {
    ATTACK,
    DEFEND,
    SPECIAL
};

enum class CardEffect {
    NONE,
    POISON,
    BURN,
    STUN,
    WEAK,
    COUNTER,
    PARRY
};

class Card {
private:
    std::string name;
    std::string description;
    CardType type;
    CardEffect effect;
    int cost;
    int value;
    int upgradeCount;

public:
    Card(std::string n, std::string desc, CardType t, int c, int v,
         CardEffect e = CardEffect::NONE);

    std::string getName() const;
    std::string getDescription() const;
    CardType getType() const;
    std::string getTypeString() const;
    CardEffect getEffect() const;
    int getCost() const;
    int getValue() const;
    bool isUpgraded() const;
    int getUpgradeCount() const;
    int getMaxUpgrades() const;   // starter cards cap at 1, unlockable cards cap at 3
    std::string getBaseName() const; // name with trailing '+' upgrade markers stripped

    void upgrade();
    void display() const;
};

#endif
