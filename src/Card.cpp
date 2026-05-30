#include "Card.h"
#include <iostream>

Card::Card(std::string n, std::string desc, CardType t, int c, int v)
    : name(n), description(desc), type(t), cost(c), value(v) {}

std::string Card::getName() const {
    return name;
}

std::string Card::getDescription() const {
    return description;
}

CardType Card::getType() const {
    return type;
}

std::string Card::getTypeString() const {
    switch(type) {
        case CardType::ATTACK: return "ATTACK";
        case CardType::DEFEND: return "DEFEND";
        case CardType::SPECIAL: return "SPECIAL";
        default: return "UNKNOWN";
    }
}

int Card::getCost() const {
    return cost;
}

int Card::getValue() const {
    return value;
}

void Card::display() const {
    std::string typeStr;
    switch(type) {
        case CardType::ATTACK: typeStr = "ATTACK"; break;
        case CardType::DEFEND: typeStr = "DEFEND"; break;
        case CardType::SPECIAL: typeStr = "SPECIAL"; break;
    }
    
    std::cout << "[" << name << "] (" << typeStr << ")\n"
              << "  Cost: " << cost << " | Value: " << value << "\n"
              << "  " << description << "\n";
}
