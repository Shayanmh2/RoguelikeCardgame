#ifndef STATUS_EFFECT_H
#define STATUS_EFFECT_H

#include <string>

// The three damage-over-time effects cost the same total damage for a given
// card value. What differs is the shape of the payout, which is the point:
// picking between them is a read on how long the fight is going to last.
enum class StatusType {
    POISON,   // half the value, six turns      - attrition, wins long fights
    BURN,     // 1.5x the value, two turns      - burst, worth most near the end
    REND,     // the value, per enemy swing     - punishes anything that attacks often
    STUN,     // skip next turn - always exactly 1 turn, never stacks
    WEAK,     // damage dealt is reduced (x1/1.5-2x) for N turns
    STRENGTH  // damage dealt is multiplied (x1.2-3) for N turns (self buff)
};

class StatusEffects {
private:
    int poisonDmg, poisonTurns;
    int burnDmg,   burnTurns;
    int rendDmg,   rendCharges;   // charges are enemy swings, not turns
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
    // Deepens what is already applied, past the usual caps: Poison lasts
    // `extra` more turns, Burn ticks `extra` harder, Rend gains `extra`
    // charges. The relics that strengthen your ailments use this.
    void extend(StatusType type, int extra);
    bool hasAny() const;
    bool hasPoison() const;
    bool hasBurn() const;
    bool hasRend() const;
    bool hasStun() const;
    bool hasWeak() const;
    bool hasStrength() const;

    int    processPoison();          // returns damage dealt, decrements duration
    int    processBurn();            // returns damage dealt, decrements duration
    int    processRend();            // returns damage dealt, spends one charge.
                                     // Called when the victim ATTACKS, not on the
                                     // turn tick - that is what separates it from
                                     // the other two.
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
