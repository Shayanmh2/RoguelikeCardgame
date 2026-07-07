#ifndef ENEMY_H
#define ENEMY_H

#include "StatusEffect.h"
#include "DamageType.h"
#include <string>

enum class EnemyType  { MELEE, RANGED, TANK, CASTER, BEAST, UNDEAD };
enum class BossType   { NONE, STONE_COLOSSUS, VILE_WITCH, WARLORD, HYDRA, DRAGON };

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
    void takeDamageRaw(int damage); // bypasses armor (for DOT effects)
    void heal(int amount);
    void gainArmor(int amount);
    void resetArmor();

    // Status effect interface
    void applyStatus(StatusType type, int amount);
    int  processPoison();
    int  processBurn();
    bool processStun();
    int  getWeakPenalty() const;
    void processWeak();
    void displayStatusEffects(const std::string& prefix) const;
    std::string statusSummary() const;

    // Boss interface
    bool      isBoss() const;
    BossType  getBossType() const;
    void      setBossType(BossType bt);
    int       getBonusAttack() const;
    void      addBonusAttack(int amount);

    bool isAlive() const;

    // Matching weakness = +50% damage; matching resistance = -50% damage
    DamageType  getWeakness() const;
    std::string getWeaknessLabel() const; // e.g. "Pierce", empty if none
    DamageType  getResistance() const;
    std::string getResistanceLabel() const; // e.g. "Pierce", empty if none

    static std::string generateName(EnemyType type, int encounter);
};

#endif
