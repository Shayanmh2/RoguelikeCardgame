#include "Game.h"
#include "Audio.h"
#include "Colors.h"
#include "UIHelper.h"
#include <algorithm>
#include <iostream>
#include <random>

Game::Game() : playerDeck(), enemy("Enemy", 50, 8, 4, EnemyType::MELEE), currentRun(), playerHealth(100), maxPlayerHealth(100), playerArmor(0), playerEnergy(3), maxEnergy(3), turnNumber(1), playerTurnActive(true), running(false), inEncounter(false), equipDamageBonus(0), equipArmorBonus(0), counterAttackActive(false), parryActive(false) {}

void Game::init() {
    playerDeck.addCard(Card("Quick Jab", "Deal 3 damage (free)", CardType::ATTACK, 0, 3));
    playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
    playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
    playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
    playerDeck.addCard(Card("Bash", "Deal 8 damage", CardType::ATTACK, 2, 8));
    playerDeck.addCard(Card("Bash", "Deal 8 damage", CardType::ATTACK, 2, 8));
    playerDeck.addCard(Card("Defend", "Gain 8 armor", CardType::DEFEND, 1, 8));
    playerDeck.addCard(Card("Defend", "Gain 8 armor", CardType::DEFEND, 1, 8));
    playerDeck.addCard(Card("Defend", "Gain 8 armor", CardType::DEFEND, 1, 8));
    playerDeck.addCard(Card("Defend", "Gain 8 armor", CardType::DEFEND, 1, 8));
    playerDeck.addCard(Card("Defend", "Gain 8 armor", CardType::DEFEND, 1, 8));
    
    applyUpgrades();
    
    playerDeck.shuffle();
    
    running = true;
}

void Game::displayStatus() const {
    UIHelper::printCombatStatus(playerHealth, maxPlayerHealth, playerArmor, playerEnergy, maxEnergy,
                                 enemy.getName(), enemy.getHealth(), enemy.getMaxHealth(), 
                                 enemy.getArmor(), enemy.getBaseAttack(), enemy.getBaseDefense());
    
    playerStatus.display("  YOU:   ");
    enemy.displayStatusEffects("  ENEMY: ");

    // show bonuses only when they exist
    int totalDmg = upgrades.getDamageBonus() + equipDamageBonus;
    int totalArm = upgrades.getArmorBonus()  + equipArmorBonus;
    if (totalDmg > 0 || totalArm > 0) {
        std::cout << "BONUSES: ";
        if (totalDmg > 0) std::cout << Color::CARD_ATTACK << "Damage +" << totalDmg << Color::RESET << " ";
        if (totalArm > 0) std::cout << Color::ARMOR_CLR   << "Armor +"  << totalArm  << Color::RESET << " ";
        std::cout << "\n";
    }

}

void Game::displayEnemyInfo() const {
    int atk = enemy.getBaseAttack();
    int def = enemy.getBaseDefense();
    int arm = enemy.getArmor();

    std::cout << "\n" << Color::BOLD << Color::RED << enemy.getName() << Color::RESET << "\n";
    std::cout << "  HP:  " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
              << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << "\n";
    if (arm > 0)
        std::cout << "  ARM: " << Color::ARMOR_CLR << arm << Color::RESET << "\n";
    enemy.displayStatusEffects("  ");

    std::cout << "\n" << Color::DIM << "Possible moves:" << Color::RESET << "\n";

    if (enemy.isBoss()) {
        switch (enemy.getBossType()) {
            case BossType::STONE_COLOSSUS:
                std::cout << "  " << Color::RED    << "Earthquake Slam" << Color::RESET << " (rare)    — hits you twice, ignores armor\n";
                std::cout << "  " << Color::ARMOR_CLR << "Fortify"      << Color::RESET << " (common)  — gains a lot of armor\n";
                std::cout << "  " << Color::RED    << "Crush"           << Color::RESET << " (common)  — heavy melee strike\n";
                break;
            case BossType::VILE_WITCH:
                std::cout << "  " << Color::POISON_CLR << "Poison Cloud" << Color::RESET << " (common)  — poisons you heavily\n";
                std::cout << "  " << Color::BURN_CLR   << "Hex"          << Color::RESET << " (common)  — burns you\n";
                std::cout << "  " << Color::RED         << "Strike"       << Color::RESET << " (uncommon)— direct attack\n";
                break;
            case BossType::WARLORD:
                std::cout << "  " << Color::WEAK_CLR << "War Cry"      << Color::RESET << " (rare)    — weakens you\n";
                std::cout << "  " << Color::RED      << "Heavy Strike"  << Color::RESET << " (common)  — attack, grows stronger each turn\n";
                std::cout << "  " << Color::ARMOR_CLR<< "Defend"        << Color::RESET << " (uncommon)— gains armor\n";
                break;
            default: break;
        }
    } else {
        switch (enemy.getType()) {
            case EnemyType::MELEE:
                std::cout << "  " << Color::RED     << "Attack"  << Color::RESET << " (likely)   — deals ~" << std::max(0, atk - def) << " dmg (reduced by your armor)\n";
                std::cout << "  " << Color::ARMOR_CLR << "Defend" << Color::RESET << " (rarely)   — gains " << def << " armor\n";
                break;
            case EnemyType::RANGED:
                std::cout << "  " << Color::RED      << "Pierce attack"   << Color::RESET << " (likely)     — deals ~" << std::max(0, atk - def) << " dmg, bypasses half your armor\n";
                std::cout << "  " << Color::ARMOR_CLR << "Defend"         << Color::RESET << " (sometimes)  — gains " << std::max(1, def - 1) << " armor\n";
                std::cout << "  " << Color::WEAK_CLR  << "Crippling shot" << Color::RESET << " (rarely)     — weakens you (-2 dmg for 2 turns)\n";
                break;
            case EnemyType::TANK:
                std::cout << "  " << Color::ARMOR_CLR << "Defend" << Color::RESET << " (likely)   — gains " << def << " armor\n";
                std::cout << "  " << Color::RED       << "Attack" << Color::RESET << " (sometimes)— deals ~" << std::max(0, std::max(1, atk - 2) - def) << " dmg (reduced by your armor)\n";
                break;
            case EnemyType::CASTER:
                std::cout << "  " << Color::POISON_CLR << "Poison Bolt" << Color::RESET << " (sometimes) — poisons you (3 stacks)\n";
                std::cout << "  " << Color::BURN_CLR   << "Fireball"    << Color::RESET << " (sometimes) — burns you (2 turns)\n";
                std::cout << "  " << Color::RED         << "Attack"      << Color::RESET << " (sometimes) — deals ~" << std::max(0, atk + 1 - def) << " dmg\n";
                if (enemy.getHealth() < enemy.getMaxHealth() / 3)
                    std::cout << "  " << Color::HEAL    << "Heal"         << Color::RESET << " (likely, low HP!) — recovers ~" << (8 + def / 2) << " HP\n";
                else
                    std::cout << "  " << Color::DIM     << "[May cast Heal if HP drops below 33%]" << Color::RESET << "\n";
                break;
        }
    }
    std::cout << "\n";
}

