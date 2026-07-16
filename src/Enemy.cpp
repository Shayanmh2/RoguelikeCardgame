#include "Enemy.h"
#include <iostream>
#include <vector>
#include <random>

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

void Enemy::applyStatus(StatusType type, int amount, double weakMultiplier) { statusEffects.apply(type, amount, weakMultiplier); }
int  Enemy::processPoison()  { return statusEffects.processPoison(); }
int  Enemy::processBurn()    { return statusEffects.processBurn(); }
bool Enemy::processStun()    { return statusEffects.processStun(); }
double Enemy::getWeakMultiplier() const { return statusEffects.getWeakMultiplier(); }
void Enemy::processWeak()    { statusEffects.processWeak(); }
bool Enemy::hasPoison() const { return statusEffects.hasPoison(); }
bool Enemy::hasBurn()   const { return statusEffects.hasBurn(); }
bool Enemy::hasStun()   const { return statusEffects.hasStun(); }
bool Enemy::hasWeak()   const { return statusEffects.hasWeak(); }
void Enemy::displayStatusEffects(const std::string& prefix) const { statusEffects.display(prefix); }
std::string Enemy::statusSummary() const { return statusEffects.summary(); }

bool Enemy::tryApplyStun() {
    if (isBoss()) {
        static thread_local std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<> dist(0, 99);
        if (dist(gen) < 50) return false; // resisted
    }
    statusEffects.apply(StatusType::STUN, 1);
    return true;
}

bool Enemy::isAlive() const {
    return health > 0;
}

DamageType Enemy::getWeakness() const {
    // Fixed per archetype (bosses reuse their base EnemyType, so this covers them too)
    switch (type) {
        case EnemyType::MELEE:  return DamageType::PIERCE;
        case EnemyType::TANK:   return DamageType::SMASH;
        case EnemyType::RANGED: return DamageType::WIND;
        case EnemyType::CASTER: return DamageType::FIRE;
        case EnemyType::BEAST:  return DamageType::POISON;
        case EnemyType::UNDEAD: return DamageType::FIRE;
        default:                return DamageType::NONE;
    }
}

std::string Enemy::getWeaknessLabel() const {
    return damageTypeName(getWeakness());
}

DamageType Enemy::getResistance() const {
    // Not every archetype has one
    switch (type) {
        case EnemyType::TANK:   return DamageType::PIERCE;
        case EnemyType::UNDEAD: return DamageType::POISON;
        default:                return DamageType::NONE;
    }
}

std::string Enemy::getResistanceLabel() const {
    return damageTypeName(getResistance());
}
