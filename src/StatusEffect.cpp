#include "StatusEffect.h"
#include <iostream>

StatusEffects::StatusEffects() : poison(0), burn(0), stun(0), weak(0) {}

void StatusEffects::apply(StatusType type, int amount) {
    switch (type) {
        case StatusType::POISON: poison += amount; break;
        case StatusType::BURN:   burn   += amount; break;
        case StatusType::STUN:   stun    = 1;      break; // stun doesn't stack
        case StatusType::WEAK:   weak   += amount; break;
    }
}

bool StatusEffects::hasAny() const {
    return poison > 0 || burn > 0 || stun > 0 || weak > 0;
}

int StatusEffects::processPoison() {
    int dmg = poison;
    if (poison > 0) poison--;
    return dmg;
}

int StatusEffects::processBurn() {
    if (burn <= 0) return 0;
    burn--;
    return 5;
}

bool StatusEffects::processStun() {
    if (stun > 0) { stun--; return true; }
    return false;
}

int StatusEffects::getWeakPenalty() const {
    return (weak > 0) ? 2 : 0;
}

void StatusEffects::processWeak() {
    if (weak > 0) weak--;
}

void StatusEffects::display(const std::string& prefix) const {
    if (!hasAny()) return;
    std::cout << prefix;
    if (poison > 0) std::cout << "[Poison " << poison << "] ";
    if (burn   > 0) std::cout << "[Burn "   << burn   << "] ";
    if (stun   > 0) std::cout << "[Stunned] ";
    if (weak   > 0) std::cout << "[Weak "   << weak   << "] ";
    std::cout << "\n";
}

void StatusEffects::reset() {
    poison = burn = stun = weak = 0;
}
