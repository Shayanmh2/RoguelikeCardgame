#ifndef GAME_H
#define GAME_H

#include "Deck.h"

class Game {
private:
    Deck playerDeck;
    int playerHealth;
    int enemyHealth;
    bool running;

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
