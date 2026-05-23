#include "Enemy.h"
#include <iostream>

Enemy::Enemy(std::string n, int hp, int atk, int def) 
    : name(n), health(hp), maxHealth(hp), baseAttack(atk), baseDefense(def) {}

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

void Enemy::takeDamage(int damage) {
    health -= damage;
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

bool Enemy::isAlive() const {
    return health > 0;
}

void Enemy::displayStatus() const {
    std::cout << name << " - Health: " << health << "/" << maxHealth 
              << " | Attack: " << baseAttack << " | Defense: " << baseDefense << "\n";
}
