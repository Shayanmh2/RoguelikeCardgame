#ifndef ENEMY_H
#define ENEMY_H

#include "StatusEffect.h"
#include "DamageType.h"
#include <string>

enum class EnemyType  { MELEE, RANGED, TANK, CASTER, BEAST, UNDEAD };
enum class BossType   { NONE, STONE_COLOSSUS, VILE_WITCH, WARLORD, HYDRA, DRAGON, SHADOW_KNIGHT, FALSE_MOON };

class Enemy {
private:
    std::string name;
    int health;
    int maxHealth;
    int baseAttack;
    int baseDefense;
    int armor;
    EnemyType type;
    BossType  bossType;
    int       bonusAttack;   // used by Warlord rage
    StatusEffects statusEffects;
    int       hitDamage = 0;   // health lost to hits, see hitDamageTaken()
    bool       typesReversed = false;          // Turnabout
    DamageType blowType = DamageType::NONE;    // Feint
    DamageType baseWeakness() const;           // by archetype, before Turnabout
    DamageType baseResistance() const;

public:
    Enemy(std::string n, int hp, int atk, int def, EnemyType t = EnemyType::MELEE);

    std::string getName() const;
    EnemyType getType() const;
    int getHealth() const;
    int getMaxHealth() const;
    int getBaseAttack() const;
    int getBaseDefense() const;
    int getArmor() const;

    void takeDamage(int damage);
    void takeDamageRaw(int damage); // bypasses armor
    // Poison, burn and rend: the same damage as the two above, only not
    // counted as a hit, so a kill they make on their own can be told apart.
    void takeDamageOverTime(int damage, bool throughArmor);
    // A price it pays in its own health (the true form's bargains). Not a hit.
    void payHealth(int amount);
    // The health it has lost to hits: blows, ripostes, reversals, chip.
    int  hitDamageTaken() const { return hitDamage; }
    void heal(int amount);
    void gainArmor(int amount);
    void resetArmor();

    // Status effect interface
    void applyStatus(StatusType type, int amount, double weakMultiplier = 1.5,
                     double strengthMultiplier = 1.2);
    int  statusRemaining(StatusType type) const { return statusEffects.remaining(type); }
    // What one of its ailments still has to deal, and clearing it.
    int  pendingStatus(StatusType type) const { return statusEffects.pending(type); }
    void clearStatus(StatusType type) { statusEffects.clear(type); }
    // Everything on it, gone. Only the Paladin does this, and only to itself.
    bool hasAnyStatus() const { return statusEffects.hasAny(); }
    void clearStatuses() { statusEffects.reset(); }
    int  processPoison();
    int  processBurn();
    int  processRend();  // spent when this enemy attacks, not on the turn tick
    bool processStun();
    double getWeakMultiplier() const;
    void processWeak();
    // Outgoing damage multiplier while the enemy is buffed. Nothing read
    // this before Moon Scent: only the player could be Strengthened.
    double getStrengthMultiplier() const;
    bool   hasStrength() const;
    void   processStrength();
    bool hasPoison() const;
    bool hasBurn() const;
    bool hasRend() const;
    bool hasStun() const;
    bool hasWeak() const;

    // Attempts to stun this enemy for 1 turn. Bosses resist 50% of the time,
    // `luck` points less (returns false and applies nothing when resisted).
    bool tryApplyStun(int luck = 0);
    void displayStatusEffects(const std::string& prefix) const;
    std::string statusSummary() const;

    // Boss interface
    bool      isBoss() const;
    BossType  getBossType() const;
    void      setBossType(BossType bt);
    int       getBonusAttack() const;
    void      addBonusAttack(int amount);

    bool isAlive() const;

    // Matching weakness = +50% damage; matching resistance = -50% damage.
    // Turnabout swaps the two for the rest of the fight.
    DamageType  getWeakness() const;
    std::string getWeaknessLabel() const; // e.g. "Pierce", empty if none
    DamageType  getResistance() const;
    std::string getResistanceLabel() const; // e.g. "Pierce", empty if none
    void reverseTypes() { typesReversed = !typesReversed; }
    bool typesAreReversed() const { return typesReversed; }
    // Feint: the type its blows land as, NONE for its own.
    void       setBlowType(DamageType t) { blowType = t; }
    DamageType blowTypeOverride() const { return blowType; }
};

#endif
