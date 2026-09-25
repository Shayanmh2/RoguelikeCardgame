#ifndef GAME_H
#define GAME_H

#include "Deck.h"
#include "Enemy.h"
#include "Run.h"
#include "RewardPool.h"
#include "RunStats.h"
#include "StatusEffect.h"
#include "UpgradeSystem.h"
#include "Hud.h"
#include <vector>
#include <functional>
#include <iosfwd>

class Game {
public:
    // Which fifty you are walking. Random is Normal with the roster shuffled;
    // Hard scales as if fifty fights had come first. Saved with the run, so a
    // loaded save resumes its own road.
    enum class Mode { NORMAL, RANDOM, HARD, RANDOM_HARD };

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
    // --- the price of the cards that pay for their power -----------------
    // These bypass applyPlayerStatus(), so Status Guard cannot ward off your own
    // drawback and Dodge Reversal cannot bounce it onto the enemy.
    int  cardDamagePenalty = 0;      // Reckless Swing: flat damage off every card
    int  pendingDamagePenalty = 0;   // ...which lands on the following turn
    int  cardSoftenPct = 0;          // Heavy Guard: % off your damage this turn
    int  vulnerableTurns = 0;        // Berserk Stance: you take more, briefly
    double vulnerableMult = 1.0;
    int  energyDebt = 0;             // Adrenaline: borrowed from next turn
    int  bloodlustCrashPending = 0;  // Bloodlust: the Weaken owed when its fury ends
    int  cardLimitThisTurn = 0;      // Shatterpoint: cards allowed this turn, 0 = no cap
    bool noHealThisEncounter = false;// Last Stand, Pact of Ruin
    bool pactOfRuinActive = false;   // every attack festers, every card bleeds
    // Counters, not flags. Two Borrowed Times in one turn charged you twice for
    // one extra turn; now the second card buys a second turn and a second stun.
    int  extraTurnsPending = 0;      // Borrowed Time
    int  borrowedStunsPending = 0;   // one charged at the end of each borrowed turn
    bool armorBroken = false;           // a card spent your guard this turn
    int  maxHpDebt = 0;      // ...and what it costs until the fight ends
    std::vector<Card> exhausted;     // Sacrifice: handed back when the fight ends
    // Boons
    int  attunementBoons = 0;        // +8% elemental on-hit chance each
    int  gearInterval = 3;           // Scavenger: encounters between gear drops

    int  statusWardTurns = 0;      // Status Guard: blocks every ailment the enemy inflicts while it lasts
    bool enemyStatusWardActive = false; // Shadow Knight mirroring Status Guard: blocks the next ailment the player inflicts on it
    int  enemyTauntTurns = 0; // Taunt: enemy's action roll is forced toward Attack for this many of their turns
    int  enemyFearTurns  = 0; // Fear: each of these turns the enemy has a FEAR_BRACE_CHANCE to brace instead of acting

