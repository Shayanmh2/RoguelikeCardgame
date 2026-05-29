#include "Game.h"
#include <iostream>
#include <random>

Game::Game() : playerDeck(), enemy("Enemy", 50, 8, 4), currentRun(), playerHealth(100), maxPlayerHealth(100), playerArmor(0), playerEnergy(3), maxEnergy(3), turnNumber(1), playerTurnActive(true), running(false), inEncounter(false) {}

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

void Game::enemyTurn() {
    // Simple 50/50 AI: attack or defend
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, 1);
    int choice = dist(gen);

    if (!enemy.isAlive()) return;

    if (choice == 0) {
        // Attack
        int atk = enemy.getBaseAttack();
        int actualDamage = atk - playerArmor;
        if (actualDamage < 0) actualDamage = 0;
        // Reduce player's armor by the attack amount
        playerArmor -= atk;
        if (playerArmor < 0) playerArmor = 0;

        playerHealth -= actualDamage;
        if (playerHealth < 0) playerHealth = 0;

        std::cout << "Enemy attacks for " << actualDamage << " damage!\n";
        std::cout << "Player Health: " << playerHealth << "\n";
    } else {
        // Defend
        int amt = enemy.getBaseDefense();
        enemy.gainArmor(amt);
        std::cout << "Enemy defends and gains " << amt << " armor.\n";
    }
}

void Game::displayTurnInfo() const {
    std::cout << "\n========== TURN " << turnNumber << " ==========\n";
    if (playerTurnActive) {
        std::cout << "Player's Turn - Your move!\n";
    }
}

void Game::resetArmor() {
    playerArmor = 0;
    enemy.resetArmor();
}

void Game::endPlayerTurn() {
    playerTurnActive = false;
    std::cout << "\n--- Enemy's Turn ---\n";
    enemyTurn();
    resetArmor();
    turnNumber++;
    playerTurnActive = true;
    resetEnergy();
    std::cout << "\n--- Your Turn ---\n";
    std::cout << "Energy restored to " << playerEnergy << "/" << maxEnergy << ".\n";
}

bool Game::checkGameOver() {
    if (playerHealth <= 0) {
        return true;
    }
    if (!enemy.isAlive()) {
        return true;
    }
    return false;
}

void Game::displayGameOver() {
    if (playerHealth <= 0) {
        std::cout << "\n========== YOU LOST ==========\n";
        std::cout << "You were defeated! Better luck next time.\n";
    } else if (!enemy.isAlive()) {
        std::cout << "\n========== YOU WON! ==========\n";
        std::cout << "Enemy defeated! Onward to the next encounter!\n";
    }
}

bool Game::handleGameOverInput() {
    std::string input;
    std::cout << "\n[play] Play again or [quit]?\n";
    std::cout << "> ";
    std::getline(std::cin, input);
    
    if (input == "play") {
        return true;
    } else if (input == "quit") {
        return false;
    } else {
        std::cout << "Invalid choice. Please enter 'play' or 'quit'.\n";
        return handleGameOverInput();
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
        std::cout << "Commands: hand, status, draw, play INDEX, end, quit\n";
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
        checkGameOver();
    } else if (input == "end") {
        if (!playerTurnActive) {
            std::cout << "It's not your turn!\n";
            return;
        }
        endPlayerTurn();
        checkGameOver();
    }
}

void Game::update() {
    // Placeholder for game logic
}

void Game::render() const {
    displayStatus();
}

void Game::startEncounter() {
    // Set up enemy for current encounter with scaled stats
    int health = currentRun.getEnemyHealth();
    int attack = currentRun.getEnemyAttack();
    int defense = currentRun.getEnemyDefense();
    
    enemy = Enemy("Enemy", health, attack, defense);
    inEncounter = true;
    turnNumber = 1;
    playerTurnActive = true;
    playerArmor = 0;
    playerEnergy = 3;
    
    currentRun.displayRunStats();
    std::cout << "\n========== ENCOUNTER " << currentRun.getCurrentEncounter() << " ==========\n";
    displayStatus();
    displayTurnInfo();
}

void Game::nextEncounter() {
    // Load next encounter
    currentRun.nextEncounter();
    startEncounter();
}

void Game::handleEncounterWin() {
    currentRun.winEncounter();
    std::cout << "\n========== ENCOUNTER WON! ==========\n";
    std::cout << "Enemies Defeated: " << currentRun.getEncountersWon() << "\n";
    std::cout << "Proceed to next encounter? [continue] or [end run]?\n";
    std::cout << "> ";
    
    std::string input;
    std::getline(std::cin, input);
    
    if (input == "continue") {
        nextEncounter();
    } else {
        inEncounter = false;
        currentRun.loseRun();
    }
}

void Game::displayRunStats() const {
    currentRun.displayRunStats();
}

void Game::run() {
    init();
    
    std::cout << "Welcome to Roguelike Cardgame!\n";
    std::cout << "Type 'help' for commands.\n";
    
    currentRun.startRun();
    startEncounter();
    
    while (running) {
        std::cout << "> ";
        handleInput();
        
        if (checkGameOver()) {
            if (enemy.isAlive()) {
                // Player lost
                displayGameOver();
                std::cout << "\n========== RUN ENDED ==========\n";
                currentRun.displayRunStats();
                currentRun.loseRun();
                inEncounter = false;
            } else {
                // Player won encounter
                handleEncounterWin();
                if (inEncounter) {
                    // Continue to next encounter, loop continues
                    continue;
                }
            }
            
            // Run ended, offer new run
            if (handleGameOverInput()) {
                playerHealth = maxPlayerHealth;
                playerArmor = 0;
                playerEnergy = 3;
                turnNumber = 1;
                playerTurnActive = true;
                playerDeck = Deck();
                init();
                
                currentRun = Run();
                currentRun.startRun();
                startEncounter();
            } else {
                running = false;
            }
        }
        
        update();
    }
    
    std::cout << "Thanks for playing!\n";
}
