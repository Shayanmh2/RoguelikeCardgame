#ifndef RUN_H
#define RUN_H

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
    
    void displayRunStats() const;
};

#endif
