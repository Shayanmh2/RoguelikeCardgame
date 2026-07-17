#include "StatusEffect.h"
#include "Colors.h"
#include <iostream>
#include <sstream>
#include <algorithm>

// Fixed duration for poison/burn ticks - upgrades and rarity now scale the
// per-turn damage (the amount passed to apply()) instead of how long it lasts.
static const int DOT_BASE_DURATION = 3;
static const int DOT_MAX_DURATION  = 10;

// Weaken cards are likewise fixed at 3 turns regardless of rarity - only the
// divisor (1.5x/1.75x/2x less damage) scales with rarity instead.
static const int WEAK_DURATION = 3;

StatusEffects::StatusEffects()
    : poisonDmg(0), poisonTurns(0), burnDmg(0), burnTurns(0), stun(0),
      weakTurns(0), weakMult(1.5), strengthTurns(0), strengthMult(1.2) {}

void StatusEffects::apply(StatusType type, int amount, double weakMultiplier, double strengthMultiplier) {
    switch (type) {
        case StatusType::POISON:
            poisonDmg += amount;
            poisonTurns = std::min(DOT_MAX_DURATION, poisonTurns + DOT_BASE_DURATION);
            break;
        case StatusType::BURN:
            burnDmg += amount;
            burnTurns = std::min(DOT_MAX_DURATION, burnTurns + DOT_BASE_DURATION);
            break;
        case StatusType::STUN:
            stun = 1; // always exactly 1 turn, never stacks
            break;
        case StatusType::WEAK:
            // Reapplying keeps whichever is stronger rather than stacking - a
            // fresh Weaken shouldn't water down an active Sunder, and vice versa.
            weakMult  = (weakTurns > 0) ? std::max(weakMult, weakMultiplier) : weakMultiplier;
            weakTurns = WEAK_DURATION;
            break;
        case StatusType::STRENGTH:
            strengthMult = (strengthTurns > 0) ? std::max(strengthMult, strengthMultiplier) : strengthMultiplier;
            strengthTurns += amount;
            break;
    }
}

bool StatusEffects::hasAny() const {
    return poisonTurns > 0 || burnTurns > 0 || stun > 0 || weakTurns > 0 || strengthTurns > 0;
}

bool StatusEffects::hasPoison() const   { return poisonTurns > 0; }
bool StatusEffects::hasBurn() const     { return burnTurns > 0; }
bool StatusEffects::hasStun() const     { return stun > 0; }
bool StatusEffects::hasWeak() const     { return weakTurns > 0; }
bool StatusEffects::hasStrength() const { return strengthTurns > 0; }

int StatusEffects::processPoison() {
    if (poisonTurns <= 0) return 0;
    int dmg = poisonDmg;
    poisonTurns--;
    if (poisonTurns <= 0) poisonDmg = 0;
    return dmg;
}

int StatusEffects::processBurn() {
    if (burnTurns <= 0) return 0;
    int dmg = burnDmg;
    burnTurns--;
    if (burnTurns <= 0) burnDmg = 0;
    return dmg;
}

bool StatusEffects::processStun() {
    if (stun > 0) { stun--; return true; }
    return false;
}

double StatusEffects::getWeakMultiplier() const {
    return (weakTurns > 0) ? (1.0 / weakMult) : 1.0;
}

void StatusEffects::processWeak() {
    if (weakTurns > 0) weakTurns--;
}

double StatusEffects::getStrengthMultiplier() const {
    return (strengthTurns > 0) ? strengthMult : 1.0;
}

void StatusEffects::processStrength() {
    if (strengthTurns > 0) strengthTurns--;
}

void StatusEffects::display(const std::string& prefix) const {
    if (!hasAny()) return;
    std::cout << prefix;
    if (poisonTurns > 0) std::cout << Color::POISON_CLR   << "[Poison " << poisonDmg << "/turn x" << poisonTurns << "]" << Color::RESET << " ";
    if (burnTurns   > 0) std::cout << Color::BURN_CLR     << "[Burn "   << burnDmg   << "/turn x" << burnTurns   << "]" << Color::RESET << " ";
    if (stun        > 0) std::cout << Color::STUN_CLR     << "[Stunned]"                                          << Color::RESET << " ";
    if (weakTurns   > 0) std::cout << Color::WEAK_CLR     << "[Weak " << weakMult << "x for " << weakTurns << " turn" << (weakTurns != 1 ? "s" : "") << "]" << Color::RESET << " ";
    if (strengthTurns > 0) std::cout << Color::STRENGTH_CLR << "[Strength " << strengthMult << "x for " << strengthTurns << " turn" << (strengthTurns != 1 ? "s" : "") << "]" << Color::RESET << " ";
    std::cout << "\n";
}

std::string StatusEffects::summary() const {
    if (!hasAny()) return "";
    std::string s;
    if (poisonTurns > 0) s += std::string(" ") + Color::POISON_CLR   + "[Poison " + std::to_string(poisonDmg) + "/turn x" + std::to_string(poisonTurns) + "]" + Color::RESET;
    if (burnTurns   > 0) s += std::string(" ") + Color::BURN_CLR     + "[Burn "   + std::to_string(burnDmg)   + "/turn x" + std::to_string(burnTurns)   + "]" + Color::RESET;
    if (stun        > 0) s += std::string(" ") + Color::STUN_CLR     + "[Stunned]"                                                                          + Color::RESET;
    // "x" here is a multiplier, not a repeat count like Poison/Burn's - keep it
    // right next to the number it actually multiplies (weakMult/strengthMult),
    // with the turn count spelled out separately so the two can't be confused.
    if (weakTurns   > 0) {
        std::ostringstream m; m << weakMult;
        s += std::string(" ") + Color::WEAK_CLR + "[Weak " + m.str() + "x, " + std::to_string(weakTurns) + "t]" + Color::RESET;
    }
    if (strengthTurns > 0) {
        std::ostringstream m; m << strengthMult;
        s += std::string(" ") + Color::STRENGTH_CLR + "[Strength " + m.str() + "x, " + std::to_string(strengthTurns) + "t]" + Color::RESET;
    }
    return s;
}

void StatusEffects::reset() {
    poisonDmg = poisonTurns = burnDmg = burnTurns = stun = weakTurns = strengthTurns = 0;
    weakMult = 1.5;
    strengthMult = 1.2;
}
