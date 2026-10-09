// The Dark Mirror: the true form and the False Moon answer the knight's
// cards with his own, played whole, price and all.
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

// Which payoff a card is, as a bit: the true form plays each once a fight.
static int unleashBit(DamageType el) {
    return el == DamageType::FIRE ? 1 : el == DamageType::WIND ? 4 : 2;
}

// Secretly picks up to 3 cards to mirror this round. No-op for every other enemy.
void Game::prepareShadowKnightMoves() {
    knightPreparedMoves.clear();
    if (!mirrorBoss()) return;

    std::vector<Card> deckCards = playerDeck.getAllCardsOrdered();
    // Sacrifice and Last Stand leave the fight once the true form has played them.
    if (knightSacrificeSpent)
        deckCards.erase(std::remove_if(deckCards.begin(), deckCards.end(),
            [](const Card& c) { return c.getEffect() == CardEffect::SACRIFICE; }), deckCards.end());
    if (knightLastStandSpent)
        deckCards.erase(std::remove_if(deckCards.begin(), deckCards.end(),
            [](const Card& c) { return c.getEffect() == CardEffect::LASTSTAND; }), deckCards.end());
    // And each payoff once it has set it off.
    if (knightUnleashSpent)
        deckCards.erase(std::remove_if(deckCards.begin(), deckCards.end(),
            [this](const Card& c) {
                return c.getEffect() == CardEffect::UNLEASH && (knightUnleashSpent & unleashBit(c.getElemType()));
            }), deckCards.end());
    if (deckCards.empty()) return;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(deckCards.begin(), deckCards.end(), gen);
    // The False Moon answers more as it is hurt: three a round, four below
    // two thirds of its health, five below a third.
    int per = 3;
    if (enemy.getBossType() == BossType::FALSE_MOON) {
        if (enemy.getHealth() * 3 <= enemy.getMaxHealth()) per = 5;
        else if (enemy.getHealth() * 3 <= enemy.getMaxHealth() * 2) per = 4;
    }
    // Whatever it borrowed from this round with Adrenaline and the rest is gone.
    const size_t owed = (size_t)std::max(0, std::min(per, knightMoveDebt));
    knightMoveDebt = 0;
    size_t count = std::min((size_t)per - owed, deckCards.size());
    for (size_t i = 0; i < count; ++i) knightPreparedMoves.push_back(deckCards[i]);
}

// Draws the prepared move the final boss answers your card with, as you
// commit the card. False when it has none, or cannot answer at all.
bool Game::drawKnightAnswer(Card& out) {
    if (knightPreparedMoves.empty()) return false;
    if (!enemy.isAlive() || playerHealth <= 0) return false;
    if (!mirrorBoss()) return false;
    if (enemyTauntTurns > 0) return false; // taunted bosses get one guaranteed strike instead, in bossAction()
    if (enemy.hasStun()) return false; // stunned enemies lose their whole turn, answers included

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> pick(0, (int)knightPreparedMoves.size() - 1);
    const int idx = pick(gen);
    out = knightPreparedMoves[idx];
    knightPreparedMoves.erase(knightPreparedMoves.begin() + idx);
    return true;
}

// A stance the true form actually holds, which it takes up before your card
// lands so the blow walks into it. The plain knight cannot hold one: its copy
// lunges, and answers after your card like everything else.
bool Game::knightTakesStance(const Card& c) const {
    return wholeMirror() && (c.getEffect() == CardEffect::COUNTER || c.getEffect() == CardEffect::PARRY);
}

// Plays the answer. One that comes after your card checks again: your card may
// have killed it, stunned it or taunted it in the meantime.
void Game::playKnightAnswer(const Card& c, bool beforeYourCard) {
    if (!enemy.isAlive() || playerHealth <= 0) return;
    if (!beforeYourCard && (enemyTauntTurns > 0 || enemy.hasStun())) return;
    UIHelper::typeWrite(std::string("\n") + Color::BOLD + Color::MAGENTA
        + "The " + mirrorName() + (beforeYourCard ? " moves before your card lands!" : " strikes back!")
        + Color::RESET + "\n");
    UIHelper::pause(200);
    executeShadowKnightMirror(c);
    refreshBattleAuras();
}

