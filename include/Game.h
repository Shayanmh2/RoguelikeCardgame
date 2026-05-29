#ifndef GAME_H
#define GAME_H

#include "Deck.h"
#include "Enemy.h"
#include "Run.h"

class Game {
private:
    Deck playerDeck;
    Enemy enemy;
    Run currentRun;
    int playerHealth;
    int maxPlayerHealth;
    int playerArmor;
    int playerEnergy;
    int maxEnergy;
    int turnNumber;
    bool playerTurnActive;
    bool running;
    bool inEncounter;
    
    int calculateDamage(int attackValue, int defenseValue) const;
    void playerAttack(int cardValue, int cost);
    void playerDefend(int cardValue, int cost);
    bool spendEnergy(int cost);
    void resetEnergy();
    void playCardFromHand(int index);
    void enemyTurn();
    void endPlayerTurn();
    void resetArmor();
    void displayTurnInfo() const;
    bool checkGameOver();
    void displayGameOver();
    bool handleGameOverInput();
    void startEncounter();
    void nextEncounter();
    void handleEncounterWin();
    void displayRunStats() const;

public:
    Game();
    
    void init();
    void run();
    void displayStatus() const;
    void handleInput();
    void update();
    void render() const;
};

#endif
