// The enemy's side of a fight: how a regular enemy, a boss and the true
// form take their turns, their blows on the knight, and the saves that
// can catch a killing one.
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

// The projectile this enemy throws, looked up by name in the generated
// table so the art and its index come from one place. No entry falls back
// to the archetype.
static const ProjectileTable::Entry* projEntryFor(const std::string& n) {
    for (int i = 0; i < ProjectileTable::kByNameCount; i++)
        if (n.find(ProjectileTable::kByName[i].enemy) != std::string::npos)
            return &ProjectileTable::kByName[i];
    return nullptr;
}

// -1 means "fall back to the value derived from the sprite sheet".
int Game::enemyMuzzleX() const {
    const ProjectileTable::Entry* e = projEntryFor(enemy.getName());
    return e ? e->muzzleX : -1;
}

int Game::enemyMuzzleY() const {
    const ProjectileTable::Entry* e = projEntryFor(enemy.getName());
    return e ? e->muzzleY : -1;
}

int Game::enemyProjectile() const {
    const ProjectileTable::Entry* e = projEntryFor(enemy.getName());
    if (e) return e->frame;
    switch (enemy.getType()) {
        case EnemyType::RANGED: return ProjectileTable::GEN_RANGED;
        case EnemyType::CASTER: return ProjectileTable::GEN_CASTER;
        case EnemyType::BEAST:  return ProjectileTable::GEN_BEAST;
        case EnemyType::UNDEAD: return ProjectileTable::GEN_UNDEAD;
        default:                return ProjectileTable::GEN_MELEE;
    }
}

// The eye has one way of reaching you. Without this it fired its beam only on
// its own move and threw a little bolt across the sky on every shared ranged
// attack it rolled.
bool Game::enemyFiresBeam() const {
    return enemy.getName().find("Omneye") != std::string::npos;
}

bool Game::enemyRaisesFlames() const {
    return enemy.getName().find("Archon") != std::string::npos;
}

// The Wyvern and the Falcon dive in and pull away: they throw nothing, and
// Parry closes on empty air.
bool Game::enemyIsFlyer() const {
    const std::string n = enemy.getName();
    return n.find("Wyvern") != std::string::npos || n.find("Falcon") != std::string::npos;
}

// Archers shoot and casters cast; neither walks into sword range to do it.
bool Game::archetypeIsRanged() const {
    // The False Moon is a caster, but its blows are its claws: it comes in.
    if (enemy.getBossType() == BossType::FALSE_MOON) return false;
    return enemy.getType() == EnemyType::RANGED || enemy.getType() == EnemyType::CASTER;
}

// One regular-enemy attack against the player: armour, Dodge Reversal and
// Parry, then damage. weakMult is read once per enemy turn, before Weak
// ticks. Bosses go through bossStrikesPlayer().
void Game::enemyStrikePlayer(int atk, bool pierceHalfArmor, double weakMult, bool ranged,
                             bool useAttackFrames, int projectile, bool closeIn,
                             bool fromCompanion, bool ignoreArmor) {
    // Weak scales it down, Strength scales it up - the mirror of what the
    // player's own two buffs do to their attacks.
    atk = (int)(atk * weakMult * enemy.getStrengthMultiplier());
    // What reaches whatever stands in front of you (Raise Undead), before the
    // things only you are weak to.
    const int blow = atk;
    // Berserk Stance: you gave up your guard for the swing, and this is the bill.
    if (vulnerableTurns > 0) atk = (int)(atk * vulnerableMult);
    // What your armour makes of this kind of blow, and the Glass Moon's price.
    const DamageType blowType = enemyAttackType();
    const int typeMod = armourTypeMod(blowType);
    atk = atk * (100 + typeMod) / 100;
    if (hasRelic(Relic::GLASS_MOON)) atk = atk * 125 / 100;
    if (tickEnemyRend()) return;   // the tear finished it before the blow landed
    // A blow your raised dead will take is drawn ending on them. Only a stance
    // you hold catches it first.
    EnemyArt::setBlowsAtAlly(raisedAlive && !counterAttackActive && !parryActive);
    struct AimBack { ~AimBack() { EnemyArt::setBlowsAtAlly(false); } } aimBack;
    if (fromCompanion)
        EnemyArt::printCompanionAttack(enemy.getType(), enemy.getBossType());
    else if (enemyRaisesFlames())
        EnemyArt::printGroundFlames(enemy.getType(), enemy.getBossType(),
                                    ProjectileTable::FX_PILLAR);
    else
        EnemyArt::printBattleAttack(enemy.getType(), enemy.getBossType(), playerArmor > 0, ranged,
                                    useAttackFrames, projectile,
                                    enemyMuzzleX(), enemyMuzzleY(), closeIn);
    // Dodge Reversal fires before Parry when both are active (uncapped, higher priority)
    if (counterAttackActive) {
        counterAttackActive = false;
        if (counterWasLegendary) Audio::playSFX("legendary");
        int counterDmg = (int)((atk * 2 + counterBonusValue) * playerStatus.getStrengthMultiplier());
        int hpBefore = enemy.getHealth();
        enemy.takeDamage(counterDmg);
        int hpLost = hpBefore - enemy.getHealth();
        EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), DamageType::NONE, hpLost > 0);
        EnemyArt::popNumber(hpLost, true, EnemyArt::PopKind::DAMAGE);
        Audio::playSFX(!enemy.isAlive() ? deathSfx(enemy.isBoss()) : "attack");
        earn(Achievements::SIDESTEP);
        std::cout << Color::GREEN << "Dodge Reversal! You sidestep the attack and counter for " << hpLost << " damage!" << Color::RESET
                  << " (Enemy HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
        UIHelper::pause(200);
        return;
    }
    if (parryActive) {
        int parryCap = playerArmor + parryBonusValue * 3; // current armor + Parry's own bonus - stack armor first to parry bigger hits
        parryActive = false;
        if (atk <= parryCap) {
            // The same flag the sprite uses. A blow that never closed the distance -
            // an arrow, a spell, a thrown dagger - can be caught, but there is
            // nothing standing in front of you to hit back at.
            bool tooFarToRiposte = !enemy.isBoss() && ranged;
            if (tooFarToRiposte) {
                Audio::playSFXPitched("defend", 1.35f);   // the block rings off the blade
                if (enemyIsFlyer()) {
                    earn(Achievements::FLEW_AWAY);
                    std::cout << Color::CYAN << "Parried! But it wheels away before you can riposte."
                              << Color::RESET << "\n";
                } else
                    std::cout << Color::CYAN << "Parry! You block the shot. No damage taken, but they're too far away to riposte."
                              << Color::RESET << "\n";
                UIHelper::pause(300);
                return;
            }
            Audio::playSFXPitched("defend", 1.35f);   // the block rings off the blade
            int riposteDmg = (int)((atk * 1.5 + parryBonusValue) * playerStatus.getStrengthMultiplier());
            int hpBefore = enemy.getHealth();
            enemy.takeDamage(riposteDmg); // ignores defense - takeDamage only accounts for armor
            int hpLost = hpBefore - enemy.getHealth();
            // Before the animation, so the cue lands with the blow.
            Audio::playSFX(hpLost > 0 ? "attack" : "special");
            EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), DamageType::NONE, hpLost > 0);
            EnemyArt::popNumber(hpLost, true, EnemyArt::PopKind::DAMAGE);
            bool stunned = tryStunEnemy();
            if (stunned && enemy.isAlive())
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::STUN, true);
            if (!enemy.isAlive()) Audio::playSFX(deathSfx(enemy.isBoss()));
            std::cout << Color::CYAN << "Parry! You deflect the blow. No damage taken. Riposte for " << hpLost
                      << " damage!" << (stunned ? " The enemy is stunned!" : " The enemy resists the stun!") << Color::RESET
                      << " (Enemy HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
            earn(Achievements::PARRY);
            parryLanded = true;
            UIHelper::pause(300);
            return;
        } else {
            std::cout << Color::BOLD << Color::RED << "The blow is too powerful to parry! Your guard breaks!" << Color::RESET << "\n";
            UIHelper::pause(250);
        }
    }
    // Raise Undead: the dead stand in front of you and take the blow whole.
    if (raisedAlive) { raisedTakes(blow); return; }
    int effectiveArmor = ignoreArmor ? 0 : pierceHalfArmor ? (playerArmor / 2) : playerArmor;
    int actualDamage = atk - effectiveArmor;
    if (actualDamage < 0) actualDamage = 0;
    if (!ignoreArmor) playerArmor -= (pierceHalfArmor ? atk / 2 : atk);
    if (playerArmor < 0) playerArmor = 0;
    playerHealth -= actualDamage;
    if (playerHealth < 0) playerHealth = 0;
    const bool savedByThread = trySecondWind();
    if (actualDamage > 0) {
        EnemyArt::printBattleKnightHit(enemy.getType(), enemy.getBossType());
        EnemyArt::popNumber(actualDamage, false, EnemyArt::PopKind::DAMAGE);
    }
    Audio::playSFX("hit");
    std::cout << Color::DAMAGE << "The enemy attacks for " << actualDamage << " damage!" << Color::RESET;
    if (weakMult < 1.0)
        std::cout << " " << Color::WEAK_CLR << "[Weakened]" << Color::RESET;
    if (ignoreArmor) std::cout << " " << Color::MAGENTA << "[Ignores armor]" << Color::RESET;
    if (typeMod < 0) std::cout << " " << Color::GREEN << "[Resisted " << typeWord(blowType) << "]" << Color::RESET;
    if (typeMod > 0) std::cout << " " << Color::RED << "[Weak to " << typeWord(blowType) << "]" << Color::RESET;
    std::cout << "  HP: " << hpColor(playerHealth, maxPlayerHealth)
              << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
    if (savedByThread)
        std::cout << "  " << Color::BOLD << Color::YELLOW
                  << savedLine("You refuse to fall, and cling on at 1 HP.") << Color::RESET << "\n";
    // The Sexton: every blow of his that reaches you throws earth into your pack.
    if (!fromCompanion && playerHealth > 0 && enemyIs("Sexton")) shovelGraveDirt();
    UIHelper::pause(200);
}

// Assassin only: after the player commits to a card, one random play this turn
// triggers a single free strike from the shadows. Armed once per player turn.
void Game::triggerAssassinAmbush() {
    if (!assassinAmbushArmed) return;
    if (!enemy.isAlive() || playerHealth <= 0) return;
    // ~45% per card played, so it usually lands once a turn without hitting every card.
    std::random_device rd; std::mt19937 gen(rd());
    if (std::uniform_int_distribution<>(0, 99)(gen) >= 45) return;
    assassinAmbushArmed = false;
    UIHelper::typeWrite(std::string("\n") + Color::BOLD + Color::RED + "The Assassin strikes from the shadows!" + Color::RESET + "\n");
    UIHelper::pause(200);
    // Thrown from the dark, like the Bandit's dagger: it never closes, so Parry
    // catches it without a riposte and the sprite holds its ground.
    enemyStrikePlayer(enemy.getBaseAttack() + enemy.getBonusAttack(), false,
                      enemy.getWeakMultiplier(), /*ranged*/true, /*useFrames*/true,
                      enemyProjectile());
    refreshBattleAuras();
}

