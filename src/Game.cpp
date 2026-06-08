#include "Game.h"
#include "UIHelper.h"
#include <iostream>
#include <random>

Game::Game() : playerDeck(), enemy("Enemy", 50, 8, 4, EnemyType::MELEE), currentRun(), playerHealth(100), maxPlayerHealth(100), playerArmor(0), playerEnergy(3), maxEnergy(3), turnNumber(1), playerTurnActive(true), running(false), inEncounter(false) {}

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
    
    // Apply upgrades (bonus starting cards)
    applyUpgrades();
    
    playerDeck.shuffle();
    
    running = true;
}

void Game::displayStatus() const {
    UIHelper::printCombatStatus(playerHealth, maxPlayerHealth, playerArmor, playerEnergy, maxEnergy,
                                 enemy.getName(), enemy.getHealth(), enemy.getMaxHealth(), 
                                 enemy.getArmor(), enemy.getBaseAttack(), enemy.getBaseDefense());
    
    // Display active upgrade bonuses if any
    int damageBonus = upgrades.getDamageBonus();
    int armorBonus = upgrades.getArmorBonus();
    if (damageBonus > 0 || armorBonus > 0) {
        std::cout << "ACTIVE BONUSES: ";
        if (damageBonus > 0) std::cout << "Damage +" << damageBonus << " ";
        if (armorBonus > 0) std::cout << "Armor +" << armorBonus << " ";
        std::cout << "\n\n";
    }
    
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
            int bonusDamage = playedCard.getValue() + upgrades.getDamageBonus();
            int damageDealt = calculateDamage(bonusDamage, enemy.getBaseDefense());
            enemy.takeDamage(damageDealt);
            std::cout << "  Dealt " << damageDealt << " damage to enemy! (Enemy HP: " 
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << ")\n";
        } else if (playedCard.getType() == CardType::DEFEND) {
            int bonusArmor = playedCard.getValue() + upgrades.getArmorBonus();
            playerArmor += bonusArmor;
            std::cout << "  Gained " << bonusArmor << " armor! (Total: " << playerArmor << ")\n";
        }
    } catch (const std::out_of_range& e) {
        std::cout << "Invalid card index! Use 'hand' to see your cards.\n";
    }
}

void Game::enemyTurn() {
    if (!enemy.isAlive()) return;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> rollDist(0, 99);
    int roll = rollDist(gen);

    auto doAttack = [&](int atk, bool pierceHalfArmor){
        int effectiveArmor = pierceHalfArmor ? (playerArmor / 2) : playerArmor;
        int actualDamage = atk - effectiveArmor;
        if (actualDamage < 0) actualDamage = 0;
        // Reduce player's armor by attack (or half if pierced)
        playerArmor -= (pierceHalfArmor ? atk/2 : atk);
        if (playerArmor < 0) playerArmor = 0;
        playerHealth -= actualDamage;
        if (playerHealth < 0) playerHealth = 0;
        std::cout << "Enemy attacks for " << actualDamage << " damage!\n";
        std::cout << "Player Health: " << playerHealth << "\n";
    };

    auto doDefend = [&](int amt){
        enemy.gainArmor(amt);
        std::cout << "Enemy defends and gains " << amt << " armor.\n";
    };

    EnemyType t = enemy.getType();
    int atk = enemy.getBaseAttack();
    int def = enemy.getBaseDefense();

    switch (t) {
        case EnemyType::MELEE:
            if (roll < 70) doAttack(atk, false);
            else doDefend(def);
            break;
        case EnemyType::RANGED:
            if (roll < 60) doAttack(atk, true); // pierce half armor
            else doDefend(std::max(1, def - 1));
            break;
        case EnemyType::TANK:
            if (roll < 65) doDefend(def + 3);
            else doAttack(std::max(1, atk - 2), false);
            break;
        case EnemyType::CASTER:
            // If low health, higher chance to heal
            if (enemy.getHealth() < enemy.getMaxHealth() / 3 && roll < 60) {
                int healAmt = 8 + (def / 2);
                enemy.heal(healAmt);
                std::cout << "Enemy casts heal and recovers " << healAmt << " HP!\n";
                std::cout << "Enemy Health: " << enemy.getHealth() << "/" << enemy.getMaxHealth() << "\n";
            } else if (roll < 65) {
                doDefend(std::max(1, def - 1));
            } else {
                doAttack(atk + 1, false);
            }
            break;
        default:
            // Fallback to simple attack
            doAttack(atk, false);
    }
}

