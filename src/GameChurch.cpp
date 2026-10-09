// The Ruined Church: the rules of its nine and the False Moon's clock.
#include "Game.h"
#include "Achievements.h"
#include "Audio.h"
#include "Colors.h"
#include "UIHelper.h"
#include "EnemyArt.h"
#include "ProjectileTable.h"
#include "Console.h"
#include "CardBar.h"
#include "Hud.h"
#include "Platform.h"
#include <SDL.h>
#include <algorithm>
#include <iostream>
#include <random>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include "GameShared.h"

// The pieces it takes back, each when the clock reaches `at`.
struct MoonPiece { int at; const char* name; const char* taken; const char* back; };
static const MoonPiece MOON_PIECES[4] = {
    { 8, "strength",        "It takes back your strength. Your blows land a quarter softer.",
                            "Your strength comes back to you." },
    { 6, "wits",            "It takes back your wits. You draw a card fewer.",
                            "Your wits come back to you." },
    { 4, "speed",           "It takes back your speed. You have one energy fewer.",
                            "Your speed comes back to you." },
    { 2, "the way you move", "It takes back the way you move. Your guard comes up half as strong.",
                            "The way you move comes back to you." },
};

// The nine's rules that reach outside their own turns. Each is spelled out in
// View Enemy (churchRule) and, while it bites, on the panel (churchNotice).

void Game::churchFightStart() {
    // The Gargoyle starts on its perch, stone, so your first turn is for
    // setting up.
    if (enemyIs("Gargoyle")) {
        gargoyleStone = true;
        enemyInvulnerable = true;
    }
    EnemyArt::setEnemyStone(gargoyleStone);
    // The Templar comes in wearing the windows.
    if (enemyIs("Glass Templar")) {
        glassArmor = enemy.getBaseDefense() * 3;
        enemy.gainArmor(glassArmor);
        glassUp = true;
    }
}

// A turn it lost to a stun or spent flinching: the Bellringer misses a toll,
// the Inquisitor's marks fade, and the Gargoyle wakes without the drop.
void Game::churchTurnLost() {
    if (enemyIs("Bellringer"))
        std::cout << "  " << Color::DIM << "The bell stays silent, and a toll is missed." << Color::RESET << "\n";
    if (enemyIs("Inquisitor") && inquisitorMarks > 0) {
        inquisitorMarks = 0;
        std::cout << "  " << Color::DIM << "It does not fire, and its marks on you fade." << Color::RESET << "\n";
    }
    if (enemyIs("Gargoyle") && gargoyleStone) {
        gargoyleStone = false;
        std::cout << "  " << Color::DIM << "The stone cracks, and it stays on its perch, awake." << Color::RESET << "\n";
    }
}

// At the start of each of your turns: one attack card in your hand, at random.
void Game::confessorNames() {
    confessedCard.clear();
    if (!enemy.isAlive() || !enemyIs("Confessor")) return;
    std::vector<std::string> attacks;
    for (int i = 0; i < playerDeck.handSize(); ++i) {
        if (playerDeck.isCardUsed(i)) continue;
        const Card& c = playerDeck.getCardFromHand(i);
        if (c.getType() == CardType::ATTACK) attacks.push_back(c.getName());
    }
    if (attacks.empty()) {
        std::cout << "  " << Color::DIM << "The Confessor turns the pages of its book and finds nothing in your hand to name."
                  << Color::RESET << "\n";
        return;
    }
    static thread_local std::mt19937 gen(std::random_device{}());
    confessedCard = attacks[std::uniform_int_distribution<size_t>(0, attacks.size() - 1)(gen)];
    std::cout << Color::MAGENTA << "The Confessor reads a sin out of its book: " << Color::BOLD << confessedCard
              << Color::RESET << Color::MAGENTA << ". Play it this turn and it heals the Confessor."
              << Color::RESET << "\n";
    UIHelper::pause(150);
}

void Game::shovelGraveDirt() {
    int dirt = 0;
    for (const Card& c : playerDeck.getAllCardsOrdered()) dirt += isGraveDirt(c) ? 1 : 0;
    if (dirt >= GRAVE_DIRT_MAX) return;
    playerDeck.shuffleIn(Card(GRAVE_DIRT,
        "Earth off the Sexton's spade. It does nothing, and it goes when the fight does.",
        CardType::SPECIAL, 0, 0));
    std::cout << "  " << Color::DIM << "Earth off the spade lands in your pack: Grave Dirt, " << (dirt + 1)
              << " of " << GRAVE_DIRT_MAX << "." << Color::RESET << "\n";
}

