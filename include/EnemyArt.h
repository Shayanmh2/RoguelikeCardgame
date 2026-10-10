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
    // View Player and View Enemy lay their screen out to their text: the
    // portrait's band is capped to `rows`, the figure scaled down to fit it,
    // and 0 leaves the portrait out. -1 lifts the cap.
    void setPortraitRows(int rows);

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

    // The False Moon casting Moonstruck: it flares crimson twice, a crimson
    // wisp crosses to the knight, and the moon's red takes him, to a sound
    // of its own (sounds/moonstruck).
    void printBattleMoonstruck(EnemyType type, BossType boss);

    struct AuraFlags {
        bool strength = false;
        bool weak     = false;
        bool poison   = false;
        bool burn     = false;
        bool rend     = false;
        bool stun     = false;
        bool moonstruck = false;   // the False Moon's clock is running on him
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
    // The Gargoyle's stone turns: the sprite goes a cold, dull grey.
    void setEnemyStone(bool on);

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
    // The cutscenes over the top of the screen, a shot to each line of text:
    // the opening at the pond and the ending on the peak
    // (tools/make_intro_scene.py, tools/make_ending_scene.py).
    enum class Cutscene { INTRO, ENDING };
    // The ending without its moon, for the run that put the False Moon out in
    // the church (ending_scene_moonless.png, when it is there).
    void setEndingMoonless(bool on);
    // Puts a shot up; -1 takes the cutscene down.
    void setCutsceneShot(Cutscene scene, int shot);
    // False when its art is missing, so the words can stand on their own.
    bool cutsceneReady(Cutscene scene);
    // True while the shot is still playing its way in, before its loop.
    bool cutsceneShotPlaying();
    // Straight to the shot's loop, without the sounds it would have made.
    void finishCutsceneShot();
    // The bottom edge of the picture in pixels, so the words can go under it.
    int cutsceneBottom();
    // Item icons from items.png: swords 0-6 by gear tier, shields 7-13.
    // From MEDAL_ICON0 on, the achievement medals instead.
    void drawItemIcon(SDL_Renderer* r, int index, const SDL_Rect& dst);
    // medals.png: bronze, silver, gold, then the locked one.
    constexpr int MEDAL_ICON0 = 100, MEDAL_LOCKED = 3;
    void drawMedal(int frame, const SDL_Rect& dst);

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
    // And over your own raised dead.
    void popNumberAlly(int amount, PopKind kind);
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

    // Your own raised dead (Raise Undead): drawn just in front of the knight,
    // one scale step down and turned round to face the enemy. Empty key clears
    // it. It rises, takes its turn (connected: the blow landed; onCompanion:
    // on the Lich's add rather than its master), takes a blow, and falls.
    void setAlly(const std::string& spriteKey);
    void printAllyRise(EnemyType type, BossType boss = BossType::NONE);
    void printAllyAttack(EnemyType type, BossType boss, bool connected, bool onCompanion);
    void printAllyHit(EnemyType type, BossType boss = BossType::NONE);
    void printAllyFall(EnemyType type, BossType boss = BossType::NONE);
    // Set while a blow is on its way to your raised dead rather than to you:
    // a shot, a beam or the Archon's flames end on them.
    void setBlowsAtAlly(bool on);

    void printBattleDeath(EnemyType type, BossType boss = BossType::NONE);
    // The true form running from the peak in the church's run: it reels, then
    // draws back into the dark and fades out instead of dying.
    void printBattleFlee(EnemyType type, BossType boss);
    // The False Moon out of the eclipse: prepareArrival as the backdrop goes
    // up (true when it will come, the church under its plain moon and the
    // False Moon hidden), then printBattleArrival once the fight is on
    // screen, starting `track` as it takes shape.
    bool prepareArrival(BossType boss);
    void printBattleArrival(EnemyType type, BossType boss, const char* track);

    void printBattleKnightHit(EnemyType type, BossType boss = BossType::NONE);

    void printBattleKnightDeath(EnemyType type, BossType boss = BossType::NONE);
    // The False Moon takes him for its vessel: the last piece of him rises, it
    // comes apart and crosses to him, the dark spreads through him and he
    // stands as it wears him; the eclipse ends. In place of his death there.
    void printBattleVessel(EnemyType type, BossType boss);
}
