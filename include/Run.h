#ifndef RUN_H
#define RUN_H

#include <string>

class Run {
private:
    int currentEncounter;
    int difficulty = 0;
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
    
    // Get scaled enemy stats for current encounter
    int getEnemyHealth() const;
    int getEnemyAttack() const;
    int getEnemyDefense() const;
    
    bool isBossEncounter() const;   // bosses at 10/20/30/40, then the Dragon at 49 and Shadow Knight at 50
    int  getBossIndex() const;      // 0..5 within the cycle: colossus, witch, thunder beast, hydra, dragon, shadow knight
    int  getCycle() const;          // the difficulty tier, for naming and story gating
    // Difficulty. Every run is encounters 1-50; Hard shifts the scaling as if
    // fifty fights had already come before.
    void setDifficulty(int tier);
    int  getDifficulty() const;
    int  getBossNumber() const;     // 1-based count of bosses up to and including this one, across cycles
    int  getRegularIndex() const;
    // Area bosses already behind this run, 0 to 5. The knight gets a piece of
    // himself back off each one, so this is also how whole he is.
    int  areaBossesCleared() const;   // 0..43: which of the 44 unique regular enemies this is (non-boss only)

    void displayRunStats() const;

private:
    int scaledEncounter() const;   // the encounter number the scaling sees
};

#endif