    // The ??? encounter, once per area per run: it does not advance the fight
    // counter and losing it cannot end the run. Bit n is set once area n's has
    // been met; moonZone says which shape it wears.
    int  moonZonesSeen = 0;
    int  moonZone = 2;
    // Seals broken after bosses. Each one makes every enemy tougher for the
    // rest of the run; that is the whole of what breaking one does.
    int  sealsBroken = 0;
    // The gear being worn, as a sheet tier (0 = the starting kit). Anything up
    // to what has been claimed can be put on at a rest site.
    int  wornWeapon = 0, wornArmor = 0;
    // Relics held, one bit per Relic id (see Game.cpp).
    Mode runMode = Mode::NORMAL;
    // Random mode draws from a shuffled bag of the 44 regulars, so nothing
    // repeats until every one has been fought. The seed is saved, so a loaded
    // run keeps the order it was playing.
    unsigned randomSeed = 0;
    std::vector<int> randomOrder;
    // What this player has cleared, kept in progress.dat beside the slots:
    // bit 0 the fifty, bit 1 Hard, bit 2 Random Hard. Dying never touches it.
    int  clearedMask = 0;
    // A fresh start on a harder road carries a veteran's flat bonus on every
    // card, because the starting deck alone cannot mark those enemies.
    int  roadBonus = 0;
    // And a starting weapon and armour built for that road: a percentage on
    // every card that sits under whatever gear is picked up later.
    int  roadGearPct = 0;
    int  relicsOwned = 0;
    // Trophy sets, kept with the cleared roads rather than in a run: bit 0
    // the Shadow Knight's, bit 1 the moon's. Worn as tiers 7 and 8.
    int  unlockedSets = 0;
    static const int TIER_SHADOW = 7, TIER_MOON = 8;
    bool hasSet(int bit) const { return (unlockedSets & (1 << bit)) != 0; }
    // How far the equipment ladder goes: six rungs, plus one per trophy set.
    // Hard needs the fifty cleared first, so the Shadow Knight's set always
    // comes before the moon's.
    int  maxGearTier() const { return 6 + (hasSet(0) ? 1 : 0) + (hasSet(1) ? 1 : 0); }
    // Moonstruck fought this run, across waves. One, with four broken seals,
    // is what it takes to meet the Shadow Knight's true form.
    int  moonstruckMet = 0;
    // How far into the "Sit a while" passages this run has read.
    int  satCount = 0;
    // The true form mirrors the stances the plain knight let fizzle: Parry
    // uses enemyParryStance, Taunt uses playerAttackOnly, and Dodge Reversal
    // is this, which turns your next hit back on you.
    bool enemyReflectNext = false;
    // Set while the second phase is on the field, so the knight's death is
    // read as the form rising rather than as the fight being over.
    bool trueFormPhase = false;
    // Heads on the Hydra. It starts with two that bite, and every head it
    // grows back adds another to the same move, so mending is the threat.
    int  hydraHeads = 2;
    // Heads cut off by a Pierce hit whose stumps are still open: Regrowth
    // brings two back from each. A Fire hit sears them shut.
    int  hydraStumps = 0;
    // Every Judgement the Paladin lands makes the next one worse.
    int  paladinJudgements = 0;
    // The true form answers your cards with the whole card, price included,
    // so it runs up the same kinds of debt you do. All of it ends with the
    // fight (endEncounterEffects) and starts clean when it stands up.
    int  enemyArmorHoldTurns = 0;      // Fortify, Turtle Up, Last Stand: its armour outlasts its turn
    int  enemyVulnerableTurns = 0;     // Berserk: it takes x1.5 from your attacks until its turn
    bool enemyNoHeal = false;          // Last Stand, Pact of Ruin: its wounds stop closing
    bool enemyPactOfRuin = false;      // Pact of Ruin: its blows fester, every move costs it blood
    int  knightMoveDebt = 0;           // moves it has already spent out of next round
    int  knightChainDepth = 0;         // how deep a run of extra moves has gone
    bool knightSacrificeSpent = false; // Sacrifice leaves the fight once it is played
    static const int HYDRA_HEADS_MAX = 5;
    // The bucket the enemy's last move came out of, so it is less likely to
    // do the same thing twice running.
    int  lastMoveRoll = -1;
    bool redThreadUsed = false;     // Red Thread: once a run
    bool lastSaveByThread = false;  // the last lethal blow was caught by the Thread, not the boss save
    // Attacks played this turn: the Iron Sword and the Mythril Edge both act on
    // the first. openingAttack is true while that first attack resolves, since
    // the counter has already moved on by then.
    int  attacksPlayedThisTurn = 0;
    bool openingAttack = false;
    // Scholar's Lens: the enemy's next two rolls, drawn at the start of your
    // turn so its move can be shown before it makes it. -1 when not drawn.
    int  lensRoll = -1, lensSigRoll = -1;
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
    // Which of the dead is standing there. Every message about the add reads
    // this, so raising a Wraith does not announce a skeleton.
    std::string lichAddName = "Skeleton";
    bool fleshmassBindPending = false; // Fleshmass Bind: its lash landed; the player's next turn is bound
    bool playerBoundTurn = false;      // Bind active this player turn: only one card play allowed
    int  cardsPlayedThisTurn = 0;      // successful plays this turn (enforces Bind's 1-play limit)