void Game::clearGraveDirt() {
    while (playerDeck.removeCardByName(GRAVE_DIRT)) {}
}

// Once, at half its health, unless Fire finished it or it fell burning.
bool Game::saintRises() {
    if (enemy.isAlive() || playerHealth <= 0 || saintRisen || !enemyIs("Exhumed Saint")) return false;
    saintRisen = true;
    const bool burned = fireKill || enemy.hasBurn();
    fireKill = false;
    if (burned) {
        std::cout << "  " << Color::BURN_CLR << "The fire takes the grave cloth and the relic with it. It stays down."
                  << Color::RESET << "\n";
        UIHelper::pause(250);
        return false;
    }
    enemy.heal(enemy.getMaxHealth() / 2);
    EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::HEAL);
    Audio::playSFXPitched("special", 0.7f);
    std::cout << "  " << Color::BOLD << Color::MAGENTA << "The Exhumed Saint gets back up. It does not know it died."
              << Color::RESET << " (Enemy HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
              << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
    refreshBattleAuras();
    UIHelper::pause(300);
    return true;
}

void Game::churchBackfire(int amount, const std::string& line) {
    if (amount <= 0 || playerHealth <= 0) return;
    const int soaked = std::min(playerArmor, amount);
    playerArmor -= soaked;
    const int dmg = amount - soaked;
    playerHealth = std::max(0, playerHealth - dmg);
    const bool held = trySecondWind();
    if (dmg > 0) {
        EnemyArt::printBattleKnightHit(enemy.getType(), enemy.getBossType());
        EnemyArt::popNumber(dmg, false, EnemyArt::PopKind::DAMAGE);
    }
    Audio::playSFXPitched("hit", 1.15f);
    std::cout << "  " << Color::DAMAGE << line << " " << dmg << " damage." << Color::RESET;
    if (soaked > 0) std::cout << " " << Color::ARMOR_CLR << "[" << soaked << " blocked by armor]" << Color::RESET;
    std::cout << "  HP: " << hpColor(playerHealth, maxPlayerHealth) << playerHealth << "/" << maxPlayerHealth
              << Color::RESET << "\n";
    if (held)
        std::cout << "  " << Color::BOLD << Color::YELLOW
                  << savedLine("You refuse to fall, and cling on at 1 HP.") << Color::RESET << "\n";
    refreshBattleAuras();
    UIHelper::pause(200);
}

void Game::unchosenCopies(int armor, int heal, int strengthTurns, double strengthMult) {
    if (!enemy.isAlive() || !enemyIs("Unchosen")) return;
    if (armor > 0) {
        enemy.gainArmor(armor);
        std::cout << "  " << Color::MAGENTA << "The Unchosen raises its scrap shield the way you raised yours: +"
                  << armor << " armor." << Color::RESET << "\n";
    }
    if (heal > 0) {
        const int before = enemy.getHealth();
        enemy.heal(heal);
        std::cout << "  " << Color::MAGENTA << "It mends where you mended: +" << (enemy.getHealth() - before)
                  << " HP." << Color::RESET << "\n";
    }
    if (strengthTurns > 0) {
        enemy.applyStatus(StatusType::STRENGTH, strengthTurns, 1.5, strengthMult);
        EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::STRENGTH);
        std::cout << "  " << Color::MAGENTA << "It works itself up the way you did: its blows hit "
                  << timesText((float)strengthMult) << " for " << strengthTurns << " turns." << Color::RESET << "\n";
    }
    refreshBattleAuras();
}

