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
    
    bool isBossEncounter() const;   // true when currentEncounter % 8 == 0
    int  getBossIndex() const;       // cycles through the 5 boss types

    std::string getDifficultyTier() const;
    std::string getEncounterDifficulty() const;
    std::string getEncounterTier() const;
    void displayRunStats() const;
};

#endif
