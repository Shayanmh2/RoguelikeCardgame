#ifndef STATUS_EFFECT_H
#define STATUS_EFFECT_H

#include <string>

enum class StatusType {
    POISON,  // deal stacks damage/turn, stacks decrease by 1 each turn
    BURN,    // deal 5 damage/turn for N turns
    STUN,    // skip next turn (doesn't stack)
    WEAK     // -2 attack for N turns
};

class StatusEffects {
private:
    int poison;
    int burn;
    int stun;
    int weak;

public:
    StatusEffects();

    void apply(StatusType type, int amount);
    bool hasAny() const;

    int  processPoison();        // returns damage dealt, decrements stacks
    int  processBurn();          // returns 5 if burning, decrements duration
    bool processStun();          // returns true (and decrements) if stunned this turn
    int  getWeakPenalty() const; // flat attack reduction while WEAK is active
    void processWeak();          // decrements weak duration by 1

    void display(const std::string& prefix) const;
    std::string summary() const;  // inline colored string, empty if no effects
    void reset();
};

#endif