std::string Game::churchRule() const {
    const int atk = enemy.getBaseAttack() + enemy.getBonusAttack();
    if (enemyIs("Bellringer"))
        return "It tolls on every turn it takes, and the third toll is the Great Toll, a blow for "
             + std::to_string(atk * 2 + 10) + ". Stun it or taunt it and it misses a toll. Tolls rung: "
             + std::to_string(bellTolls) + " of 3.";
    if (enemyIs("Confessor"))
        return std::string("At the start of each of your turns it names an attack card in your hand. "
                           "Play that card and its blow heals the Confessor instead.")
             + (confessedCard.empty() ? std::string() : " Named now: " + confessedCard + ".");
    if (enemyIs("Glass Templar"))
        return "It puts on " + std::to_string(enemy.getBaseDefense() * 3) + " armor of glass every turn. "
               "Break the glass with an attack and the shards cut you for half of it. Pierce hits go round "
               "the glass and leave it standing.";
    if (enemyIs("Gargoyle"))
        return std::string("Stone on every other turn, and nothing hurts it then. It wakes by dropping on you "
                           "from the roof. ") + (gargoyleStone ? "It is stone now." : "It is awake now.");
    if (enemyIs("Exhumed Saint")) {
        const std::string bless = "Below half its health it blesses itself for "
            + std::to_string(std::max(4, enemy.getMaxHealth() / 25)) + " each turn.";
        return saintRisen ? "It has got up once already, and will not again. " + bless
                          : "When it falls it gets back up once, at half its health, unless Fire finishes it "
                            "or it falls while burning. " + bless;
    }
    if (enemyIs("Mirror Nun"))
        return "The first attack you play each turn comes back off her mirror at you for half its damage. "
               "Your armor takes it first.";
    if (enemyIs("Sexton"))
        return "Every blow of his that reaches you puts Grave Dirt in your draw pile, three at most: a card "
               "that does nothing, gone when the fight ends.";
    if (enemyIs("Inquisitor"))
        return "Every turn it fires, it marks you, and the bolt after the third mark ignores your armor ("
             + std::to_string(atk + 4) + " dmg). If a stun or a taunt stops it firing, the marks fade. "
               "Marks: " + std::to_string(inquisitorMarks) + " of 3.";
    if (enemyIs("Fleshmass"))
        return "When its lash draws blood it holds on, and you can play only one card on your next turn. "
               "A blow your armor takes whole, or one you dodge or parry, leaves you free.";
    if (enemyIs("Unchosen"))
        return "Whatever armor, Strength or healing your cards give you, it gives itself the same.";
    return "";
}

std::string Game::churchNotice() const {
    if (!enemy.isAlive()) return "";
    if (moonClock > 0)
        return "MOONSTRUCK - its vessel in " + std::to_string(moonClock)
             + (moonClock == 1 ? " turn" : " turns");
    if (!confessedCard.empty() && enemyIs("Confessor"))
        return "CONFESSED - playing " + confessedCard + " heals the Confessor";
    if (enemyInvulnerable && enemyIs("Gargoyle"))
        return "STONE - nothing hurts the Gargoyle this turn";
    if (bellTolls >= 2 && enemyIs("Bellringer"))
        return "GREAT TOLL NEXT - the bell has rung twice";
    if (inquisitorMarks >= 3 && enemyIs("Inquisitor"))
        return "MARKED 3 of 3 - its next bolt ignores your armor";
    return "";
}

void Game::moonstruckCast() {
    moonClock = MOON_CLOCK_TURNS;
    moonQuarters = 0;
    // Its own animation and sound: it flares, a crimson wisp crosses, the
    // moon's red takes the knight and stays on him.
    EnemyArt::printBattleMoonstruck(enemy.getType(), enemy.getBossType());
    refreshBattleAuras();
    UIHelper::typeWrite(std::string(Color::BOLD) + Color::RED + "The False Moon opens its arms: MOONSTRUCK."
                        + Color::RESET + "\n");
    std::cout << "  " << Color::MAGENTA << "In ten turns you are its vessel. Every two, it takes back a piece of you,"
                 " in the order the road gave them back. Under five turns left, every quarter of its health you"
                 " take wins a turn back. Sacrifice and Last Stand each win a turn back."
              << (hasRelic(Relic::RED_THREAD) && !redThreadUsed
                  ? " If your Red Thread saves you, you have at least five turns left."
                  : "")
              << Color::RESET << "\n";
    UIHelper::pause(400);
}

