#include "Game.h"
#include <iostream>

Game::Game() : playerDeck(), enemy("Enemy", 50, 8, 4), playerHealth(100), playerArmor(0), playerEnergy(3), maxEnergy(3), running(false) {}

void Game::init() {
    // Initialize starting deck (10 cards total)
    playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
    playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
    playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
    playerDeck.addCard(Card("Bash", "Deal 8 damage", CardType::ATTACK, 2, 8));
    playerDeck.addCard(Card("Bash", "Deal 8 damage", CardType::ATTACK, 2, 8));
    playerDeck.addCard(Card("Defend", "Gain 5 armor", CardType::DEFEND, 1, 5));
    playerDeck.addCard(Card("Defend", "Gain 5 armor", CardType::DEFEND, 1, 5));
    playerDeck.addCard(Card("Defend", "Gain 5 armor", CardType::DEFEND, 1, 5));
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
    std::cout << "Player Health: " << playerHealth << " | Armor: " << playerArmor << " | Energy: " << playerEnergy << "/" << maxEnergy << " | ";
    enemy.displayStatus();
    std::cout << "----------------------------------------\n";
    playerDeck.displayDeck();
}

int Game::calculateDamage(int attackValue, int defenseValue) const {
    int damage = attackValue - defenseValue;
    return (damage < 0) ? 0 : damage;
}

bool Game::spendEnergy(int cost) {
    if (playerEnergy < cost) {
        std::cout << "Not enough energy! Need " << cost << " but only have " << playerEnergy << ".\n";
        return false;
    }
    playerEnergy -= cost;
    return true;
}

void Game::resetEnergy() {
    playerEnergy = maxEnergy;
}

void Game::playerAttack(int cardValue, int cost) {
    if (!spendEnergy(cost)) return;
    
    int damageDealt = calculateDamage(cardValue, enemy.getBaseDefense());
    enemy.takeDamage(damageDealt);
    std::cout << "You attacked for " << damageDealt << " damage! (Attack: " << cardValue 
              << " - Defense: " << enemy.getBaseDefense() << ")\n";
    std::cout << "Enemy Health: " << enemy.getHealth() << "/" << enemy.getMaxHealth() << "\n";
}

void Game::playerDefend(int cardValue, int cost) {
    if (!spendEnergy(cost)) return;
    
    playerArmor += cardValue;
    std::cout << "You gained " << cardValue << " armor! (Total armor: " << playerArmor << ")\n";
}

void Game::playCardFromHand(int index) {
    try {
        const Card& card = playerDeck.getCardFromHand(index - 1);
        
        if (!spendEnergy(card.getCost())) return;
        
        Card playedCard = playerDeck.playCard(index - 1);
        
        std::cout << "Played: [" << playedCard.getName() << "] (Cost: " << playedCard.getCost() << ")\n";
        
        if (playedCard.getType() == CardType::ATTACK) {
            int damageDealt = calculateDamage(playedCard.getValue(), enemy.getBaseDefense());
            enemy.takeDamage(damageDealt);
            std::cout << "  Dealt " << damageDealt << " damage to enemy! (Enemy HP: " 
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << ")\n";
        } else if (playedCard.getType() == CardType::DEFEND) {
            playerArmor += playedCard.getValue();
            std::cout << "  Gained " << playedCard.getValue() << " armor! (Total: " << playerArmor << ")\n";
        }
    } catch (const std::out_of_range& e) {
        std::cout << "Invalid card index! Use 'hand' to see your cards.\n";
    }
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
        std::cout << "Commands: hand, status, draw, play INDEX, rest, quit\n";
    } else if (input == "draw") {
        try {
            playerDeck.drawCard();
            std::cout << "Drew a card!\n";
        } catch (const std::exception& e) {
            std::cout << e.what() << "\n";
        }
    } else if (input.substr(0, 4) == "play") {
        int index = std::stoi(input.substr(5));
        playCardFromHand(index);
    } else if (input == "rest") {
        resetEnergy();
        std::cout << "You rested and restored energy to " << playerEnergy << "/" << maxEnergy << ".\n";
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
    
    displayStatus();
    while (running) {
        std::cout << "> ";
        handleInput();
        update();
    }
    
    std::cout << "how could you stop playing?\n";
}
