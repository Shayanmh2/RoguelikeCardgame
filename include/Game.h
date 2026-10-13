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
#include <map>
#include <set>
#include <vector>
#include <functional>
#include <iosfwd>

struct SDL_Renderer;
struct SDL_Rect;
namespace CardBar { struct Card; struct Action; }

class Game {
public:
    // Which road you're on. Random is Normal shuffled, Hard scales like fifty
    // fights already happened, Quick is 25 (every area half length). Saved with
    // the run. Only add to the end, the save stores the number.
    enum class Mode { NORMAL, RANDOM, HARD, RANDOM_HARD, QUICK };

    // A card's rarity as a step: 0 starter, 1 uncommon, 2 rare, 3 super rare,
    // 4 legendary; and its name, as the details panel shows it.
    static int rarityOf(const Card& c);
    static std::string rarityName(const Card& c);
    // The starting deck, one of each starter.
    static std::vector<Card> starterCards();

    // One page of the Lore Book: a story beat (tab 0), a fire (1) or a
    // cutscene to watch again (2, which says which).
    struct LoreEntry { int tab; std::string title; std::vector<std::string> lines; bool seen; int cutscene; };

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
    // Four, down from five. Cutting the action advantage needed both the cost
    // floor and a smaller hand; extra cards come from the 15/30/45 boon now.
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
    // These skip applyPlayerStatus() on purpose, so Status Guard can't block your
    // own drawback and Dodge Reversal can't bounce it onto the enemy.
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
    // The keepsakes from the camps (GameStops.cpp): held for the run, each
    // working on its own, the once-a-fight ones spent until the fight ends.
    int  keepsakes = 0;              // held, a bit each in KEEP_ order (the camps' zones)
    int  keepsakesSpent = 0;         // used up this fight
    bool hasKeepsake(int k) const { return (keepsakes >> k) & 1; }
    bool keepsakeReady(int k) const { return hasKeepsake(k) && !((keepsakesSpent >> k) & 1); }
    int  wickBonus(const Card& c) const;   // what the Lamplighter's Wick adds to this card's blow
    bool charmTakes(const Card& c) const;  // whether the Sister's Charm plays this card twice
    void springSnare();              // the Hunter's Snare, on the fight's first attack
    int  castNet(int blow);          // the Fisher's Net: the first blow past your stances, halved
    void templarsCandle();           // the Templar's Candle, at the start of a turn below half
    static const char* keepsakeName(int k);
    static std::string keepsakeRule(int k);   // what it does, for View Player and its card
    CardBar::Card keepsakeCard(int k) const;  // as the camp shows it
    // Boons
    int  attunementBoons = 0;        // +12% elemental on-hit chance each
    int  gearInterval = 3;           // Scavenger: encounters between gear drops

    int  statusWardTurns = 0;      // Status Guard: blocks every ailment the enemy inflicts while it lasts
    bool enemyStatusWardActive = false; // Shadow Knight mirroring Status Guard: blocks the next ailment the player inflicts on it
    int  enemyTauntTurns = 0; // Taunt: enemy's action roll is forced toward Attack for this many of their turns
    int  enemyFearTurns  = 0; // Fear: each of these turns the enemy has a FEAR_BRACE_CHANCE to brace instead of acting

    // The Moon Shade fight, once per area per run. Doesn't advance the fight
    // counter and losing it can't end the run. Bit n = area n's has been met,
    // moonZone says which shape it's wearing.
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
    // Random draws from a shuffled bag of the 44 regulars (no repeats until all
    // are fought), Quick keeps its four per area here. The seed's saved so a
    // loaded run keeps its order.
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
    // How far the gear ladder goes: six rungs, plus one per trophy set. Hard
    // needs the fifty cleared first, so the Shadow set always comes before the
    // moon's.
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
    // Upgraded cards thrown out at a rest site this run: when the same card
    // turns up as a reward, it comes back the way you left it.
    std::vector<Card> discardedCards;
    void rememberDiscard(const Card& c);
    void showDiscardUpgrades(std::vector<Card>& offer) const;
    Card takeBackDiscard(const Card& c);

