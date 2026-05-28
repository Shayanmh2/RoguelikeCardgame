#ifndef ENEMY_H
#define ENEMY_H

#include <string>

class Enemy {
private:
    std::string name;
    int health;
    int maxHealth;
    int baseAttack;
    int baseDefense;
    int armor;

public:
    Enemy(std::string n, int hp, int atk, int def);
    
    std::string getName() const;
    int getHealth() const;
    int getMaxHealth() const;
    int getBaseAttack() const;
    int getBaseDefense() const;
    int getArmor() const;
    
    void takeDamage(int damage);
    void heal(int amount);
    void gainArmor(int amount);
    void resetArmor();
    
    bool isAlive() const;
    void displayStatus() const;
};

#endif
