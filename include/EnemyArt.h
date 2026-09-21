#pragma once

#include "Enemy.h"
#include <vector>
#include <string>

// Battle sprites. The terminal build rendered these as 24-bit color
// half-blocks (U+2580); this build draws the same PNG sheets as real textures
// through SDL, at a proper on-screen scale, with the sword-trail and cast
// overlays composited on top. The interface is unchanged so Game.cpp compiles
// against it untouched.
// For drawItemIcon. Forward declared so this header stays free of SDL.h.
struct SDL_Renderer;
struct SDL_Rect;

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
    // The knight alone, in the gear he is wearing: View Player's portrait.
    void printPlayerPortrait();

    // --- battle scene: knight on the left, enemy on the right ---

    void printBattle(EnemyType type, BossType boss = BossType::NONE);

    // Redraws the scene in place (menu idle tick), at the row printBattle()
    // last drew it - tracked internally so it can't drift out of sync with
    // wherever the console actually put it.
    void animateBattleIdleAt(EnemyType type, BossType boss);

    // Enemy attack: 3-frame windup/swing/impact. knightGuard renders the
    // knight in his shield-brace pose (set when the player has armor up).
    // Three flags that disagree for some enemies:
    //   ranged          - does the attacker close the distance, or shoot from where
    //                     it stands (also decides whether Parry has a riposte)
    //   useAttackFrames - does it play its ATK pose. The Vampire's frames show her
    //                     casting, so her bite closes without them.
    //   closeIn         - overrides the first flag for movement only. A diving
    //                     flyer crosses the field like a brawler but still throws
    //                     nothing and still leaves Parry with nothing to riposte.
    void printBattleAttack(EnemyType type, BossType boss = BossType::NONE, bool knightGuard = false,
                           bool ranged = false, bool useAttackFrames = true,
                           int projectile = -1, int muzzleX = -1, int muzzleY = -1,
                           bool closeIn = false);   // Proj index, -1 = pick by archetype

    // projectiles.png, one per KIND of move rather than one per archetype.
    namespace Proj {
        // Frame indices live in include/ProjectileTable.h, which is generated from
        // the sprites alongside the sheet itself. Any negative value means nothing
        // crosses the gap.
        enum { NONE = -2 };
    }

    // Player damage lands: knight swings, enemy flashes with its hit face.
    // trailElem recolors the sword trail + impact spark by the attack's
    // elemental tag (FIRE/POISON/WIND); NONE/physical keeps the steel trail.
    // connected=false still swings and draws the trail, but skips the enemy's
    // flinch and hit-flash: the attack happened, it just did no damage.
    // onCompanion: the blow lands on the summoned add standing in front of the
    // enemy (the Lich's skeleton), so IT flashes and sparks, not its master.
    void printBattleHit(EnemyType type, BossType boss = BossType::NONE, DamageType trailElem = DamageType::NONE,
                        bool connected = true, bool onCompanion = false);

    // DEFEND card: knight raises and braces his shield.
    void printBattleBlock(EnemyType type, BossType boss = BossType::NONE);
    // Shield Bash: shield raised and driven into the enemy. It deals damage, so
    // unlike a plain block the knight closes the distance to deliver it.
    void printBattleShieldBash(EnemyType type, BossType boss = BossType::NONE,
                               bool connected = true);

    // Ailment cast: knight's palm glows in the ailment's color.
    enum class CastGlow { POISON, BURN, STUN, WEAK, REND };
    void printBattleCast(EnemyType type, BossType boss, CastGlow glow);
    // The enemy casting or shooting something that is not a plain attack: its
    // attack frames, then a bolt across the field.
    // projectile: a Proj index for the art that crosses. -1 keeps the status orb,
    // which is right for an ailment with no art of its own.
    // scalePct: how big the thing that crosses the field is drawn, as a
    // percentage of a sprite. 30 is a bolt; a dragon's breath needs far more.
    void printEnemyCast(EnemyType type, BossType boss, CastGlow glow, int projectile = -1,
                        int muzzleX = -1, int muzzleY = -1, int scalePct = 30);

    // A beam, which is not a projectile: it stays attached to the eye that fires
    // it and reaches across the field, rather than travelling as an object.
    // weakGlow washes the ray in the Weak colour, for a gaze that saps rather
    // than burns. The art is the same ray either way.
    void printEnemyBeam(EnemyType type, BossType boss, int projectile,
                        int muzzleX = -1, int muzzleY = -1, bool weakGlow = false);

    // Flame climbing out of the floor across the whole arena, the Archon's own
    // attack frames spread over the ground between the two fighters.
    void printGroundFlames(EnemyType type, BossType boss, int projectile);

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
        bool rend     = false;
        bool stun     = false;
    };

    // Persistent auras while a status lasts, e.g. the knight glows red under
    // Strength, blue while Weakened. When a side carries more than one status
    // at once, its glow cycles through each active color every 2 seconds.
    // Set before drawing the scene.
    void setBattleAuras(AuraFlags knight, AuraFlags enemy);


    // The gear the knight is carrying, as tier counts (0 = nothing taken yet).
    // Armour recolours his plate; the weapon recolours the trail his blade
    // leaves, which is the only part of the sword that is its own art. A status
    // flash or an aura still wins: this is the colour he rests at.
    void setGearTiers(int weaponTiers, int armorTiers);

    // Ghost/Illusion: render the enemy faded and spectral (Mystic, Specter,
    // Wraith while invulnerable). Set before drawing the scene, cleared after.
    void setEnemyGhost(bool on);

    // Build the sprite library up front so the first fight does not pay for it.
    void preload();

    // Title screen backdrop. Draws under the text.
    void setTitleMode(bool on);
    // The seal offered after a boss, drawn over the screen: -1 hides it,
    // 0 is whole, 1-3 crack it, 4 is broken.
    void setSealFrame(int frame);
    // Item icons from items.png: swords 0-6 by gear tier, shields 7-13.
    void drawItemIcon(SDL_Renderer* r, int index, const SDL_Rect& dst);

