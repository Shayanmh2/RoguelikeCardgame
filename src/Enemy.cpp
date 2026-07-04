#include "Enemy.h"
#include <iostream>
#include <vector>

Enemy::Enemy(std::string n, int hp, int atk, int def, EnemyType t)
    : name(n), health(hp), maxHealth(hp), baseAttack(atk), baseDefense(def),
      armor(0), type(t), bossType(BossType::NONE), bonusAttack(0) {}

EnemyType Enemy::getType() const { return type; }

std::string Enemy::getName() const {
    return name;
}

int Enemy::getHealth() const {
    return health;
}

int Enemy::getMaxHealth() const {
    return maxHealth;
}

int Enemy::getBaseAttack() const {
    return baseAttack;
}

int Enemy::getBaseDefense() const {
    return baseDefense;
}

int Enemy::getArmor() const {
    return armor;
}

void Enemy::takeDamage(int damage) {
    int actualDamage = damage - armor;
    if (actualDamage < 0) actualDamage = 0;
    armor -= damage;
    if (armor < 0) armor = 0;
    health -= actualDamage;
    if (health < 0) health = 0;
}

void Enemy::takeDamageRaw(int damage) {
    health -= damage;
    if (health < 0) health = 0;
}

void Enemy::heal(int amount) {
    health += amount;
    if (health > maxHealth) {
        health = maxHealth;
    }
}

void Enemy::gainArmor(int amount) {
    armor += amount;
}

void Enemy::resetArmor() {
    armor = 0;
}

bool     Enemy::isBoss()          const { return bossType != BossType::NONE; }
BossType Enemy::getBossType()     const { return bossType; }
void     Enemy::setBossType(BossType bt) { bossType = bt; }
int      Enemy::getBonusAttack()  const { return bonusAttack; }
void     Enemy::addBonusAttack(int a)   { bonusAttack += a; }

void Enemy::applyStatus(StatusType type, int amount) { statusEffects.apply(type, amount); }
int  Enemy::processPoison()  { return statusEffects.processPoison(); }
int  Enemy::processBurn()    { return statusEffects.processBurn(); }
bool Enemy::processStun()    { return statusEffects.processStun(); }
int  Enemy::getWeakPenalty() const { return statusEffects.getWeakPenalty(); }
void Enemy::processWeak()    { statusEffects.processWeak(); }
bool Enemy::hasStatusEffects() const { return statusEffects.hasAny(); }
void Enemy::displayStatusEffects(const std::string& prefix) const { statusEffects.display(prefix); }
std::string Enemy::statusSummary() const { return statusEffects.summary(); }

bool Enemy::isAlive() const {
    return health > 0;
}

DamageType Enemy::getWeakness() const {
    // Fixed per archetype so it's learnable: bring the right card for the enemy.
    // Bosses reuse their base EnemyType, so this covers them for free.
    switch (type) {
        case EnemyType::MELEE:  return DamageType::PIERCE; // lightly armored, mobile — precise strikes find the gaps
        case EnemyType::TANK:   return DamageType::SMASH;  // heavy armor doesn't stop blunt trauma
        case EnemyType::RANGED: return DamageType::WIND;   // agile skirmishers get knocked off balance
        case EnemyType::CASTER: return DamageType::FIRE;   // robes and concentration both burn easily
        case EnemyType::BEAST:  return DamageType::POISON; // no resistance built up to toxins
        case EnemyType::UNDEAD: return DamageType::FIRE;    // fire is the classic answer to the restless dead
        default:                return DamageType::NONE;
    }
}

std::string Enemy::getWeaknessLabel() const {
    return damageTypeName(getWeakness());
}

DamageType Enemy::getResistance() const {
    // Only some archetypes get one — not every enemy needs a hard counter-counter.
    switch (type) {
        case EnemyType::TANK:   return DamageType::PIERCE; // armor plating shrugs off precise strikes
        case EnemyType::UNDEAD: return DamageType::POISON; // nothing left alive in there for poison to work on
        default:                return DamageType::NONE;
    }
}

std::string Enemy::getResistanceLabel() const {
    return damageTypeName(getResistance());
}

void Enemy::displayStatus() const {
    std::cout << name << " - Health: " << health << "/" << maxHealth 
              << " | Armor: " << armor << " | Attack: " << baseAttack << " | Defense: " << baseDefense << "\n";
}

std::string Enemy::generateName(EnemyType type, int encounter) {
    // Unique themed names per type, with tier-based prefixes for higher encounters
    std::string prefix = "";
    if (encounter > 10) prefix = "Tyrant ";
    else if (encounter > 5) prefix = "Greater ";
    
    switch (type) {
        case EnemyType::MELEE: {
            std::vector<std::string> names = {"Goblin", "Orc", "Bandit", "Brute", "Warrior", "Barbarian", "Gladiator", "Enforcer"};
            return prefix + names[(encounter - 1) % names.size()];
        }
        case EnemyType::RANGED: {
            std::vector<std::string> names = {"Archer", "Scout", "Ranger", "Marksman", "Hunter", "Sniper", "Bowmaster", "Sharpshooter"};
            return prefix + names[(encounter - 1) % names.size()];
        }
        case EnemyType::TANK: {
            std::vector<std::string> names = {"Knight", "Guardian", "Paladin", "Colossus", "Fortress", "Sentinel", "Bastion", "Warden"};
            return prefix + names[(encounter - 1) % names.size()];
        }
        case EnemyType::CASTER: {
            std::vector<std::string> names = {"Wizard", "Sage", "Sorcerer", "Warlock", "Enchanter", "Mystic", "Archon", "Spellmaster"};
            return prefix + names[(encounter - 1) % names.size()];
        }
        case EnemyType::BEAST: {
            std::vector<std::string> names = {"Wolf", "Spider", "Serpent", "Wyvern", "Basilisk", "Chimera", "Manticore", "Direwolf"};
            return prefix + names[(encounter - 1) % names.size()];
        }
        case EnemyType::UNDEAD: {
            std::vector<std::string> names = {"Skeleton", "Zombie", "Wraith", "Ghoul", "Lich", "Revenant", "Banshee", "Specter"};
            return prefix + names[(encounter - 1) % names.size()];
        }
        default:
            return prefix + "Enemy";
    }
}