int Game::calculateDamage(int attackValue, int defenseValue) const {
    int damage = attackValue - defenseValue;
    return (damage < 0) ? 0 : damage;
}

bool Game::spendEnergy(int cost) {
    if (playerEnergy < cost) {
        std::cout << Color::DAMAGE << "Not enough plays! Need " << cost
                  << " but only have " << playerEnergy << " remaining." << Color::RESET << "\n";
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
        case CardEffect::COUNTER:
            counterAttackActive = true;
            std::cout << "  " << Color::CYAN << "You brace for a counterattack. If they strike, you hit back for double." << Color::RESET << "\n";
            break;
        case CardEffect::PARRY:
            parryActive = true;
            std::cout << "  " << Color::CYAN << "You raise your guard. If they attack, you'll parry and stun them." << Color::RESET << "\n";
            break;
        default:
            break;
    }
}

void Game::playCardFromHand(int index) {
    try {
        const Card& card = playerDeck.getCardFromHand(index - 1);

        if (playerDeck.isCardUsed(index - 1)) {
            std::cout << Color::DIM << "Card " << index << " already played this turn." << Color::RESET << "\n";
            return;
        }
        if (!spendEnergy(card.getCost())) return;

        Card playedCard = playerDeck.playCard(index - 1);

        std::cout << "Played: [" << playedCard.getName() << "] (Cost: " << playedCard.getCost() << ")\n";

        if (playedCard.getType() == CardType::ATTACK) {
            int weakPenalty = playerStatus.getWeakPenalty();
            int bonusDamage = std::max(0, playedCard.getValue() + upgrades.getDamageBonus() + equipDamageBonus - weakPenalty);
            int damageDealt = calculateDamage(bonusDamage, enemy.getBaseDefense());
            int hpBefore = enemy.getHealth();
            enemy.takeDamage(damageDealt);
            int hpLost = hpBefore - enemy.getHealth();
            int armorBlocked = damageDealt - hpLost;
            Audio::playSFX(!enemy.isAlive() ? "dead" : "attack");
            std::cout << "  " << Color::PLAYER_ATTACK << "Dealt " << hpLost << " damage to enemy!"
                      << Color::RESET << " (Enemy HP: "
                      << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")";
            if (weakPenalty > 0)
                std::cout << " " << Color::WEAK_CLR << "[Weakened -" << weakPenalty << "]" << Color::RESET;
            if (armorBlocked > 0)
                std::cout << " " << Color::ARMOR_CLR << "[" << armorBlocked << " blocked by armor]" << Color::RESET;
            std::cout << "\n";
        } else if (playedCard.getType() == CardType::DEFEND) {
            int bonusArmor = playedCard.getValue() + upgrades.getArmorBonus() + equipArmorBonus;
            playerArmor += bonusArmor;
            Audio::playSFX("defend");
            std::cout << "  " << Color::ARMOR_CLR << "Gained " << bonusArmor << " armor!"
                      << Color::RESET << " (Total: " << Color::ARMOR_CLR << playerArmor << Color::RESET << ")\n";
        } else if (playedCard.getType() == CardType::SPECIAL) {
            Audio::playSFX("special");
            applyCardEffect(playedCard);
        }
        if (playerEnergy > 0)
            std::cout << Color::DIM << "  (" << playerEnergy << " play" << (playerEnergy != 1 ? "s" : "") << " left)" << Color::RESET << "\n";
    } catch (const std::out_of_range&) {
        std::cout << "Invalid card index!\n";
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
        UIHelper::pause(250);
        if (!enemy.isAlive()) { Audio::playSFX("dead"); return; }
    }
    int burnDmg = enemy.processBurn();
    if (burnDmg > 0) {
        enemy.takeDamageRaw(burnDmg);
        std::cout << Color::BURN_CLR << "Burn:" << Color::RESET
                  << " enemy takes " << Color::PLAYER_ATTACK << burnDmg << Color::RESET << " damage! ("
                  << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << " HP)\n";
        UIHelper::pause(250);
        if (!enemy.isAlive()) { Audio::playSFX("dead"); return; }
    }
    if (enemy.processStun()) {
        UIHelper::typeWrite(std::string(Color::STUN_CLR) + "Enemy is STUNNED and loses their turn!" + Color::RESET + "\n");
        UIHelper::pause(400);
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
        Audio::playSFX("hit");
        std::cout << Color::DAMAGE << "Enemy attacks for " << actualDamage << " damage!" << Color::RESET;
        if (weakPenalty > 0)
            std::cout << " " << Color::WEAK_CLR << "[Weakened -" << weakPenalty << "]" << Color::RESET;
        std::cout << "  HP: " << hpColor(playerHealth, maxPlayerHealth)
                  << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
        UIHelper::pause(200);
        if (counterAttackActive) {
            counterAttackActive = false;
            int counterDmg = atk * 2;
            int hpBefore = enemy.getHealth();
            enemy.takeDamage(counterDmg);
            int hpLost = hpBefore - enemy.getHealth();
            Audio::playSFX(!enemy.isAlive() ? "dead" : "attack");
            std::cout << Color::GREEN << "Counter! You hit back for " << hpLost << " damage!" << Color::RESET
                      << " (Enemy HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
            UIHelper::pause(200);
        }
        if (parryActive) {
            parryActive = false;
            enemy.applyStatus(StatusType::STUN, 1);
            Audio::playSFX("special");
            std::cout << Color::CYAN << "Parry! You deflect the blow — enemy is stunned and loses their next turn!" << Color::RESET << "\n";
            UIHelper::pause(200);
        }
    };

    auto doDefend = [&](int amt) {
        enemy.gainArmor(amt);
        bool fizzled = counterAttackActive || parryActive;
        counterAttackActive = false;
        parryActive = false;
        if (fizzled)
            std::cout << Color::ARMOR_CLR << "Enemy braces — +" << amt << " armor." << Color::RESET
                      << " " << Color::DIM << "(No attack — your stance fizzles.)" << Color::RESET << "\n";
        else
            std::cout << Color::ARMOR_CLR << "Enemy braces — +" << amt << " armor (absorbs incoming damage)." << Color::RESET << "\n";
        UIHelper::pause(150);
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
                Audio::playSFX("special");
                std::cout << Color::WEAK_CLR << "Enemy fires a crippling shot! You are Weakened for 2 turns." << Color::RESET << "\n";
            }
            break;
        case EnemyType::TANK:
            if (roll < 65) doDefend(def); // was def+3, too much in early encounters
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
                Audio::playSFX("special");
                std::cout << Color::POISON_CLR << "Enemy casts Poison Bolt! You are poisoned for 3 stacks." << Color::RESET << "\n";
            } else if (roll < 60) {
                playerStatus.apply(StatusType::BURN, 2);
                Audio::playSFX("special");
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
    std::cout << "\n" << Color::BOLD << "-- Turn " << turnNumber << " --" << Color::RESET << "\n";
    if (playerTurnActive) {
        std::cout << Color::ENERGY_CLR << playerEnergy << "/" << maxEnergy << Color::RESET << " plays remaining\n";
    }
}