    std::vector<Card> knightPreparedMoves; // Shadow Knight: up to 3 cards mirrored this round, revealed one per card played

    // Set by playCardFromHand(), so the tutorial can tell a card was played even
    // when the same handleInput() call ended the turn and reset the hand.
    bool lastActionWasCardPlay = false;
    CardType lastPlayedCardType = CardType::ATTACK;
    bool lastPlayedCardWasRisky = false;   // the tutorial points the gold ring out once
    DamageType lastPlayedPhysType = DamageType::NONE;
    DamageType lastPlayedPhysType2 = DamageType::NONE;

    // A card's value after the flat meta upgrade and the gear percentage.
    // Everything that shows or deals a card's number goes through these.
    int atkWithGear(int rawValue) const;
    // Gear percentages including the road's baseline, for the cards and for
    // every screen that quotes them.
    int weaponPct() const { return equipDamagePercent + roadGearPct; }
    int armorPct() const { return equipArmorPercent + roadGearPct; }
    int defWithGear(int rawValue) const;
    int gearedValue(const Card& c, int rawValue) const;  // dispatches on card type
    int calculateDamage(int attackValue, int defenseValue) const;
    // HP the enemy would lose to this card right now, for the hover preview.
    // 0 for anything that would not land: non-attacks, a phased enemy, or
    // while the Lich's skeleton is soaking hits.
    int previewDamage(const Card& c) const;
    // What a card is worth on this board, not on its face: All In and Last Stand
    // read your armour and your wounds rather than their own printed value.
    int liveValue(const Card& c) const;
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
    // ranged: a shot, spell or thrown weapon. The attacker holds its ground in
    // the scene, and Parry blocks it but has nothing in reach to riposte.
    // ignoreArmor: straight through the armour, which is left standing.
    void enemyStrikePlayer(int atk, bool pierceHalfArmor, double weakMult, bool ranged = false,
                           bool useAttackFrames = true, int projectile = -1, bool closeIn = false, bool fromCompanion = false,
                           bool ignoreArmor = false);
    int  enemyProjectile() const;    // frame from the generated ProjectileTable
    int  enemyMuzzleX() const;       // and where on its sprite the shot leaves
    int  enemyMuzzleY() const;
    // The Archon's attack frames ARE its pillars of flame, so every move it
    // makes raises them across the arena rather than only its Hellfire.
    // The Omneye reaches you with a beam joined to its pupil on every attack.
    bool enemyFiresBeam() const;
    void offerSeal();
    // Difficulty plumbing: the picker, the permanent record of what has been
    // cleared, and the snapshot of the run that cleared it.
    bool chooseMode(Mode& out, bool& carryWinningRun);
    void startRunInMode(Mode m, bool carryWinningRun);
    void applyRoadStart(Mode m);   // the kit a fresh run on a harder road starts with
    void buildRandomOrder();
    int  rosterIndexFor(int regularIndex) const;
    // One step up, and only one. Random Hard is Hard with the roster
    // shuffled, not a tier above it.
    static int difficultyFor(Mode m) { return (m == Mode::HARD || m == Mode::RANDOM_HARD) ? 1 : 0; }
    const char* modeName() const;
    std::string progressPath() const;
    std::string winSavePath() const;
    void loadProgress();
    void saveProgress() const;
    void recordClear();
    void writeWinSave() const;
    bool loadWinSave();
    bool hasWinSave() const;
    void writeSaveTo(std::ostream& out) const;
    bool loadSaveFrom(std::istream& in);
    bool trueFormEarned() const;
    // Settings, as percentages of the authored values. They live beside the
    // saves rather than in one, because they describe the player and not the
    // run: dying must not reset how fast the text reads.
    int  optTextSpeed = 100;
    int  optPace      = 150;
    int  optMusic     = 100;
    int  optSfx       = 100;
    std::string settingsPath() const;
    void loadSettings();
    void saveSettings() const;
    void applySettings() const;
    void showSettings();
    void beginTrueForm();
    void displayPlayerInfo() const;
    bool hasRelic(int id) const;
    void offerRelic();
    void applyFightStartRelics();
    void rollLens();
    std::string lensIntent() const;
    DamageType enemyAttackType() const;
    int  armourTypeMod(DamageType t) const;   // -25 resisted, 0, +25 weak
    int  effectiveCost(const Card& c) const;  // after the Mythril Edge
    void onPlayerHit(const Card& card, int hpLost);
    int  sealScaled(int base, int pctPerSeal) const;
    void equipmentMenu();
    void showIntro();
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
    void  bossStrikesPlayer(int damage, bool raw, bool closeIn = false,
                            bool unstoppable = false); // shared boss-attack resolution (armor, Dodge Reversal/Parry interception, damage); unstoppable goes through all of it
    bool  trySecondWind(); // catches a lethal blow: the boss save leaves 1 HP, the Red Thread half; false if neither is left
    std::string savedLine(const char* bossSave) const; // what to say about the catch; bossSave is the 1 HP wording
    void  prepareShadowKnightMoves(); // Shadow Knight only: secretly pick up to 3 cards to mirror this turn
    void  executeShadowKnightMirror(const Card& mirrored); // plays out one mirrored card's effect against the player
    bool  trueFormMirror(const Card& mirrored, int atk, int v); // the true form's version of the cards the knight only half knew
    void  knightExtraMove();  // one more mirrored move, straight away (Blood Price, Adrenaline, Borrowed Time)
    void  triggerShadowKnightAmbush(); // reveals + plays one prepared move, called right after the player plays a card
    void  offerBossReward();
    void  offerExtraPlay(); // every 2nd boss kill - separate from the card reward
    void displayRunStats() const;
    void displayEnemyInfo() const;
    void offerCardReward();
    void offerExhaustedReward();   // pool is dry: a forge visit first, then duplicates
    void presentCardChoice(const std::vector<Card>& rewards,
                           const std::string& title, const std::string& skipPrompt,
                           std::function<std::vector<Card>()> reroll = nullptr);
    bool forgeMenu(const std::string& baseTitle); // true only if a card was upgraded
    void offerEquipmentDrop();
    void offerBoon();       // every 12th encounter
    int  attunementChance() const;   // elemental on-hit chance, 10% plus boons
    void payPactOfRuin();            // the HP every card costs under the pact
    void endEncounterEffects();      // hand back exhausted cards, clear per-fight costs
    int  luckBonus() const; // percentage points added to the run's rolls
    void applyUpgrades();
    void selectUpgrades();
    void viewDeckManage(); // browse/discard cards; never costs the rest site visit - always returns to its menu
    int  showMainMenu();   // 0 = Start Game, 1 = Load Save, 2 = Quit/ESC
    bool mainMenuFlow();   // the menu plus whatever it starts; false if quit
    void showHowToPlay();
    void showTutorial();   // interactive practice fight vs. a Training Dummy; restores state on exit

    // Three save slots, written only between encounters (at Continue/End Run) so
    // they cannot be used as a combat checkpoint. Dying wipes only the slot this
    // run came from.
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
    Hud::State hudState() const;     // that state, read fresh; the panel pulls it every frame
    void handleInput();
    void displayActionLog() const;   // scrollable replay of this fight
};

#endif