// Re-arms per-turn enemy mechanics at the start of each player turn.
void Game::armPerTurnEnemyMechanics() {
    assassinAmbushArmed = enemy.isAlive() && enemy.getName().find("Assassin") != std::string::npos;
}

void Game::enemyTurn() {
    if (!enemy.isAlive()) return;

    refreshBattleAuras();

    // Armour from the enemy's own last turn goes now, so a Defend this turn
    // survives the player's whole next turn. The true form's holding guards
    // outlast it, the way yours do.
    if (enemyArmorHoldTurns > 0) enemyArmorHoldTurns--;
    else enemy.resetArmor();
    glassUp = false;   // the Glass Templar's glass went with it, and goes back on below
    // Berserk's opening lasts until its turn comes round.
    enemyVulnerableTurns = 0;

    // Tick enemy status effects at the start of their turn. Poison/Burn are
    // elemental (Poison/Fire respectively), so they get the same weakness (+50%)
    // and resistance (-50%) treatment as a matching-tagged attack card would.
    int poisonDmg = enemy.processPoison();
    if (poisonDmg > 0) {
        bool poisonWeak   = enemy.getWeakness()   == DamageType::POISON;
        bool poisonResist = enemy.getResistance() == DamageType::POISON;
        if (poisonWeak)   poisonDmg = (int)(poisonDmg * 1.5);
        if (poisonResist) poisonDmg = (int)(poisonDmg * 0.5);
        enemy.takeDamageOverTime(poisonDmg, false);
        std::cout << Color::POISON_CLR << "Poison:" << Color::RESET
                  << " the enemy takes " << Color::PLAYER_ATTACK << poisonDmg << Color::RESET << " damage! ("
                  << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << " HP)";
        if (poisonWeak)   std::cout << " " << Color::YELLOW << "[Weakness! x1.5]" << Color::RESET;
        if (poisonResist) std::cout << " " << Color::DIM << "[Resisted x0.5]" << Color::RESET;
        std::cout << "\n";
        UIHelper::pause(250);
        if (!enemy.isAlive()) {
            Audio::playSFX(deathSfx(enemy.isBoss()));
            if (saintRises()) return;   // it spends the turn getting up
            earn(Achievements::SLOW_BURN);
            return;
        }
    }
    int burnDmg = enemy.processBurn();
    if (burnDmg > 0) {
        bool burnWeak   = enemy.getWeakness()   == DamageType::FIRE;
        bool burnResist = enemy.getResistance() == DamageType::FIRE;
        if (burnWeak)   burnDmg = (int)(burnDmg * 1.5);
        if (burnResist) burnDmg = (int)(burnDmg * 0.5);
        enemy.takeDamageOverTime(burnDmg, false);
        std::cout << Color::BURN_CLR << "Burn:" << Color::RESET
                  << " the enemy takes " << Color::PLAYER_ATTACK << burnDmg << Color::RESET << " damage! ("
                  << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << " HP)";
        if (burnWeak)   std::cout << " " << Color::YELLOW << "[Weakness! x1.5]" << Color::RESET;
        if (burnResist) std::cout << " " << Color::DIM << "[Resisted x0.5]" << Color::RESET;
        std::cout << "\n";
        UIHelper::pause(250);
        // A burn on the Hydra sears its open stumps, as a Fire hit does.
        if (enemy.isAlive()) searStumps("burn");
        if (!enemy.isAlive()) {
            fireKill = true;
            Audio::playSFX(deathSfx(enemy.isBoss()));
            if (saintRises()) return;
            earn(Achievements::SLOW_BURN);
            return;
        }
    }
    if (enemy.processStun()) {
        UIHelper::typeWrite(std::string(Color::STUN_CLR) + "The enemy is STUNNED and loses its turn!" + Color::RESET + "\n");
        churchTurnLost();
        UIHelper::pause(400);
        return;
    }

    // Bosses have their own AI
    if (enemy.isBoss()) {
        // The true form can raise your own dead to stand in front of it. One it
        // raised on this turn waits for the next, as the Lich's does.
        const bool addActs = lichAddAlive;
        bossAction();
        if (addActs && lichAddAlive && playerHealth > 0 && enemy.isAlive()) addStrikes();
        return;
    }

    // The Unchosen's copy of your Strength wears off one of its own turns at
    // a time, as yours wears off on yours. Nothing else ticks a regular
    // enemy's Strength but the Moonstruck, which does its own.
    struct TickCopiedStrength {
        Enemy& e; bool on;
        ~TickCopiedStrength() { if (on && e.isAlive() && e.hasStrength()) e.processStrength(); }
    } tickCopiedStrength{ enemy, enemyIs("Unchosen") };

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> rollDist(0, 99);
    // Scholar's Lens drew this turn's rolls at the start of yours, and showed
    // what they pick. Use those, so what was shown is what happens.
    int roll = lensRoll >= 0 ? lensRoll : rollDist(gen);
    // One reroll when the roll lands in the same tenth as last turn's. Not a
    // ban on repeating - an enemy that has your number should be able to press
    // it - but three Howls in a row is not a fight, it is a wall.
    if (lensRoll < 0 && lastMoveRoll >= 0 && roll / 10 == lastMoveRoll / 10) roll = rollDist(gen);
    lastMoveRoll = roll;
    lensRoll = -1;

    // How often a feared enemy braces instead of taking its turn. Deliberately not
    // 100: a two-turn guaranteed skip would be far stronger than Taunt, which only
    // changes WHICH action happens, never whether one happens at all.
    const int FEAR_BRACE_CHANCE = 60;

    // Taunt: force this turn's roll into whichever bucket guarantees an Attack
    // action for this enemy type, rather than leaving it to chance.
    bool taunted = enemyTauntTurns > 0;
    if (taunted) {
        enemyTauntTurns--;
        switch (enemy.getType()) {
            case EnemyType::MELEE:  roll = 0;  break;
            case EnemyType::RANGED: roll = 0;  break;
            case EnemyType::TANK:   roll = 99; break;
            case EnemyType::CASTER: roll = 99; break;
            case EnemyType::BEAST:  roll = 0;  break;
            case EnemyType::UNDEAD: roll = 0;  break;
            default: break;
        }
    }

    // Apply WEAK penalty to attack, then tick it
    double weakMult = enemy.getWeakMultiplier();
    enemy.processWeak();

    bool volleyBroken = false;
    // doAttack takes its range from the archetype (RANGED and CASTER never
    // close); a melee enemy with a thrown move calls doAttackAt directly. A
    // lambda cannot default an argument that touches `this`.
    auto doAttackAt = [&](int atk, bool pierceHalfArmor, bool ranged, bool useFrames = true,
                          int projectile = -1, bool closeIn = false, bool ignoreArmor = false) {
        if (enemy.hasStun()) {
            if (!volleyBroken) {
                volleyBroken = true;
                std::cout << "  " << Color::CYAN
                          << "The stun lands mid-swing. The rest of the assault never comes."
                          << Color::RESET << "\n";
            }
            return;
        }
        if (enemyFiresBeam() && ranged && !closeIn) {
            // Drawn first and joined to the pupil, then the blow lands with
            // nothing else crossing the gap. At your raised dead, if they take it.
            EnemyArt::setBlowsAtAlly(raisedAlive && !counterAttackActive && !parryActive);
            EnemyArt::printEnemyBeam(enemy.getType(), enemy.getBossType(), ProjectileTable::FX_BEAM,
                                     enemyMuzzleX(), enemyMuzzleY());
            enemyStrikePlayer(atk, pierceHalfArmor, weakMult, ranged, /*useFrames*/false,
                              EnemyArt::Proj::NONE, closeIn, false, ignoreArmor);
            return;
        }
        enemyStrikePlayer(atk, pierceHalfArmor, weakMult, ranged, useFrames, projectile, closeIn,
                          false, ignoreArmor);
    };
    auto doAttack = [&](int atk, bool pierceHalfArmor) {
        // enemyProjectile(), not the default -1, which the art layer reads as
        // "nothing crosses the gap".
        doAttackAt(atk, pierceHalfArmor, archetypeIsRanged(), true, enemyProjectile());
    };

    auto doDefend = [&](int amt) {
        enemy.gainArmor(amt);
        // The player's own defend cue, pitched down: same action, other side.
        Audio::playSFXPitched("defend", 0.8f);
        bool fizzled = counterAttackActive || parryActive;
        counterAttackActive = false;
        parryActive = false;
        if (fizzled)
            std::cout << Color::ARMOR_CLR << "The enemy braces, +" << amt << " armor." << Color::RESET
                      << " " << Color::DIM << "(No attack. Your stance fizzles.)" << Color::RESET << "\n";
        else
            std::cout << Color::ARMOR_CLR << "The enemy braces, +" << amt << " armor (absorbs incoming damage)." << Color::RESET << "\n";
        UIHelper::pause(150);
    };

    EnemyType t = enemy.getType();
    // bonusAttack MUST be included: every +2 a regular enemy earns (a roar, a
    // scream, a frenzy) is spent here.
    int atk = enemy.getBaseAttack() + enemy.getBonusAttack();
    int def = enemy.getBaseDefense();

    // How often this enemy reaches for its own moves. The rest of the time it
    // runs its archetype template. Rolled separately from `roll`, which the few
    // enemies with two signature moves use to pick between them.
    const int SIGNATURE_CHANCE = ownMoveChance();
    const int sigRoll = lensSigRoll >= 0 ? lensSigRoll : rollDist(gen);
    lensSigRoll = -1;

    // Every move that inflicts something shows it crossing the field. proj
    // picks the art; -1 keeps the generic status orb.
    auto cast = [&](EnemyArt::CastGlow g, int proj = -1) {
        if (enemyFiresBeam()) {
            EnemyArt::printEnemyBeam(enemy.getType(), enemy.getBossType(), ProjectileTable::FX_BEAM,
                                     enemyMuzzleX(), enemyMuzzleY(),
                                     /*weakGlow*/g == EnemyArt::CastGlow::WEAK);
            return;
        }
        if (enemyRaisesFlames()) {
            EnemyArt::printGroundFlames(enemy.getType(), enemy.getBossType(),
                                        ProjectileTable::FX_PILLAR);
            return;
        }
        EnemyArt::printEnemyCast(enemy.getType(), enemy.getBossType(), g, proj, enemyMuzzleX(), enemyMuzzleY());
    };
    // For a status that RIDES an attack. The blow already crossed the field, so
    // firing a second bolt after it would read as two separate attacks; this just
    // washes the knight in the ailment colour.
    auto flash = [&](EnemyArt::CastGlow g) {
        EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), g, false);
    };
    // A status delivered by CONTACT rather than at range: the enemy closes the
    // gap the way an attack does, then the ailment lands. A chilling touch that
    // reached across the room without either sprite moving was the odd one out.
    auto touch = [&](EnemyArt::CastGlow g) {
        EnemyArt::printBattleAttack(enemy.getType(), enemy.getBossType(), playerArmor > 0,
                                    /*ranged*/false);
        EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), g, false);
    };
    // A dive: crosses the field like a brawler, throws nothing, and is away
    // again before Parry can answer it.
    auto dive = [&](int a) {
        doAttackAt(a, true, /*ranged*/true, /*useFrames*/true, EnemyArt::Proj::NONE, /*closeIn*/true);
    };
    auto themedGeneric = [&](const char* msg) {
        UIHelper::typeWrite(std::string(Color::MAGENTA) + msg + Color::RESET + "\n");
        UIHelper::pause(150);
    };
    auto applyEnemyStatusOnPlayerPoison = [&](int amt) {
        EnemyArt::printEnemyCast(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::POISON,
                                 enemyProjectile(), enemyMuzzleX(), enemyMuzzleY());
        Audio::playSFX("poison");
        if (applyPlayerStatus(StatusType::POISON, amt))
            std::cout << Color::POISON_CLR << "Grave rot sets in. Poison " << amt << "."
                      << Color::RESET << "\n";
        UIHelper::pause(150);
    };

    // Fear resolves here, once, as a flat chance to brace instead of acting:
    // unlike Taunt there is no defend bucket to force, and several enemies
    // have no defensive move at all.
    if (enemyFearTurns > 0 && enemyCanDefend()) {
        enemyFearTurns--;
        if (rollDist(gen) < FEAR_BRACE_CHANCE) {
            UIHelper::typeWrite(std::string(Color::CYAN)
                + "The enemy flinches back and throws up its guard." + Color::RESET + "\n");
            earn(Achievements::COLD_FEET);
            doDefend(def);
            churchTurnLost();
            return;
        }
    }

    // --- Archetype kits --------------------------------------------------
    // Three kinds of turn per archetype. MELEE and RANGED have no brace on
    // purpose: they are what Fear should fail against (see enemyCanDefend).
    auto archetypeTurn = [&](int r) {
        r = kitRoll(r);   // early on, no Weaken bucket
        switch (enemy.getType()) {
            case EnemyType::MELEE:
                if (r < 60) { themedGeneric("It swings at you."); doAttack(atk, false); }
                else if (r < 85 && enemy.getBonusAttack() < 6) {
                    enemy.addBonusAttack(2);
                    std::cout << Color::RED << "It winds up, gaining +2 attack." << Color::RESET << "\n";
                    UIHelper::pause(150);
                } else { themedGeneric("It throws its weight into a heavy swing!"); doAttack(atk + 3, false); }
                break;
            case EnemyType::TANK:
                if (r < 45) doDefend(def + 2);
                else if (r < 85) { themedGeneric("It brings its weapon down hard!"); doAttack(atk + 2, false); }
                else {
                    // gainArmor, NOT doDefend: doDefend means "brace instead of attacking"
                    // and fizzles a pending Parry or Dodge Reversal, but this move braces and
                    // then swings.
                    enemy.gainArmor(def);
                    std::cout << Color::ARMOR_CLR << "It raises its guard (+" << def << " armor)."
                              << Color::RESET << "\n";
                    UIHelper::pause(150);
                    themedGeneric("It shoves forward behind its guard.");
                    doAttack(std::max(1, atk / 2), false);
                }
                break;
            case EnemyType::RANGED: {
                // A flyer has no bow. It is RANGED so that Parry closes on empty
                // air as it climbs away, but a shot and a double shot were the
                // wrong moves entirely: it dives, rakes and beats its wings.
                if (enemyIsFlyer()) {
                    if (r < 60) { themedGeneric("It folds its wings and dives at you!"); dive(atk); }
                    else if (r < 85) {
                        // The gust itself crosses the field: wings, not a bolt.
                        cast(EnemyArt::CastGlow::WEAK, ProjectileTable::FX_WIND);
                        Audio::playSFXPitched("special", 0.85f);
                        std::cout << Color::WEAK_CLR << "Its wings beat the air into your face." << Color::RESET << "\n";
                        if (applyPlayerStatus(StatusType::WEAK, 2))
                            std::cout << "  You are " << Color::WEAK_CLR << "Weakened" << Color::RESET << ".\n";
                        UIHelper::pause(150);
                    } else {
                        themedGeneric("It rakes past twice, turning on a wingtip!");
                        dive(std::max(1, atk / 2));
                        if (playerHealth > 0) dive(std::max(1, atk / 2));
                    }
                    break;
                }
                // The Assassin's are thrown blades, and your armour stops them.
                const bool slips = enemy.getName().find("Assassin") == std::string::npos;
                if (r < 60) {
                    themedGeneric(slips ? "It looses a shot straight through your guard." : "It throws a blade at you.");
                    doAttack(atk, slips);
                }
                else if (r < 85) {
                    cast(EnemyArt::CastGlow::WEAK, enemyProjectile());
                    Audio::playSFXPitched("special", 0.85f);
                    std::cout << Color::WEAK_CLR << "A shot clips your arm." << Color::RESET << "\n";
                    if (applyPlayerStatus(StatusType::WEAK, 2))
                        std::cout << "  You are " << Color::WEAK_CLR << "Weakened" << Color::RESET << ".\n";
                    UIHelper::pause(150);
                } else {
                    themedGeneric("It fires twice in quick succession!");
                    doAttack(std::max(1, atk / 2), slips);
                    if (playerHealth > 0) doAttack(std::max(1, atk / 2), slips);
                }
                break;
            }
            case EnemyType::CASTER: {
                // The Wizard is the third fight: no hex, and a mend of 3.
                const bool wizard = enemy.getName().find("Wizard") != std::string::npos;
                if (r < 45) { themedGeneric("It hurls a bolt of raw force."); doAttack(atk, false); }
                else if (r < 75 && !wizard) {
                    cast(EnemyArt::CastGlow::WEAK, enemyProjectile());
                    Audio::playSFXPitched("special", 0.85f);
                    std::cout << Color::WEAK_CLR << "A hex settles over you." << Color::RESET << "\n";
                    if (applyPlayerStatus(StatusType::WEAK, 2))
                        std::cout << "  You are " << Color::WEAK_CLR << "Weakened" << Color::RESET << ".\n";
                    UIHelper::pause(150);
                } else if (enemy.getHealth() < enemy.getMaxHealth() / 2) {
                    int h = wizard ? 3 : 6 + def;
                    enemy.heal(h);
                    std::cout << Color::HEAL << "It knits its wounds closed, healing " << h << " HP." << Color::RESET << "\n";
                    UIHelper::pause(150);
                } else doDefend(def);
                break;
            }
            case EnemyType::BEAST:
                if (r < 55) { themedGeneric("It lunges at you with teeth and claws."); doAttack(atk, false); }
                else if (r < 85 && enemy.getBonusAttack() < 6) {
                    enemy.addBonusAttack(2);
                    std::cout << Color::RED << "It works itself into a frenzy, gaining +2 attack." << Color::RESET << "\n";
                    UIHelper::pause(150);
                } else doDefend(def);
                break;
            case EnemyType::UNDEAD:
                if (r < 50) { themedGeneric("It claws at you with dead hands."); doAttack(atk, false); }
                else if (r < 80) {
                    applyEnemyStatusOnPlayerPoison(3);
                } else {
                    int h = 5 + def;
                    enemy.heal(h);
                    themedGeneric("It drains the air around it and knits itself together.");
                    std::cout << Color::HEAL << "It heals " << h << " HP." << Color::RESET << "\n";
                    UIHelper::pause(150);
                }
                break;
            default:
                doAttack(atk, false);
                break;
        }
    };

    // The skeleton claws on every turn once it has stood for one, whichever way
    // its master spends its own turn.
    const bool skeletonActs = lichAddAlive;   // the dead it just raised waits a turn
    auto skeletonStrike = [&]() {
        if (!skeletonActs || !lichAddAlive || playerHealth <= 0 || !enemy.isAlive()) return;
        addStrikes();
    };

    // --- The ruined church ------------------------------------------------
    // Each of the nine has a rule that runs on every turn it takes, around or
    // in place of its moves, so it comes before the signature roll.
    if (enemyIs("Gargoyle")) {
        // Stone, then awake: it wakes by dropping on you from the roof and
        // climbs back to its perch on the turn after. Taunted while awake, it
        // stays down and fights.
        if (gargoyleStone) {
            gargoyleStone = false;
            themedGeneric("The stone cracks open, and the Gargoyle drops on you from the roof!");
            doAttack(atk + 4, false);
        } else if (taunted) {
            themedGeneric("Goaded, it stays off its perch and lunges at you.");
            doAttack(atk, false);
        } else {
            gargoyleStone = true;
            enemyInvulnerable = true;
            EnemyArt::setEnemyStone(true);
            Audio::playSFXPitched("defend", 0.6f);
            std::cout << Color::CYAN << "The Gargoyle climbs back to its perch and turns to stone." << Color::RESET
                      << " Nothing hurts it on your next turn.\n";
            UIHelper::pause(250);
        }
        return;
    }
    if (enemyIs("Glass Templar")) {
        // It wears the windows: the glass goes back on every turn it gets.
        glassArmor = def * 3;
        enemy.gainArmor(glassArmor);
        glassUp = true;
        Audio::playSFXPitched("defend", 1.3f);
        std::cout << Color::ARMOR_CLR << "It gathers up the broken glass and wears it again: +" << glassArmor
                  << " armor." << Color::RESET << "\n";
        UIHelper::pause(150);
    }
    if (enemyIs("Bellringer")) {
        if (taunted) {
            themedGeneric("Goaded, it swings the bell at you instead of ringing it, and a toll is missed.");
            doAttack(atk, false);
            return;
        }
        if (++bellTolls >= 3) {
            bellTolls = 0;
            Audio::playSFXPitched("church_bell", 0.8f);
            themedGeneric("The third toll. The Bellringer brings the whole bell down on you: THE GREAT TOLL!");
            doAttack(atk * 2 + 10, false);
            return;
        }
        Audio::playSFXPitched("church_bell", 1.25f);
        std::cout << Color::MAGENTA << "The bell tolls, " << bellTolls << " of 3." << Color::RESET
                  << (bellTolls == 2 ? " The next toll is the great one." : "") << "\n";
        UIHelper::pause(200);
    }
    if (enemyIs("Inquisitor")) {
        if (taunted) {
            inquisitorMarks = 0;
            themedGeneric("Goaded, it comes at you with the stock of its crossbow. Its marks on you fade.");
            doAttackAt(atk, false, /*ranged*/false, /*useFrames*/false);
            return;
        }
        if (inquisitorMarks >= 3) {
            inquisitorMarks = 0;
            themedGeneric("It reads your name off its list and fires: SENTENCE! The bolt goes straight through your armor.");
            doAttackAt(atk + 4, false, /*ranged*/true, /*useFrames*/true, enemyProjectile(),
                       /*closeIn*/false, /*ignoreArmor*/true);
            return;
        }
        ++inquisitorMarks;
        std::cout << Color::MAGENTA << "It marks you on its list, " << inquisitorMarks << " of 3." << Color::RESET
                  << (inquisitorMarks == 3 ? " Its next bolt ignores your armor." : "") << "\n";
        UIHelper::pause(150);
    }
    if (enemyIs("Exhumed Saint") && enemy.getHealth() < enemy.getMaxHealth() / 2) {
        // Below half it blesses itself every turn, whatever else it does.
        const int h = std::max(4, enemy.getMaxHealth() / 25);
        enemy.heal(h);
        EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(), EnemyArt::SelfGlow::HEAL);
        std::cout << Color::HEAL << "It blesses itself, and its wounds close a little: +" << h << " HP."
                  << Color::RESET << "\n";
        UIHelper::pause(150);
    }

    // The signature fires on its own roll. Taunt skips the gate (every branch
    // attacks when taunted), and so does the Moonstruck: its three moves are
    // all it does, so it never falls into the shared archetype kit.
    const bool moonstruck = enemy.getName().find("Moon Shade") != std::string::npos;
    if (!taunted && !moonstruck && sigRoll >= SIGNATURE_CHANCE) { archetypeTurn(roll); skeletonStrike(); return; }

    // --- Signature moves ---------------------------------------------------
    // When the gate opens the move happens, so the odds shown are the odds you
    // get. A move that cannot be used right now hands the turn to the kit.
    auto nameHas = [&](const char* k){ return enemy.getName().find(k) != std::string::npos; };
    auto themed  = [&](const char* msg){ UIHelper::typeWrite(std::string(Color::MAGENTA) + msg + Color::RESET + "\n"); UIHelper::pause(150); };

    // THE RUINED CHURCH: their own moves. Their rules ran before the roll.
    if (nameHas("Sexton"))        { themed("The Sexton swings the spade and throws earth in your face!"); doAttack(atk + 2, false); return; }
    if (nameHas("Mirror Nun")) {
        if (taunted) { doAttack(atk, false); return; }
        cast(EnemyArt::CastGlow::WEAK, enemyProjectile());
        nextHandPenalty = std::max(nextHandPenalty, 1);
        Audio::playSFXPitched("special", 0.85f);
        std::cout << Color::MAGENTA << "She turns the mirror on you, and you see the moon in the glass." << Color::RESET
                  << " Your next hand is a card smaller.\n";
        if (enemyWeakens() && applyPlayerStatus(StatusType::WEAK, 2))   // always, this far up the road
            std::cout << "  You are " << Color::WEAK_CLR << "Weakened 2" << Color::RESET << ".\n";
        UIHelper::pause(250);
        return;
    }
    if (nameHas("Bellringer")) {
        // A taunted Bellringer never gets here: it swung the bell above.
        nextHandPenalty = std::max(nextHandPenalty, 1);
        Audio::playSFXPitched("special", 0.8f);
        std::cout << Color::MAGENTA << "It rings a PEAL that goes on ringing in your head." << Color::RESET
                  << " Your next hand is a card smaller.\n";
        UIHelper::pause(250);
        return;
    }
    if (nameHas("Inquisitor"))    { themed("The Inquisitor looses a heavy QUARREL!"); doAttack(atk + 3, true); return; }
    if (nameHas("Exhumed Saint")) { themed("The Exhumed Saint brings its CROSIER down on you!"); doAttack(atk + 3, false); return; }
    if (nameHas("Confessor"))     { themed("The Confessor reads out your PENANCE!"); doAttack(atk + 2, true); return; }
    if (nameHas("Glass Templar")) { themed("The Glass Templar bashes you with its LEADED SHIELD!"); doAttack(atk + 2, false); return; }
    if (nameHas("Unchosen"))  { themed("The Unchosen swings at you the way you swing: BORROWED CUT!"); doAttack(atk + 2, false); return; }

    // MELEE
    if (nameHas("Goblin"))    { themed("The Goblin jabs at you!"); doAttack(atk, false); return; }
    // Thrown, not swung: a MELEE enemy making a ranged attack. It holds its
    // ground and Parry catches the dagger without a riposte.
    if (nameHas("Bandit"))    { themed("The Bandit hurls a dagger!"); doAttackAt(atk, false, /*ranged*/true, /*useFrames*/true, enemyProjectile()); return; }
    if (nameHas("Raider"))    { themed("The Raider bashes with brute force!"); doAttack(atk, false); return; }
    if (nameHas("Warrior"))   { themed("The Warrior lunges, piercing your guard!"); doAttack(atk, !taunted); return; }
    if (nameHas("Knight")) {
        if (taunted) { doAttack(atk, false); return; }
        enemy.gainArmor(def);
        std::cout << Color::ARMOR_CLR << "The Knight raises its shield (+" << def << " armor), then bashes!" << Color::RESET << "\n";
        UIHelper::pause(150);
        doAttack(std::max(1, atk / 2), false);
        return;
    }
    if (nameHas("Berserker")) {
        // Two moves of its own, so this one still splits. Capped at +6.
        if (!taunted && roll < 45 && enemy.getBonusAttack() < 6) {
            enemy.addBonusAttack(2);
            std::cout << Color::RED << "The Berserker roars, growing stronger! (+2 attack)" << Color::RESET << "\n";
            UIHelper::pause(200); return;
        }
        themed("The Berserker swings in a frenzy!"); doAttack(atk, false); return;
    }
    if (nameHas("Gladiator")) { themed("The Gladiator lands a brutal UPPERCUT!"); doAttack(atk + 3, true); return; }
    if (nameHas("Enforcer")) {
        themed("The Enforcer unleashes a COMBO STRIKE!");
        doAttack(atk, false);
        if (playerHealth > 0 && enemy.isAlive()) doAttack(atk, false);
        return;
    }

    // TANK. Their braces live in the kit, so the signature is the move itself.
    if (nameHas("Guardian"))  { themed("The Guardian sweeps a WHIRLWIND!"); doAttack(atk, true); return; }
    if (nameHas("Barbarian")) {
        if (taunted) { doAttack(atk, false); return; }
        enemy.gainArmor(def + 6);
        std::cout << Color::ARMOR_CLR << "The Barbarian hardens its IRON SKIN (+" << (def + 6) << " armor)!" << Color::RESET << "\n";
        UIHelper::pause(150);
        if (roll < 50 && enemyWeakens()) { flash(EnemyArt::CastGlow::WEAK); Audio::playSFXPitched("special", 0.85f);
                         if (applyPlayerStatus(StatusType::WEAK, 2))
                             std::cout << "  You are " << Color::WEAK_CLR << "Weakened 2" << Color::RESET << ".\n"; }
        return;
    }
    if (nameHas("Sentinel")) {
        if (taunted) { doAttack(atk, false); return; }
        enemy.gainArmor(def + 4);
        std::cout << Color::ARMOR_CLR << "The Sentinel FORTIFIES (+" << (def + 4) << " armor)!" << Color::RESET << "\n";
        UIHelper::pause(150);
        return;
    }
    if (nameHas("Warden"))    { themed("The Warden delivers a SMACKDOWN!"); doAttack(atk + 1, false); return; }
    if (nameHas("Paladin")) {
        // Two moves, split on the same roll the other two-move enemies use.
        // Judgement cannot be turtled and Absolution cannot be poisoned, so
        // the answer to it is to be quick, which nothing else up here asks.
        if (!taunted && roll < 40) {
            enemy.heal(20 + enemy.getBaseDefense());
            const bool cleansed = enemy.hasAnyStatus();
            enemy.clearStatuses();
            // The regular turn has no bossMend(): this is the self-buff glow
            // every other roster enemy uses when it mends itself.
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(),
                                          EnemyArt::SelfGlow::HEAL);
            Audio::playSFX("heal");
            themed("The Paladin speaks an ABSOLUTION over itself.");
            std::cout << "  " << Color::HEAL << "It heals " << (20 + enemy.getBaseDefense()) << " HP"
                      << Color::RESET;
            if (cleansed)
                std::cout << Color::MAGENTA << ", and everything you put on it burns off" << Color::RESET;
            std::cout << ". (" << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
            UIHelper::pause(250);
            return;
        }
        paladinJudgements++;
        themed("The Paladin passes JUDGEMENT on you!");
        // Through your armour entirely, as View Enemy says. Piercing half of it
        // let a big enough guard soak the whole judgement.
        doAttackAt(atk + 2 + 3 * paladinJudgements, false, archetypeIsRanged(), true,
                   enemyProjectile(), false, /*ignoreArmor*/true);
        return;
    }
    if (nameHas("Bastion")) {
        if (taunted) { doAttack(atk, false); return; }
        if (roll < 60) {
            enemy.gainArmor(def + 8);
            std::cout << Color::ARMOR_CLR << "The Bastion raises an impenetrable wall (+" << (def + 8)
                      << " armor)!" << Color::RESET << "\n";
            UIHelper::pause(150);
        } else {
            // The challenge is the point of the armor: it stops you waiting
            // the wall out behind your own guard.
            playerAttackOnly = true;
            Audio::playSFXPitched("special", 0.85f);
            std::cout << Color::RED << "The Bastion HAMMERS its shield and dares you to break it!" << Color::RESET
                      << " Next turn you can only play " << Color::CARD_ATTACK << "ATTACK" << Color::RESET << " cards.\n";
            UIHelper::pause(250);
        }
        return;
    }
    if (nameHas("Fortress")) {
        if (taunted) { doAttack(atk, false); return; }
        enemy.gainArmor(def);
        std::cout << Color::ARMOR_CLR << "The Fortress braces (+" << def << " armor), then shield-bashes!" << Color::RESET << "\n";
        UIHelper::pause(150);
        doAttack(std::max(1, atk / 2), false);
        return;
    }
    if (nameHas("Orc"))       { themed("The Orc hurls a crushing BODY SLAM!"); doAttack(atk + 2, false); return; }

    // CASTER. The big three still heal when badly hurt.
    const bool lowHp = enemy.getHealth() < enemy.getMaxHealth() / 3;
    if (nameHas("Sage")) {
        if (taunted) { doAttack(atk, false); return; }
        if (lowHp && roll < 40) { int h = 8 + def / 2; enemy.heal(h); std::cout << Color::HEAL << "The Sage channels a healing light (+" << h << " HP)." << Color::RESET << "\n"; UIHelper::pause(200); }
        else { cast(EnemyArt::CastGlow::BURN, enemyProjectile()); Audio::playSFX("fire"); std::cout << Color::BURN_CLR << "The Sage hurls a TORCH!" << Color::RESET << "\n";
               if (applyPlayerStatus(StatusType::BURN, 5)) std::cout << "  You gain " << Color::BURN_CLR << "Burn 5" << Color::RESET << ".\n";
               UIHelper::pause(250); }
        return;
    }
    if (nameHas("Archon")) {
        if (taunted) { doAttack(atk, false); return; }
        if (lowHp && roll < 35) { int h = 10 + def / 2; enemy.heal(h); std::cout << Color::HEAL << "The Archon mends itself (+" << h << " HP)." << Color::RESET << "\n"; UIHelper::pause(200); }
        else {
            // Its own frames show fire climbing out of the floor around its feet.
            // Hellfire is that, spread across the arena, rising as it goes.
            EnemyArt::printGroundFlames(enemy.getType(), enemy.getBossType(),
                                        ProjectileTable::FX_PILLAR);
            Audio::playSFX("fire");
            std::cout << Color::BURN_CLR << "The Archon calls down HELLFIRE!" << Color::RESET << "\n";
            if (applyPlayerStatus(StatusType::BURN, 12))
                std::cout << "  You gain " << Color::BURN_CLR << "Burn 12" << Color::RESET << ".\n";
            UIHelper::pause(250);
        }
        return;
    }
    if (nameHas("Spellmaster")) {
        if (taunted) { doAttack(atk, false); return; }
        if (lowHp && roll < 35) { int h = 10 + def / 2; enemy.heal(h); std::cout << Color::HEAL << "The Spellmaster mends itself (+" << h << " HP)." << Color::RESET << "\n"; UIHelper::pause(200); }
        else { cast(EnemyArt::CastGlow::POISON, enemyProjectile()); Audio::playSFX("poison"); std::cout << Color::POISON_CLR << "The Spellmaster spreads a VIRULENT PLAGUE!" << Color::RESET << "\n";
               if (applyPlayerStatus(StatusType::POISON, 14)) std::cout << "  You gain " << Color::POISON_CLR << "Poison 14" << Color::RESET << ".\n"; UIHelper::pause(250); }
        return;
    }
    if (nameHas("Enchanter")) {
        if (taunted) { doAttack(atk, false); return; }
        nextHandPenalty = 2;
        Audio::playSFXPitched("special", 0.85f);
        // Relative, not "3 cards": the hand size moves with Endurance and upgrades.
        std::cout << Color::MAGENTA << "The Enchanter TEMPTS you into hesitation." << Color::RESET
                  << " Your next hand is " << Color::CYAN << "2 cards smaller" << Color::RESET << ".\n";
        UIHelper::pause(250);
        return;
    }
    if (nameHas("Sorcerer")) {
        if (taunted) { doAttack(atk, false); return; }
        cast(EnemyArt::CastGlow::WEAK, enemyProjectile());
        const bool chills = enemyWeakens();   // not in the first ten fights
        nextHandPenalty = std::max(nextHandPenalty, 1);
        Audio::playSFXPitched("special", 0.85f);
        std::cout << Color::BLUE << "The Sorcerer hurls an ICE BLAST!" << Color::RESET << " Your next hand loses a card.\n";
        if (chills && applyPlayerStatus(StatusType::WEAK, 2))
            std::cout << "  You are " << Color::WEAK_CLR << "Weakened 2" << Color::RESET << ".\n";
        UIHelper::pause(250);
        return;
    }
    if (nameHas("Vampire")) {
        if (taunted) { doAttack(atk, false); return; }
        std::cout << Color::MAGENTA << "The Vampire sinks in a VAMPIRIC DRAIN!" << Color::RESET << "\n";
        UIHelper::pause(150);
        // A bite, not a spell: she closes in, so Parry can catch it, but her
        // attack frames show casting, so this swings without them.
        doAttackAt(10, false, /*ranged*/false, /*useFrames*/false);
        if (enemy.isAlive()) {
            enemy.heal(6); enemy.addBonusAttack(1);
            std::cout << Color::HEAL << "She drinks deep, mending herself (+6 HP) and growing stronger (+1 attack)." << Color::RESET << "\n";
        }
        if (playerHealth > 0 && enemyWeakens()) { flash(EnemyArt::CastGlow::WEAK); Audio::playSFXPitched("special", 0.85f);
                                                  if (applyPlayerStatus(StatusType::WEAK, 2))
                                                      std::cout << "  You are " << Color::WEAK_CLR << "Weakened 2" << Color::RESET << ".\n"; }
        UIHelper::pause(200);
        return;
    }
    if (nameHas("Mystic")) {
        if (taunted) { doAttack(atk, false); return; }
        if (enemyInvulnerable) { archetypeTurn(roll); return; }
        enemyInvulnerable = true;
        Audio::playSFXPitched("special", 0.85f);
        std::cout << Color::CYAN << "The Mystic weaves an ILLUSION, splitting into fading copies." << Color::RESET
                  << " It takes no damage next turn.\n";
        UIHelper::pause(250);
        return;
    }

    // RANGED
    if (nameHas("Deadeye")) { themed("The Deadeye lines up a DEAD SHOT!");
                              doAttackAt(atk, true, true, true, enemyProjectile()); return; }
    if (nameHas("Wyvern"))  { themed("The Wyvern dives with a FLYING GNASH!");
                              doAttackAt(atk + 2, true, true, true, EnemyArt::Proj::NONE, /*closeIn*/true); return; }
    if (nameHas("Omneye")) {
        if (!taunted && roll < 30 && enemyWeakens()) {
            // The same ray it shoots with, washed blue: the eye has one way of
            // reaching you, and it was throwing an orb across the sky.
            EnemyArt::printEnemyBeam(enemy.getType(), enemy.getBossType(), ProjectileTable::FX_BEAM,
                                     enemyMuzzleX(), enemyMuzzleY(), /*weakGlow*/true);
            Audio::playSFXPitched("special", 0.85f);
            std::cout << Color::WEAK_CLR << "The Omneye turns its gaze on you." << Color::RESET << "\n";
            if (applyPlayerStatus(StatusType::WEAK, 2))
                std::cout << "  You are " << Color::WEAK_CLR << "Weakened 2" << Color::RESET << ".\n";
            UIHelper::pause(200);
            return;
        }
        themed("The Omneye fires a searing eye-beam!");
        // A beam, not a bolt: it stays joined to the pupil.
        EnemyArt::printEnemyBeam(enemy.getType(), enemy.getBossType(), ProjectileTable::FX_BEAM,
                                 enemyMuzzleX(), enemyMuzzleY());
        doAttackAt(atk + 2, true, true, /*useFrames*/false, EnemyArt::Proj::NONE); return;
    }
    if (nameHas("Falcon")) {
        // Nothing crosses the gap: it dives in and pulls out again, so Proj::NONE.
        themed("The falcon stoops and GOUGES on a howling gust!");
        doAttackAt(atk + 1, true, true, true, EnemyArt::Proj::NONE, /*closeIn*/true);
        return;
    }

    // BEAST
    if (nameHas("Wolf"))    { themed("The Wolf lunges with a BITE!"); doAttack(atk, false); return; }
    if (nameHas("Spider"))  { if (taunted) { doAttack(atk, false); return; } cast(EnemyArt::CastGlow::WEAK, enemyProjectile()); Audio::playSFXPitched("special", 0.85f); std::cout << Color::WEAK_CLR << "The Spider snares you in a WEB TRAP!" << Color::RESET << "\n"; if (applyPlayerStatus(StatusType::WEAK, 2)) std::cout << "  You are " << Color::WEAK_CLR << "Weakened 2" << Color::RESET << ".\n"; UIHelper::pause(200); return; }
    if (nameHas("Serpent")) { if (taunted) { doAttack(atk, false); return; } cast(EnemyArt::CastGlow::WEAK, enemyProjectile()); Audio::playSFXPitched("special", 0.85f); std::cout << Color::WEAK_CLR << "The Serpent ENTANGLES you!" << Color::RESET << "\n"; if (applyPlayerStatus(StatusType::WEAK, 3)) std::cout << "  You are " << Color::WEAK_CLR << "Weakened 3" << Color::RESET << ".\n"; UIHelper::pause(200); return; }
    if (nameHas("Basilisk")) {
        if (taunted) { doAttack(atk, false); return; }
        if (curseTurnsLeft != 0) { archetypeTurn(roll); return; }
        // Its own purple breath, out of the mouth: the generic status orb sailed
        // across the sky with nothing to do with the creature below it.
        cast(EnemyArt::CastGlow::STUN, enemyProjectile());
        curseTurnsLeft = 7;
        Audio::playSFXPitched("special", 0.85f);
        std::cout << Color::BOLD << Color::MAGENTA << "The Basilisk fixes you with a petrifying CURSE!" << Color::RESET << "\n"
                  << "  " << Color::RED << "Defeat it within 7 turns or turn to stone." << Color::RESET << "\n";
        UIHelper::pause(300);
        return;
    }
    if (nameHas("Cockatrice")) {
        // Reuses the Basilisk curse slot, so a countdown can never stack.
        if (taunted) { doAttack(atk, false); return; }
        if (curseTurnsLeft != 0) { archetypeTurn(roll); return; }
        themed("The Cockatrice sinks in a PETRIFYING BITE!");
        doAttack(atk, false);
        if (playerHealth > 0) {
            curseTurnsLeft = 5;
            Audio::playSFXPitched("special", 0.85f);
            std::cout << "  " << Color::BOLD << Color::MAGENTA << "Stone creeps out from the wound!" << Color::RESET << "\n"
                      << "  " << Color::RED << "Defeat it within 5 turns or turn to stone." << Color::RESET << "\n";
            UIHelper::pause(300);
        }
        return;
    }
    if (nameHas("Manticore")) {
        themed("The Manticore lunges with a TWIN MAW - both heads at once!");
        doAttack(atk, false);
        if (playerHealth > 0 && enemy.isAlive()) doAttack(atk, false);
        return;
    }
    if (nameHas("Fleshmass")) {
        themed("The Fleshmass lashes out with grasping tentacles!");
        int hpBefore = playerHealth;
        doAttack(atk, false);
        // BIND: a lash that draws blood coils on. Skips whiffs (dodged/parried/armor-eaten).
        if (playerHealth < hpBefore && playerHealth > 0) {
            fleshmassBindPending = true;
            Audio::playSFXPitched("special", 0.85f);
            std::cout << Color::BOLD << Color::MAGENTA << "Tentacles coil around you! BOUND:" << Color::RESET
                      << " you can play only one card next turn.\n";
            UIHelper::pause(250);
        }
        return;
    }

    // UNDEAD
    if (nameHas("Ghoul")) {
        if (taunted) { doAttack(atk, false); return; }
        themed("The Ghoul CHOMPS down, feeding on you!");
        // It feeds on what it takes: a bite your armor eats whole feeds it nothing.
        int hpBefore = playerHealth;
        doAttack(atk, false);
        const int fed = playerHealth < hpBefore ? 8 : 0;
        if (enemy.isAlive() && fed > 0) { enemy.heal(fed); std::cout << Color::HEAL << "The Ghoul heals " << fed << " HP from the bite." << Color::RESET << "\n"; }
        else if (enemy.isAlive()) { std::cout << "  " << Color::DIM << "It draws no blood, and feeds on nothing." << Color::RESET << "\n"; }
        if (playerHealth > 0) { flash(EnemyArt::CastGlow::POISON); Audio::playSFX("poison");
                                if (applyPlayerStatus(StatusType::POISON, 3))
                                    std::cout << "  You gain " << Color::POISON_CLR << "Poison 3" << Color::RESET << ".\n"; }
        UIHelper::pause(200);
        return;
    }
    if (nameHas("Banshee")) {
        if (taunted) { doAttack(atk, false); return; }
        cast(EnemyArt::CastGlow::WEAK, enemyProjectile());
        const bool chills = enemyWeakens();   // not in the first ten fights
        if (enemy.getBonusAttack() < 6) enemy.addBonusAttack(2);
        std::cout << Color::MAGENTA << "The Banshee looses a WAILING SCREAM!" << Color::RESET << " She grows stronger (+2 attack).\n";
        if (chills && applyPlayerStatus(StatusType::WEAK, 2))
            std::cout << "  You are " << Color::WEAK_CLR << "Weakened 2" << Color::RESET << ".\n";
        UIHelper::pause(250);
        return;
    }
    if (nameHas("Specter") || nameHas("Wraith")) {
        if (taunted) { doAttack(atk, false); return; }
        if (enemyInvulnerable) { archetypeTurn(roll); return; }
        enemyInvulnerable = true;
        Audio::playSFXPitched("special", 0.85f);
        std::cout << Color::CYAN << "The spirit turns GHOSTLY, fading half out of sight." << Color::RESET
                  << " It takes no damage next turn.\n";
        UIHelper::pause(250);
        return;
    }
    if (nameHas("Revenant")) {
        if (taunted) { doAttack(atk, false); return; }
        enemyParryStance = true;
        // With a challenge attached, so the turn is a real decision.
        playerAttackOnly = true;
        Audio::playSFXPitched("special", 0.85f);
        std::cout << Color::CYAN << "The Revenant raises a PARRY stance and dares you to swing." << Color::RESET
                  << " Next turn you can only play " << Color::CARD_ATTACK << "ATTACK" << Color::RESET << " cards.\n";
        UIHelper::pause(250);
        return;
    }
    // The secret fight. Moon Scent when calm, and while the scent lasts its
    // signature turns become the Maul, so its own moves never go quiet the way
    // they did when a lapsed-scent check dropped it to a plain attack.
    if (nameHas("Moon Shade")) {
        // Three moves in every shape: Moon Scent to work itself up, the Maul while
        // the scent lasts, and the one move the shape it copied taught it.
        const int z = std::max(0, std::min(4, moonZone));
        const bool calm = !enemy.hasStrength();
        // Still learning (the first three shapes): its scent is spent on its
        // very next turn, and that turn is always the Maul.
        const bool learning = z <= MOON_LEARNING_LAST;
        // Every turn is one of these three: it works itself up, then comes at
        // you, and its own move stays the rarest of the three.
        const bool ownMove = !taunted && (calm ? roll >= 72 : (!learning && roll >= 78));
        if (!taunted && calm && !ownMove && roll < 40) {
            enemy.applyStatus(StatusType::STRENGTH, learning ? 1 : 3, 1.5, MOON_FRENZY[z]);
            if (learning) enemyVulnerableTurns = 1;   // open to you until its next turn
            Audio::playSFXPitched("special", 0.85f);
            EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(),
                                          EnemyArt::SelfGlow::STRENGTH);
            std::cout << Color::BOLD << Color::MAGENTA << "The Moon Shade catches your scent: MOON SCENT!" << Color::RESET;
            if (learning)
                std::cout << " Its next blow lands at " << Color::STRENGTH_CLR << timesText(MOON_FRENZY[z]) << Color::RESET
                          << ", and it has let its guard fall: " << Color::YELLOW
                          << "your attacks deal x1.5" << Color::RESET << " until then.\n";
            else
                std::cout << " Its blows land at " << Color::STRENGTH_CLR << timesText(MOON_FRENZY[z])
                          << Color::RESET << " for 3 turns.\n";
            UIHelper::pause(300);
        } else if (ownMove) {
            themed((std::string("The Moon Shade uses ") + MOON_MOVES[z].name + "!").c_str());
            switch (z) {
                case 0:   // Grave Silk; in the first ten fights, no Weaken
                    cast(EnemyArt::CastGlow::POISON, enemyProjectile());
                    Audio::playSFX("poison");
                    std::cout << Color::POISON_CLR << "Bone-white thread wraps you." << Color::RESET << "\n";
                    if (enemyWeakens() && applyPlayerStatus(StatusType::WEAK, 2))
                        std::cout << "  You are " << Color::WEAK_CLR << "Weakened 2" << Color::RESET << ".\n";
                    if (applyPlayerStatus(StatusType::POISON, 3))
                        std::cout << "  You gain " << Color::POISON_CLR << "Poison 3" << Color::RESET << ".\n";
                    break;
                case 1: { // Lunar Mirage
                    cast(EnemyArt::CastGlow::WEAK, enemyProjectile());
                    nextHandPenalty = std::max(nextHandPenalty, 2);
                    const int guard = def + 6;
                    enemy.gainArmor(guard);
                    Audio::playSFXPitched("special", 0.8f);
                    std::cout << Color::MAGENTA << "The room doubles, then triples. You will draw 2 fewer cards,"
                              << " and it hides behind " << guard << " armor." << Color::RESET << "\n";
                    break;
                }
                case 2: { // Howl at the Red Moon
                    const int heal = howlHeal(enemy.getMaxHealth());
                    enemy.heal(heal);
                    if (enemy.getBonusAttack() < 6) enemy.addBonusAttack(2);
                    EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(),
                                                  EnemyArt::SelfGlow::STRENGTH);
                    Audio::playSFXPitched("special", 0.7f);
                    std::cout << Color::MAGENTA << "It howls at the red moon. +" << heal
                              << " HP, and its attack rises." << Color::RESET << "\n";
                    break;
                }
                case 3:   // Petrifying Gaze, the shape it copied being a cockatrice
                    cast(EnemyArt::CastGlow::STUN, enemyProjectile());
                    if (curseTurnsLeft != 0) {          // already ticking: it just mauls
                        themed("The Moon Shade drives its stone-hard beak in: MOONLIT MAUL!");
                        doAttack(atk + 3, false);
                        break;
                    }
                    curseTurnsLeft = 6;
                    Audio::playSFXPitched("special", 0.75f);
                    std::cout << Color::BOLD << Color::MAGENTA << "Its stare settles on you, and stone creeps up your legs!"
                              << Color::RESET << "\n  " << Color::RED
                              << "Kill it within 6 turns or turn to stone." << Color::RESET << "\n";
                    break;
                default: { // Eclipse Ward
                    const int guard = def * 2 + 8;
                    enemy.gainArmor(guard);
                    enemy.heal(8);
                    Audio::playSFXPitched("defend", 0.7f);
                    std::cout << Color::ARMOR_CLR << "The moon goes behind it. +" << guard
                              << " armor, and it heals 8." << Color::RESET << "\n";
                    break;
                }
            }
            UIHelper::pause(300);
        } else {
            static const char* MAUL[5] = {
                "The Moon Shade lashes out with bone and leg at once: MOONLIT MAUL!",
                "The Moon Shade strikes with a hand that was never there: MOONLIT MAUL!",
                "The Moon Shade tears into you with a MOONLIT MAUL!",
                "The Moon Shade drives its stone-hard beak in: MOONLIT MAUL!",
                "The Moon Shade brings a pale blade down on you: MOONLIT MAUL!",
            };
            themed(MAUL[z]);
            doAttack(atk + 3, false);
        }
        // The scent wears off one of its own turns at a time: a learning
        // shape spends all of it on the Maul, the last two hold it for three
        // turns. Nothing else ticks a regular enemy's Strength, so without this
        // the last two would stay in their frenzy for the rest of the fight.
        if (!calm) enemy.processStrength();
        return;
    }
    if (nameHas("Lich")) {
        if (taunted) doAttack(atk, false);
        else if (lichAddAlive) archetypeTurn(roll);
        else {
            // Every undead on the roster except another Lich. A fat one you
            // have to chew through and a thin one that hurts are different
            // problems, and the Lich picks which one you get.
            struct Raised { const char* name; int hp; int atk; };
            static const Raised DEAD[] = {
                { "Skeleton", 24, 6 },   // the middle of the range
                { "Ghoul",    30, 5 },
                { "Wraith",   18, 8 },
                { "Specter",  16, 7 },
                { "Banshee",  20, 7 },
                { "Revenant", 34, 5 },
            };
            const int n = (int)(sizeof(DEAD) / sizeof(DEAD[0]));
            const Raised& r = DEAD[rollDist(gen) % n];
            lichAddName  = r.name;
            lichAddMaxHp = r.hp; lichAddHp = r.hp; lichAddAtk = r.atk;
            lichAddAlive = true;
            EnemyArt::setCompanion(lichAddName);
            Audio::playSFXPitched("special", 0.85f);
            std::cout << Color::BOLD << Color::MAGENTA << "The Lich RAISES a " << lichAddName
                      << " to fight at its side!" << Color::RESET
                      << " (" << lichAddName << " HP: " << lichAddHp << "/" << lichAddMaxHp << ")\n";
            UIHelper::pause(300);
        }
        skeletonStrike();
        return;
    }

    // Everything above is named; everything below is the fallback. TODO: the
    // Wizard, Skeleton and Archer still land here and are the flattest fights.
    switch (t) {
        case EnemyType::MELEE:
            if (roll < 70) doAttack(atk, false);
            else doDefend(def);
            break;
        case EnemyType::RANGED: {
            // Early on the crippling shot is gone: attack and brace split its share.
            const int r = enemyWeakens() ? roll : roll * 80 / 100;
            if (r < 60) {
                doAttack(atk, true); // pierce half armor
            } else if (r < 80) {
                doDefend(std::max(1, def - 1));
            } else {
                cast(EnemyArt::CastGlow::WEAK, enemyProjectile());
                Audio::playSFXPitched("special", 0.85f);
                std::cout << Color::WEAK_CLR << "The enemy fires a crippling shot!" << Color::RESET << "\n";
                if (applyPlayerStatus(StatusType::WEAK, 2))
                    std::cout << "  You are " << Color::WEAK_CLR << "Weakened" << Color::RESET << " for 2 turns.\n";
            }
            break;
        }
        case EnemyType::TANK:
            if (roll < 65) doDefend(def); // was def+3, too much in early encounters
            else doAttack(std::max(1, atk - 2), false);
            break;
        case EnemyType::CASTER:
            if (enemy.getHealth() < enemy.getMaxHealth() / 3 && roll < 60) {
                int healAmt = nameHas("Wizard") ? 3 : 8 + (def / 2);
                enemy.heal(healAmt);
                std::cout << "The enemy casts a heal and recovers " << healAmt << " HP! ("
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << ")\n";
            } else if (roll < 40) {
                cast(EnemyArt::CastGlow::POISON, enemyProjectile());
                Audio::playSFX("poison");
                std::cout << Color::POISON_CLR << "The enemy casts Poison Bolt!" << Color::RESET << "\n";
                if (applyPlayerStatus(StatusType::POISON, 3))
                    std::cout << "  " << Color::POISON_CLR << "Poison 3: 2 damage a turn for 6 turns." << Color::RESET << "\n";
            } else if (roll < 60) {
                cast(EnemyArt::CastGlow::BURN, enemyProjectile());
                Audio::playSFX("fire");
                std::cout << Color::BURN_CLR << "The enemy casts Fireball!" << Color::RESET << "\n";
                if (applyPlayerStatus(StatusType::BURN, 2))
                    std::cout << "  " << Color::BURN_CLR << "Burn 2: 3 damage a turn for 2 turns." << Color::RESET << "\n";
            } else {
                doAttack(atk + 1, false);
            }
            break;
        case EnemyType::BEAST:
            if (roll < 60) {
                doAttack(atk, false);
            } else if (roll < 85) {
                cast(EnemyArt::CastGlow::POISON, enemyProjectile());
                Audio::playSFX("poison");
                std::cout << Color::POISON_CLR << "The enemy sinks its fangs in with a venomous bite!" << Color::RESET << "\n";
                if (applyPlayerStatus(StatusType::POISON, 3))
                    std::cout << "  You gain " << Color::POISON_CLR << "Poison 3" << Color::RESET << ".\n";
            } else {
                doDefend(std::max(1, def - 1));
            }
            break;
        case EnemyType::UNDEAD:
            if (roll < 75 || !enemyWeakens()) {   // early on, no chilling touch
                doAttack(atk, false);
            } else {
                touch(EnemyArt::CastGlow::WEAK);
                Audio::playSFXPitched("special", 0.85f);
                std::cout << Color::WEAK_CLR << "The enemy reaches for you with a chilling touch!" << Color::RESET << "\n";
                if (applyPlayerStatus(StatusType::WEAK, 2))
                    std::cout << "  It saps your strength. " << Color::WEAK_CLR << "Weakened" << Color::RESET << " for 2 turns.\n";
            }
            break;
        default:
            doAttack(atk, false);
    }
}