void Game::resetArmor() {
    playerArmor = 0;
    enemy.resetArmor();
}

void Game::endPlayerTurn() {
    playerTurnActive = false;
    UIHelper::typeWrite(std::string("\n") + Color::BOLD + "--- Enemy's Turn ---" + Color::RESET + "\n");
    UIHelper::pause(350);
    enemyTurn();
    counterAttackActive = false;
    parryActive = false;
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
        UIHelper::pause(250);
    }
    int playerBurnDmg = playerStatus.processBurn();
    if (playerBurnDmg > 0) {
        playerHealth = std::max(0, playerHealth - playerBurnDmg);
        std::cout << Color::BURN_CLR << "Burn:" << Color::RESET
                  << " you take " << Color::DAMAGE << playerBurnDmg << Color::RESET
                  << " damage! (HP: " << hpColor(playerHealth, maxPlayerHealth)
                  << playerHealth << Color::RESET << ")\n";
        UIHelper::pause(250);
    }
    // WEAK ticks at end of player turn (after all attacks are resolved)
    playerStatus.processWeak();

    // Discard remaining hand cards and draw a fresh hand for next turn
    playerDeck.resetDeck();
    int drawCount = 5 + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    UIHelper::pause(800);
    // handleInput() will clear and redraw the full state for the new turn
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
    UIHelper::clearScreen();
    if (playerHealth <= 0) {
        Audio::playSFX("lose");
        UIHelper::pause(300);
        UIHelper::printGameOverScreen(false, currentRun.getEncountersWon(), runStats.getTotalCardsCollected());
        std::cout << "You were defeated! Better luck next time.\n";
    } else if (!enemy.isAlive()) {
        UIHelper::printGameOverScreen(true, currentRun.getEncountersWon(), runStats.getTotalCardsCollected());
        std::cout << "Enemy defeated! Onward to the next encounter!\n";
    }
}

