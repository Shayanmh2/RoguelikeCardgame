#pragma once

#include "Enemy.h"
#include <vector>
#include <string>

// Battle sprites, rendered as 24-bit color half-blocks (U+2580): each cell's
// foreground is the top pixel, background the bottom, so two image rows share
// one terminal row.
namespace EnemyArt {
    struct RGB { unsigned char r, g, b; };

    // True-color pixel frame, loaded from a PNG sprite sheet.
    struct Art {
        std::vector<std::vector<RGB>> trueColorGrid;
        std::vector<std::vector<bool>> trueColorOpaque; // false = transparent
    };

    // Boss != NONE takes priority over the base EnemyType.
    const Art& get(EnemyType type, BossType boss = BossType::NONE);

    // Idle frame - alternates per call.
    const Art& getWalkFrame(EnemyType type, BossType boss = BossType::NONE);

    // Hit/death variants; fall back to the normal portrait.
    const Art& getHitArt(EnemyType type, BossType boss = BossType::NONE);
    const Art& getDeathArt(EnemyType type, BossType boss = BossType::NONE);

    // Single portrait (View Enemy screen).
    void print(const Art& art, int indent = 6);

    // --- battle scene: knight on the left, enemy on the right ---

    // Full scene, both idle frames.
    void printBattle(EnemyType type, BossType boss = BossType::NONE);

    // Redraws the scene in place at console row startRow (menu idle tick).
    void animateBattleIdleAt(EnemyType type, BossType boss, int startRow);

    // Enemy attack: 3-frame windup/swing/impact. knightGuard renders the
    // knight in his shield-brace pose (set when the player has armor up).
    void printBattleAttack(EnemyType type, BossType boss = BossType::NONE, bool knightGuard = false);

    // Player damage lands: knight swings, enemy flashes with its hit face.
    // trailElem recolors the sword trail + impact spark by the attack's
    // elemental tag (FIRE/POISON/WIND); NONE/physical keeps the steel trail.
    void printBattleHit(EnemyType type, BossType boss = BossType::NONE, DamageType trailElem = DamageType::NONE);

    // DEFEND card: knight raises and braces his shield.
    void printBattleBlock(EnemyType type, BossType boss = BossType::NONE);

    // Ailment cast: knight's palm glows in the ailment's color.
    enum class CastGlow { POISON, BURN, STUN, WEAK };
    void printBattleCast(EnemyType type, BossType boss, CastGlow glow);

    // Ailment lands: the afflicted side flashes the status color for a beat.
    // Stacked ailments call this once per status, giving the 1s-per-color chain.
    void printBattleStatusFlash(EnemyType type, BossType boss, CastGlow glow, bool onEnemy);

    // Self-buff flash: strengthen = red, heal = light green.
    enum class SelfGlow { STRENGTH, HEAL };
    void printBattleSelfBuff(EnemyType type, BossType boss, SelfGlow glow);

    // Which status auras are currently active on one side of the battle scene.
    struct AuraFlags {
        bool strength = false;
        bool weak     = false;
        bool poison   = false;
        bool burn     = false;
        bool stun     = false;
    };

    // Persistent auras while a status lasts, e.g. the knight glows red under
    // Strength, blue while Weakened. When a side carries more than one status
    // at once, its glow cycles through each active color every 2 seconds.
    // Set before drawing the scene.
    void setBattleAuras(AuraFlags knight, AuraFlags enemy);

    // Picks the backdrop for this encounter (a new environment every 10
    // encounters, cycling after the last).
    void setBattleBackdrop(int encounterNumber);

    // Day-forest scene used only by the tutorial fight.
    void setTutorialBackdrop();

    // Picks a per-name sprite (beasts, undead, the wyvern) by matching the
    // enemy's name. Call at encounter start; names without their own sheet
    // fall back to their type's sprite.
    void setEnemyVariant(const std::string& enemyName);

    // Enemy defeated: enemy darkens into its death pose.
    void printBattleDeath(EnemyType type, BossType boss = BossType::NONE);

    // Enemy's blow lands: knight recoils and flashes.
    void printBattleKnightHit(EnemyType type, BossType boss = BossType::NONE);

    // Player defeated: knight slumps, darkened.
    void printBattleKnightDeath(EnemyType type, BossType boss = BossType::NONE);
}
