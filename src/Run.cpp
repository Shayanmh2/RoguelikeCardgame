#include "Run.h"
#include <iostream>

// One run cycle: 44 unique regulars, bosses at 10/20/30/40/49/50.
namespace {
    constexpr int CYCLE_LENGTH = 50;
    constexpr int BOSS_POSITIONS[] = {10, 20, 30, 40, 49, 50};
    constexpr int BOSS_COUNT = 6;

    int cyclePos(int encounter) { return ((encounter - 1) % CYCLE_LENGTH) + 1; }

    int bossSlot(int pos) {
        for (int i = 0; i < BOSS_COUNT; ++i)
            if (BOSS_POSITIONS[i] == pos) return i;
        return -1;
    }
}

Run::Run() : currentEncounter(0), encountersWon(0), runActive(false) {}

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

void Run::loadState(int encounter, int won) {
    currentEncounter = encounter;
    encountersWon = won;
    runActive = true;
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
    return 2 + (currentEncounter - 1) / 5; // gentler ramp, starts at 2
}

bool Run::isBossEncounter() const {
    return bossSlot(cyclePos(currentEncounter)) >= 0;
}

int Run::getBossIndex() const {
    int s = bossSlot(cyclePos(currentEncounter));
    return s < 0 ? 0 : s; // 0..5: colossus, witch, thunder beast, hydra, dragon, shadow knight
}

int Run::getCycle() const {
    return (currentEncounter - 1) / CYCLE_LENGTH; // 0 = main game, 1+ = endless
}

int Run::getBossNumber() const {
    return getCycle() * BOSS_COUNT + getBossIndex() + 1; // 1-based, across cycles
}

int Run::getRegularIndex() const {
    int pos = cyclePos(currentEncounter);
    int bossesBefore = 0;
    for (int i = 0; i < BOSS_COUNT; ++i)
        if (BOSS_POSITIONS[i] < pos) ++bossesBefore;
    return pos - 1 - bossesBefore; // 0..43 within the cycle
}

std::string Run::getDifficultyTier() const {
    // One name per 10-encounter boss-gated segment (1-10/11-20/.../41-50), so
    // the label now tracks the boss schedule instead of outpacing it - INSANE
    // no longer shows up until the endless cycles past the Shadow Knight.
    int tier = (currentEncounter - 1) / 10;
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

void Run::displayRunStats() const {
    std::cout << "Encounter " << currentEncounter;
    if (encountersWon > 0) std::cout << "  |  Won: " << encountersWon;
    std::cout << "\n";
}