void Game::moonClockTick() {
    if (moonClock <= 0 || playerHealth <= 0 || !enemy.isAlive()) return;
    moonClockWinBack();   // what you took from it this round counts first
    const int before = moonClock;
    if (--moonClock <= 0) {
        // The Red Thread holds against the moon too, once a run like any
        // save: it hauls you back out of it with five turns to go.
        if (hasRelic(Relic::RED_THREAD) && !redThreadUsed) {
            redThreadUsed = true;
            earn(Achievements::THREAD);
            Audio::playSFXPitched("special", 0.8f);
            std::cout << "  " << Color::BOLD << Color::YELLOW
                      << "The Red Thread pulls tight and hauls you back out of the moon." << Color::RESET << "\n";
            moonClock = MOON_THREAD_TURNS;
            moonPiecesChanged(before, moonClock);
            return;
        }
        Audio::playSFXPitched("church_bell", 0.5f);
        UIHelper::typeWrite(std::string("\n") + Color::BOLD + Color::RED
            + "The last piece goes up into the moon, and the rest of you with it. You are its vessel."
            + Color::RESET + "\n");
        UIHelper::pause(600);
        playerHealth = 0;
        // It took all of you, not your health, so there is no 1 HP to cling
        // to: a Poison or Burn tick after this cannot stand you back up.
        bossSecondWindAvailable = false;
        return;
    }
    moonPiecesChanged(before, moonClock);
}

// A turn let go against the False Moon. Half the time it takes one of the
// shapes it wore as the Moon Shades, the five the knight beat to get here,
// and uses that shape's own move in place of the light swing: never the same
// shape twice running, the Gaze only while the clock has more than two turns
// (it never takes the turn that ends you), the Howl only while its attack can
// still rise. Returns false when it swings instead.
bool Game::falseMoonShadeMove() {
    static thread_local std::mt19937 gen(std::random_device{}());
    if (std::uniform_int_distribution<>(0, 99)(gen) >= FALSE_MOON_SHADE_PCT) return false;
    std::vector<int> shapes;
    for (int z = 0; z < 5; ++z) {
        if (z == moonShadeLast) continue;
        if (z == 3 && moonClock <= 2) continue;
        if (z == 2 && enemy.getBonusAttack() >= 6) continue;
        shapes.push_back(z);
    }
    if (shapes.empty()) return false;
    const int z = shapes[std::uniform_int_distribution<>(0, (int)shapes.size() - 1)(gen)];
    moonShadeLast = z;
    static const char* SHAPE[5] = { "the Weaver's", "the Beguiler's", "the wolf's", "the Gorgon's", "the Templar's" };
    UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "You let the turn go, and the False Moon wears "
        + SHAPE[z] + " shape again: " + MOON_MOVES[z].name + "!" + Color::RESET + "\n");
    UIHelper::pause(200);
    auto castAt = [&](EnemyArt::CastGlow g) {
        EnemyArt::printEnemyCast(enemy.getType(), enemy.getBossType(), g, enemyProjectile(),
                                 enemyMuzzleX(), enemyMuzzleY(), 30);
    };
    const int def = enemy.getBaseDefense();
    switch (z) {
        case 0:   // Grave Silk
            castAt(EnemyArt::CastGlow::POISON);
            Audio::playSFX("poison");
            std::cout << Color::POISON_CLR << "Bone-white thread wraps you." << Color::RESET << "\n";
            if (applyPlayerStatus(StatusType::WEAK, 2))
                std::cout << "  You are " << Color::WEAK_CLR << "Weakened 2" << Color::RESET << ".\n";
            if (applyPlayerStatus(StatusType::POISON, 4))
                std::cout << "  You gain " << Color::POISON_CLR << "Poison 4" << Color::RESET << ".\n";
            break;
        case 1: { // Lunar Mirage
            castAt(EnemyArt::CastGlow::WEAK);
            nextHandPenalty = std::max(nextHandPenalty, 2);
            const int guard = def + 6;
            enemy.gainArmor(guard);
            Audio::playSFXPitched("special", 0.8f);
            std::cout << Color::MAGENTA << "The church doubles, then triples. You will draw 2 fewer cards,"
                      << " and it hides behind " << guard << " armor." << Color::RESET << "\n";
            break;
        }
        case 2:   // the Howl, without the healing: its health is the clock's
            enemy.addBonusAttack(std::min(2, 6 - enemy.getBonusAttack()));
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::STRENGTH);
            Audio::playSFXPitched("special", 0.7f);
            std::cout << Color::MAGENTA << "It howls the way the wolf did. Its attack rises." << Color::RESET << "\n";
            break;
        case 3: { // the Gaze: stone, and a turn of yours with it
            castAt(EnemyArt::CastGlow::STUN);
            const int before = moonClock;
            --moonClock;
            Audio::playSFXPitched("special", 0.75f);
            std::cout << Color::BOLD << Color::MAGENTA << "Its stare settles on you, and you stand there as stone"
                      << " while a turn goes by." << Color::RESET << "\n";
            moonPiecesChanged(before, moonClock);
            break;
        }
        default: { // Eclipse Ward
            const int guard = def * 2 + 8;
            enemy.gainArmor(guard);
            Audio::playSFXPitched("defend", 0.7f);
            std::cout << Color::ARMOR_CLR << "It goes dark behind its own eclipse. +" << guard << " armor."
                      << Color::RESET << "\n";
            break;
        }
    }
    UIHelper::pause(300);
    return true;
}

