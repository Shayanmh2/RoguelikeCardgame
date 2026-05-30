#include "RunStats.h"
#include <iostream>

RunStats::RunStats() 
    : totalEncountersWon(0), totalCardsCollected(0), totalRunsCompleted(0), 
      bestRunEncounters(0), cardsAddedThisRun(0) {}

void RunStats::addCardToRun() {
    cardsAddedThisRun++;
    totalCardsCollected++;
}

int RunStats::getCardsThisRun() const {
    return cardsAddedThisRun;
}

void RunStats::resetRunStats() {
    cardsAddedThisRun = 0;
}

void RunStats::completeRun(int encountersWon) {
    totalEncountersWon += encountersWon;
    totalRunsCompleted++;
    if (encountersWon > bestRunEncounters) {
        bestRunEncounters = encountersWon;
    }
}

int RunStats::getTotalEncountersWon() const {
    return totalEncountersWon;
}

int RunStats::getTotalCardsCollected() const {
    return totalCardsCollected;
}

int RunStats::getTotalRunsCompleted() const {
    return totalRunsCompleted;
}

int RunStats::getBestRunEncounters() const {
    return bestRunEncounters;
}

void RunStats::displayRunStats() const {
    std::cout << "\n========== THIS RUN ==========\n";
    std::cout << "Cards Collected: " << cardsAddedThisRun << "\n";
}

void RunStats::displayCumulativeStats() const {
    std::cout << "\n========== CUMULATIVE STATS ==========\n";
    std::cout << "Total Runs: " << totalRunsCompleted << "\n";
    std::cout << "Total Encounters Won: " << totalEncountersWon << "\n";
    std::cout << "Total Cards Collected: " << totalCardsCollected << "\n";
    std::cout << "Best Run: " << bestRunEncounters << " encounters\n";
    if (totalRunsCompleted > 0) {
        std::cout << "Average: " << (totalEncountersWon / totalRunsCompleted) << " encounters/run\n";
    }
    std::cout << "====================================\n";
}

void RunStats::displayRunSummary(int encountersWon) const {
    std::cout << "\n========== RUN SUMMARY ==========\n";
    std::cout << "Encounters Won: " << encountersWon << "\n";
    std::cout << "Cards Collected: " << cardsAddedThisRun << "\n";
    std::cout << "\n--- CUMULATIVE STATS ---\n";
    std::cout << "Total Runs: " << totalRunsCompleted << "\n";
    std::cout << "Total Encounters: " << totalEncountersWon << "\n";
    std::cout << "Best Run: " << bestRunEncounters << " encounters\n";
    std::cout << "================================\n";
}