// Shared by bossAction() and the Shadow Knight's mirrored attacks (can't be a lambda - those resolve outside bossAction()).
void Game::bossStrikesPlayer(int damage, bool raw, bool closeIn, bool unstoppable) {
    double weakMult = enemy.getWeakMultiplier() * enemy.getStrengthMultiplier();
    // What reaches whatever stands in front of you (Raise Undead).
    const int blow = damage;
    if (vulnerableTurns > 0) damage = (int)(damage * vulnerableMult);
    // Bosses' blows have types too, and the Glass Moon charges here as well.
    damage = damage * (100 + armourTypeMod(enemyAttackType())) / 100;
    if (hasRelic(Relic::GLASS_MOON)) damage = damage * 125 / 100;
    if (tickEnemyRend()) return;   // the tear finished it before the blow landed
    EnemyArt::setBlowsAtAlly(raisedAlive && !unstoppable && !counterAttackActive && !parryActive);
    struct AimBack { ~AimBack() { EnemyArt::setBlowsAtAlly(false); } } aimBack;
    // Bosses take their range from their archetype - the Undead Dragon is RANGED
    // and should breathe from where it stands rather than walking over first.
    EnemyArt::printBattleAttack(enemy.getType(), enemy.getBossType(), playerArmor > 0,
                                archetypeIsRanged(), /*useAttackFrames*/true,
                                /*projectile*/-1, enemyMuzzleX(), enemyMuzzleY(), closeIn);
    // Dodge Reversal fires before Parry when both are active (uncapped, higher priority).
    // An unstoppable blow (the true form's Truestrike) goes straight through
    // both, and leaves them standing for whatever comes next.
    if (counterAttackActive && !unstoppable) {
        counterAttackActive = false;
        if (counterWasLegendary) Audio::playSFX("legendary");
        int counterDmg = (int)((damage * 2 + counterBonusValue) * playerStatus.getStrengthMultiplier());
        int hpBefore = enemy.getHealth();
        enemy.takeDamage(counterDmg);
        int hpLost = hpBefore - enemy.getHealth();
        EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), DamageType::NONE, hpLost > 0);
        EnemyArt::popNumber(hpLost, true, EnemyArt::PopKind::DAMAGE);
        Audio::playSFX(!enemy.isAlive() ? deathSfx(enemy.isBoss()) : "attack");
        earn(Achievements::SIDESTEP);
        std::cout << Color::GREEN << "Dodge Reversal! You sidestep the boss's attack and counter for " << hpLost << " damage!" << Color::RESET
                  << " (Boss HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
        UIHelper::pause(200);
        return;
    }
    if (parryActive && !unstoppable) {
        int parryCap = playerArmor + parryBonusValue * 3; // current armor + Parry's own bonus - stack armor first to parry bigger hits
        parryActive = false;
        if (damage <= parryCap) {
            Audio::playSFXPitched("defend", 1.35f);   // the block rings off the blade
            int riposteDmg = (int)((damage * 1.5 + parryBonusValue) * playerStatus.getStrengthMultiplier());
            int hpBefore = enemy.getHealth();
            enemy.takeDamage(riposteDmg); // ignores defense - takeDamage only accounts for armor
            int hpLost = hpBefore - enemy.getHealth();
            // Before the animation, so the cue lands with the blow.
            Audio::playSFX(hpLost > 0 ? "attack" : "special");
            EnemyArt::printBattleHit(enemy.getType(), enemy.getBossType(), DamageType::NONE, hpLost > 0);
            EnemyArt::popNumber(hpLost, true, EnemyArt::PopKind::DAMAGE);
            bool stunned = tryStunEnemy();
            if (stunned && enemy.isAlive())
                EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::STUN, true);
            if (!enemy.isAlive()) Audio::playSFX(deathSfx(enemy.isBoss()));
            std::cout << Color::CYAN << "Parry! You deflect the blow. No damage taken. Riposte for " << hpLost
                      << " damage!" << (stunned ? " It is stunned!" : " It resists the stun!") << Color::RESET
                      << " (Boss HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
            earn(Achievements::PARRY);
            parryLanded = true;
            UIHelper::pause(300);
            return;
        } else {
            std::cout << Color::BOLD << Color::RED << "The blow is too powerful to parry! Your guard breaks!" << Color::RESET << "\n";
            UIHelper::pause(250);
        }
    }
    // Raise Undead: the dead take it whole. Only an unstoppable blow goes past.
    if (raisedAlive && !unstoppable) { raisedTakes(blow); return; }
    if (raw || unstoppable) {
        playerHealth = std::max(0, playerHealth - damage);
        bool saved = trySecondWind();
        if (damage > 0) {
            EnemyArt::printBattleKnightHit(enemy.getType(), enemy.getBossType());
            EnemyArt::popNumber(damage, false, EnemyArt::PopKind::DAMAGE);
        }
        Audio::playSFX("boss_attack");
        std::cout << Color::BOLD << Color::DAMAGE << "  It slams through your armor for " << damage
                  << "!" << Color::RESET
                  << " HP: " << hpColor(playerHealth, maxPlayerHealth)
                  << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
        if (saved) {
            Audio::playSFX("special");
            std::cout << "  " << Color::BOLD << Color::YELLOW
                      << savedLine("You refuse to fall! Clinging to 1 HP, you survive the killing blow!")
                      << Color::RESET << "\n";
        }
    } else {
        int actual = std::max(0, damage - playerArmor);
        playerArmor = std::max(0, playerArmor - damage);
        playerHealth = std::max(0, playerHealth - actual);
        bool saved = trySecondWind();
        if (actual > 0) {
            EnemyArt::printBattleKnightHit(enemy.getType(), enemy.getBossType());
            EnemyArt::popNumber(actual, false, EnemyArt::PopKind::DAMAGE);
        }
        Audio::playSFX("boss_attack");
        std::cout << Color::BOLD << Color::DAMAGE << "  It strikes for " << actual
                  << " damage!" << Color::RESET
                  << " HP: " << hpColor(playerHealth, maxPlayerHealth)
                  << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
        if (saved) {
            Audio::playSFX("special");
            std::cout << "  " << Color::BOLD << Color::YELLOW
                      << savedLine("You refuse to fall! Clinging to 1 HP, you survive the killing blow!")
                      << Color::RESET << "\n";
        }
    }
    if (weakMult < 1.0)
        std::cout << "  " << Color::WEAK_CLR << "[Weakened]" << Color::RESET << "\n";
    UIHelper::pause(200);
}

