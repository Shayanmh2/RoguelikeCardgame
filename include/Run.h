#ifndef RUN_H
#define RUN_H

#include <string>

class Run {
private:
    int currentEncounter;
    int encountersWon;
    bool runActive;
    
public:
    Run();
    
    void startRun();
    void nextEncounter();
    void winEncounter();
    void loseRun();
    void loadState(int encounter, int won); // restore from a save file - jumps straight to the given progress
    
    int getCurrentEncounter() const;
    int getEncountersWon() const;
    bool isRunActive() const;
    
    // Get scaled enemy stats for current encounter
    int getEnemyHealth() const;
    int getEnemyAttack() const;
    int getEnemyDefense() const;
    
    bool isBossEncounter() const;   // bosses at 10/20/30/40, then the Dragon at 49 and Shadow Knight at 50
    int  getBossIndex() const;      // 0..5 within the cycle: colossus, witch, thunder beast, hydra, dragon, shadow knight
    int  getCycle() const;          // 0 = main game, 1+ = endless repeats of the cycle
    int  getBossNumber() const;     // 1-based count of bosses up to and including this one, across cycles
    int  getRegularIndex() const;   // 0..43: which of the 44 unique regular enemies this is (non-boss only)

    std::string getDifficultyTier() const;
    std::string getEncounterDifficulty() const;
    std::string getEncounterTier() const;
    void displayRunStats() const;
};

#endif
