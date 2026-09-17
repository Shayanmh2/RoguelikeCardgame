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
    REND,       // SPECIAL: damage per enemy SWING rather than per turn
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
    HEAL,       // SPECIAL: restores the card's value in HP
    WARD,       // DEFEND: also blocks the next incoming ailment (Poison/Burn/Weak/Stun)
    TAUNT,      // SPECIAL: enemy is much more likely to attack for the next 2 of their turns
    FEAR,       // SPECIAL: the mirror - enemy is much more likely to brace instead
    TRUESTRIKE, // ATTACK: lands in full - no defense, resistance, parry or phase applies
    TRUE_DOUBLE,// ATTACK: Truestrike, twice. One card cannot carry two effects,
                //         and Reckoning needs both, so it gets its own value.

    // Cards that pay for their power. Each carries its own drawback, so the
    // effect is the whole deal rather than a modifier on a plain card.
    SCRAP,       // DEFEND: armor, and it nicks you for 1 on the way up
    RECKLESS,    // ATTACK: big hit, your cards deal 2 less next turn
    SELFWEAK,    // DEFEND: heavy armor, your own attacks soften this turn
    OVEREXTEND,  // ATTACK: ignores defense, costs you a card next turn
    BLOODPRICE,  // SPECIAL: energy now, paid for in HP
    WILDCHARGE,  // ATTACK: big hit, your armor is gone
    BERSERK,     // SPECIAL: you hit harder next turn and take more until then
    TURTLE,      // DEFEND: armor that persists, and you are Weakened while it does
    EMBERBLADE,  // ATTACK: burns them, and you catch fire too
    ADRENALINE,  // SPECIAL: energy now, borrowed from next turn
    SHATTERPOINT,// ATTACK: huge hit, one more card this turn
    BLOODPACT,   // SPECIAL: doubles your damage, paid for in HP
    UNSTABLEWARD,// DEFEND: armor and a long ward, a thin hand next turn
    ALLIN,       // ATTACK: your whole guard, thrown
    LASTSTAND,   // DEFEND: armor from your wounds, no healing after it
    BORROWED,    // SPECIAL: two turns now, one lost later
    PACTRUIN,    // ATTACK: every attack festers, and every card bleeds you
    SACRIFICE,   // SPECIAL: heal to full, the card leaves the fight
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
    bool legendary; // true only for Dodge Reversal - a tier above Super Rare (visual tint only)
    DamageType physType;  // physical school tag (SMASH/PIERCE), NONE if untyped
    DamageType physType2; // second physical school tag - only Finishing Blow uses both
    DamageType elemType;  // elemental tag (FIRE/POISON/WIND), NONE if untyped

public:
    // The one place a config/save string becomes an enum. There used to be a
    // copy of this in Game.cpp and another in RewardPool.cpp; an effect added
    // to one and not the other silently produced inert cards.
    // The one definition of the Strength ladder. There were four copies of this
    // expression (two here, two in Game.cpp) and they all had to be edited in step;
    // that is precisely how REND went missing from one of the two effect tables.
    double strengthMultiplier() const;

    // How low upgrades may drive this card's cost. Almost everything reaches 1;
    // a short list of genuinely strong cards stops at 2 so they cannot be fired
    // three and four times in a turn once fully upgraded. Legendaries are exempt -
    // they are rare enough already that reaching 1 is the reward.
    int minCost() const;

    // Heals top you up to a floor instead of adding a flat slice. A flat
    // percentage was worth MOST when you were already healthy, which is
    // backwards for an emergency button.
    //
    // `value` is the floor, as a percent of max HP. The card also always heals
    // at least a smaller "top-up" (two fifths of the floor), and the result is
    // whichever is larger - otherwise the card would pay 1% at 49% HP and 20%
    // at 51%, which is a cliff in exactly the wrong direction.
    static int healAmount(int value, int current, int maxHp);

    static CardEffect effectFromString(const std::string& s);
    static DamageType damageTypeFromString(const std::string& s);

    Card(std::string n, std::string desc, CardType t, int c, int v,
         CardEffect e = CardEffect::NONE, bool isRare = false,
         DamageType physT = DamageType::NONE, DamageType elemT = DamageType::NONE,
         bool isSuperRare = false, DamageType physT2 = DamageType::NONE,
         bool isLegendary = false, int savedUpgradeCount = 0); // savedUpgradeCount: only set when restoring a card from a save file

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
    // True for the cards that come with a cost attached: the card face
    // outlines those instead of outlining every rare and above.
    bool hasDrawback() const;
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
