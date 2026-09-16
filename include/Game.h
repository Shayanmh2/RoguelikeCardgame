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
    // Four, down from five. Halving the action advantage needed both the cost
    // floor and a smaller hand; extra cards come from the encounter-15/30/45
    // boon instead of being free from the start.
    static constexpr int BASE_HAND_SIZE = 4;
    int handSizeBonus = 0;      // boon: +1 card drawn each turn
    int runLuck = 0;            // boon: raises every roll in the run, see luckBonus()
    int rewardChoiceBonus = 0;  // boon: +1 card to choose from on reward screens
    int playerEnergy;
    int maxEnergy;
    int turnNumber;
    bool playerTurnActive;
    bool running;
    bool inEncounter;
    // Percent, not flat. See the note on weaponTierAt() in Game.cpp.
    int equipDamagePercent;
    int equipArmorPercent;
    int weaponTier; // number of weapon upgrades claimed so far (picks the gear name/bonus tier)
    int armorTier;  // number of armor upgrades claimed so far
    bool counterAttackActive;
    bool parryActive;
    int  counterBonusValue; // Dodge Reversal's current value - added as flat bonus riposte damage
    bool counterWasLegendary = false; // armed by a legendary, so the payoff gets the legendary cue
    int  parryBonusValue;   // Parry's current value - added as flat bonus riposte damage
    int  statusWardTurns = 0;      // Status Guard: blocks every ailment the enemy inflicts while it lasts
    bool enemyStatusWardActive = false; // Shadow Knight mirroring Status Guard: blocks the next ailment the player inflicts on it
    int  enemyTauntTurns = 0; // Taunt: enemy's action roll is forced toward Attack for this many of their turns
    int  enemyFearTurns  = 0; // Fear: each of these turns the enemy has a FEAR_BRACE_CHANCE to brace instead of acting

    // The ??? encounter. Outside the run's numbering: it does not advance the
    // counter, losing it cannot end the run, and it happens at most once.
    bool secretUsedThisRun  = false;
    bool inSecretEncounter  = false;
    bool bossSecondWindAvailable = false; // once per boss attempt: a lethal hit leaves you at 1 HP instead

    // Per-enemy signature mechanics, all reset in startEncounter().
    bool playerAttackOnly = false;   // Revenant taunt: player may only play ATTACK cards this turn
    bool enemyInvulnerable = false;  // Mystic Illusion / Specter+Wraith Ghost: enemy takes 0 direct damage for the player's turn
    bool enemyParryStance = false;   // Revenant parry: deflects + ripostes the player's next attack
    int  nextHandPenalty = 0;        // Enchanter Tempt / Sorcerer Ice Blast: draw this many fewer cards next hand
    int  curseTurnsLeft = 0;         // Petrify countdown, shared by the Basilisk (5 turns) and the
                                     // Cockatrice (3): player turns left before an automatic loss.
                                     // One slot, so the two can never stack a second countdown.
    bool assassinAmbushArmed = false;// Assassin: one free mid-turn strike on a random card the player plays this turn
    bool lichAddAlive = false;       // Lich Raise Undead: a summoned skeleton bodyguards the Lich until cut down
    int  lichAddHp = 0;
    int  lichAddMaxHp = 0;
    int  lichAddAtk = 0;
    bool fleshmassBindPending = false; // Fleshmass Bind: its lash landed; the player's next turn is bound
    bool playerBoundTurn = false;      // Bind active this player turn: only one card play allowed
    int  cardsPlayedThisTurn = 0;      // successful plays this turn (enforces Bind's 1-play limit)

    std::vector<Card> knightPreparedMoves; // Shadow Knight: up to 3 cards mirrored this round, revealed one per card played

    // Set by playCardFromHand() every time a card is played, so callers (the tutorial)
    // can tell what was just played even if that same handleInput() call also auto-ended
    // the turn and reset the hand - a hand-reset wipes the "used" flags this would
    // otherwise need to diff against.
    bool lastActionWasCardPlay = false;
    CardType lastPlayedCardType = CardType::ATTACK;
    DamageType lastPlayedPhysType = DamageType::NONE;
    DamageType lastPlayedPhysType2 = DamageType::NONE;

    // A card's value after the flat meta upgrade and the gear percentage.
    // Every place that used to write "value + bonus + equipBonus" goes through
    // these, so the two can never drift apart again.
    int atkWithGear(int rawValue) const;
    int defWithGear(int rawValue) const;
    int gearedValue(const Card& c, int rawValue) const;  // dispatches on card type
    int calculateDamage(int attackValue, int defenseValue) const;
    // HP the enemy would lose to this card right now, for the hover preview.
    // 0 for anything that would not land: non-attacks, a phased enemy, or
    // while the Lich's skeleton is soaking hits.
    int previewDamage(const Card& c) const;
    bool spendEnergy(int cost);
    void resetEnergy();
    void playCardFromHand(int index);
    void applyCardEffect(const Card& card);
    void applyPlayerStatus(StatusType type, int amount, double weakMultiplier = 1.5); // routes through Status Guard's ward
    bool applyEnemyStatus(StatusType type, int amount, double weakMultiplier = 1.5); // same, mirrored; false if warded
    void tickPlayerRend(); // Rend fires on the player's swing too (Shadow Knight mirror)
    bool tickEnemyRend(); // Rend fires on the enemy's swing; true if it killed them
    bool enemyCanDefend() const; // Fear only works on something that has a guard to raise
    bool tryStunEnemy(); // enemy.tryApplyStun(), blockable by a mirrored ward
    // ranged: the blow never closes the distance - a shot, a spell or a thrown
    // weapon. It drives both the sprite (the attacker holds its ground) and the
    // rules (Parry blocks it but has nothing in reach to riposte), so what the
    // scene shows and what the fight does stay in agreement.
    void enemyStrikePlayer(int atk, bool pierceHalfArmor, double weakMult, bool ranged = false,
                           bool useAttackFrames = true, int projectile = -1, bool closeIn = false, bool fromCompanion = false);
    int  enemyProjectile() const;    // frame from the generated ProjectileTable
    int  enemyMuzzleX() const;       // and where on its sprite the shot leaves
    int  enemyMuzzleY() const;
    // The Archon's attack frames ARE its pillars of flame, so every move it
    // makes raises them across the arena rather than only its Hellfire.
    bool enemyRaisesFlames() const;
    bool enemyIsFlyer() const;       // Wyvern, Falcon: dives in, pulls away, throws nothing
    bool archetypeIsRanged() const;  // RANGED and CASTER fight at a distance by nature
    void triggerAssassinAmbush(); // Assassin only: one free strike after a random card the player plays
    void armPerTurnEnemyMechanics(); // re-arms Assassin ambush at the start of each player turn
    void refreshBattleAuras(); // syncs the battle scene's persistent status glows to current playerStatus/enemy state
    void enemyTurn();
    void endPlayerTurn();
    void resetArmor();
    bool checkGameOver();
    void displayGameOver();
    bool handleGameOverInput();
    void finishRun(); // shared tail: record stats, ask Play again, reset or quit accordingly
    bool selectCardToCarryOver(Card& outCard); // on replay, before the deck resets - lets the player keep 1 card
    void startEncounter();
    bool rollSecretEncounter();  // true if this fight is the ??? one
    void beginSecretEncounter();
    void handleSecretWin();
    void handleSecretDefeat();
    void nextEncounter();
    void handleEncounterWin();
    void handleGameVictory(); // first Shadow Knight kill: legendary drop, victory screen, run ends
    void offerContinueOrEndRun(bool justWonEncounter = true); // justWonEncounter=false (load) starts the saved encounter instead of advancing past it
    void restSite();
    Enemy generateBossEnemy();
    void  bossAction();
    // closeIn: a RANGED boss that lunges for this particular move - the dragon
    // rakes with its claws, which means crossing the field to reach you.
    void  bossStrikesPlayer(int damage, bool raw, bool closeIn = false); // shared boss-attack resolution (armor, Dodge Reversal/Parry interception, damage)
    bool  trySecondWind(); // clamps a lethal playerHealth to 1 and consumes bossSecondWindAvailable; false if already 0 or already used
    void  prepareShadowKnightMoves(); // Shadow Knight only: secretly pick up to 3 cards to mirror this turn
    void  executeShadowKnightMirror(const Card& mirrored); // plays out one mirrored card's effect against the player
    void  triggerShadowKnightAmbush(); // reveals + plays one prepared move, called right after the player plays a card
    void  offerBossReward();
    void  offerExtraPlay(); // every 2nd boss kill - separate from the card reward
    void displayRunStats() const;
    void displayEnemyInfo() const;
    void offerCardReward();
    void offerExhaustedReward();   // pool is dry: a forge visit first, then duplicates
    void presentCardChoice(const std::vector<Card>& rewards,
                           const std::string& title, const std::string& skipPrompt);
    bool forgeMenu(const std::string& baseTitle); // true only if a card was upgraded
    void offerEquipmentDrop();
    void offerBoon();       // every 12th encounter
    int  luckBonus() const; // percentage points added to the run's rolls
    void applyUpgrades();
    void selectUpgrades();
    void viewDeckManage(); // browse/discard cards; never costs the rest site visit - always returns to its menu
    int  showMainMenu();   // 0 = Start Game, 1 = Load Save, 2 = Quit/ESC
    bool mainMenuFlow();   // the menu plus whatever it starts; false if quit
    void showHowToPlay();
    void showTutorial();   // interactive practice fight vs. a Training Dummy; restores state on exit

    // Three save slots, only ever written at the Continue/End Run choice (i.e.
    // between encounters, never mid-combat), so they can't be abused as a combat
    // checkpoint. Dying wipes the slot THIS run came from and no other: with one
    // shared file, starting a new game and dying deleted a saved run the player
    // had never touched.
    static const int SAVE_SLOTS = 3;
    int  currentSaveSlot = 0;        // 1-3 once this run is tied to a slot, else 0
    std::string savePath(int slot) const;
    bool saveExists(int slot) const;
    bool anySaveExists() const;
    // "Encounter 12, 11 defeated" for the slot picker, or an empty string.
    std::string saveSummary(int slot) const;
    // The slot picker itself. Returns 1-3, or 0 if the player backed out.
    int  chooseSaveSlot(const std::string& title, bool forSaving);
    void saveGame(int slot);
    bool loadGame(int slot); // false if no save file, or it couldn't be parsed
    void deleteSave(int slot) const;
    // Dying: only the slot this run was loaded from or saved into.
    void deleteCurrentSave();
    // One-time move of an old single-file save into slot 1.
    void migrateLegacySave() const;

public:
    Game();
    
    void init();
    void run();
    void notice(const std::string& text);      // centred one line result screen
    bool confirm(const std::string& prompt);   // centred yes/no
    void syncHud();                  // push current combat state into the panel
    void handleInput();
    void displayActionLog() const;   // scrollable replay of this fight
};

#endif