bool Game::handleGameOverInput() {
    UIHelper::clearScreen();
    std::cout << "\n";
    int choice = UIHelper::menuSelect({"Play again", "Quit"});
    return (choice == 0);
}

void Game::handleInput() {
    if (!playerTurnActive) return;

    UIHelper::clearScreen();

    // Compact 4-line header so the full turn fits on one screen
    std::string encLabel = currentRun.isBossEncounter()
        ? std::string(Color::BOLD) + Color::MAGENTA + "BOSS" + Color::RESET
        : "Encounter " + std::to_string(currentRun.getCurrentEncounter()) + " " + currentRun.getEncounterDifficulty();
    std::cout << Color::BOLD << "Turn " << turnNumber << Color::RESET
              << "  |  " << Color::ENERGY_CLR << playerEnergy << "/" << maxEnergy << " plays" << Color::RESET
              << "  |  " << encLabel << "\n";

    int totalDmg = upgrades.getDamageBonus() + equipDamageBonus;
    int totalArm = upgrades.getArmorBonus()  + equipArmorBonus;
    std::cout << Color::BOLD << Color::WHITE << "YOU" << Color::RESET
              << "   HP:" << hpColor(playerHealth, maxPlayerHealth) << playerHealth << "/" << maxPlayerHealth << Color::RESET
              << "  " << UIHelper::createHealthBar(playerHealth, maxPlayerHealth, 16)
              << "  ARM:" << Color::ARMOR_CLR << playerArmor << Color::RESET;
    if (totalDmg > 0) std::cout << "  +" << Color::CARD_ATTACK << totalDmg << "dmg" << Color::RESET;
    if (totalArm > 0) std::cout << "  +" << Color::ARMOR_CLR   << totalArm << "arm" << Color::RESET;
    std::cout << playerStatus.summary() << "\n";

    std::cout << Color::BOLD << Color::RED << "ENEMY" << Color::RESET
              << "  " << enemy.getName()
              << "  HP:" << hpColor(enemy.getHealth(), enemy.getMaxHealth())
              << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET
              << "  " << UIHelper::createHealthBar(enemy.getHealth(), enemy.getMaxHealth(), 16)
              << "  ARM:" << Color::ARMOR_CLR << enemy.getArmor() << Color::RESET
              << "  ATK:" << Color::RED << enemy.getBaseAttack() << Color::RESET
              << enemy.statusSummary() << "\n";

    std::cout << Color::DIM;
    for (int i = 0; i < 68; i++) std::cout << '-';
    std::cout << Color::RESET << "\n";

    int handCount = playerDeck.handSize();
    int dmgBonus  = upgrades.getDamageBonus() + equipDamageBonus;
    int armBonus  = upgrades.getArmorBonus()  + equipArmorBonus;
    int weakPen   = playerStatus.getWeakPenalty();

    // Build hand lines + option index mapping for the side-by-side display
    std::vector<std::string> leftLines;
    std::vector<int>         optionIndices;
    std::vector<std::string> options;
    std::vector<bool>        disabled;

    for (int i = 0; i < handCount; i++) {
        bool used       = playerDeck.isCardUsed(i);
        bool cantAfford = !used && (playerDeck.getCardFromHand(i).getCost() > playerEnergy);

        std::string optLabel = "Select Card " + std::to_string(i + 1);
        if (used) optLabel += " (used)";
        else if (cantAfford) optLabel += " (no energy)";
        options.push_back(optLabel);
        disabled.push_back(used || cantAfford);
        int optIdx = (int)options.size() - 1;

        if (used) {
            leftLines.push_back(std::string("  ") + Color::DIM + std::to_string(i + 1) + ". [USED]" + Color::RESET);
            optionIndices.push_back(optIdx);
            leftLines.push_back(""); optionIndices.push_back(-1);
        } else {
            const Card& c = playerDeck.getCardFromHand(i);
            int dispVal = c.getValue();
            if      (c.getType() == CardType::ATTACK) dispVal = std::max(0, dispVal + dmgBonus - weakPen);
            else if (c.getType() == CardType::DEFEND) dispVal = dispVal + armBonus;

            const char* typeColor = (c.getTypeString() == "ATTACK") ? Color::CARD_ATTACK
                                  : (c.getTypeString() == "DEFEND") ? Color::CARD_DEFEND
                                  : Color::CARD_SPECIAL;
            std::string valLabel = (c.getTypeString() == "ATTACK") ? "DMG"
                                 : (c.getTypeString() == "DEFEND") ? "ARM" : "STK";

            // Pad type to 6, name to 18 (matching printCardRow exactly)
            std::string typePad = c.getTypeString();
            while ((int)typePad.size() < 6) typePad += ' ';
            std::string namePad = c.getName();
            while ((int)namePad.size() < 18) namePad += ' ';

            std::string mainLine =
                std::string("  ") + Color::DIM + std::to_string(i + 1) + "." + Color::RESET
                + " [" + typeColor + typePad + Color::RESET + "] "
                + Color::BOLD + Color::CARD_NAME + namePad + Color::RESET
                + " cost:" + Color::ENERGY_CLR + std::to_string(c.getCost()) + Color::RESET
                + "  " + valLabel + ":" + Color::GREEN + std::to_string(dispVal) + Color::RESET;

            std::string descLine = "     " + std::string(Color::DIM) + c.getDescription() + Color::RESET;

            leftLines.push_back(mainLine);    optionIndices.push_back(optIdx);
            leftLines.push_back(descLine);    optionIndices.push_back(-1);
            leftLines.push_back("");          optionIndices.push_back(-1);
        }
    }

    options.push_back("End Turn");    disabled.push_back(false);
    options.push_back("View Enemy");  disabled.push_back(false);
    options.push_back("Status");      disabled.push_back(false);

    std::cout << "\n";
    int choice = UIHelper::menuSelectRight(leftLines, optionIndices, options, 50, 0, disabled);
    if (choice < 0) return;

    if (choice < handCount) {
        playCardFromHand(choice + 1);
        if (checkGameOver()) return;
        UIHelper::pause(600);  // let the card result stay visible before redraw
        if (playerEnergy <= 0 && playerTurnActive) {
            std::cout << "\n" << Color::DIM << "[No plays left — ending your turn automatically]" << Color::RESET << "\n";
            UIHelper::pause(400);
            endPlayerTurn();
        }
    } else if (choice == handCount) {
        endPlayerTurn();
        checkGameOver();
    } else if (choice == handCount + 1) {
        UIHelper::clearScreen();
        displayEnemyInfo();
        UIHelper::waitForKey();
    } else {
        UIHelper::clearScreen();
        displayStatus();
        UIHelper::waitForKey();
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
    int bossHealth  = currentRun.getEnemyHealth() * 2;
    int bossAttack  = currentRun.getEnemyAttack() + 4;
    int bossDefense = currentRun.getEnemyDefense();

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
            Audio::playSFX("hit");
            std::cout << Color::BOLD << Color::DAMAGE << "  BOSS slams for " << damage
                      << " (ignores armor)!" << Color::RESET
                      << " HP: " << hpColor(playerHealth, maxPlayerHealth)
                      << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
        } else {
            int actual = std::max(0, damage - playerArmor);
            playerArmor = std::max(0, playerArmor - damage);
            playerHealth = std::max(0, playerHealth - actual);
            Audio::playSFX("hit");
            std::cout << Color::BOLD << Color::DAMAGE << "  BOSS strikes for " << actual
                      << " damage!" << Color::RESET
                      << " HP: " << hpColor(playerHealth, maxPlayerHealth)
                      << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
        }
        if (weakPenalty > 0)
            std::cout << "  " << Color::WEAK_CLR << "[Weakened -" << weakPenalty << "]" << Color::RESET << "\n";
        UIHelper::pause(350);
        if (counterAttackActive) {
            counterAttackActive = false;
            int counterDmg = damage * 2;
            int hpBefore = enemy.getHealth();
            enemy.takeDamage(counterDmg);
            int hpLost = hpBefore - enemy.getHealth();
            Audio::playSFX(!enemy.isAlive() ? "dead" : "attack");
            std::cout << Color::GREEN << "Counter! You hit back for " << hpLost << " damage!" << Color::RESET
                      << " (Boss HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
            UIHelper::pause(200);
        }
        if (parryActive) {
            parryActive = false;
            enemy.applyStatus(StatusType::STUN, 1);
            Audio::playSFX("special");
            std::cout << Color::CYAN << "Parry! You deflect the blow — boss is stunned and loses their next turn!" << Color::RESET << "\n";
            UIHelper::pause(200);
        }
    };

    switch (enemy.getBossType()) {
        case BossType::STONE_COLOSSUS:
            if (roll < 15) {
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Stone Colossus uses EARTHQUAKE SLAM!" + Color::RESET + "\n");
                UIHelper::pause(300);
                doAttack(15, true);
            } else if (roll < 45) {
                enemy.gainArmor(8);
                std::cout << Color::MAGENTA << "Stone Colossus hardens!" << Color::RESET
                          << " +" << Color::ARMOR_CLR << 8 << Color::RESET
                          << " armor (" << enemy.getArmor() << " total)\n";
                UIHelper::pause(200);
            } else {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Stone Colossus strikes!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk + 4, false);
            }
            break;

        case BossType::VILE_WITCH:
            if (roll < 30) {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Vile Witch attacks!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
            } else if (roll < 70) {
                playerStatus.apply(StatusType::POISON, 4);
                playerStatus.apply(StatusType::BURN, 2);
                Audio::playSFX("special");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Vile Witch casts PLAGUE!" + Color::RESET
                    + " You gain " + Color::POISON_CLR + "Poison 4" + Color::RESET
                    + " and " + Color::BURN_CLR + "Burn 2" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 85) {
                int healAmt = 20;
                enemy.heal(healAmt);
                std::cout << Color::MAGENTA << "Vile Witch siphons life, healing " << Color::HEAL
                          << healAmt << " HP!" << Color::RESET << " ("
                          << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                UIHelper::pause(250);
            } else {
                playerStatus.apply(StatusType::POISON, 6);
                Audio::playSFX("special");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Vile Witch casts TOXIC ERUPTION!" + Color::RESET
                    + " You gain " + Color::POISON_CLR + "Poison 6" + Color::RESET + "!\n");
                UIHelper::pause(350);
            }
            break;

        case BossType::WARLORD:
            if (roll < 15) {
                playerStatus.apply(StatusType::WEAK, 3);
                Audio::playSFX("special");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Warlord roars a BATTLECRY!" + Color::RESET
                    + " You are " + Color::WEAK_CLR + "Weakened 3" + Color::RESET + "!\n");
                UIHelper::pause(350);
            }
            UIHelper::typeWrite(std::string(Color::MAGENTA) + "Warlord attacks!" + Color::RESET + "\n");
            UIHelper::pause(200);
            doAttack(atk, false);
            if (enemy.getBonusAttack() < 6) {
                enemy.addBonusAttack(1);
                std::cout << Color::MAGENTA << "Warlord grows stronger!" << Color::RESET
                          << " (total bonus +" << Color::RED << enemy.getBonusAttack() << Color::RESET << " attack)\n";
                UIHelper::pause(200);
            }
            break;

        default:
            doAttack(atk, false);
    }
}

