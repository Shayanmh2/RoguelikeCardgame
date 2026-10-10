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
    // Which road you are walking. Random is Normal with the roster shuffled;
    // Hard scales as if fifty fights had come first; Quick is twenty-five,
    // every area at half the length. Saved with the run, so a loaded save
    // resumes its own road. Append only: the save holds the number.
    enum class Mode { NORMAL, RANDOM, HARD, RANDOM_HARD, QUICK };

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
    // Counters, not flags: a second Borrowed Time in the same turn buys a
    // second turn and a second stun.
    int  extraTurnsPending = 0;      // Borrowed Time
    int  borrowedStunsPending = 0;   // one charged at the end of each borrowed turn
    bool armorBroken = false;           // a card spent your guard this turn
    int  maxHpDebt = 0;      // ...and what it costs until the fight ends
    std::vector<Card> exhausted;     // Sacrifice, Last Stand: handed back when the fight ends
    // Boons
    int  attunementBoons = 0;        // +12% elemental on-hit chance each
    int  gearInterval = 3;           // Scavenger: encounters between gear drops

    int  statusWardTurns = 0;      // Status Guard: blocks every ailment the enemy inflicts while it lasts
    bool enemyStatusWardActive = false; // Shadow Knight mirroring Status Guard: blocks the next ailment the player inflicts on it
    int  enemyTauntTurns = 0; // Taunt: enemy's action roll is forced toward Attack for this many of their turns
    int  enemyFearTurns  = 0; // Fear: each of these turns the enemy has a FEAR_BRACE_CHANCE to brace instead of acting

    // The ??? encounter, once per area per run: it does not advance the fight
    // counter and losing it cannot end the run. Bit n is set once area n's has
    // been met; moonZone says which shape it wears.
    int  moonZonesSeen = 0;
    int  moonZonesBeaten = 0;     // the Moon Shades beaten this run, by area: all five open the church
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
    // repeats until every one has been fought, and Quick keeps the four it
    // drew from each area here. The seed is saved, so a loaded run keeps the
    // order it was playing.
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
    // Whether this run has rested at a rest site, or thrown a card away there:
    // Sleepless and Kept Them All are for a clear that did neither.
    bool restedThisRun = false;
    bool discardedThisRun = false;
    // A card other than poison, burn or rend played from encounter 10 on:
    // Magician is for a clear that never did.
    bool playedNonDot = false;
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
    // For the achievements that watch one fight: the Hydra growing a head, the
    // Thunder Beast's stuns that landed, and whether the last blow was parried.
    bool hydraRegrew  = false;
    int  thunderStuns = 0;
    bool parryLanded  = false;
    // Every Judgement the Paladin lands makes the next one worse.
    int  paladinJudgements = 0;
    // The ruined church's nine: each has one rule of its own, and all of it
    // is reset in startEncounter().
    int  bellTolls = 0;          // Bellringer: tolls rung; the third is the Great Toll
    int  inquisitorMarks = 0;    // Inquisitor: its marks on you; the bolt after the third ignores armour
    bool gargoyleStone = false;  // Gargoyle: stone through your turn, so nothing hurts it
    int  glassArmor = 0;         // Glass Templar: the glass it put on this turn
    bool glassUp = false;        // and whether that glass still stands
    bool saintRisen = false;     // Exhumed Saint: it gets back up once
    bool fireKill = false;       // the blow that just felled the enemy was Fire
    std::string confessedCard;   // Confessor: the card it named this turn
    // The False Moon's Moonstruck: turns until you are its vessel, counting
    // down from ten, and the quarters of its health you have taken, each of
    // which won one back. 0 when it is not running.
    int  moonClock = 0;
    int  moonQuarters = 0;
    int  moonShadeLast = -1;            // the shape the False Moon wore last, so it never wears one twice running
    // The true form answers your cards with the whole card, price included,
    // so it runs up the same kinds of debt you do. All of it ends with the
    // fight (endEncounterEffects) and starts clean when it stands up.
    int  enemyArmorHoldTurns = 0;      // Fortify, Turtle Up, Last Stand: its armour outlasts its turn
    int  enemyVulnerableTurns = 0;     // Berserk: it takes x1.5 from your attacks until its turn
    bool enemyNoHeal = false;          // Last Stand, Pact of Ruin: its wounds stop closing
    bool enemyPactOfRuin = false;      // Pact of Ruin: its blows fester, every move costs it blood
    int  knightMoveDebt = 0;           // moves it has already spent out of next round
    bool playerSkippedTurn = false;    // the turn just ended with no card played: the final boss swings
    int  knightChainDepth = 0;         // how deep a run of extra moves has gone
    bool knightSacrificeSpent = false; // Sacrifice leaves the fight once it is played
    bool knightLastStandSpent = false; // and so does Last Stand
    int  knightUnleashSpent = 0;       // and each payoff it set off: a bit each, Burn 1, Poison 2, Rend 4
    static const int HYDRA_HEADS_MAX = 9;
    // The bucket the enemy's last move came out of, so it is less likely to
    // do the same thing twice running.
    int  lastMoveRoll = -1;
    bool redThreadUsed = false;     // Red Thread: once a run
    bool lastSaveByThread = false;  // the last lethal blow was caught by the Thread, not the boss save
    int  threadClockFrom = 0;       // what the False Moon's clock read before that catch moved it, or 0
    // Attacks played this turn: the Iron Sword and the Mythril Edge both act on
    // the first. openingAttack is true while that first attack resolves, since
    // the counter has already moved on by then.
    int  attacksPlayedThisTurn = 0;
    bool openingAttack = false;
    // The Mythril Edge's discount, spent this turn on an attack you paid for.
    // A free attack never spends it.
    bool mythrilSpent = false;
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
    // Raise Undead: the last undead you killed this run, called up to fight
    // beside you. It takes the enemy's blows until it falls and strikes at the
    // end of each of your turns. raisedValue is the card's own number: the
    // weapon and the undead's own weight go on as it strikes.
    std::string lastUndead;
    bool raisedAlive = false;
    std::string raisedName;
    int  raisedHp = 0, raisedMaxHp = 0, raisedValue = 0;
    // The true form's Turnabout: your armor's resistances and its weakness
    // swap places until the fight ends.
    bool armorReversed = false;
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
    // Routes through Dodge Reversal and Status Guard's ward; true only if it landed on you.
    bool applyPlayerStatus(StatusType type, int amount, double weakMultiplier = 1.5);
    bool applyEnemyStatus(StatusType type, int amount, double weakMultiplier = 1.5); // same, mirrored; false if warded
    void tickPlayerRend(); // Rend fires on the player's swing too (Shadow Knight mirror)
    bool tickEnemyRend(); // Rend fires on the enemy's swing; true if it killed them
    bool enemyCanDefend() const; // Fear only works on something that has a guard to raise
    bool tryStunEnemy(); // enemy.tryApplyStun(), blockable by a mirrored ward
    // Raise Undead: the kill it remembers, the card's face in the hand, the
    // dead's turn, a blow they take for you, and what they strike for.
    void noteUndeadKill(const std::string& name);
    std::string raiseFace(const Card& c) const;
    void raisedStrikes();
    void raisedTakes(int blow);
    // The add in front of the enemy takes its own turn: the Lich's, or the
    // one the true form raises out of your own dead.
    void addStrikes();
    int  raisedBase(int value) const;
    int  raisedStrikeFor(const std::string& name, int value) const;
    // The payoff cards: one ailment set off at once, before and after armour.
    int  unleashTotal(const Card& c) const;
    int  unleashDamage(const Card& c) const;
    // Feint's pieces: the type your armor resists that its blows can turn to
    // (NONE if none), whether that would help right now, and what Turnabout
    // and Feint say in the hand about this enemy.
    DamageType feintType() const;
    bool feintHelps() const;
    std::string typesFace(const Card& c) const;
    // The Hydra's open stumps, seared shut: `by` is what did it.
    void searStumps(const char* by);
    // A Rend tear on the Hydra: a chance to take a head off. True if it did.
    bool rendCutsHead();
    // What a poison, burn or rend of `amount` comes to under its relic.
    int  dotPower(StatusType type, int amount) const;
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
    bool onQuick() const { return runMode == Mode::QUICK; }
    // The road's own pace: Quick hands out the same four boons and four
    // relics in half the fights, and gear every second fight.
    int  boonInterval() const;
    int  relicFirst() const;
    int  relicInterval() const;
    int  baseGearInterval() const;
    const char* modeName() const;
    std::string progressPath() const;
    std::string winSavePath() const;
    void loadProgress();
    void saveProgress() const;
    void recordClear();
    void earn(int achievement);       // Achievements::earn, saved at once if it is new
    void updateHeartbeat();           // beats under a fight at a fifth of your health or less
    void checkDeckAchievements();     // the ones a card in the deck earns, whichever way it arrived
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
    // Which prompts to skip: 0 none, 1 the battle's "press a key for your
    // turn", 2 that and every yes/no and result notice as well.
    int  optConfirm   = 0;
    // The soft taps on moving through and choosing from a menu: 1 on, 0 off.
    int  optMenuSounds = 1;
    std::string settingsPath() const;
    void loadSettings();
    void saveSettings() const;
    void applySettings() const;
    void showSettings();
    void beginTrueForm();
    // The ruined church, past the peak: open when every Moonstruck shape has
    // been beaten in some run and every vigil is out in this one. The true
    // form then runs there instead of going down.
    bool churchEarned() const;
    void trueFormRuns();          // the true form beaten, the church open: it flees, the run goes on
    // The roads and sets a clear opened. On the way down to the church
    // nothing is said when nothing new opened, and the moon is not asleep.
    void announceClear(int before, int beforeSets, bool toTheChurch = false);
    void handleTrueVictory();     // the False Moon put out: the true ending, the run ends
    // The final bosses that answer your cards with your own: the Shadow
    // Knight and the False Moon. The second plays every card whole, as the
    // true form does.
    bool mirrorBoss() const;
    bool wholeMirror() const;
    std::string mirrorName() const;   // "Shadow Knight" or "False Moon", for its lines
    std::string mirrorThe() const;    // "The shadow" or "The False Moon"
    bool mirrorBossIsMoon() const { return enemy.getBossType() == BossType::FALSE_MOON; }
    // The ruined church's rules, where they reach outside the enemy's own turn.
    bool enemyIs(const char* name) const { return enemy.getName().find(name) != std::string::npos; }
    void churchFightStart();      // what each of the nine starts the fight with
    void churchTurnLost();        // a turn it lost to a stun or spent cowering
    void confessorNames();        // the Confessor names the card it will take as a confession
    void shovelGraveDirt();       // the Sexton's blows fill your draw pile, three at most
    void clearGraveDirt();        // and the dirt goes when the fight does
    bool saintRises();            // the Exhumed Saint gets back up; true if it did
    // Your own blow, back at you off the Mirror Nun's glass or the Glass
    // Templar's shards. Your armour takes it first.
    void churchBackfire(int amount, const std::string& line);
    void unchosenCopies(int armor, int heal, int strengthTurns, double strengthMult);
    std::string churchRule() const;    // View Enemy: the rule, and where it stands
    std::string churchNotice() const;  // the panel's one-line warning, when there is one
    // The False Moon's Moonstruck: cast before your first turn, a turn nearer
    // at the end of each round, a turn back for every quarter of it you take.
    void moonstruckCast();
    void moonClockTick();
    bool falseMoonShadeMove();                       // a turn let go: one of the shapes it wore, sometimes
    void moonClockWinBack();
    void moonClockOffering(const Card& card);        // Sacrifice and Last Stand each win a turn back
    void moonPiecesChanged(int before, int after);   // says which pieces went, or came back
    std::string moonPiecesText(int before, int after) const;  // the lines that says, without printing
    std::string moonTaken() const;                   // the pieces it has right now, by name
    // Whether it has taken the piece it takes when the clock reaches `at`.
    bool moonTook(int at) const { return moonClock > 0 && moonClock <= at; }
    void displayPlayerInfo() const;
    bool hasRelic(int id) const;
    void offerRelic();
    void applyFightStartRelics();
    void rollLens();
    std::string lensIntent() const;
    // The move a signature turn will be, named as View Enemy names it, for a
    // roll; empty when that turn goes to the kit instead. The fallback is what
    // an enemy with no move of its own does on that turn.
    std::string ownMoveName(int roll, bool taunted) const;
    std::string fallbackMoveName(int roll) const;
    bool enemyWeakens() const;   // false in the first ten fights, see NO_WEAKEN_UNTIL
    int  ownMoveChance() const;  // how often the enemy uses its own moves right now
    int  kitRoll(int r) const;   // the kit's roll, with its Weaken bucket folded away early
    DamageType enemyAttackType() const;
    int  armourTypeMod(DamageType t) const;   // -25 resisted, 0, +25 weak
    int  effectiveCost(const Card& c) const;  // after the Mythril Edge
    // A card in hand that can still be played for nothing: out of energy only
    // ends the turn by itself when there is none.
    bool freeCardPlayable() const;
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
    int  handleGameOverInput(bool saved); // 0 play again, 1 main menu (after a save), 2 quit
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
    // The final boss answers each card you play with one of its prepared
    // moves: drawn as you commit the card, played before it lands when it is
    // a stance the true form holds, after it otherwise.
    bool  drawKnightAnswer(Card& out);
    bool  knightTakesStance(const Card& c) const;
    void  playKnightAnswer(const Card& c, bool beforeYourCard);
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
    int  lanternArmor() const;  // Warden's Lantern: the armor each turn starts with
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
    bool leftBySaving = false;       // the run just ended with Save and quit
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
    // Centred yes/no. Skipped under the "skip all" setting unless `always`:
    // the one that overwrites a save still asks.
    bool confirm(const std::string& prompt, bool always = false);
    void syncHud();                  // push current combat state into the panel
    Hud::State hudState() const;     // that state, read fresh; the panel pulls it every frame
    void handleInput();
    void displayActionLog() const;   // scrollable replay of this fight
    // View Player and View Enemy from the battle menu, each laid out to its
    // text (infoScreen) and the fight's screen put back afterwards.
    void showPlayerScreen();
    void showEnemyScreen();
    void infoScreen(void (Game::*print)() const);
};

#endif