// Drawn text: the title wordmark and the headline banners, art rather than a
// face. `name` is the file stem under assets/sprites. Size returns false when
// that art is missing, which is the caller's cue to set the words in text
// rather than show nothing.
bool wordmarkSize(const std::string& name, int& w, int& h);
void drawWordmark(const std::string& name, const SDL_Rect& dst);

    // Floating combat numbers and impact sparks. Fire-and-forget: each effect
    // owns its lifetime, so nothing has to tick or clear them.
    enum class PopKind { DAMAGE, HEAL, BLOCKED, WEAK_HIT };
    void popNumber(int amount, bool onEnemy, PopKind kind = PopKind::DAMAGE);
    // Over the summoned add, which has its own health bar to track.
    void popNumberAdd(int amount, PopKind kind);
    void popSparks(bool onEnemy);

    // Picks the backdrop for this encounter (a new environment every 10
    // encounters, cycling after the last).
    void setBattleBackdrop(int encounterNumber);

    // The ??? encounter: the forest under a blood moon.
    // The Moonstruck's crimson sky for the area it appears in (0-4).
    void setSecretBackdrop(int zone = 2);

    // Tutorial fight only.
    void setTutorialBackdrop();

    // Picks a per-name sprite (beasts, undead, the wyvern) by matching the
    // enemy's name. Call at encounter start; names without their own sheet
    // fall back to their type's sprite.
    void setEnemyVariant(const std::string& enemyName);

    // A summoned add drawn beside the enemy, one scale step down. Empty key
    // clears it.
    void setCompanion(const std::string& spriteKey);
    // The add takes its own turn: it steps out at the knight and back.
    void printCompanionAttack(EnemyType type, BossType boss = BossType::NONE);

    void printBattleDeath(EnemyType type, BossType boss = BossType::NONE);

    void printBattleKnightHit(EnemyType type, BossType boss = BossType::NONE);

    void printBattleKnightDeath(EnemyType type, BossType boss = BossType::NONE);
}
