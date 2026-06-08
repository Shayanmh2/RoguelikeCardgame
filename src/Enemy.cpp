#include "Enemy.h"
#include <iostream>
#include <vector>

Enemy::Enemy(std::string n, int hp, int atk, int def, EnemyType t)
    : name(n), health(hp), maxHealth(hp), baseAttack(atk), baseDefense(def), armor(0), type(t) {}

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

void Enemy::applyStatus(StatusType type, int amount) { statusEffects.apply(type, amount); }
int  Enemy::processPoison()  { return statusEffects.processPoison(); }
int  Enemy::processBurn()    { return statusEffects.processBurn(); }
bool Enemy::processStun()    { return statusEffects.processStun(); }
int  Enemy::getWeakPenalty() const { return statusEffects.getWeakPenalty(); }
void Enemy::processWeak()    { statusEffects.processWeak(); }
bool Enemy::hasStatusEffects() const { return statusEffects.hasAny(); }
void Enemy::displayStatusEffects(const std::string& prefix) const { statusEffects.display(prefix); }

bool Enemy::isAlive() const {
    return health > 0;
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
        default:
            return prefix + "Enemy";
    }
}