void Game::offerBossReward() {
    UIHelper::clearScreen();
    std::vector<Card> rewards = rewardPool.generateRareRewards(2, maxEnergy, playerDeck.getAllCardNames());

    if (rewards.empty()) {
        std::cout << "No rare cards available.\n";
        return;
    }

    std::cout << "\n" << Color::BOLD << Color::YELLOW << "Boss reward" << Color::RESET << " — pick a rare card:\n\n";

    std::vector<std::string> leftLines;
    std::vector<int>         optionIndices;
    std::vector<std::string> options;

    for (size_t i = 0; i < rewards.size(); i++) {
        const Card& c = rewards[i];
        const char* typeColor = (c.getTypeString() == "ATTACK") ? Color::CARD_ATTACK
                              : (c.getTypeString() == "DEFEND") ? Color::CARD_DEFEND
                              : Color::CARD_SPECIAL;
        const char* valLabel = (c.getTypeString() == "ATTACK") ? "DMG"
                             : (c.getTypeString() == "DEFEND") ? "ARM" : "STK";

        leftLines.push_back(std::string("  ") + Color::BOLD + std::to_string(i + 1) + "." + Color::RESET
            + " [" + typeColor + c.getTypeString() + Color::RESET + "] "
            + Color::BOLD + Color::YELLOW + c.getName() + Color::RESET);
        optionIndices.push_back((int)i);

        leftLines.push_back(std::string("     Cost:") + Color::ENERGY_CLR + std::to_string(c.getCost()) + Color::RESET
            + "  " + valLabel + ":" + Color::GREEN + std::to_string(c.getValue()) + Color::RESET);
        optionIndices.push_back(-1);

        leftLines.push_back(std::string("     ") + Color::DIM + c.getDescription() + Color::RESET);
        optionIndices.push_back(-1);

        leftLines.push_back("");
        optionIndices.push_back(-1);

        options.push_back("Select Card " + std::to_string(i + 1));
    }
    options.push_back("Skip");

    int choice = UIHelper::menuSelectRight(leftLines, optionIndices, options, 40);
    if (choice < 0 || choice >= (int)rewards.size()) {
        std::cout << "Skipping reward.\n";
        UIHelper::waitForKey();
        return;
    }

    playerDeck.addCard(rewards[choice]);
    runStats.addCardToRun();
    std::cout << "\n" << Color::YELLOW << "[RARE] Added " << rewards[choice].getName() << " to your deck!" << Color::RESET << "\n";
    UIHelper::waitForKey();
}