    // The stops on the road (GameStops.cpp): the Anvil and the Offering, each
    // passed once a run, and View Deck's trade of three cards for one.
    int stopsDone = 0;   // a bit each: 1 the Anvil, 2 the Offering
    void roadStops();
    void anvilStop(bool unchosens);      // false: the witch's guards', after 15
    void offeringStop(bool paladins);    // false: the roadside shrine, after 25
    void tradeThree();
    bool pickThreeOfARarity(const std::string& what, std::vector<Card>& out);
    bool hasThreeOfARarity() const;
    bool cardOfRarity(int rank, const std::vector<Card>& given, Card& out);
    void giveUp(const std::vector<Card>& cards);
    void showGotCard(const std::string& title, const Card& got);
    void leftWithTheArmor();   // the church road's legendary, at the peak
    // The Lost Camps: after a Moon Shade, the camp of the person tied to it.
    // Read the letter and take their keepsake, or bury them for max HP.
    void lostCamp(int zone);
    void campLore(std::vector<LoreEntry>& out) const;   // the Lore Book's letters
    int lettersRead = 0;   // a bit per camp whose letter has been read, in any run (progress.dat)
    std::vector<Card> moonShadePrize(int beatenThisRun);   // what a beaten Moon Shade was carrying
    std::vector<int> relicChoices(int nth) const;          // the relics on offer at the nth choice
    int  pickCardPaged(const std::string& title, const std::vector<Card>& cards, const std::vector<int>& counts);
    // A card as the screens outside a fight show it: through gear, at rest.
    CardBar::Card cardWidget(const Card& c, bool disabled = false) const;
    // A card other than poison, burn or rend played from encounter 10 on:
    // Magician is for a clear that never did.
    bool playedNonDot = false;
    // The true form copies the stances: Parry uses enemyParryStance, Taunt uses
    // playerAttackOnly, and Dodge Reversal is this, which turns your next hit
    // back on you.
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
    // The False Moon's Moonstruck clock: turns until you're its vessel (counts
    // down from ten), and how many quarters of its health you've taken (each won
    // a turn back). 0 when it isn't running.
    int  moonClock = 0;
    int  moonQuarters = 0;
    int  moonShadeLast = -1;            // the shape the False Moon wore last, so it never wears one twice running
    // The true form plays your cards back whole, price included, so it runs up
    // the same kinds of debt you do. All of it resets with the fight.
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
    // Attacks played this turn. The Iron Sword and the Mythril Edge care about
    // the first one; openingAttack is true while it resolves, since the counter
    // has already moved on by then.
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
    // Raise Undead: the last undead you killed this run, back to fight for you.
    // It takes the enemy's blows until it falls and hits at the end of your turns.
    // raisedValue is the card's number; weapon and the undead's weight go on top.
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
    // HP the enemy would lose to this card right now, for the hover preview. 0
    // for anything that wouldn't land (non-attacks, a phased enemy, the Lich's
    // skeleton soaking hits).
    int previewDamage(const Card& c) const;
    // What a card is worth on this board, not on its face: All In and Last Stand
    // read your armour and your wounds rather than their own printed value.
    int liveValue(const Card& c) const;
    bool spendEnergy(int cost);
    void resetEnergy();
    void playCardFromHand(int index);
    // What a card does once it's paid for. A weld runs each half through here;
    // `playedAs` is the name it was played under, which is what the Confessor
    // names.
    void resolveCardEffect(const Card& playedCard, const std::string& playedAs);
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
    // Feint stuff: the type your armour resists that its blows can turn into
    // (NONE if none), whether that helps right now, and what Turnabout and Feint
    // say in the hand for this enemy.
    DamageType feintType() const;
    bool feintHelps() const;
    std::string typesFace(const Card& c) const;
    // The Hydra's open stumps, seared shut: `by` is what did it.
    void searStumps(const char* by);
    // A Rend tear on the Hydra: a chance to take a head off. True if it did.
    bool rendCutsHead();
    // What a poison, burn or rend of `amount` comes to under its relic.
    int  dotPower(StatusType type, int amount) const;
    // ranged: a shot, spell or thrown weapon. The attacker stays put, and Parry
    // blocks it but has nothing in reach to riposte.
    // ignoreArmor: straight through your armour, which is left standing.
    void enemyStrikePlayer(int atk, bool pierceHalfArmor, double weakMult, bool ranged = false,
                           bool useAttackFrames = true, int projectile = -1, bool closeIn = false, bool fromCompanion = false,
                           bool ignoreArmor = false);
    int  enemyProjectile() const;    // frame from the generated ProjectileTable
    int  enemyMuzzleX() const;       // and where on its sprite the shot leaves
    int  enemyMuzzleY() const;
    // The Archon's attack frames ARE its pillars of flame, so every move raises
    // them, not just Hellfire. The Omneye hits you with a beam from its pupil on
    // every attack.
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
    // The Card Ledger: every card held in any run, by name (progress.dat).
    std::set<std::string> cardsHeld;
    void noteCardsHeld();                  // the deck's cards go in; all of them earns A Full Ledger
    void noteLorekeeper();                 // every enemy met and every Lore Book page earns Lorekeeper
    // The bestiary's record of each enemy, across runs (progress.dat REC), by
    // its place in the bestiary like MET.
    struct BeastRecord { int fought = 0, won = 0, lost = 0, fastest = 0; };
    BeastRecord beastRecords[128];
    int  recordIdNow() const;               // the one in front of you, as the bestiary knows it
    // Champions: one ordinary enemy in each area (the church's too), picked when
    // a run starts and saved with it, comes with a trait and a rarer reward.
    enum ChampionTrait { CHAMP_HARDENED, CHAMP_SWIFT, CHAMP_VENOMOUS, CHAMP_WARDED, CHAMP_THORNED, CHAMP_TRAITS };
    int  championRoster[6] = { -1, -1, -1, -1, -1, -1 };   // per area, which of the roster
    int  championTraitOf[6] = { -1, -1, -1, -1, -1, -1 };
    int  championNow = -1;           // this fight's trait, or -1
    bool championWardUp = false;     // Warded, until the first ailment breaks it
    bool championActed = false;      // the enemy got to act this turn, not stunned
    int  championTurns = 0;          // Swift's count
    void pickChampions();
    // The Chronicle: this run's numbers as it goes (saved with the run), and
    // every finished run's record in saves/chronicle.dat.
    struct RunTally {
        int turns = 0, dealt = 0, taken = 0, biggest = 0, rests = 0, forges = 0, sits = 0, seconds = 0;
        std::map<std::string, int> played;   // each card's plays, for the most played
    };
    RunTally runTally;
    unsigned runClock = 0;   // SDL ticks when this stretch of the run began
    int runResult = 0;       // how it ended: 1 died, 2 cleared the road, 3 the true ending, 4 ended early
    void tallySeconds();
    std::string chroniclePath() const;
    void writeChronicle();
    void showChronicle();
    // Crossroads, on the quick road only: the forks passed so far (a bit per
    // regular fight) and the ones that went the other way (fight, roster).
    unsigned crossDone = 0;
    std::vector<std::pair<int, int>> crossPicks;
    bool crossroadsAt(int regularIndex) const;
    int  crossroadsOther(int regularIndex) const;
    void offerCrossroads(int regularIndex);
    std::string lensNextFight() const;   // the Scholar's Lens at a rest site
    CardBar::Card crossroadsCard(int rosterIndex) const;
    int  bestiaryIcon(const std::string& enemyName) const;
    void championAfterTurn();
    static const char* championName(int trait);
    static const char* championRule(int trait);
    const char* championRewardNote() const;
    int rarityGate() const;   // 0 uncommons only, 1 adds rares, 2 adds super rares
    void recordFight(int outcome);          // 0 a fight starts, 1 you won it, 2 it beat you
    std::vector<std::string> recordLines(int id, int id2) const;   // its record page
    std::vector<Card> allCards() const;    // every card in the game: the starters, then the pool
    CardBar::Card printedWidget(const Card& c) const;   // a card at its printed numbers
    void writeWinSave() const;
    bool loadWinSave();
    bool hasWinSave() const;
    void writeSaveTo(std::ostream& out) const;
    bool loadSaveFrom(std::istream& in);
    bool trueFormEarned() const;
    // Settings, as % of the authored values. Kept next to the saves, not in one,
    // because they're about the player and not the run: dying shouldn't reset
    // how fast your text reads.
    int  optTextSpeed = 100;
    int  optPace      = 150;
    int  optMusic     = 100;
    int  optSfx       = 100;
    // Which prompts to skip: 0 none, 1 the battle's "press a key for your
    // turn", 2 that and every yes/no and result notice as well.
    int  optConfirm   = 0;
    // The soft taps on moving through and choosing from a menu: 1 on, 0 off.
    int  optMenuSounds = 1;
    // The combat panel's status tags: 0 symbols and words, 1 words only,
    // 2 symbols only (Hud::TagStyle).
    int  optTagStyle  = 0;
    std::string settingsPath() const;
    void loadSettings();
    void saveSettings() const;
    void applySettings() const;
    void showSettings();
    // The bestiary (GameBestiary.cpp): every enemy met, in a gallery off the
    // title menu. Who has been met is kept with the progress, a bit each.
    unsigned long long metMask[2] = { 0, 0 };
    bool hasMet(int id) const;
    bool churchFound() const;   // anything in the ruined church met: its tab and its pages show
    void markMet(const std::string& enemyName);   // saved at once if it is new
    void seedMetFromProgress();                   // what a save from before the bestiary has seen
    void showBestiary();
    // The Gallery (GameBestiary.cpp): the bestiary, the Lore Book and the Card
    // Ledger. The Lore Book reads every story beat and fire from Game.cpp and can
    // replay the cutscenes.
    // `all` lists every page, found or not: for counting, so the secret ones
    // keep the Gallery short of 100% until they are found.
    std::vector<LoreEntry> loreEntries(bool all = false) const;
    void loreProgress(int& have, int& need) const;      // bestiary entries met and pages found, of all of them
    int  galleryPercent() const;                        // the Gallery's corner: entries, pages and cards found
    bool achievementProgress(int id, int& have, int& need) const;
    void replayCutscene(int which);   // 0 the pond, 1 the morning on the peak, 2 the long night ending, 3 the Hard ending
    void showGallery();
    void showLoreBook();
    void showCardLedger();
    // Esc in a fight: Resume, Settings, or back to the main menu (always asks,
    // since a fight in progress isn't saved). True means leave.
    bool pauseMenu();
    bool quitToMenu = false;   // set by the pause menu, picked up by the run loop
    void abandonToMenu();      // drop the fight and everything since the last save
    void resetRunState();      // clean slate for the next run (finishRun and abandonToMenu)
    // The moments with no other record of being seen, a bit each
    // (progress.dat): 1 the False Moon taking him for its vessel.
    int scenesSeen = 0;
    void replayMoment(int which);   // the Lore Book's moments, 4 on (replayCutscene has 0-3)
    std::string bestiaryEntryText(int id) const;   // its entry, then weak to, resists and blows
    static int bestiaryIndexOf(const std::string& enemyName);
    // Card pictures: the bestiary's enemies here, everything else in the art layer.
    static void drawCardIcon(SDL_Renderer* r, int icon, const SDL_Rect& dst);
    static DamageType blowTypeOf(const std::string& name, EnemyType type);   // the kind of blow, by name
    static std::string flavorTextOf(const std::string& name);              // View Enemy's line
    void beginTrueForm();
    // The ruined church after the peak: open once every Moon Shade has been
    // beaten in some run and every vigil is out in this one. The true form runs
    // there instead of dying.
    bool churchEarned() const;
    void trueFormRuns();          // the true form beaten, the church open: it flees, the run goes on
    // The roads and sets a clear opened. On the way down to the church
    // nothing is said when nothing new opened, and the moon is not asleep.
    void announceClear(int before, int beforeSets, bool toTheChurch = false);
    void handleTrueVictory();     // the False Moon put out: the true ending, the run ends
    // The bosses that answer your cards with your own: the Shadow Knight and the
    // False Moon. The False Moon plays every card whole, like the true form.
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
    // The move a signature turn will be, named like View Enemy names it, for a
    // roll. Empty when the turn goes to the kit. The fallback is what an enemy
    // without its own move does then.
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
    int  sealScaled(int base, bool health) const;   // through every vigil put out
    int  sealPct(bool health) const;                 // the percent they add, all told
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
    // The final boss answers each card you play with one of its prepared moves:
    // drawn as you commit, played before yours lands if it's a stance the true
    // form holds, after it otherwise.
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

    // Three save slots, only written between encounters (Continue/End Run) so
    // they can't be used as a mid-fight checkpoint. Dying wipes only the slot the
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
    int  infoButtons(const std::vector<CardBar::Action>& acts);   // scrolls a long one while they wait
};

#endif
