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

void RunStats::displayCumulativeStats() const {
    std::cout << "\nCareer stats:\n";
    std::cout << "  Runs: " << totalRunsCompleted << "\n";
    std::cout << "  Encounters won: " << totalEncountersWon << "\n";
    std::cout << "  Cards collected: " << totalCardsCollected << "\n";
    std::cout << "  Best run: " << bestRunEncounters << " encounters\n";
    if (totalRunsCompleted > 0) {
        std::cout << "  Average: " << (totalEncountersWon / totalRunsCompleted) << " encounters/run\n";
    }
}

void RunStats::displayRunSummary(int encountersWon) const {
    std::cout << "\nRun over - " << encountersWon << " encounter" << (encountersWon != 1 ? "s" : "") << " won";
    std::cout << ", " << cardsAddedThisRun << " card" << (cardsAddedThisRun != 1 ? "s" : "") << " collected.\n";
    if (totalRunsCompleted > 0) {
        std::cout << "Best run: " << bestRunEncounters << " encounters\n";
    }
}