void Game::startEncounter() {
    UIHelper::clearScreen();
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
        Audio::playSFX("boss");
        UIHelper::printBossHeader(currentRun.getCurrentEncounter(), enemy.getName());
    } else {
        UIHelper::printEncounterHeader(currentRun.getCurrentEncounter(),
                                       currentRun.getEncounterDifficulty(),
                                       currentRun.getEncounterTier());
    }

    displayStatus();
    displayTurnInfo();
    UIHelper::pause(600);
    // handleInput() will render the hand + menu side-by-side on first input
}

void Game::nextEncounter() {
    currentRun.nextEncounter();
    startEncounter();
}

void Game::restSite() {
    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << Color::CYAN << "Rest site" << Color::RESET << "\n\n";

    std::vector<std::string> leftLines = {
        std::string("  ") + Color::HEAL + "Rest" + Color::RESET
            + "  — heal to full (" + std::to_string(playerHealth) + "/" + std::to_string(maxPlayerHealth) + " HP)",
        std::string("  ") + Color::YELLOW + "Forge" + Color::RESET
            + " — upgrade a card (+3 value, -1 cost)",
        std::string("  ") + Color::DIM + "Skip" + Color::RESET
            + "  — press on without resting"
    };
    std::vector<int> optionIndices = {0, 1, 2};
    std::vector<std::string> options = {"Rest", "Forge", "Skip"};

    int siteChoice = UIHelper::menuSelectRight(leftLines, optionIndices, options, 52);

    if (siteChoice == 0) {
        playerHealth = maxPlayerHealth;
        Audio::playSFX("heal");
        std::cout << Color::HEAL << "\nYou rest and fully recover to " << maxPlayerHealth << " HP." << Color::RESET << "\n";
        UIHelper::waitForKey();
    } else if (siteChoice == 1) {
        if (playerDeck.totalCards() == 0) {
            std::cout << "Your deck is empty — nothing to upgrade.\n";
            return;
        }

        std::vector<Card> allCards = playerDeck.getAllCardsOrdered();
        std::vector<std::string> cardLines;
        std::vector<int>         cardOptIdx;
        std::vector<std::string> cardOptions;

        for (size_t i = 0; i < allCards.size(); i++) {
            const Card& c = allCards[i];
            const char* typeColor = (c.getTypeString() == "ATTACK") ? Color::CARD_ATTACK
                                  : (c.getTypeString() == "DEFEND") ? Color::CARD_DEFEND
                                  : Color::CARD_SPECIAL;
            const char* valLabel = (c.getTypeString() == "ATTACK") ? "DMG"
                                 : (c.getTypeString() == "DEFEND") ? "ARM" : "STK";
            std::string typePad = c.getTypeString();
            while ((int)typePad.size() < 6) typePad += ' ';
            std::string namePad = c.getName();
            while ((int)namePad.size() < 18) namePad += ' ';

            cardLines.push_back(std::string("  ") + Color::DIM + std::to_string(i + 1) + "." + Color::RESET
                + " [" + typeColor + typePad + Color::RESET + "] "
                + Color::BOLD + Color::CARD_NAME + namePad + Color::RESET
                + " cost:" + Color::ENERGY_CLR + std::to_string(c.getCost()) + Color::RESET
                + "  " + valLabel + ":" + Color::GREEN + std::to_string(c.getValue()) + Color::RESET);
            cardOptIdx.push_back((int)i);

            cardLines.push_back(std::string("     ") + Color::DIM + c.getDescription() + Color::RESET);
            cardOptIdx.push_back(-1);

            cardLines.push_back("");
            cardOptIdx.push_back(-1);

            cardOptions.push_back("Select Card " + std::to_string(i + 1));
        }
        cardOptions.push_back("Cancel");

        UIHelper::clearScreen();
        std::cout << "\n" << Color::BOLD << Color::YELLOW << "Forge" << Color::RESET << " — pick a card to upgrade:\n\n";
        int cardChoice = UIHelper::menuSelectRight(cardLines, cardOptIdx, cardOptions, 50);

        if (cardChoice < 0 || cardChoice >= (int)allCards.size()) {
            std::cout << "Cancelled.\n";
        } else if (playerDeck.upgradeCardAt(cardChoice)) {
            Audio::playSFX("upgrade");
            std::cout << Color::GREEN << "\nCard upgraded!" << Color::RESET << "\n";
            UIHelper::waitForKey();
        }
    } else {
        std::cout << Color::DIM << "\nYou press on without resting." << Color::RESET << "\n";
        UIHelper::waitForKey();
    }
}

