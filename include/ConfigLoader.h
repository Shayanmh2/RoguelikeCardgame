#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "Card.h"

class ConfigLoader {
public:
    struct CardData {
        std::string name;
        std::string description;
        std::string type;
        std::string effect;
        std::string physType;  // optional: "SMASH" / "PIERCE"
        std::string physType2; // optional: second physical tag - only Finishing Blow uses both
        std::string elemType;  // optional: "FIRE" / "POISON" / "WIND"
        bool superRare = false; // optional: marks a standout rare card for a distinct tint
        bool legendary = false; // optional: marks the single legendary-tier card (Dodge Reversal)
        int cost;
        int value;
    };

    static std::vector<CardData> loadCommonCards(const std::string& configPath);
    static std::vector<CardData> loadRareCards(const std::string& configPath);

private:
    static std::string trim(const std::string& str);
    static CardData parseCard(const std::string& cardStr);
};
