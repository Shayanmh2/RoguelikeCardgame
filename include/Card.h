#ifndef CARD_H
#define CARD_H

#include <string>
#include "DamageType.h"

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
    DOUBLE_HIT, // ATTACK: hits twice, each hit using the card's value
    IMPAIR,     // DEFEND: 50% chance to Weaken the enemy on play
    CHIP,       // DEFEND: also deals a small flat amount of direct damage
    HEAL        // SPECIAL: restores the card's value in HP
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
    bool rare;      // true if drawn from the rare reward pool (raises the upgrade cap)
    bool superRare; // true for a curated subset of standout rare cards (visual tint only)
    bool legendary; // true only for Dodge - a tier above Super Rare (visual tint only)
    DamageType physType;  // physical school tag (SMASH/PIERCE), NONE if untyped
    DamageType physType2; // second physical school tag - only Finishing Blow uses both
    DamageType elemType;  // elemental tag (FIRE/POISON/WIND), NONE if untyped

public:
    Card(std::string n, std::string desc, CardType t, int c, int v,
         CardEffect e = CardEffect::NONE, bool isRare = false,
         DamageType physT = DamageType::NONE, DamageType elemT = DamageType::NONE,
         bool isSuperRare = false, DamageType physT2 = DamageType::NONE,
         bool isLegendary = false);

    std::string getName() const;
    std::string getDescription() const;
    CardType getType() const;
    std::string getTypeString() const;
    CardEffect getEffect() const;
    int getCost() const;
    int getValue() const;
    int getUpgradeCount() const;
    bool isRare() const;
    bool isSuperRare() const;
    bool isLegendary() const;
    bool isStarter() const; // true for the fixed starting-deck cards
    int getMaxUpgrades() const;   // starter caps at 1, common at 3, rare at 4, super rare/legendary at 5
    std::string getBaseName() const; // name with trailing '+' upgrade markers stripped
    DamageType getPhysType() const;
    DamageType getPhysType2() const;
    DamageType getElemType() const;
    std::string getTypeTag() const; // bracketed display tag, e.g. "[PIERCE][WIND]", empty if untyped

    void upgrade();
};

#endif
