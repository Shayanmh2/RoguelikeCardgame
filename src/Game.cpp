#include "Game.h"
#include "Colors.h"
#include "UIHelper.h"
#include <algorithm>
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
    
    // Status effects
    playerStatus.display("  YOU:   ");
    enemy.displayStatusEffects("  ENEMY: ");

    // Active upgrade bonuses
    int damageBonus = upgrades.getDamageBonus();
    int armorBonus = upgrades.getArmorBonus();
    if (damageBonus > 0 || armorBonus > 0) {
        std::cout << "BONUSES: ";
        if (damageBonus > 0) std::cout << "Damage +" << damageBonus << " ";
        if (armorBonus > 0) std::cout << "Armor +"  << armorBonus  << " ";
        std::cout << "\n";
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

void Game::applyCardEffect(const Card& card) {
    int val = card.getValue();
    switch (card.getEffect()) {
        case CardEffect::POISON:
            enemy.applyStatus(StatusType::POISON, val);
            std::cout << "  " << Color::POISON_CLR << "Applied " << val << " Poison to enemy!" << Color::RESET << "\n";
            break;
        case CardEffect::BURN:
            enemy.applyStatus(StatusType::BURN, val);
            std::cout << "  " << Color::BURN_CLR << "Applied " << val << " Burn to enemy! (5 dmg/turn for " << val << " turns)" << Color::RESET << "\n";
            break;
        case CardEffect::STUN:
            enemy.applyStatus(StatusType::STUN, 1);
            std::cout << "  " << Color::STUN_CLR << "Enemy is STUNNED — they'll lose their next turn!" << Color::RESET << "\n";
            break;
        case CardEffect::WEAK:
            enemy.applyStatus(StatusType::WEAK, val);
            std::cout << "  " << Color::WEAK_CLR << "Applied Weak " << val << " to enemy! (-2 attack for " << val << " turns)" << Color::RESET << "\n";
            break;
        default:
            break;
    }
}

void Game::playCardFromHand(int index) {
    try {
        const Card& card = playerDeck.getCardFromHand(index - 1);

        if (!spendEnergy(card.getCost())) return;

        Card playedCard = playerDeck.playCard(index - 1);

        std::cout << "Played: [" << playedCard.getName() << "] (Cost: " << playedCard.getCost() << ")\n";

        if (playedCard.getType() == CardType::ATTACK) {
            int weakPenalty = playerStatus.getWeakPenalty();
            int bonusDamage = std::max(0, playedCard.getValue() + upgrades.getDamageBonus() - weakPenalty);
            int damageDealt = calculateDamage(bonusDamage, enemy.getBaseDefense());
            enemy.takeDamage(damageDealt);
            std::cout << "  " << Color::PLAYER_ATTACK << "Dealt " << damageDealt << " damage to enemy!"
                      << Color::RESET << " (Enemy HP: "
                      << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")";
            if (weakPenalty > 0)
                std::cout << " " << Color::WEAK_CLR << "[Weakened -" << weakPenalty << "]" << Color::RESET;
            std::cout << "\n";
        } else if (playedCard.getType() == CardType::DEFEND) {
            int bonusArmor = playedCard.getValue() + upgrades.getArmorBonus();
            playerArmor += bonusArmor;
            std::cout << "  " << Color::ARMOR_CLR << "Gained " << bonusArmor << " armor!"
                      << Color::RESET << " (Total: " << Color::ARMOR_CLR << playerArmor << Color::RESET << ")\n";
        } else if (playedCard.getType() == CardType::SPECIAL) {
            applyCardEffect(playedCard);
        }
    } catch (const std::out_of_range& e) {
        std::cout << "Invalid card index! Use 'hand' to see your cards.\n";
    }
}

void Game::enemyTurn() {
    if (!enemy.isAlive()) return;

    // Tick enemy status effects at the start of their turn
    int poisonDmg = enemy.processPoison();
    if (poisonDmg > 0) {
        enemy.takeDamageRaw(poisonDmg);
        std::cout << Color::POISON_CLR << "Poison:" << Color::RESET
                  << " enemy takes " << Color::PLAYER_ATTACK << poisonDmg << Color::RESET << " damage! ("
                  << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << " HP)\n";
        if (!enemy.isAlive()) return;
    }
    int burnDmg = enemy.processBurn();
    if (burnDmg > 0) {
        enemy.takeDamageRaw(burnDmg);
        std::cout << Color::BURN_CLR << "Burn:" << Color::RESET
                  << " enemy takes " << Color::PLAYER_ATTACK << burnDmg << Color::RESET << " damage! ("
                  << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << " HP)\n";
        if (!enemy.isAlive()) return;
    }
    if (enemy.processStun()) {
        std::cout << Color::STUN_CLR << "Enemy is STUNNED and loses their turn!" << Color::RESET << "\n";
        return;
    }

    // Bosses have their own AI
    if (enemy.isBoss()) {
        bossAction();
        return;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> rollDist(0, 99);
    int roll = rollDist(gen);

    // Apply WEAK penalty to attack, then tick it
    int weakPenalty = enemy.getWeakPenalty();
    enemy.processWeak();

    auto doAttack = [&](int atk, bool pierceHalfArmor) {
        atk = std::max(0, atk - weakPenalty);
        int effectiveArmor = pierceHalfArmor ? (playerArmor / 2) : playerArmor;
        int actualDamage = atk - effectiveArmor;
        if (actualDamage < 0) actualDamage = 0;
        playerArmor -= (pierceHalfArmor ? atk / 2 : atk);
        if (playerArmor < 0) playerArmor = 0;
        playerHealth -= actualDamage;
        if (playerHealth < 0) playerHealth = 0;
        std::cout << Color::DAMAGE << "Enemy attacks for " << actualDamage << " damage!" << Color::RESET;
        if (weakPenalty > 0)
            std::cout << " " << Color::WEAK_CLR << "[Weakened -" << weakPenalty << "]" << Color::RESET;
        std::cout << "  HP: " << hpColor(playerHealth, maxPlayerHealth)
                  << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
    };

    auto doDefend = [&](int amt) {
        enemy.gainArmor(amt);
        std::cout << Color::ARMOR_CLR << "Enemy defends and gains " << amt << " armor." << Color::RESET << "\n";
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
            if (roll < 60) {
                doAttack(atk, true); // pierce half armor
            } else if (roll < 80) {
                doDefend(std::max(1, def - 1));
            } else {
                playerStatus.apply(StatusType::WEAK, 2);
                std::cout << Color::WEAK_CLR << "Enemy fires a crippling shot! You are Weakened for 2 turns." << Color::RESET << "\n";
            }
            break;
        case EnemyType::TANK:
            if (roll < 65) doDefend(def + 3);
            else doAttack(std::max(1, atk - 2), false);
            break;
        case EnemyType::CASTER:
            if (enemy.getHealth() < enemy.getMaxHealth() / 3 && roll < 60) {
                int healAmt = 8 + (def / 2);
                enemy.heal(healAmt);
                std::cout << "Enemy casts heal and recovers " << healAmt << " HP! ("
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << ")\n";
            } else if (roll < 40) {
                playerStatus.apply(StatusType::POISON, 3);
                std::cout << Color::POISON_CLR << "Enemy casts Poison Bolt! You are poisoned for 3 stacks." << Color::RESET << "\n";
            } else if (roll < 60) {
                playerStatus.apply(StatusType::BURN, 2);
                std::cout << Color::BURN_CLR << "Enemy casts Fireball! You are burning for 2 turns." << Color::RESET << "\n";
            } else {
                doAttack(atk + 1, false);
            }
            break;
        default:
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

    // Tick player status effects (start of player's new turn)
    int playerPoisonDmg = playerStatus.processPoison();
    if (playerPoisonDmg > 0) {
        playerHealth = std::max(0, playerHealth - playerPoisonDmg);
        std::cout << Color::POISON_CLR << "Poison:" << Color::RESET
                  << " you take " << Color::DAMAGE << playerPoisonDmg << Color::RESET
                  << " damage! (HP: " << hpColor(playerHealth, maxPlayerHealth)
                  << playerHealth << Color::RESET << ")\n";
    }
    int playerBurnDmg = playerStatus.processBurn();
    if (playerBurnDmg > 0) {
        playerHealth = std::max(0, playerHealth - playerBurnDmg);
        std::cout << Color::BURN_CLR << "Burn:" << Color::RESET
                  << " you take " << Color::DAMAGE << playerBurnDmg << Color::RESET
                  << " damage! (HP: " << hpColor(playerHealth, maxPlayerHealth)
                  << playerHealth << Color::RESET << ")\n";
    }
    // WEAK ticks at end of player turn (after all attacks are resolved)
    playerStatus.processWeak();

    // Discard remaining hand cards and draw a fresh hand for next turn
    playerDeck.resetDeck();
    int drawCount = 5 + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    std::cout << "\n--- Your Turn (Turn " << turnNumber << ") ---\n";
    std::cout << "Energy: " << playerEnergy << "/" << maxEnergy << " | Drew " << drawCount << " cards.\n";
    playerStatus.display("Status: ");
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

Enemy Game::generateBossEnemy() {
    int enc = currentRun.getCurrentEncounter();
    int bossHealth  = currentRun.getEnemyHealth() * 3;
    int bossAttack  = currentRun.getEnemyAttack() * 3 / 2;
    int bossDefense = currentRun.getEnemyDefense() + 2;

    std::string name;
    EnemyType   etype;
    BossType    btype;

    switch (currentRun.getBossIndex()) {
        case 1:
            name  = "The Vile Witch";
            etype = EnemyType::CASTER;
            btype = BossType::VILE_WITCH;
            break;
        case 2:
            name  = "The Warlord";
            etype = EnemyType::MELEE;
            btype = BossType::WARLORD;
            break;
        default: // 0, and fallback
            name  = "The Stone Colossus";
            etype = EnemyType::TANK;
            btype = BossType::STONE_COLOSSUS;
            break;
    }

    // Add tier suffix on repeat cycles
    int cycle = (enc / 5 - 1) / 3;
    if (cycle == 1) name = "Ancient " + name;
    else if (cycle >= 2) name = "Eternal " + name;

    Enemy boss(name, bossHealth, bossAttack, bossDefense, etype);
    boss.setBossType(btype);
    if (btype == BossType::STONE_COLOSSUS) boss.gainArmor(10);
    return boss;
}

void Game::bossAction() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> rollDist(0, 99);
    int roll = rollDist(gen);

    int weakPenalty = enemy.getWeakPenalty();
    enemy.processWeak();
    int atk = std::max(0, enemy.getBaseAttack() + enemy.getBonusAttack() - weakPenalty);

    auto doAttack = [&](int damage, bool raw) {
        if (raw) {
            playerHealth = std::max(0, playerHealth - damage);
            std::cout << Color::BOLD << Color::DAMAGE << "  BOSS slams for " << damage
                      << " (ignores armor)!" << Color::RESET
                      << " HP: " << hpColor(playerHealth, maxPlayerHealth)
                      << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
        } else {
            int actual = std::max(0, damage - playerArmor);
            playerArmor = std::max(0, playerArmor - damage);
            playerHealth = std::max(0, playerHealth - actual);
            std::cout << Color::BOLD << Color::DAMAGE << "  BOSS strikes for " << actual
                      << " damage!" << Color::RESET
                      << " HP: " << hpColor(playerHealth, maxPlayerHealth)
                      << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
        }
        if (weakPenalty > 0)
            std::cout << "  " << Color::WEAK_CLR << "[Weakened -" << weakPenalty << "]" << Color::RESET << "\n";
    };

    switch (enemy.getBossType()) {
        case BossType::STONE_COLOSSUS:
            if (roll < 15) {
                std::cout << Color::BOLD << Color::MAGENTA << "Stone Colossus uses EARTHQUAKE SLAM!" << Color::RESET << "\n";
                doAttack(15, true);
            } else if (roll < 45) {
                enemy.gainArmor(8);
                std::cout << Color::MAGENTA << "Stone Colossus hardens!" << Color::RESET
                          << " +" << Color::ARMOR_CLR << 8 << Color::RESET
                          << " armor (" << enemy.getArmor() << " total)\n";
            } else {
                std::cout << Color::MAGENTA << "Stone Colossus strikes!" << Color::RESET << "\n";
                doAttack(atk + 4, false);
            }
            break;

        case BossType::VILE_WITCH:
            if (roll < 30) {
                std::cout << Color::MAGENTA << "Vile Witch attacks!" << Color::RESET << "\n";
                doAttack(atk, false);
            } else if (roll < 70) {
                playerStatus.apply(StatusType::POISON, 4);
                playerStatus.apply(StatusType::BURN, 2);
                std::cout << Color::BOLD << Color::MAGENTA << "Vile Witch casts PLAGUE!" << Color::RESET
                          << " You gain " << Color::POISON_CLR << "Poison 4" << Color::RESET
                          << " and " << Color::BURN_CLR << "Burn 2" << Color::RESET << "!\n";
            } else if (roll < 85) {
                int healAmt = 20;
                enemy.heal(healAmt);
                std::cout << Color::MAGENTA << "Vile Witch siphons life, healing " << Color::HEAL
                          << healAmt << " HP!" << Color::RESET << " ("
                          << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
            } else {
                playerStatus.apply(StatusType::POISON, 6);
                std::cout << Color::BOLD << Color::MAGENTA << "Vile Witch casts TOXIC ERUPTION!" << Color::RESET
                          << " You gain " << Color::POISON_CLR << "Poison 6" << Color::RESET << "!\n";
            }
            break;

        case BossType::WARLORD:
            if (roll < 15) {
                playerStatus.apply(StatusType::WEAK, 3);
                std::cout << Color::BOLD << Color::MAGENTA << "Warlord roars a BATTLECRY!" << Color::RESET
                          << " You are " << Color::WEAK_CLR << "Weakened 3" << Color::RESET << "!\n";
            }
            std::cout << Color::MAGENTA << "Warlord attacks!" << Color::RESET << "\n";
            doAttack(atk, false);
            if (enemy.getBonusAttack() < 6) {
                enemy.addBonusAttack(1);
                std::cout << Color::MAGENTA << "Warlord grows stronger!" << Color::RESET
                          << " (total bonus +" << Color::RED << enemy.getBonusAttack() << Color::RESET << " attack)\n";
            }
            break;

        default:
            doAttack(atk, false);
    }
}

void Game::offerBossReward() {
    std::cout << "\n========== BOSS REWARD ==========\n";
    std::cout << "Choose 1 of 2 RARE cards:\n\n";
    std::vector<Card> rewards = rewardPool.generateRareRewards(2);
    rewardPool.displayRewardChoices(rewards);

    std::string choice;
    std::getline(std::cin, choice);

    int choiceIndex = -1;
    try { choiceIndex = std::stoi(choice) - 1; } catch (...) {}

    if (choiceIndex < 0 || choiceIndex >= (int)rewards.size()) {
        std::cout << "Invalid choice. Skipping reward.\n";
        return;
    }
    playerDeck.addCard(rewards[choiceIndex]);
    runStats.addCardToRun();
    std::cout << "\n[RARE] Added " << rewards[choiceIndex].getName() << " to your deck!\n";
}

void Game::startEncounter() {
    // Reset deck: move any leftover hand/discard back so drawCard() can reshuffle them
    playerDeck.resetDeck();
    int drawCount = 5 + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    if (currentRun.isBossEncounter()) {
        enemy = generateBossEnemy();
    } else {
        int health  = currentRun.getEnemyHealth();
        int attack  = currentRun.getEnemyAttack();
        int defense = currentRun.getEnemyDefense();

        int enc = currentRun.getCurrentEncounter();
        int typeIndex = (enc - 1) % 4;
        EnemyType etype;
        switch (typeIndex) {
            case 0: etype = EnemyType::MELEE;  break;
            case 1: etype = EnemyType::RANGED; break;
            case 2: etype = EnemyType::TANK;   break;
            case 3: etype = EnemyType::CASTER; break;
            default: etype = EnemyType::MELEE; break;
        }
        std::string name = Enemy::generateName(etype, enc);
        std::string typeLabel = (etype == EnemyType::MELEE) ? "Melee"
                              : (etype == EnemyType::RANGED) ? "Ranged"
                              : (etype == EnemyType::TANK)   ? "Tank" : "Caster";
        name += " (" + typeLabel + ")";
        enemy = Enemy(name, health, attack, defense, etype);
    }

    inEncounter = true;
    turnNumber = 1;
    playerTurnActive = true;
    playerArmor = 0;
    playerEnergy = maxEnergy;
    playerStatus.reset();

    currentRun.displayRunStats();

    if (currentRun.isBossEncounter()) {
        UIHelper::printBossHeader(currentRun.getCurrentEncounter(), enemy.getName());
    } else {
        UIHelper::printEncounterHeader(currentRun.getCurrentEncounter(),
                                       currentRun.getEncounterDifficulty(),
                                       currentRun.getEncounterTier());
    }

    displayStatus();
    displayTurnInfo();
}

void Game::nextEncounter() {
    // Load next encounter
    currentRun.nextEncounter();
    startEncounter();
}

void Game::restSite() {
    std::cout << "\n" << Color::BOLD << Color::CYAN << "========== REST SITE ==========" << Color::RESET << "\n";
    int healAmount = maxPlayerHealth * 30 / 100;
    std::cout << "  [rest]  - Heal " << healAmount << " HP  (currently " << playerHealth << "/" << maxPlayerHealth << ")\n";
    std::cout << "  [forge] - Upgrade a card (+3 value, -1 cost)\n";
    std::cout << "  [skip]  - Press on without resting\n";
    std::cout << "> ";

    std::string input;
    std::getline(std::cin, input);

    if (input == "rest") {
        playerHealth = std::min(maxPlayerHealth, playerHealth + healAmount);
        std::cout << Color::HEAL << "You rest and recover " << healAmount << " HP." << Color::RESET
                  << " (HP: " << hpColor(playerHealth, maxPlayerHealth) << playerHealth << "/" << maxPlayerHealth << Color::RESET << ")\n";
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
    std::cout << "\n" << Color::BOLD << Color::GREEN << "========== ENCOUNTER WON! ==========" << Color::RESET << "\n";
    std::cout << "Enemies Defeated: " << Color::GREEN << currentRun.getEncountersWon() << Color::RESET << "\n";

    // Boss reward is always 2 rare cards; normal encounters use weighted rewards
    if (enemy.isBoss()) {
        offerBossReward();
    } else {
        offerCardReward();
    }

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
