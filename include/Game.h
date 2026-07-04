#ifndef GAME_H
#define GAME_H

#include "Deck.h"
#include "Enemy.h"
#include "Run.h"
#include "RewardPool.h"
#include "RunStats.h"
#include "StatusEffect.h"
#include "UpgradeSystem.h"

class Game {
private:
    Deck playerDeck;
    Enemy enemy;
    Run currentRun;
    RewardPool rewardPool;
    RunStats runStats;
    UpgradeSystem upgrades;
    StatusEffects playerStatus;
    int playerHealth;
    int maxPlayerHealth;
    int playerArmor;
    int playerArmorPersistTurns; // FORTIFY defend cards: turns remaining before armor resets on its own
    int playerEnergy;
    int maxEnergy;
    int turnNumber;
    bool playerTurnActive;
    bool running;
    bool inEncounter;
    int equipDamageBonus;
    int equipArmorBonus;
    int weaponTier; // number of weapon upgrades claimed so far (picks the gear name/bonus tier)
    int armorTier;  // number of armor upgrades claimed so far
    bool counterAttackActive;
    bool parryActive;
    int  counterBonusValue; // Dodge's current value — added as flat bonus riposte damage
    int  parryBonusValue;   // Parry's current value — added as flat bonus riposte damage
    
    int calculateDamage(int attackValue, int defenseValue) const;
    void playerAttack(int cardValue, int cost);
    void playerDefend(int cardValue, int cost);
    bool spendEnergy(int cost);
    void resetEnergy();
    void playCardFromHand(int index);
    void applyCardEffect(const Card& card);
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
    void restSite();
    Enemy generateBossEnemy();
    void  bossAction();
    void  offerBossReward();
    void  offerExtraPlay(); // every 2nd boss kill — separate from the card reward
    void displayRunStats() const;
    void displayEnemyInfo() const;
    void offerCardReward();
    void offerEquipmentDrop();
    void applyUpgrades();
    void selectUpgrades();
    bool viewDeckManage(); // returns true if a card was actually discarded (vs. just Return/browse)
    bool showMainMenu();   // returns true if "Start Game" was chosen, false if "Quit"
    void showHowToPlay() const;

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