void Game::handleEncounterWin() {
    currentRun.winEncounter();
    Audio::playSFX("win");
    UIHelper::pause(300);
    UIHelper::clearScreen();
    UIHelper::typeWrite(std::string("\n") + Color::BOLD + Color::GREEN + "Victory!" + Color::RESET + "\n");
    UIHelper::pause(300);
    std::cout << "Enemies Defeated: " << Color::GREEN << currentRun.getEncountersWon() << Color::RESET << "\n";

    if (enemy.isBoss()) {
        offerBossReward();
    } else {
        offerCardReward();
    }

    if (currentRun.getCurrentEncounter() % 3 == 0)
        offerEquipmentDrop();

    restSite();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << Color::GREEN << "Round complete!" << Color::RESET << "\n\n";
    std::cout << "  Encounters cleared: " << Color::GREEN << currentRun.getEncountersWon() << Color::RESET << "\n";
    std::cout << "  HP: " << hpColor(playerHealth, maxPlayerHealth)
              << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n\n";

    std::vector<std::string> contLines = {
        std::string("  ") + Color::GREEN + "Continue" + Color::RESET + " — enter the next encounter",
        std::string("  ") + Color::DIM   + "End run" + Color::RESET  + "  — finish here and bank your progress"
    };
    std::vector<int> contIdx = {0, 1};
    int choice = UIHelper::menuSelectRight(contLines, contIdx, {"Continue", "End run"}, 50);

    if (choice == 0) {
        nextEncounter();
    } else {
        inEncounter = false;
        currentRun.loseRun();
    }
}

