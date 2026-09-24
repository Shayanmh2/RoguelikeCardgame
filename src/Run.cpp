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

void Run::setDifficulty(int tier) { difficulty = tier < 0 ? 0 : tier; }
int Run::getDifficulty() const { return difficulty; }

// What the scaling is told the encounter number is: Hard adds a cycle's
// worth of encounters to every stat formula.
int Run::scaledEncounter() const { return currentEncounter + difficulty * CYCLE_LENGTH; }

int Run::getEnemyHealth() const {
    const int enc = scaledEncounter();
    int tier = (enc - 1) / 5;
    // Scaled for a player who plays about three cards a turn with percentage
    // gear.
    int tierMultiplier = 1 + (tier * 8);   // each tier adds 8% more health
    int baseHealth = 25 + (enc - 1) * 5;
    return (baseHealth * (100 + tierMultiplier)) / 100;
}

int Run::getEnemyAttack() const {
    const int enc = scaledEncounter();
    int tier = (enc - 1) / 5;
    // +0.6 per encounter and +1 per tier: a steeper curve had enemies killing
    // the player in about five turns by encounter 20.
    int tierBonus = tier;
    return 7 + (enc - 1) * 6 / 10 + tierBonus;
}

int Run::getEnemyDefense() const {
    // Every 7 encounters, not 5. Defense is subtracted per hit, so it bites hardest
    // on the cheap cards a three-card turn relies on.
    return 2 + (scaledEncounter() - 1) / 7;
}

bool Run::isBossEncounter() const {
    return bossSlot(cyclePos(currentEncounter)) >= 0;
}

int Run::getBossIndex() const {
    int s = bossSlot(cyclePos(currentEncounter));
    return s < 0 ? 0 : s; // 0..5: colossus, witch, thunder beast, hydra, dragon, shadow knight
}

int Run::getCycle() const {
    // The tier the run is being played at: naming ("Greater"), the enemy
    // shade and the story gate all key off it.
    return difficulty;
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

int Run::areaBossesCleared() const {
    // The five that carry a piece of him: the peak's own boss is not one of
    // them. Counted off the encounter rather than stored, so a loaded save is
    // right without having to have written it down.
    int done = 0;
    const int pos = cyclePos(currentEncounter);
    for (int i = 0; i < BOSS_COUNT - 1; ++i)
        if (BOSS_POSITIONS[i] < pos) ++done;
    return done;
}

void Run::displayRunStats() const {
    std::cout << "Encounter " << currentEncounter;
    if (encountersWon > 0) std::cout << "  |  Won: " << encountersWon;
    std::cout << "\n";
}
