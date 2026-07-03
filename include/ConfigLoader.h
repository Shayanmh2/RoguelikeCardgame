#pragma once

#include <string>
#include <vector>
#include <unordered_map>
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
        std::string physType; // optional: "SMASH" / "PIERCE"
        std::string elemType; // optional: "FIRE" / "POISON" / "WIND"
        int cost;
        int value;
    };

    struct EnemyTypeData {
        std::string type;
        int baseAttackProb;
        int baseDefendProb;
        int healThreshold;
        std::vector<std::string> names;
    };

    static std::vector<CardData> loadCommonCards(const std::string& configPath);
    static std::vector<CardData> loadRareCards(const std::string& configPath);
    static std::vector<EnemyTypeData> loadEnemyTypes(const std::string& configPath);
    static std::unordered_map<std::string, std::string> loadTierPrefixes(const std::string& configPath);

private:
    static std::string trim(const std::string& str);
    static std::string extractValue(const std::string& line, const std::string& key);
    static std::vector<std::string> extractArray(const std::string& content, size_t& pos);
    static CardData parseCard(const std::string& cardStr);
};
