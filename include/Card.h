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
    PARRY,
    PIERCE,     // ATTACK: ignores enemy's base defense stat
    FORTIFY,    // DEFEND: granted armor persists across turns instead of resetting
    STRENGTH,   // ATTACK: grants the player a temporary self attack buff
    DOUBLE_HIT  // ATTACK: hits twice, each hit using the card's value
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
    bool rare; // true if drawn from the rare reward pool (raises the upgrade cap)

public:
    Card(std::string n, std::string desc, CardType t, int c, int v,
         CardEffect e = CardEffect::NONE, bool isRare = false);

    std::string getName() const;
    std::string getDescription() const;
    CardType getType() const;
    std::string getTypeString() const;
    CardEffect getEffect() const;
    int getCost() const;
    int getValue() const;
    bool isUpgraded() const;
    int getUpgradeCount() const;
    bool isRare() const;
    int getMaxUpgrades() const;   // starter cards cap at 1, common at 3, rare at 5
    std::string getBaseName() const; // name with trailing '+' upgrade markers stripped

    void upgrade();
    void display() const;
};

#endif
