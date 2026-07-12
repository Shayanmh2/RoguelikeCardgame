#include "Run.h"
#include <iostream>

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
    return currentEncounter % 8 == 0; // first boss at encounter 8
}

int Run::getBossIndex() const {
    return ((currentEncounter / 8) - 1) % 5; // 5 bosses in rotation
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

void Run::displayRunStats() const {
    std::cout << "Encounter " << currentEncounter;
    if (encountersWon > 0) std::cout << "  |  Won: " << encountersWon;
    std::cout << "\n";
}