bool Game::trySecondWind() {
    lastSaveByThread = false;
    threadClockFrom = 0;
    if (playerHealth > 0) return false;
    if (bossSecondWindAvailable) {
        bossSecondWindAvailable = false;
        playerHealth = 1;
        return true;
    }
    // The Red Thread: once a run, any fight, any source, and it brings you
    // back at half your health rather than hanging on at one.
    if (hasRelic(Relic::RED_THREAD) && !redThreadUsed) {
        redThreadUsed = true;
        lastSaveByThread = true;
        playerHealth = std::max(1, maxPlayerHealth / 2);
        // Against the False Moon it also leaves you five turns from being
        // its vessel, if you were nearer than that.
        if (moonClock > 0 && moonClock < MOON_THREAD_TURNS) {
            threadClockFrom = moonClock;
            moonClock = MOON_THREAD_TURNS;
        }
        earn(Achievements::THREAD);
        return true;
    }
    return false;
}

std::string Game::savedLine(const char* bossSave) const {
    if (!lastSaveByThread) return bossSave;
    std::string s = "The Red Thread pulls tight and hauls you back to your feet at "
                  + std::to_string(playerHealth) + " HP.";
    // The turns it won on the clock follow on lines of their own. The caller
    // closes the last one.
    if (threadClockFrom > 0) s += Color::RESET + std::string("\n") + moonPiecesText(threadClockFrom, moonClock);
    return s;
}

