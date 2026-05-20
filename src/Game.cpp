#include "Game.h"
#include <iostream>

Game::Game() : playerHealth(100), enemyHealth(50), running(false) {}

void Game::init() {
    // Initialize starting deck
    playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
    playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
    playerDeck.addCard(Card("Bash", "Deal 8 damage", CardType::ATTACK, 2, 8));
    playerDeck.addCard(Card("Defend", "Gain 5 armor", CardType::DEFEND, 1, 5));
    playerDeck.addCard(Card("Defend", "Gain 5 armor", CardType::DEFEND, 1, 5));
    
    playerDeck.shuffle();
    
    // Draw initialized hand
    for (int i = 0; i < 5; ++i) {
        try {
            playerDeck.drawCard();
        } catch (const std::exception& e) {
            std::cerr << e.what() << "\n";
        }
    }
    
    running = true;
}

void Game::displayStatus() const {
    std::cout << "\n----------------------------------------\n";
    std::cout << "Player Health: " << playerHealth << " | Enemy Health: " << enemyHealth << "\n";
    std::cout << "----------------------------------------\n";
    playerDeck.displayDeck();
}

void Game::handleInput() {
    std::string input;
    std::getline(std::cin, input);
    
    if (input == "quit") {
        running = false;
    } else if (input == "hand") {
        playerDeck.displayHand();
    } else if (input == "status") {
        displayStatus();
    } else if (input == "help") {
        std::cout << "Commands: hand, status, draw, quit\n";
    } else if (input == "draw") {
        try {
            playerDeck.drawCard();
            std::cout << "Drew a card!\n";
        } catch (const std::exception& e) {
            std::cout << e.what() << "\n";
        }
    }
}

void Game::update() {
    // Placeholder for game logic
}

void Game::render() const {
    displayStatus();
}

void Game::run() {
    init();
    
    std::cout << "Welcome to Roguelike Cardgame!\n";
    std::cout << "Type 'help' for commands.\n";
    
    while (running) {
        render();
        std::cout << "> ";
        handleInput();
        update();
    }
    
    std::cout << "how could you stop playing?\n";
}