// Sacrifice and Last Stand are offerings the moon takes: each wins a turn
// back whenever the clock runs, up to the full ten.
void Game::moonClockOffering(const Card& card) {
    if (moonClock <= 0 || moonClock >= MOON_CLOCK_TURNS || enemy.getBossType() != BossType::FALSE_MOON) return;
    if (card.getEffect() != CardEffect::SACRIFICE && card.getEffect() != CardEffect::LASTSTAND) return;
    const int before = moonClock;
    ++moonClock;
    Audio::playSFXPitched("special", 1.2f);
    std::cout << "  " << Color::YELLOW << "Your " << card.getBaseName() << " wins a turn back from the moon."
              << Color::RESET << "\n";
    moonPiecesChanged(before, moonClock);
}

void Game::moonClockWinBack() {
    if (moonClock <= 0 || enemy.getBossType() != BossType::FALSE_MOON) return;
    const int maxHp = std::max(1, enemy.getMaxHealth());
    // Three quarters can be won: the fourth is the end of it.
    const int quarters = std::min(3, std::max(0, (maxHp - enemy.getHealth()) * 4 / maxHp));
    if (quarters <= moonQuarters) return;
    const int won = quarters - moonQuarters;
    moonQuarters = quarters;
    // Only under five turns from being its vessel does a quarter win a turn
    // back; taken earlier, it is just damage, or the clock would never run
    // down.
    if (moonClock >= MOON_WINBACK_UNDER) return;
    const int before = moonClock;
    moonClock = std::min(MOON_CLOCK_TURNS, moonClock + won);
    if (moonClock == before) return;
    Audio::playSFXPitched("special", 1.2f);
    std::cout << "  " << Color::YELLOW << (won == 1 ? "A quarter of it is gone, and you win a turn back."
                                                    : "Quarters of it are gone, and you win turns back.")
              << Color::RESET << "\n";
    moonPiecesChanged(before, moonClock);
}

void Game::moonPiecesChanged(int before, int after) {
    std::cout << moonPiecesText(before, after) << "\n";
    refreshBattleAuras();
    UIHelper::pause(250);
}

// One line for each piece that went or came back, then the turns left.
std::string Game::moonPiecesText(int before, int after) const {
    std::string s;
    for (const MoonPiece& p : MOON_PIECES) {
        if (before > p.at && after <= p.at)
            s += std::string("  ") + Color::RED + p.taken + Color::RESET + "\n";
        else if (before <= p.at && after > p.at)
            s += std::string("  ") + Color::GREEN + p.back + Color::RESET + "\n";
    }
    return s + "  " + Color::MAGENTA + "Moonstruck: " + std::to_string(after) + (after == 1 ? " turn" : " turns")
             + " until you are its vessel." + Color::RESET;
}

std::string Game::moonTaken() const {
    std::string s;
    for (const MoonPiece& p : MOON_PIECES)
        if (moonTook(p.at)) s += (s.empty() ? "" : ", ") + std::string(p.name);
    return s;
}

// All five Moon Shades beaten in this run, not across runs: the Moonlit
// Locket makes finding them certain, so a record kept across runs would
// open the church on every run after the first.
bool Game::churchEarned() const { return trueFormEarned() && moonZonesBeaten == 31; }