void Game::offerCardReward() {
    UIHelper::clearScreen();
    bool rarityBoost = upgrades.isActive(6);
    std::vector<Card> rewards = rewardPool.generateWeightedRewards(currentRun.getCurrentEncounter(), 3, rarityBoost, maxEnergy, playerDeck.getAllCardNames());

    if (rewards.empty()) {
        std::cout << "No cards available as rewards.\n";
        return;
    }

    std::cout << "\n" << Color::BOLD << Color::YELLOW << "Card reward" << Color::RESET << " — pick one to add to your deck:\n\n";

    std::vector<std::string> leftLines;
    std::vector<int>         optionIndices;
    std::vector<std::string> options;

    for (size_t i = 0; i < rewards.size(); i++) {
        const Card& c = rewards[i];
        const char* typeColor = (c.getTypeString() == "ATTACK") ? Color::CARD_ATTACK
                              : (c.getTypeString() == "DEFEND") ? Color::CARD_DEFEND
                              : Color::CARD_SPECIAL;
        const char* valLabel = (c.getTypeString() == "ATTACK") ? "DMG"
                             : (c.getTypeString() == "DEFEND") ? "ARM" : "STK";

        leftLines.push_back(std::string("  ") + Color::BOLD + std::to_string(i + 1) + "." + Color::RESET
            + " [" + typeColor + c.getTypeString() + Color::RESET + "] "
            + Color::BOLD + Color::WHITE + c.getName() + Color::RESET);
        optionIndices.push_back((int)i);

        leftLines.push_back(std::string("     Cost:") + Color::ENERGY_CLR + std::to_string(c.getCost()) + Color::RESET
            + "  " + valLabel + ":" + Color::GREEN + std::to_string(c.getValue()) + Color::RESET);
        optionIndices.push_back(-1);

        leftLines.push_back(std::string("     ") + Color::DIM + c.getDescription() + Color::RESET);
        optionIndices.push_back(-1);

        leftLines.push_back("");
        optionIndices.push_back(-1);

        options.push_back("Select Card " + std::to_string(i + 1));
    }
    options.push_back("Skip");

    int choice = UIHelper::menuSelectRight(leftLines, optionIndices, options, 40);
    if (choice < 0 || choice >= (int)rewards.size()) {
        std::cout << "Skipping reward.\n";
        UIHelper::waitForKey();
        return;
    }

    playerDeck.addCard(rewards[choice]);
    runStats.addCardToRun();
    std::cout << "\n" << Color::GREEN << "Added " << rewards[choice].getName() << " to your deck!" << Color::RESET << "\n";
    UIHelper::waitForKey();
}

void Game::applyUpgrades() {
    if (upgrades.isActive(1)) {
        playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
        playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
    }
    maxPlayerHealth += upgrades.getHealthBonus();
    playerHealth = maxPlayerHealth;
    maxEnergy += upgrades.getEnergyBonus();
    playerEnergy = maxEnergy;
}

void Game::offerEquipmentDrop() {
    UIHelper::clearScreen();
    std::cout << "\n" << Color::YELLOW << "Equipment" << Color::RESET << " — you spot some gear on the ground:\n\n";

    std::vector<std::string> leftLines = {
        std::string("  ") + Color::RED + "[WEAPON]" + Color::RESET + " Rusty Blade",
        std::string("     ") + Color::DIM + "+3 permanent damage on all attacks" + Color::RESET,
        "",
        std::string("  ") + Color::ARMOR_CLR + "[ARMOR]" + Color::RESET + "  Iron Plating",
        std::string("     ") + Color::DIM + "+3 permanent armor per defend" + Color::RESET,
        "",
        std::string("  ") + Color::DIM + "Leave it behind" + Color::RESET,
        ""
    };
    std::vector<int> optionIndices = {0, -1, -1, 1, -1, -1, 2, -1};
    std::vector<std::string> options = {"Take Rusty Blade", "Take Iron Plating", "Leave it"};

    int choice = UIHelper::menuSelectRight(leftLines, optionIndices, options, 44);

    if (choice == 0) {
        equipDamageBonus += 3;
        std::cout << Color::RED << "\nYou equip the Rusty Blade. +3 damage!" << Color::RESET << "\n";
        Audio::playSFX("upgrade");
        UIHelper::waitForKey();
    } else if (choice == 1) {
        equipArmorBonus += 3;
        std::cout << Color::CYAN << "\nYou equip the Iron Plating. +3 armor per defend!" << Color::RESET << "\n";
        Audio::playSFX("upgrade");
        UIHelper::waitForKey();
    } else {
        std::cout << Color::DIM << "\nYou leave it behind." << Color::RESET << "\n";
        UIHelper::waitForKey();
    }
}

void Game::selectUpgrades() {
    upgrades.checkAndUnlockUpgrades(runStats.getTotalEncountersWon(), runStats.getTotalCardsCollected());
    upgrades.selectActiveUpgrades();
}

void Game::displayRunStats() const {
    currentRun.displayRunStats();
}

void Game::run() {
    init();
    
    UIHelper::printTitle();
    
    upgrades.checkAndUnlockUpgrades(0, 0);
    upgrades.displayUpgradeInfo();
    
    currentRun.startRun();
    startEncounter();
    
    while (running) {
        handleInput();
        
        if (checkGameOver()) {
            if (enemy.isAlive()) {
                displayGameOver();
                currentRun.displayRunStats();
                int encounters = currentRun.getEncountersWon();
                runStats.completeRun(encounters);
                runStats.displayRunSummary(encounters);
                
                currentRun.loseRun();
                inEncounter = false;
            } else {
                handleEncounterWin();
                if (inEncounter) {
                    continue;
                }
                int encounters = currentRun.getEncountersWon();
                runStats.completeRun(encounters);
                runStats.displayRunSummary(encounters);
            }
            
            if (handleGameOverInput()) {
                selectUpgrades();
                maxPlayerHealth = 100;
                playerHealth = maxPlayerHealth;
                playerArmor = 0;
                playerEnergy = 3;
                maxEnergy = 3;
                equipDamageBonus = 0;
                equipArmorBonus  = 0;
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
