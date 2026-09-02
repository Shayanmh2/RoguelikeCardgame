#pragma once

#include "Enemy.h"
#include <vector>
#include <string>

// Battle sprites. The terminal build rendered these as 24-bit color
// half-blocks (U+2580); this build draws the same PNG sheets as real textures
// through SDL, at a proper on-screen scale, with the sword-trail and cast
// overlays composited on top. The interface is unchanged so Game.cpp compiles
// against it untouched.
namespace EnemyArt {
    struct RGB { unsigned char r, g, b; };

    // A single frame of a loaded sheet. Opaque handle - the pixels live in a
    // GPU texture now, so callers just pass this back to print().
    struct Art {
        const void* set = nullptr; // ArtSet the frame belongs to
        int frame = 0;
    };

    // Idle frame, alternating per call. Boss != NONE wins over EnemyType.
    const Art& getWalkFrame(EnemyType type, BossType boss = BossType::NONE);

    void print(const Art& art, int indent = 6);

    // --- battle scene: knight on the left, enemy on the right ---

    void printBattle(EnemyType type, BossType boss = BossType::NONE);

    // Redraws the scene in place (menu idle tick), at the row printBattle()
    // last drew it - tracked internally so it can't drift out of sync with
    // wherever the console actually put it.
    void animateBattleIdleAt(EnemyType type, BossType boss);

    // Enemy attack: 3-frame windup/swing/impact. knightGuard renders the
    // knight in his shield-brace pose (set when the player has armor up).
    void printBattleAttack(EnemyType type, BossType boss = BossType::NONE, bool knightGuard = false);

    // Player damage lands: knight swings, enemy flashes with its hit face.
    // trailElem recolors the sword trail + impact spark by the attack's
    // elemental tag (FIRE/POISON/WIND); NONE/physical keeps the steel trail.
    // connected=false still swings and draws the trail, but skips the enemy's
    // flinch and hit-flash: the attack happened, it just did no damage.
    void printBattleHit(EnemyType type, BossType boss = BossType::NONE, DamageType trailElem = DamageType::NONE,
                        bool connected = true);

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

    // Ghost/Illusion: render the enemy faded and spectral (Mystic, Specter,
    // Wraith while invulnerable). Set before drawing the scene, cleared after.
    void setEnemyGhost(bool on);

    // Build the sprite library up front so the first fight does not pay for it.
    void preload();

    // Title screen backdrop. Draws under the text.
    void setTitleMode(bool on);

    // Floating combat numbers and impact sparks. Fire-and-forget: each effect
    // owns its lifetime, so nothing has to tick or clear them.
    enum class PopKind { DAMAGE, HEAL, BLOCKED, WEAK_HIT };
    void popNumber(int amount, bool onEnemy, PopKind kind = PopKind::DAMAGE);
    void popSparks(bool onEnemy);

    // Picks the backdrop for this encounter (a new environment every 10
    // encounters, cycling after the last).
    void setBattleBackdrop(int encounterNumber);

    // Tutorial fight only.
    void setTutorialBackdrop();

    // Picks a per-name sprite (beasts, undead, the wyvern) by matching the
    // enemy's name. Call at encounter start; names without their own sheet
    // fall back to their type's sprite.
    void setEnemyVariant(const std::string& enemyName);

    // A summoned add drawn beside the enemy, one scale step down. Empty key
    // clears it.
    void setCompanion(const std::string& spriteKey);

    void printBattleDeath(EnemyType type, BossType boss = BossType::NONE);

    void printBattleKnightHit(EnemyType type, BossType boss = BossType::NONE);

    void printBattleKnightDeath(EnemyType type, BossType boss = BossType::NONE);
}
