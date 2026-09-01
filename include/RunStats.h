#ifndef RUNSTATS_H
#define RUNSTATS_H

#include <string>

class RunStats {
private:
    int totalEncountersWon;
    int totalCardsCollected;
    int totalRunsCompleted;
    int bestRunEncounters;
    int cardsAddedThisRun;
    
public:
    RunStats();
    
    // Current run tracking
    void addCardToRun();
    void resetRunStats();
    
    // Run completion
    void completeRun(int encountersWon);
    
    // Persistent stats
    int getTotalEncountersWon() const;
    int getTotalCardsCollected() const;
    
    void displayCumulativeStats() const;
    void displayRunSummary(int encountersWon) const;
};

#endif
