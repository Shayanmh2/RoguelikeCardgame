#ifndef CARD_H
#define CARD_H

#include <string>

enum class CardType {
    ATTACK,
    DEFEND,
    SPECIAL
};

class Card {
private:
    std::string name;
    std::string description;
    CardType type;
    int cost;
    int value;

public:
    Card(std::string n, std::string desc, CardType t, int c, int v);
    
    std::string getName() const;
    std::string getDescription() const;
    CardType getType() const;
    int getCost() const;
    int getValue() const;
    
    void display() const;
};

#endif
