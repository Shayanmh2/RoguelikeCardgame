#ifndef RUN_H
#define RUN_H

#include <string>

class Run {
private:
    int currentEncounter;
    int encountersWon;
    int startingHealth;
    bool runActive;
    
public:
    Run();
    
    void startRun();
    void nextEncounter();
    void winEncounter();
    void loseRun();
    
    int getCurrentEncounter() const;
    int getEncountersWon() const;
    bool isRunActive() const;
    
    // Get scaled enemy stats for current encounter
    int getEnemyHealth() const;
    int getEnemyAttack() const;
    int getEnemyDefense() const;
    
    bool isBossEncounter() const;   // true when currentEncounter % 5 == 0
    int  getBossIndex() const;       // 0/1/2 cycling for the three boss types

    std::string getDifficultyTier() const;
    std::string getEncounterDifficulty() const;
    std::string getEncounterTier() const;
    void displayRunStats() const;
    void displayEncounterDifficulty() const;
};

#endif
