#pragma once

#include "Enemy.h"
#include <vector>
#include <string>

// Battle sprites: the PNG sheets drawn as SDL textures, with the sword-trail
// and cast overlays composited on top.
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

    // Enemy attack: windup, swing, impact. knightGuard shows the knight's shield
    // brace (he has armour up). Three flags that differ between enemies:
    //   ranged          - shoots from where it stands; Parry has nothing to riposte
    //   useAttackFrames - plays its ATK pose (the Vampire's bite closes without it)
    //   closeIn         - crosses the field like a brawler, for a diving flyer,
    //                     while staying ranged for every other rule
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

    // The knight's blow lands and the enemy flashes. trailElem colours the sword
    // trail and spark by element. connected=false swings without the flinch, for
    // a blow that did no damage. onCompanion lands it on the summoned add in
    // front (the Lich's skeleton) instead of its master.
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
    // A cast or shot that is not a plain attack: attack frames, then a bolt.
    // projectile is a Proj index (-1 is the plain status orb); scalePct is its
    // size as a percentage of a sprite (30 for a bolt, far more for breath).
    void printEnemyCast(EnemyType type, BossType boss, CastGlow glow, int projectile = -1,
                        int muzzleX = -1, int muzzleY = -1, int scalePct = 30);

    // A beam stays attached to the eye that fires it instead of travelling.
    // weakGlow tints it the Weak colour, for a gaze that saps rather than burns.
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

    // Glows that last as long as a status does (red under Strength, blue while
    // Weak). Several at once cycle every 2 seconds. Set before drawing the scene.
    void setBattleAuras(AuraFlags knight, AuraFlags enemy);


    // The gear the knight is wearing, as tiers (0 = the starting kit): each picks
    // a row of the armour and weapon sheets he is drawn from.
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
    // The knight sat at his fire, in the armour he is wearing, over the top of
    // the screen. -1 takes it down.
    void setRestScene(int armorTier);
    // Item icons from items.png: swords 0-6 by gear tier, shields 7-13.
    void drawItemIcon(SDL_Renderer* r, int index, const SDL_Rect& dst);

// The title wordmark and headline banners, drawn as art. `name` is the file
// stem under assets/sprites; false means the art is missing, so use text.
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
    // The Shadow Knight becoming what was wearing it: the sprite bleaches to
    // white, the true form comes up inside the glare, and the backdrop
    // crossfades to the final arena underneath.
    void transformToTrueForm(int zone, const std::string& trueName);

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
