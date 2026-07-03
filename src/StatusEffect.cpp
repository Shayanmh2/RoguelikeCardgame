#include "StatusEffect.h"
#include "Colors.h"
#include <iostream>

StatusEffects::StatusEffects() : poison(0), burn(0), stun(0), weak(0), strength(0) {}

void StatusEffects::apply(StatusType type, int amount) {
    switch (type) {
        case StatusType::POISON:   poison   += amount; break;
        case StatusType::BURN:     burn     += amount; break;
        case StatusType::STUN:     stun     += amount; break; // duration accumulates (each stun tick decrements by 1)
        case StatusType::WEAK:     weak     += amount; break;
        case StatusType::STRENGTH: strength += amount; break;
    }
}

bool StatusEffects::hasAny() const {
    return poison > 0 || burn > 0 || stun > 0 || weak > 0 || strength > 0;
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

int StatusEffects::getStrengthBonus() const {
    return (strength > 0) ? 3 : 0;
}

void StatusEffects::processStrength() {
    if (strength > 0) strength--;
}

void StatusEffects::display(const std::string& prefix) const {
    if (!hasAny()) return;
    std::cout << prefix;
    if (poison   > 0) std::cout << Color::POISON_CLR   << "[Poison "   << poison   << "]" << Color::RESET << " ";
    if (burn     > 0) std::cout << Color::BURN_CLR     << "[Burn "     << burn     << "]" << Color::RESET << " ";
    if (stun     > 0) std::cout << Color::STUN_CLR     << "[Stunned]"                     << Color::RESET << " ";
    if (weak     > 0) std::cout << Color::WEAK_CLR     << "[Weak "     << weak     << "]" << Color::RESET << " ";
    if (strength > 0) std::cout << Color::STRENGTH_CLR << "[Strength " << strength << "]" << Color::RESET << " ";
    std::cout << "\n";
}

std::string StatusEffects::summary() const {
    if (!hasAny()) return "";
    std::string s;
    if (poison   > 0) s += std::string(" ") + Color::POISON_CLR   + "[Poison "   + std::to_string(poison)   + "]" + Color::RESET;
    if (burn     > 0) s += std::string(" ") + Color::BURN_CLR     + "[Burn "     + std::to_string(burn)     + "]" + Color::RESET;
    if (stun     > 0) s += std::string(" ") + Color::STUN_CLR     + "[Stunned]"                                  + Color::RESET;
    if (weak     > 0) s += std::string(" ") + Color::WEAK_CLR     + "[Weak "     + std::to_string(weak)     + "]" + Color::RESET;
    if (strength > 0) s += std::string(" ") + Color::STRENGTH_CLR + "[Strength " + std::to_string(strength) + "]" + Color::RESET;
    return s;
}

void StatusEffects::reset() {
    poison = burn = stun = weak = strength = 0;
}