// Plays out one mirrored card's effect against the player.
void Game::executeShadowKnightMirror(const Card& mirrored) {
    // Weakness and strength both come in here: bossStrikesPlayer() only shows
    // them. Nothing gave the knight strength until the true form learned your
    // Strengthen, Berserk and Blood Pact, so this is what makes those count.
    double weakMult = enemy.getWeakMultiplier() * enemy.getStrengthMultiplier();
    int atk = (int)(std::max(0, enemy.getBaseAttack() + enemy.getBonusAttack()) * weakMult);
    int v = std::max(1, mirrored.getValue());
    UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + mirrorName() + " mirrors your "
        + mirrored.getName() + "!" + Color::RESET + "\n");
    UIHelper::pause(300);

    // Pact of Ruin: every move it makes now costs it blood, as every card costs you.
    if (enemyPactOfRuin && enemy.getHealth() > 1) {
        const int blood = std::min(enemy.getHealth() - 1, std::max(6, enemy.getMaxHealth() * 2 / 100));
        enemy.payHealth(blood);
        std::cout << "  " << Color::DAMAGE << "The pact takes " << blood << " of its health." << Color::RESET
                  << " (" << hpColor(enemy.getHealth(), enemy.getMaxHealth()) << enemy.getHealth() << "/"
                  << enemy.getMaxHealth() << Color::RESET << ")\n";
    }

    // The true form plays the whole card. Anything it handles is done here;
    // what the knight already knew runs through the code below as before.
    const bool handled = wholeMirror() && trueFormMirror(mirrored, atk, v);
    if (handled) {
        // nothing more: trueFormMirror played it
    } else if (mirrored.getType() == CardType::ATTACK) {
        bool pierce = (mirrored.getEffect() == CardEffect::PIERCE);
        if (mirrored.getEffect() == CardEffect::DOUBLE_HIT) {
            bossStrikesPlayer(atk / 2 + v / 2, false);
            if (playerHealth > 0) bossStrikesPlayer(atk / 2 + v / 2, false);
        } else {
            bossStrikesPlayer(atk + v / 2, pierce);
        }
        switch (mirrored.getEffect()) {
            case CardEffect::POISON: if (applyPlayerStatus(StatusType::POISON, 3))
                std::cout << "  " << Color::POISON_CLR << (mirrorBossIsMoon()
                    ? "It turns your own venom on you, inflicting Poison 3!"
                    : "The shadow's blade drips venom, inflicting Poison 3!") << Color::RESET << "\n"; break;
            case CardEffect::BURN:   if (applyPlayerStatus(StatusType::BURN, 2))
                std::cout << "  " << Color::BURN_CLR << (mirrorBossIsMoon()
                    ? "It turns your own fire on you, inflicting Burn 2!"
                    : "The shadow's blade sears, inflicting Burn 2!") << Color::RESET << "\n"; break;
            case CardEffect::WEAK:   if (applyPlayerStatus(StatusType::WEAK, 2))
                std::cout << "  " << Color::WEAK_CLR << "The blow saps your strength. You are Weakened 2!" << Color::RESET << "\n"; break;
            case CardEffect::STUN:   if (applyPlayerStatus(StatusType::STUN, 1))
                std::cout << "  " << Color::STUN_CLR << "The blow leaves you reeling! STUNNED!" << Color::RESET << "\n"; break;
            case CardEffect::STRENGTH:
                enemy.addBonusAttack(2);
                std::cout << "  " << Color::RED << mirrorThe() << " grows stronger! (+2 attack)" << Color::RESET << "\n"; break;
            default: break;
        }
    } else if (mirrored.getType() == CardType::DEFEND) {
        int armorGain = std::max(6, v);
        enemy.gainArmor(armorGain);
        std::cout << Color::MAGENTA << mirrorThe() << " raises your own guard against you!" << Color::RESET
                  << " +" << Color::ARMOR_CLR << armorGain << Color::RESET
                  << " armor (" << enemy.getArmor() << " total)\n";
        if (mirrored.getEffect() == CardEffect::CHIP) {
            UIHelper::pause(200);
            bossStrikesPlayer(v / 2 + 2, true);
        } else if (mirrored.getEffect() == CardEffect::IMPAIR) {
            if (applyPlayerStatus(StatusType::WEAK, 2))
                std::cout << "  " << Color::WEAK_CLR << "Its stance unsettles you. You are Weakened 2!" << Color::RESET << "\n";
        } else if (mirrored.getEffect() == CardEffect::WARD) {
            enemyStatusWardActive = true;
            std::cout << "  " << Color::CYAN << mirrorThe() << " readies its own ward against your next ailment!" << Color::RESET << "\n";
        }
        UIHelper::pause(250);
    } else { // SPECIAL
        switch (mirrored.getEffect()) {
            case CardEffect::HEAL: {
                if (enemyNoHeal) {
                    std::cout << Color::DIM << "Its wounds refuse to close. The mending does nothing." << Color::RESET << "\n";
                    UIHelper::pause(200);
                    break;
                }
                // Your heal card mirrored back, run through the same floor rule against
                // ITS pool, then halved - the boss pool dwarfs yours and a full-strength
                // mirror undid two whole turns of damage.
                int healAmt = std::max(1, Card::healAmount(v, enemy.getHealth(),
                                                           enemy.getMaxHealth()) / 2);
                enemy.heal(healAmt);
                std::cout << Color::MAGENTA << mirrorThe() << " knits itself back together, healing " << Color::HEAL
                          << healAmt << " HP!" << Color::RESET << " ("
                          << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                UIHelper::pause(250);
                break;
            }
            case CardEffect::POISON:
                Audio::playSFX("poison");
                if (applyPlayerStatus(StatusType::POISON, v))
                    std::cout << "  " << Color::POISON_CLR << (mirrorBossIsMoon() ? "Your own venom seeps in" : "Shadow venom seeps in")
                              << ", inflicting Poison " << v << "!" << Color::RESET << "\n";
                UIHelper::pause(300);
                break;
            case CardEffect::BURN:
                Audio::playSFX("fire");
                if (applyPlayerStatus(StatusType::BURN, v))
                    std::cout << "  " << Color::BURN_CLR << "Black flames catch, inflicting Burn " << v << "!" << Color::RESET << "\n";
                UIHelper::pause(300);
                break;
            case CardEffect::REND:
                if (applyPlayerStatus(StatusType::REND, v))
                    std::cout << "  " << Color::REND_CLR << mirrorThe() << " opens a wound that tears when you strike: Rend "
                              << v << "!" << Color::RESET << "\n";
                UIHelper::pause(300);
                break;
            case CardEffect::WEAK:
                if (applyPlayerStatus(StatusType::WEAK, v))
                    std::cout << "  " << Color::WEAK_CLR << "A creeping dread sets in. You are Weakened " << v << "!" << Color::RESET << "\n";
                UIHelper::pause(300);
                break;
            case CardEffect::STUN:
                Audio::playSFX("volt");
                if (applyPlayerStatus(StatusType::STUN, 1))
                    std::cout << "  " << Color::STUN_CLR << "Shadows bind you! STUNNED!" << Color::RESET << "\n";
                UIHelper::pause(300);
                break;
            default: // reactive cards (Dodge Reversal/Parry/Taunt)
                // The plain knight cannot hold a stance and lunges instead.
                // Its true form takes each one up for real.
                const CardEffect me = mirrored.getEffect();
                const bool stanceCard = me == CardEffect::COUNTER || me == CardEffect::PARRY
                                     || me == CardEffect::TAUNT;
                // Only a card that IS a stance becomes one. Everything unhandled
                // fell through to here, so the true form was answering a Blood
                // Pact with a reversal the player had never owned.
                if (stanceCard && wholeMirror()) {
                    if (me == CardEffect::PARRY) {
                        enemyParryStance = true;
                        std::cout << Color::MAGENTA << "It takes your own parry stance. Your next blow will be caught"
                                  << " and answered." << Color::RESET << "\n";
                    } else if (me == CardEffect::TAUNT) {
                        playerAttackOnly = true;
                        std::cout << Color::MAGENTA << "It taunts you with your own taunt. Next turn you may only"
                                  << " attack." << Color::RESET << "\n";
                    } else {
                        enemyReflectNext = true;
                        std::cout << Color::MAGENTA << "It sets your own reversal. Your next blow will be turned"
                                  << " back on you." << Color::RESET << "\n";
                    }
                    Audio::playSFXPitched("special", 0.7f);
                    UIHelper::pause(300);
                    break;
                }
                std::cout << Color::MAGENTA << "The mirrored stance dissolves, and "
                          << (mirrorBossIsMoon() ? "the False Moon" : "the shadow") << " lunges!" << Color::RESET << "\n";
                UIHelper::pause(200);
                bossStrikesPlayer(atk, false);
                break;
        }
    }

    // Under the pact, every attack it makes festers, the way yours do.
    if (enemyPactOfRuin && mirrored.getType() == CardType::ATTACK && playerHealth > 0) {
        if (applyPlayerStatus(StatusType::BURN, 3))
            std::cout << "  " << Color::BURN_CLR << "The wound festers: Burn 3." << Color::RESET << "\n";
        if (applyPlayerStatus(StatusType::REND, 2))
            std::cout << "  " << Color::REND_CLR << "The wound festers: Rend 2." << Color::RESET << "\n";
    }
}

