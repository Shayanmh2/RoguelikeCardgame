#ifndef GAME_H
#define GAME_H

#include "Deck.h"
#include "Enemy.h"
#include "Run.h"
#include "RewardPool.h"
#include "RunStats.h"
#include "StatusEffect.h"
#include "UpgradeSystem.h"
#include <vector>

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
    int  counterBonusValue; // Dodge Reversal's current value - added as flat bonus riposte damage
    int  parryBonusValue;   // Parry's current value - added as flat bonus riposte damage
    bool statusWardActive = false; // Status Guard: blocks the next ailment the enemy inflicts on the player
    bool enemyStatusWardActive = false; // Shadow Knight mirroring Status Guard: blocks the next ailment the player inflicts on it
    int  enemyTauntTurns = 0; // Taunt: enemy's action roll is forced toward Attack for this many of their turns

    // Once per boss attempt: a hit that would kill the player instead leaves
    // them at 1 HP. Reset to true every time a new boss encounter starts;
    // consumed the first time it saves them, so a second lethal hit kills normally.
    bool bossSecondWindAvailable = false;

    // Shadow Knight only: up to 3 cards mirrored from the player's own deck,
    // picked in secret at the start of the player's turn. One is revealed and
    // played immediately after each card the player plays this turn, so the
    // knight's response to a given play is never visible in advance - by the
    // time you see it, it's already resolving. Any left unused at turn's end
    // are played out during the knight's own turn instead of being wasted.
    std::vector<Card> knightPreparedMoves;

    // Set by playCardFromHand() every time a card is played, so callers (the tutorial)
    // can tell what was just played even if that same handleInput() call also auto-ended
    // the turn and reset the hand - a hand-reset wipes the "used" flags this would
    // otherwise need to diff against.
    bool lastActionWasCardPlay = false;
    CardType lastPlayedCardType = CardType::ATTACK;
    DamageType lastPlayedPhysType = DamageType::NONE;
    DamageType lastPlayedPhysType2 = DamageType::NONE;

    int calculateDamage(int attackValue, int defenseValue) const;
    bool spendEnergy(int cost);
    void resetEnergy();
    void playCardFromHand(int index);
    void applyCardEffect(const Card& card);
    void applyPlayerStatus(StatusType type, int amount, double weakMultiplier = 1.5); // routes through Status Guard's ward, if active
    bool applyEnemyStatus(StatusType type, int amount, double weakMultiplier = 1.5); // routes through a mirrored Status Guard's ward, if active; false if warded
    bool tryStunEnemy(); // enemy.tryApplyStun(), but blockable by a mirrored Status Guard's ward
    void refreshBattleAuras(); // syncs the battle scene's persistent status glows to current playerStatus/enemy state
    void enemyTurn();
    void endPlayerTurn();
    void resetArmor();
    void displayTurnInfo() const;
    bool checkGameOver();
    void displayGameOver();
    bool handleGameOverInput();
    void finishRun(); // shared tail: record stats, ask Play again, reset or quit accordingly
    bool selectCardToCarryOver(Card& outCard); // on replay, before the deck resets - lets the player keep 1 card
    void startEncounter();
    void nextEncounter();
    void handleEncounterWin();
    void handleGameVictory(); // first Shadow Knight kill: legendary drop, victory screen, run ends
    // The Continue/End Run choice - shared by handleEncounterWin() and a fresh Load Save.
    // justWonEncounter distinguishes the two: after a real win, Continue must advance
    // past the encounter just cleared (nextEncounter()); after a load, the saved
    // encounter number is already the one not yet fought, so Continue must start
    // THAT encounter directly (startEncounter()) - advancing here would skip it entirely.
    void offerContinueOrEndRun(bool justWonEncounter = true);
    void restSite();
    Enemy generateBossEnemy();
    void  bossAction();
    void  bossStrikesPlayer(int damage, bool raw); // shared boss-attack resolution (armor, Dodge Reversal/Parry interception, damage)
    bool  trySecondWind(); // clamps a lethal playerHealth to 1 and consumes bossSecondWindAvailable; false if already 0 or already used
    void  prepareShadowKnightMoves(); // Shadow Knight only: secretly pick up to 3 cards to mirror this turn
    void  executeShadowKnightMirror(const Card& mirrored); // plays out one mirrored card's effect against the player
    void  triggerShadowKnightAmbush(); // reveals + plays one prepared move, called right after the player plays a card
    void  offerBossReward();
    void  offerExtraPlay(); // every 2nd boss kill - separate from the card reward
    void displayRunStats() const;
    void displayEnemyInfo() const;
    void offerCardReward();
    void offerEquipmentDrop();
    void applyUpgrades();
    void selectUpgrades();
    void viewDeckManage(); // browse/discard cards; never costs the rest site visit - always returns to its menu
    int  showMainMenu();   // 0 = Start Game, 1 = Load Save, 2 = Quit/ESC
    void showHowToPlay();
    void showTutorial();   // interactive practice fight vs. a Training Dummy; restores state on exit

    // Single-slot save system: only ever written at the Continue/End Run choice
    // (i.e. between encounters, never mid-combat), so it can't be abused as a
    // combat checkpoint. Any existing save is wiped the moment the player dies.
    std::string savePath() const;
    bool saveExists() const;
    void saveGame() const;
    bool loadGame(); // false if no save file, or it couldn't be parsed
    void deleteSave() const;

public:
    Game();
    
    void init();
    void run();
    void displayStatus() const;
    void handleInput();
};

#endif