void Game::displayTurnInfo() const {
    std::cout << "\n========== TURN " << turnNumber << " ==========\n";
    if (playerTurnActive) {
        std::cout << "Player's Turn - You have " << playerEnergy << " energy (can play " << playerEnergy 
                  << " card" << (playerEnergy != 1 ? "s" : "") << ")\n";
        std::cout << "Type 'end' to end your turn and let enemy act.\n";
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

    // Discard remaining hand cards and draw a fresh hand for next turn
    playerDeck.resetDeck();
    int drawCount = 5 + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    std::cout << "\n--- Your Turn (Turn " << turnNumber << ") ---\n";
    std::cout << "Energy: " << playerEnergy << "/" << maxEnergy << " | Drew " << drawCount << " cards.\n";
    playerDeck.displayHand();
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
        UIHelper::printGameOverScreen(false, currentRun.getEncountersWon(), runStats.getTotalCardsCollected());
        std::cout << "You were defeated! Better luck next time.\n";
    } else if (!enemy.isAlive()) {
        UIHelper::printGameOverScreen(true, currentRun.getEncountersWon(), runStats.getTotalCardsCollected());
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
        try {
            int index = std::stoi(input.substr(5));
            playCardFromHand(index);
            checkGameOver();

            // Auto-end turn if out of energy
            if (playerEnergy <= 0 && playerTurnActive) {
                std::cout << "\n[No energy left - ending your turn automatically]\n";
                endPlayerTurn();
                checkGameOver();
            }
        } catch (const std::exception&) {
            std::cout << "Usage: play INDEX (e.g. play 1). Type 'hand' to see your cards.\n";
        }
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
    // Reset deck: move any leftover hand/discard back so drawCard() can reshuffle them
    playerDeck.resetDeck();
    int drawCount = 5 + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    // Set up enemy for current encounter with scaled stats
    int health = currentRun.getEnemyHealth();
    int attack = currentRun.getEnemyAttack();
    int defense = currentRun.getEnemyDefense();
    
    // Choose enemy type (cycle by encounter for variety)
    int enc = currentRun.getCurrentEncounter();
    int typeIndex = (enc - 1) % 4;
    EnemyType etype;
    switch (typeIndex) {
        case 0: etype = EnemyType::MELEE; break;
        case 1: etype = EnemyType::RANGED; break;
        case 2: etype = EnemyType::TANK; break;
        case 3: etype = EnemyType::CASTER; break;
        default: etype = EnemyType::MELEE; break;
    }
    
    // Generate unique name based on type and encounter
    std::string name = Enemy::generateName(etype, enc);
    
    // Add type label for flavor
    std::string typeLabel = (etype == EnemyType::MELEE) ? "Melee" : (etype == EnemyType::RANGED) ? "Ranged" : (etype == EnemyType::TANK) ? "Tank" : "Caster";
    name += " (" + typeLabel + ")";

    enemy = Enemy(name, health, attack, defense, etype);
    inEncounter = true;
    turnNumber = 1;
    playerTurnActive = true;
    playerArmor = 0;
    playerEnergy = maxEnergy;
    
    currentRun.displayRunStats();
    
    // Get difficulty info
    std::string difficulty = currentRun.getEncounterDifficulty();
    std::string tierLabel = currentRun.getEncounterTier();
    UIHelper::printEncounterHeader(currentRun.getCurrentEncounter(), difficulty, tierLabel);
    
    displayStatus();
    displayTurnInfo();
}

void Game::nextEncounter() {
    // Load next encounter
    currentRun.nextEncounter();
    startEncounter();
}

void Game::restSite() {
    std::cout << "\n========== REST SITE ==========\n";
    int healAmount = maxPlayerHealth * 30 / 100;
    std::cout << "  [rest]  - Heal " << healAmount << " HP  (currently " << playerHealth << "/" << maxPlayerHealth << ")\n";
    std::cout << "  [forge] - Upgrade a card (+3 value, -1 cost)\n";
    std::cout << "  [skip]  - Press on without resting\n";
    std::cout << "> ";

    std::string input;
    std::getline(std::cin, input);

    if (input == "rest") {
        playerHealth = std::min(maxPlayerHealth, playerHealth + healAmount);
        std::cout << "You rest and recover " << healAmount << " HP. (HP: " << playerHealth << "/" << maxPlayerHealth << ")\n";
    } else if (input == "forge") {
        if (playerDeck.totalCards() == 0) {
            std::cout << "Your deck is empty — nothing to upgrade.\n";
            return;
        }
        playerDeck.displayAllCards();
        std::cout << "Choose a card to upgrade (1-" << playerDeck.totalCards() << ") or 0 to cancel:\n> ";
        std::string choice;
        std::getline(std::cin, choice);
        try {
            int idx = std::stoi(choice) - 1;
            if (idx < 0) {
                std::cout << "Cancelled.\n";
            } else if (playerDeck.upgradeCardAt(idx)) {
                std::cout << "Card upgraded!\n";
            } else {
                std::cout << "Invalid choice.\n";
            }
        } catch (...) {
            std::cout << "Invalid input.\n";
        }
    } else if (input == "skip") {
        std::cout << "You press on without resting.\n";
    } else {
        std::cout << "Invalid choice. Type rest, forge, or skip.\n";
        restSite();
    }

    std::cout << "================================\n";
}

void Game::handleEncounterWin() {
    currentRun.winEncounter();
    std::cout << "\n========== ENCOUNTER WON! ==========\n";
    std::cout << "Enemies Defeated: " << currentRun.getEncountersWon() << "\n";

    // Offer card reward
    offerCardReward();

    // Offer rest site
    restSite();

    // Ask to continue
    std::cout << "\nProceed to next encounter? [continue] or [end run]?\n";
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

void Game::offerCardReward() {
    // Generate 3 reward cards with rarity boost if active
    bool rarityBoost = upgrades.isActive(6);  // Rarity Boost upgrade (index 6)
    std::vector<Card> rewards = rewardPool.generateWeightedRewards(currentRun.getCurrentEncounter(), 3, rarityBoost);
    
    rewardPool.displayRewardChoices(rewards);
    
    std::string choice;
    std::getline(std::cin, choice);
    
    int choiceIndex = -1;
    try {
        choiceIndex = std::stoi(choice) - 1;
    } catch (...) {
        std::cout << "Invalid choice. Skipping reward.\n";
        return;
    }
    
    if (choiceIndex < 0 || choiceIndex >= (int)rewards.size()) {
        std::cout << "Invalid choice. Skipping reward.\n";
        return;
    }
    
    playerDeck.addCard(rewards[choiceIndex]);
    runStats.addCardToRun();
    std::cout << "\n✓ Added " << rewards[choiceIndex].getName() << " to your deck!\n";
}

void Game::applyUpgrades() {
    // Apply starting strike cards from upgrade
    if (upgrades.isActive(1)) {  // Extra Strike upgrade
        playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
        playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
    }
    
    // Apply health bonus
    int healthBonus = upgrades.getHealthBonus();
    maxPlayerHealth += healthBonus;
    playerHealth = maxPlayerHealth;

    // Apply energy bonus
    int energyBonus = upgrades.getEnergyBonus();
    maxEnergy += energyBonus;
    playerEnergy = maxEnergy;
}

void Game::selectUpgrades() {
    // Check for newly unlocked upgrades
    upgrades.checkAndUnlockUpgrades(runStats.getTotalEncountersWon(), runStats.getTotalCardsCollected());
    
    // Show upgrade selection
    upgrades.selectActiveUpgrades();
}

void Game::displayRunStats() const {
    currentRun.displayRunStats();
}

void Game::run() {
    init();
    
    UIHelper::printTitle();
    
    // Initialize upgrades for first run
    upgrades.checkAndUnlockUpgrades(0, 0);
    upgrades.displayUpgradeInfo();
    
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
                
                // Track run completion
                int encounters = currentRun.getEncountersWon();
                runStats.completeRun(encounters);
                runStats.displayRunSummary(encounters);
                
                currentRun.loseRun();
                inEncounter = false;
            } else {
                // Player won encounter
                handleEncounterWin();
                if (inEncounter) {
                    // Continue to next encounter, loop continues
                    continue;
                }
                // Player voluntarily ended the run — record stats now
                int encounters = currentRun.getEncountersWon();
                runStats.completeRun(encounters);
                runStats.displayRunSummary(encounters);
            }
            
            // Run ended, offer new run
            if (handleGameOverInput()) {
                // Show upgrade selection before new run
                selectUpgrades();
                
                // Reset for new run
                maxPlayerHealth = 100;  // Reset before applying upgrades
                playerHealth = maxPlayerHealth;
                playerArmor = 0;
                playerEnergy = 3;
                maxEnergy = 3;
                turnNumber = 1;
                playerTurnActive = true;
                playerDeck = Deck();
                init();
                
                runStats.resetRunStats();
                currentRun = Run();
                currentRun.startRun();
                
                upgrades.displayUpgradeInfo();
                startEncounter();
            } else {
                running = false;
                runStats.displayCumulativeStats();
            }
        }
        
        update();
    }
    
    std::cout << "Thanks for playing!\n";
}
