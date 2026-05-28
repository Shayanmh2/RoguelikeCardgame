#include "Enemy.h"
#include <iostream>

Enemy::Enemy(std::string n, int hp, int atk, int def) 
    : name(n), health(hp), maxHealth(hp), baseAttack(atk), baseDefense(def), armor(0) {}

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
    if (actualDamage < 0) {
        actualDamage = 0;
    }
    armor -= damage;
    if (armor < 0) {
        armor = 0;
    }
    health -= actualDamage;
    if (health < 0) {
        health = 0;
    }
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

bool Enemy::isAlive() const {
    return health > 0;
}

void Enemy::displayStatus() const {
    std::cout << name << " - Health: " << health << "/" << maxHealth 
              << " | Armor: " << armor << " | Attack: " << baseAttack << " | Defense: " << baseDefense << "\n";
}