// One more of its moves, straight away: a card it prepared if one is left, one
// of yours if not. Capped at two deep so a chain of them cannot run away.
void Game::knightExtraMove() {
    if (knightChainDepth >= 2 || !enemy.isAlive() || playerHealth <= 0) return;
    std::vector<Card> next;
    if (!knightPreparedMoves.empty()) {
        next.push_back(knightPreparedMoves.back());
        knightPreparedMoves.pop_back();
    } else {
        std::vector<Card> deck = playerDeck.getAllCardsOrdered();
        if (deck.empty()) return;
        static thread_local std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<> pick(0, (int)deck.size() - 1);
        next.push_back(deck[pick(gen)]);
    }
    ++knightChainDepth;
    UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "It moves again!" + Color::RESET + "\n");
    UIHelper::pause(150);
    executeShadowKnightMirror(next.front());
    --knightChainDepth;
}

// The cards the plain knight only half knew, played whole by the true form:
// what each one does and what it costs, the same bargain the card offers you.
// Returns false for anything the shared mirror already plays properly.
bool Game::trueFormMirror(const Card& mirrored, int atk, int v) {
    const CardEffect e = mirrored.getEffect();
    auto say = [](const std::string& colour, const std::string& text) {
        std::cout << "  " << colour << text << Color::RESET << "\n";
    };
    auto hp = [&]() {
        return std::string(" (") + hpColor(enemy.getHealth(), enemy.getMaxHealth())
             + std::to_string(enemy.getHealth()) + "/" + std::to_string(enemy.getMaxHealth())
             + Color::RESET + ")";
    };
    // A price paid in health never kills it: the card is a bargain, not a way out.
    auto pay = [&](int amount) {
        const int paid = std::max(0, std::min(enemy.getHealth() - 1, amount));
        enemy.payHealth(paid);
        return paid;
    };
    const int guard = std::max(6, v);

    if (mirrored.getType() == CardType::ATTACK) {
        switch (e) {
            case CardEffect::TRUESTRIKE:
                say(Color::MAGENTA, "It strikes clean through everything you put in its way.");
                bossStrikesPlayer(atk + v / 2, true, false, /*unstoppable*/true);
                return true;
            case CardEffect::TRUE_DOUBLE:
                say(Color::MAGENTA, "Twice, and nothing you raise can stop either.");
                bossStrikesPlayer(atk / 2 + v / 2, true, false, true);
                if (playerHealth > 0) bossStrikesPlayer(atk / 2 + v / 2, true, false, true);
                return true;
            case CardEffect::RECKLESS:
                bossStrikesPlayer(atk + v, false);
                enemy.applyStatus(StatusType::WEAK, 1);
                say(Color::WEAK_CLR, "It overswings. Its own blows come softer for a turn.");
                return true;
            case CardEffect::OVEREXTEND:
                bossStrikesPlayer(atk + v / 2, true);
                knightMoveDebt += 1;
                say(Color::WEAK_CLR, "It overreaches. One move fewer next round.");
                return true;
            case CardEffect::WILDCHARGE:
                bossStrikesPlayer(atk + v, false);
                enemy.resetArmor();
                enemyArmorHoldTurns = 0;
                say(Color::WEAK_CLR, "It charges in open. Its armor is gone.");
                return true;
            case CardEffect::EMBERBLADE:
                bossStrikesPlayer(atk + v / 2, false);
                if (playerHealth > 0 && applyPlayerStatus(StatusType::BURN, 4))
                    say(Color::BURN_CLR, "The blade sets you alight! Burn 4.");
                enemy.applyStatus(StatusType::BURN, 2);
                say(Color::BURN_CLR, "The flames lick back at it. Burn 2.");
                return true;
            case CardEffect::SHATTERPOINT:
                bossStrikesPlayer(atk + v, false);
                if (knightPreparedMoves.size() > 1)
                    knightPreparedMoves.erase(knightPreparedMoves.begin() + 1, knightPreparedMoves.end());
                say(Color::WEAK_CLR, "The blow costs it its footing. One more move at most this round.");
                return true;
            case CardEffect::ALLIN:
                bossStrikesPlayer(std::max(atk, enemy.getArmor() * 2), false);
                enemy.resetArmor();
                enemyArmorHoldTurns = 0;
                enemy.applyStatus(StatusType::WEAK, 3);
                say(Color::WEAK_CLR, "Everything it had, thrown. It is Weakened and unguarded.");
                return true;
            case CardEffect::PACTRUIN:
                bossStrikesPlayer(atk + v / 2, false);
                enemyPactOfRuin = true;
                enemyNoHeal = true;
                std::cout << "  " << Color::BOLD << Color::MAGENTA
                          << "It takes the pact. Its blows fester now, its wounds will not close, "
                          << "and every move costs it blood." << Color::RESET << "\n";
                return true;
            default:
                return false;
        }
    }

    if (mirrored.getType() == CardType::DEFEND) {
        auto guardUp = [&](int amount, const char* how) {
            enemy.gainArmor(amount);
            std::cout << Color::MAGENTA << how << Color::RESET << " +" << Color::ARMOR_CLR << amount
                      << Color::RESET << " armor (" << enemy.getArmor() << " total)\n";
        };
        switch (e) {
            case CardEffect::FORTIFY:
                guardUp(guard, "It fortifies behind your own guard!");
                enemyArmorHoldTurns = 3;
                say(Color::CYAN, "The armor will not fade for 3 turns.");
                break;
            case CardEffect::SCRAP:
                guardUp(guard, "It throws up your scrap shield!");
                pay(1);
                say(Color::DAMAGE, "The scrap edge nicks it for 1.");
                break;
            case CardEffect::SELFWEAK:
                guardUp(guard * 3 / 2, "It braces heavy behind your guard!");
                enemy.applyStatus(StatusType::WEAK, 1);
                say(Color::WEAK_CLR, "Its own blows soften for a turn.");
                break;
            case CardEffect::TURTLE:
                guardUp(guard, "It digs in behind your guard!");
                enemyArmorHoldTurns = 3;
                enemy.applyStatus(StatusType::WEAK, 3);
                say(Color::CYAN, "The armor holds for 3 turns, and it is Weakened while it does.");
                break;
            case CardEffect::UNSTABLEWARD:
                guardUp(guard, "It works your unstable ward!");
                enemyStatusWardActive = true;
                knightMoveDebt += 2;
                say(Color::CYAN, "Your next ailment will not take, and the working costs it two moves next round.");
                break;
            case CardEffect::LASTSTAND: {
                // Once a fight, as it is for you.
                if (knightLastStandSpent) {
                    say(Color::DIM, "It has already made its last stand this fight.");
                    break;
                }
                knightLastStandSpent = true;
                // Its wounds as a share of its health, turned into the same
                // share of yours, so the card is the same size in its hands.
                const int missing = enemy.getMaxHealth() - enemy.getHealth();
                const int fromWounds = missing * std::max(1, maxPlayerHealth)
                                     / std::max(1, enemy.getMaxHealth()) + v;
                guardUp(fromWounds, "Its wounds harden into armor!");
                enemyArmorHoldTurns = 3;
                enemyNoHeal = true;
                say(Color::WEAK_CLR, "It holds for 3 turns, and it cannot heal for the rest of the fight.");
                say(Color::DIM, "It will not have that card again this fight.");
                break;
            }
            default:
                return false;
        }
        UIHelper::pause(250);
        return true;
    }

    // SPECIAL
    switch (e) {
        case CardEffect::STRENGTH: {
            const double buff = mirrored.strengthMultiplier();
            enemy.applyStatus(StatusType::STRENGTH, 2, 1.5, buff);
            std::ostringstream o;
            o << "It takes your strength: x" << buff << " damage for 2 turns.";
            say(Color::STRENGTH_CLR, o.str());
            break;
        }
        case CardEffect::FEAR:
            if (provokeFizzles(0)) {
                say(Color::DIM, "Its stare slides off you. The fear does not take.");
                break;
            }
            nextHandPenalty = std::max(nextHandPenalty, 1);
            say(Color::WEAK_CLR, "Its stare gets into you. One fewer card next turn.");
            break;
        case CardEffect::BLOODPRICE: {
            const int paid = pay(std::max(6, enemy.getMaxHealth() * 2 / 100));
            std::cout << "  " << Color::DAMAGE << "It pays " << paid << " of its health to move again."
                      << Color::RESET << hp() << "\n";
            knightExtraMove();
            break;
        }
        case CardEffect::BERSERK:
            enemy.applyStatus(StatusType::STRENGTH, 2, 1.5, 1.5);
            enemyVulnerableTurns = 1;
            say(Color::STRENGTH_CLR, "It throws its guard away and winds up: x1.5 damage, and it takes x1.5 from you until its turn.");
            break;
        case CardEffect::ADRENALINE:
            knightMoveDebt += 1;
            say(Color::WEAK_CLR, "It borrows a move from next round.");
            knightExtraMove();
            break;
        case CardEffect::BLOODPACT: {
            const int paid = pay(enemy.getMaxHealth() * 15 / 100);
            enemy.applyStatus(StatusType::STRENGTH, 3, 1.5, 2.0);
            std::cout << "  " << Color::STRENGTH_CLR << "x2 damage for 3 turns" << Color::RESET
                      << Color::DAMAGE << ", paid with " << paid << " of its health." << Color::RESET << hp() << "\n";
            break;
        }
        case CardEffect::BORROWED: {
            const int paid = pay(enemy.getMaxHealth() * 15 / 100);
            knightMoveDebt += 3;
            std::cout << "  " << Color::BOLD << Color::CYAN << "Time folds for it. It moves twice more, now."
                      << Color::RESET << Color::DAMAGE << " It loses its next round, and " << paid
                      << " of its health." << Color::RESET << hp() << "\n";
            knightExtraMove();
            knightExtraMove();
            break;
        }
        case CardEffect::SACRIFICE: {
            if (knightSacrificeSpent || enemyNoHeal) {
                say(Color::DIM, "There is nothing left for it to give up.");
                break;
            }
            knightSacrificeSpent = true;
            const int before = enemy.getHealth();
            enemy.heal(enemy.getMaxHealth() * 30 / 100);
            const int mend = enemy.getHealth() - before;
            std::cout << "  " << Color::HEAL << "It gives up your Sacrifice and mends " << mend << " health."
                      << Color::RESET << hp() << Color::DIM << " It will not have that card again this fight."
                      << Color::RESET << "\n";
            break;
        }
        case CardEffect::RAISE: {
            // Your own Raise Undead, played whole: the undead you had last rises
            // for it, with what it would have risen with for you, and stands in
            // front of it like the Lich's, taking your blows and striking at you.
            const RaisedDead* d = raisedDead(lastUndead);
            if (!d) {
                say(Color::MAGENTA, "None of your dead answer it, so it strikes you itself.");
                bossStrikesPlayer(atk / 2 + v, false);
                break;
            }
            const bool again = lichAddAlive && lichAddName == d->name;
            // It pays the card's price too, scaled to its health like Blood Price.
            const int paid = pay(std::max(RAISE_PRICE, enemy.getMaxHealth() * 7 / 100));
            lichAddName  = d->name;
            lichAddMaxHp = raisedBody(*d, maxPlayerHealth);
            lichAddHp    = lichAddMaxHp;
            lichAddAtk   = raisedStrikeFor(lichAddName, v);
            lichAddAlive = true;
            EnemyArt::setCompanion(lichAddName);
            Audio::playSFXPitched("special", 0.85f);
            say(Color::MAGENTA, again ? "The " + lichAddName + " at its side pulls itself back together."
                                      : "It raises your " + lichAddName + " to fight at its side!");
            std::cout << "  " << Color::DAMAGE << "It pays " << paid << " of its health for it."
                      << Color::RESET << hp() << "\n";
            std::cout << "  (" << lichAddName << " HP: " << lichAddHp << "/" << lichAddMaxHp
                      << ", strikes for " << lichAddAtk << ")\n";
            break;
        }
        case CardEffect::TURNABOUT:
            // Your Turnabout, turned on you: your armor's resistances and its
            // weakness swap places for the rest of the fight.
            if (armorReversed) {
                say(Color::DIM, "Your armor is already turned against you.");
                break;
            }
            armorReversed = true;
            say(Color::MAGENTA, "It turns your armor inside out: what it resisted it now fears, and what it "
                                "feared it resists, for the rest of the fight.");
            break;
        case CardEffect::FEINT: {
            // Your Feint, turned on you: its blows come the way your armor is weakest.
            DamageType to = DamageType::NONE;
            for (DamageType t : { DamageType::SMASH, DamageType::PIERCE, DamageType::FIRE,
                                  DamageType::POISON, DamageType::WIND })
                if (armourTypeMod(t) > 0) { to = t; break; }
            if (to == DamageType::NONE || enemyAttackType() == to) {
                say(Color::DIM, "Its blows already find your armor's weak spot.");
                break;
            }
            enemy.setBlowType(to);
            say(Color::MAGENTA, std::string("It feints, and its blows come as ") + typeWord(to)
                                + " now, which your armor is weak to (+25%), for the rest of the fight.");
            break;
        }
        case CardEffect::UNLEASH: {
            // Your payoff, turned on whatever of yours it still has burning.
            const DamageType el = mirrored.getElemType();
            const bool rend = el == DamageType::WIND;
            const StatusType st = el == DamageType::FIRE ? StatusType::BURN
                                : rend ? StatusType::REND : StatusType::POISON;
            const char* word = el == DamageType::FIRE ? "Burn" : rend ? "Rend" : "Poison";
            if (knightUnleashSpent & unleashBit(el)) {
                say(Color::DIM, "It has already set that off this fight.");
                break;
            }
            int total = playerStatus.pending(st);
            if (total <= 0) {
                say(Color::DIM, std::string("There is no ") + word + " on you for it to set off.");
                break;
            }
            playerStatus.clear(st);
            total = total * 3 / 2 * (100 + armourTypeMod(el)) / 100;
            playerHealth = std::max(0, playerHealth - total);
            const bool held = trySecondWind();
            EnemyArt::printBattleKnightHit(enemy.getType(), enemy.getBossType());
            EnemyArt::popNumber(total, false, EnemyArt::PopKind::DAMAGE);
            say(Color::DAMAGE, std::string("It sets off your ") + word + ": every " + (rend ? "charge" : "tick")
                               + " of it at once, " + std::to_string(total) + " damage.");
            if (held) say(std::string(Color::BOLD) + Color::YELLOW, savedLine("You refuse to fall, and cling on at 1 HP."));
            // And it pays what you would: the same on itself, once a fight.
            knightUnleashSpent |= unleashBit(el);
            enemy.applyStatus(st, UNLEASH_PRICE);
            say(Color::DIM, std::string("It takes ") + word + " " + std::to_string(UNLEASH_PRICE)
                            + " for it, and will not have that card again this fight.");
            break;
        }
        default:
            return false;
    }
    UIHelper::pause(250);
    return true;
}

bool Game::mirrorBoss() const {
    return enemy.isBoss() && (enemy.getBossType() == BossType::SHADOW_KNIGHT
                              || enemy.getBossType() == BossType::FALSE_MOON);
}

bool Game::wholeMirror() const { return trueFormPhase || enemy.getBossType() == BossType::FALSE_MOON; }

std::string Game::mirrorName() const {
    return enemy.getBossType() == BossType::FALSE_MOON ? "False Moon" : "Shadow Knight";
}

std::string Game::mirrorThe() const {
    return enemy.getBossType() == BossType::FALSE_MOON ? "The False Moon" : "The shadow";
}
