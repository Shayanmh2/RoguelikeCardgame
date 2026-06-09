#include "Run.h"
#include <iostream>

Run::Run() : currentEncounter(0), encountersWon(0), startingHealth(100), runActive(false) {}

void Run::startRun() {
    currentEncounter = 1;
    encountersWon = 0;
    runActive = true;
}

void Run::nextEncounter() {
    currentEncounter++;
}

void Run::winEncounter() {
    encountersWon++;
}

void Run::loseRun() {
    runActive = false;
}

int Run::getCurrentEncounter() const {
    return currentEncounter;
}

int Run::getEncountersWon() const {
    return encountersWon;
}

bool Run::isRunActive() const {
    return runActive;
}

// Improved scaling formula with difficulty tiers
// Each tier (every 5 encounters) increases multiplier
// Encounter 1-5: Tier 1, 6-10: Tier 2, 11-15: Tier 3, etc.
// Health scales quadratically, Attack scales linearly, Defense scales gradually

int Run::getEnemyHealth() const {
    int tier = (currentEncounter - 1) / 5;
    int tierMultiplier = 1 + (tier * 15);  // Each tier adds 15% more health
    int baseHealth = 25 + (currentEncounter - 1) * 6;  // Lowered from 50 to 25, scaling reduced from 8 to 6
    return (baseHealth * (100 + tierMultiplier)) / 100;
}

int Run::getEnemyAttack() const {
    int tier = (currentEncounter - 1) / 5;
    int tierBonus = tier * 2;  // Each tier adds +2 attack
    return 8 + (currentEncounter - 1) * 1 + tierBonus;
}

int Run::getEnemyDefense() const {
    // Defense scales every 3 encounters to make late game harder
    return 4 + (currentEncounter - 1) / 3;
}

bool Run::isBossEncounter() const {
    return currentEncounter % 5 == 0;
}

int Run::getBossIndex() const {
    return ((currentEncounter / 5) - 1) % 3;
}

std::string Run::getDifficultyTier() const {
    int tier = (currentEncounter - 1) / 5;
    switch (tier) {
        case 0: return "EASY";
        case 1: return "NORMAL";
        case 2: return "HARD";
        case 3: return "NIGHTMARE";
        case 4: return "IMPOSSIBLE";
        default: return "INSANE";
    }
}

std::string Run::getEncounterDifficulty() const {
    return getDifficultyTier();
}

std::string Run::getEncounterTier() const {
    int tier = (currentEncounter - 1) / 5;
    return "Tier " + std::to_string(tier + 1);
}

void Run::displayEncounterDifficulty() const {
    std::cout << "Difficulty: " << getDifficultyTier();
    int tier = (currentEncounter - 1) / 5;
    std::cout << " [Tier " << (tier + 1) << "]\n";
}

void Run::displayRunStats() const {
    std::cout << "\n========== RUN STATS ==========\n";
    std::cout << "Encounter: " << currentEncounter << "\n";
    std::cout << "Encounters Won: " << encountersWon << "\n";
    std::cout << "================================\n";
}