void Game::bossAction() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> rollDist(0, 99);
    int roll = rollDist(gen);

    // Taunt: force this turn's roll into whichever bucket guarantees an Attack
    // action for this boss type.
    if (enemyTauntTurns > 0) {
        enemyTauntTurns--;
        switch (enemy.getBossType()) {
            case BossType::STONE_COLOSSUS: roll = 99; break;
            case BossType::VILE_WITCH:     roll = 0;  break;
            case BossType::WARLORD:        roll = 50; break;
            case BossType::HYDRA:          roll = 80; break;
            case BossType::DRAGON:         roll = 80; break;
            case BossType::SHADOW_KNIGHT:  roll = -1; break; // plain strike
            case BossType::FALSE_MOON:     roll = -1; break;
            default: break;
        }
    }

    double weakMult = enemy.getWeakMultiplier();
    enemy.processWeak(); // the one place this ticks - exactly once per round, regardless of boss type
    enemy.processStrength();
    int atk = (int)(std::max(0, enemy.getBaseAttack() + enemy.getBonusAttack()) * weakMult);

    // Fear reaches a boss as well, half as often as anything else: it flinches,
    // raises its guard and gives up whatever it was about to do.
    const int BOSS_FEAR_BRACE_CHANCE = 30;
    if (enemyFearTurns > 0) {
        enemyFearTurns--;
        if (rollDist(gen) < BOSS_FEAR_BRACE_CHANCE) {
            const int guard = enemy.getBaseDefense();
            enemy.gainArmor(guard);
            const bool fizzled = counterAttackActive || parryActive;
            counterAttackActive = false;
            parryActive = false;
            Audio::playSFXPitched("defend", 0.8f);
            UIHelper::typeWrite(std::string(Color::CYAN) + "The " + enemy.getName()
                + " flinches back and throws up its guard, +" + std::to_string(guard) + " armor."
                + Color::RESET + "\n");
            if (fizzled)
                std::cout << "  " << Color::DIM << "(No attack. Your stance fizzles.)" << Color::RESET << "\n";
            earn(Achievements::COLD_FEET);
            UIHelper::pause(250);
            return;
        }
    }

    // Every boss move that is not a plain attack shows something crossing the
    // field or landing on one of the two fighters.
    auto bossCast = [&](EnemyArt::CastGlow g, int proj = -1, int scalePct = 30) {
        EnemyArt::printEnemyCast(enemy.getType(), enemy.getBossType(), g, proj,
                                 enemyMuzzleX(), enemyMuzzleY(), scalePct);
    };
    auto bossTouch = [&](EnemyArt::CastGlow g) {
        EnemyArt::printBattleAttack(enemy.getType(), enemy.getBossType(), playerArmor > 0,
                                    /*ranged*/false);
        EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), g, false);
    };
    auto bossMend = [&]() {
        EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(),
                                      EnemyArt::SelfGlow::HEAL);
    };

    bool bossVolleyBroken = false;
    auto doAttack = [&](int damage, bool raw, bool closeIn = false) {
        if (enemy.hasStun()) {   // see the note on the regular doAttack
            if (!bossVolleyBroken) {
                bossVolleyBroken = true;
                std::cout << "  " << Color::CYAN
                          << "The stun lands mid-swing. The rest of the assault never comes."
                          << Color::RESET << "\n";
            }
            return;
        }
        bossStrikesPlayer(damage, raw, closeIn);
    };

    switch (enemy.getBossType()) {
        case BossType::STONE_COLOSSUS:
            if (roll < 15) {
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "The Stone Colossus uses EARTHQUAKE SLAM!" + Color::RESET + "\n");
                UIHelper::pause(300);
                parryLanded = false;
                doAttack(15, true);
                if (parryLanded) earn(Achievements::UNSHAKEN);
            } else if (roll < 45) {
                EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(),
                                              EnemyArt::SelfGlow::STRENGTH);
                Audio::playSFXPitched("defend", 0.8f);
                enemy.gainArmor(8);
                std::cout << Color::MAGENTA << "The Stone Colossus hardens!" << Color::RESET
                          << " +" << Color::ARMOR_CLR << 8 << Color::RESET
                          << " armor (" << enemy.getArmor() << " total)\n";
                UIHelper::pause(200);
            } else {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "The Stone Colossus strikes!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk + 4, false);
            }
            break;

        case BossType::VILE_WITCH:
            if (roll < 30) {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "The Vile Witch attacks!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
            } else if (roll < 70) {
                // Thrown from the staff head she holds high on the left.
                bossCast(EnemyArt::CastGlow::POISON, enemyProjectile());
                Audio::playSFX("poison");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "The Vile Witch casts PLAGUE!" + Color::RESET + "\n");
                if (applyPlayerStatus(StatusType::POISON, 4))
                    std::cout << "  You gain " << Color::POISON_CLR << "Poison 4" << Color::RESET << "!\n";
                if (applyPlayerStatus(StatusType::BURN, 2))
                    std::cout << "  You gain " << Color::BURN_CLR << "Burn 2" << Color::RESET << "!\n";
                UIHelper::pause(350);
            } else if (roll < 85) {
                int healAmt = 20;
                // It drains YOU: the pull crosses the field, then she mends.
                bossCast(EnemyArt::CastGlow::WEAK, enemyProjectile());
                enemy.heal(healAmt);
                bossMend();
                std::cout << Color::MAGENTA << "The Vile Witch siphons life, healing " << Color::HEAL
                          << healAmt << " HP!" << Color::RESET << " ("
                          << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                UIHelper::pause(250);
            } else {
                // Thrown larger than a bolt: the ground itself comes up.
                bossCast(EnemyArt::CastGlow::POISON, enemyProjectile(), 55);
                Audio::playSFX("poison");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "The Vile Witch casts TOXIC ERUPTION!"
                    + Color::RESET + "\n");
                // Announced first, so a reversal or a ward reads as the answer to it.
                const bool reversing = counterAttackActive;
                if (applyPlayerStatus(StatusType::POISON, 6))
                    std::cout << "  You gain " << Color::POISON_CLR << "Poison 6" << Color::RESET << "!\n";
                if (reversing) earn(Achievements::HER_OWN);
                UIHelper::pause(350);
            }
            break;

        case BossType::WARLORD:
            if (roll < 12) {
                bossCast(EnemyArt::CastGlow::STUN);
                Audio::playSFXPitched("volt", 0.85f);
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "The Thunder Beast unleashes a THUNDERSTRIKE!"
                    + Color::RESET + "\n");
                if (applyPlayerStatus(StatusType::STUN, 1)) {
                    std::cout << "  You are " << Color::STUN_CLR << "STUNNED" << Color::RESET << "!\n";
                    if (++thunderStuns >= 2) earn(Achievements::TWICE_STRUCK);
                }
                UIHelper::pause(350);
            } else if (roll < 27) {
                // A roar is thrown at you, not cast: it works itself up and the
                // sound rolls over the knight.
                EnemyArt::printBattleSelfBuff(enemy.getType(), enemy.getBossType(),
                                              EnemyArt::SelfGlow::STRENGTH);
                Audio::playSFXPitched("special", 0.85f);
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "The Thunder Beast roars a BATTLECRY!" + Color::RESET + "\n");
                if (applyPlayerStatus(StatusType::WEAK, 3))
                    std::cout << "  You are " << Color::WEAK_CLR << "Weakened 3" << Color::RESET << "!\n";
                UIHelper::pause(350);
            } else {
                // Exclusive with the two above: a stun or a roar costs it the swing.
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "The Thunder Beast attacks!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
                if (enemy.getBonusAttack() < 10) {
                    enemy.addBonusAttack(1);
                    std::cout << Color::MAGENTA << "The Thunder Beast grows stronger!" << Color::RESET
                              << " (total bonus +" << Color::RED << enemy.getBonusAttack() << Color::RESET << " attack)\n";
                    UIHelper::pause(200);
                }
            }
            break;

        case BossType::HYDRA:
            if (roll < 20) {
                const int healAmt = 18;
                enemy.heal(healAmt);
                bossMend();
                // Two grow back from every open stump; with none open, one new
                // head pushes through. Seared stumps stay shut.
                const int  before  = hydraHeads;
                const int  stumps  = hydraStumps;
                hydraHeads  = std::min(HYDRA_HEADS_MAX, hydraHeads + (stumps > 0 ? 2 * stumps : 1));
                hydraStumps = 0;
                const bool grew = hydraHeads > before;
                if (grew) hydraRegrew = true;
                if (grew) Audio::playSFXPitched("poison", 0.6f);   // wet and low: something growing
                std::cout << Color::MAGENTA
                          << (!grew       ? "It has no room for another head. The Hydra heals "
                              : stumps > 1 ? "Two grow back where each one fell. The Hydra heals "
                              : stumps == 1 ? "Two grow back where one fell. The Hydra heals "
                                            : "A new head pushes out of its neck. The Hydra heals ")
                          << Color::HEAL << healAmt << " HP" << Color::RESET;
                if (grew)
                    std::cout << Color::MAGENTA << ", and strikes with " << hydraHeads
                              << " heads from here." << Color::RESET;
                std::cout << " (" << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                UIHelper::pause(250);
            } else if (roll < 50) {
                // Fangs, not a spell: it closes, bites, and the venom follows.
                bossTouch(EnemyArt::CastGlow::POISON);
                Audio::playSFX("poison");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "The Hydra sinks its fangs in with a VENOMOUS BITE!" + Color::RESET + "\n");
                if (applyPlayerStatus(StatusType::POISON, 5))
                    std::cout << "  You gain " << Color::POISON_CLR << "Poison 5" << Color::RESET << "!\n";
                UIHelper::pause(350);
            } else if (roll < 75) {
                // One bite per head, each at half weight: two heads land one
                // full bite, nine land four and a half. At full weight, five
                // heads could take a whole health bar in one turn.
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "The Hydra lashes out with "
                    + std::to_string(hydraHeads) + " HEADS AT ONCE!" + Color::RESET + "\n");
                UIHelper::pause(200);
                for (int h = 0; h < hydraHeads && enemy.isAlive() && playerHealth > 0; ++h)
                    doAttack(std::max(1, atk / 2), false);
            } else {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "The Hydra bites!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
            }
            break;

        case BossType::DRAGON:
            if (roll < 20) {
                // A wall of flame out of the jaws, at well over a bolt's size.
                bossCast(EnemyArt::CastGlow::BURN, enemyProjectile(), 80);
                Audio::playSFX("fire");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "The Dragon unleashes FIRE BREATH!"
                    + Color::RESET + "\n");
                // Burn 8: four stacks was two ticks of chip damage at encounter 40.
                const bool reversing = counterAttackActive;
                if (applyPlayerStatus(StatusType::BURN, 8))
                    std::cout << "  You gain " << Color::BURN_CLR << "Burn 8" << Color::RESET << "!\n";
                if (reversing) earn(Achievements::BACKDRAFT);
                UIHelper::pause(350);
            } else if (roll < 45) {
                // A gust, not a bolt: wind has its own art.
                bossCast(EnemyArt::CastGlow::WEAK, ProjectileTable::FX_WIND, 70);
                Audio::playSFXPitched("special", 0.85f);
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "The Dragon's WING BUFFET knocks you off balance!" + Color::RESET + "\n");
                if (applyPlayerStatus(StatusType::WEAK, 3))
                    std::cout << "  You are " << Color::WEAK_CLR << "Weakened 3" << Color::RESET << "!\n";
                UIHelper::pause(350);
            } else if (roll < 70) {
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "The Dragon tears into you with a CLAW RAKE!" + Color::RESET + "\n");
                UIHelper::pause(200);
                // Claws mean closing, even though its breath is a ranged move.
                doAttack(atk + 5, true, /*closeIn*/true);
            } else {
                // It had two claw attacks and one of them was just "a direct
                // attack". A thing that died once and came back should leave
                // something behind when it bites.
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA
                    + "The Dragon sinks a CURSED BITE into you!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false, /*closeIn*/true);
                if (playerHealth > 0 && applyPlayerStatus(StatusType::REND, 3))
                    std::cout << "  " << Color::REND_CLR
                              << "The wound will not close: Rend 3, opening again each time it strikes."
                              << Color::RESET << "\n";
                UIHelper::pause(250);
            }
            break;

        case BossType::SHADOW_KNIGHT:
        case BossType::FALSE_MOON: {
            // Leftover prepared moves play out here; Taunt forces one guaranteed strike instead.
            if (roll < 0) {
                // Taunted: it has to swing, and this is the swing.
                knightPreparedMoves.clear();
                UIHelper::typeWrite(std::string(Color::MAGENTA) + mirrorThe() + " strikes!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(std::max(1, atk * 55 / 100), false);
                break;
            }
            // One for one: it answers each card as you play it, so the rest of
            // its queue goes (playing it here would give it a second turn). A
            // turn let go without a card is the exception: it swings, lighter
            // than a real blow, or ending turn after turn to cycle the deck for
            // a better hand would cost nothing.
            knightPreparedMoves.clear();
            if (playerSkippedTurn && mirrorBossIsMoon() && falseMoonShadeMove()) {
                // it wore a shape instead of swinging
            } else if (playerSkippedTurn) {
                UIHelper::typeWrite(std::string(Color::MAGENTA)
                    + "You let the turn go, and the " + mirrorName() + " swings." + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(std::max(1, atk * KNIGHT_TURN_END_PCT / 100), false);
            } else {
                UIHelper::typeWrite(std::string(Color::DIM)
                    + (mirrorBossIsMoon() ? "The False Moon hangs over you and waits."
                                          : "The shadow lowers your sword and waits.") + Color::RESET + "\n");
                UIHelper::pause(200);
            }
            playerSkippedTurn = false;
            break;
        }

        default:
            doAttack(atk, false);
    }
}
