#ifndef STATUS_EFFECT_H
#define STATUS_EFFECT_H

#include <string>

enum class StatusType {
    POISON,   // damage/turn for a fixed duration; stacking raises the damage, not the duration
    BURN,     // same shape as poison, separate pool
    STUN,     // skip next turn - always exactly 1 turn, never stacks
    WEAK,     // damage dealt is reduced (x1/1.5-2x) for N turns
    STRENGTH  // damage dealt is multiplied (x1.2-3) for N turns (self buff)
};

class StatusEffects {
private:
    int poisonDmg, poisonTurns;
    int burnDmg,   burnTurns;
    int stun;
    int weakTurns;
    double weakMult;
    int strengthTurns;
    double strengthMult;

public:
    StatusEffects();

    // amount is a duration (in turns) for WEAK/STRENGTH, a flat per-turn damage
    // contribution for POISON/BURN (duration is fixed, see .cpp), and ignored for
    // STUN (always exactly 1 turn, never stacks). weakMultiplier/strengthMultiplier
    // only matter for WEAK/STRENGTH respectively - reapplying while already active
    // keeps whichever is stronger rather than stacking.
    void apply(StatusType type, int amount, double weakMultiplier = 1.5, double strengthMultiplier = 1.2);
    bool hasAny() const;

    int    processPoison();          // returns damage dealt, decrements duration
    int    processBurn();            // returns damage dealt, decrements duration
    bool   processStun();            // returns true (and decrements) if stunned this turn
    double getWeakMultiplier() const;     // damage dealt is multiplied by this while WEAK is active
    void   processWeak();            // decrements weak duration by 1
    double getStrengthMultiplier() const; // damage dealt is multiplied by this while STRENGTH is active
    void   processStrength();        // decrements strength duration by 1

    void display(const std::string& prefix) const;
    std::string summary() const;  // inline colored string, empty if no effects
    void reset();
};

#endif
