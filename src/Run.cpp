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

// Scaling formula: each encounter increases difficulty
// Health: base 50 + (encounter - 1) * 10
// Attack: base 8 + (encounter - 1) * 1
// Defense: base 4 (stays constant for now)
int Run::getEnemyHealth() const {
    return 50 + (currentEncounter - 1) * 10;
}

int Run::getEnemyAttack() const {
    return 8 + (currentEncounter - 1) * 1;
}

int Run::getEnemyDefense() const {
    return 4;
}

void Run::displayRunStats() const {
    std::cout << "\n========== RUN STATS ==========\n";
    std::cout << "Encounter: " << currentEncounter << "\n";
    std::cout << "Encounters Won: " << encountersWon << "\n";
    std::cout << "================================\n";
}
