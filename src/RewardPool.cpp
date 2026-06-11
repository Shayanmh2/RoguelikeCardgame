#include "RewardPool.h"
#include "Colors.h"
#include <random>
#include <iostream>
#include <filesystem>

RewardPool::RewardPool() {
    initializeCardPool();
}

void RewardPool::initializeCardPool() {
    // Try to load from config file
    std::string configPath = "config/cards.json";
    
    // Check if config file exists
    if (std::filesystem::exists(configPath)) {
        std::cout << "Loading cards from " << configPath << "...\n";
        
        auto commonData = ConfigLoader::loadCommonCards(configPath);
        auto rareData = ConfigLoader::loadRareCards(configPath);
        
        // Convert ConfigLoader data to Card objects
        for (const auto& data : commonData) {
            CardType type = CardType::ATTACK;
            if (data.type == "DEFEND") type = CardType::DEFEND;
            else if (data.type == "SPECIAL") type = CardType::SPECIAL;
            
            commonCards.push_back(Card(data.name, data.description, type, data.cost, data.value));
        }
        
        for (const auto& data : rareData) {
            CardType type = CardType::ATTACK;
            if (data.type == "DEFEND") type = CardType::DEFEND;
            else if (data.type == "SPECIAL") type = CardType::SPECIAL;
            
            rareCards.push_back(Card(data.name, data.description, type, data.cost, data.value));
        }
        
        std::cout << "Loaded " << commonCards.size() << " common cards and " << rareCards.size() << " rare cards.\n";
    } else {
        std::cout << "Warning: config/cards.json not found. Using default cards.\n";
        
        // Fallback to hardcoded cards
        commonCards.push_back(Card("Strike",       "Deal 5 damage",         CardType::ATTACK,  1, 5));
        commonCards.push_back(Card("Strike",       "Deal 5 damage",         CardType::ATTACK,  1, 5));
        commonCards.push_back(Card("Defend",       "Gain 5 armor",          CardType::DEFEND,  1, 5));
        commonCards.push_back(Card("Defend",       "Gain 5 armor",          CardType::DEFEND,  1, 5));
        commonCards.push_back(Card("Bash",         "Deal 8 damage",         CardType::ATTACK,  2, 8));
        commonCards.push_back(Card("Bash",         "Deal 8 damage",         CardType::ATTACK,  2, 8));
        // Common SPECIAL cards
        commonCards.push_back(Card("Poison Dart",  "Apply 3 Poison stacks", CardType::SPECIAL, 1, 3, CardEffect::POISON));
        commonCards.push_back(Card("Torch",        "Apply 2 Burn (5 dmg x2 turns)", CardType::SPECIAL, 1, 2, CardEffect::BURN));
        commonCards.push_back(Card("Stun Strike",  "Stun enemy for 1 turn", CardType::SPECIAL, 2, 1, CardEffect::STUN));
        commonCards.push_back(Card("Weaken",       "Apply 3 Weak (-2 atk x3 turns)", CardType::SPECIAL, 1, 3, CardEffect::WEAK));

        rareCards.push_back(Card("Power Strike",   "Deal 12 damage",        CardType::ATTACK,  3, 12));
        rareCards.push_back(Card("Iron Skin",      "Gain 12 armor",         CardType::DEFEND,  3, 12));
        rareCards.push_back(Card("Cleave",         "Deal 15 damage",        CardType::ATTACK,  3, 15));
        // Rare SPECIAL cards
        rareCards.push_back(Card("Toxic Cloud",    "Apply 6 Poison stacks", CardType::SPECIAL, 2, 6, CardEffect::POISON));
        rareCards.push_back(Card("Inferno",        "Apply 4 Burn (5 dmg x4 turns)", CardType::SPECIAL, 3, 4, CardEffect::BURN));
        rareCards.push_back(Card("Paralysis",      "Stun enemy for 1 turn", CardType::SPECIAL, 3, 1, CardEffect::STUN));
        rareCards.push_back(Card("Shatter",        "Apply 5 Weak (-2 atk x5 turns)", CardType::SPECIAL, 2, 5, CardEffect::WEAK));
    }
}


std::vector<Card> RewardPool::generateRewardChoices(int count) {
    std::vector<Card> choices;
    std::random_device rd;
    std::mt19937 gen(rd());

    std::vector<Card> allCards;
    allCards.insert(allCards.end(), commonCards.begin(), commonCards.end());
    allCards.insert(allCards.end(), rareCards.begin(), rareCards.end());

    for (int i = 0; i < count && !allCards.empty(); ++i) {
        // Recreate distribution each iteration so size stays valid after erasing
        std::uniform_int_distribution<> dis(0, (int)allCards.size() - 1);
        int index = dis(gen);
        choices.push_back(allCards[index]);
        allCards.erase(allCards.begin() + index);
    }

    return choices;
}

std::vector<Card> RewardPool::generateWeightedRewards(int encounterNumber, int count, bool rarityBoost) {
    std::vector<Card> choices;
    std::random_device rd;
    std::mt19937 gen(rd());

    int rareChance = 20 + (encounterNumber * 5);
    if (rarityBoost) rareChance += 20;
    if (rareChance > 80) rareChance = 80;

    // Local copies so we can remove without touching the pool
    std::vector<Card> commonPool = commonCards;
    std::vector<Card> rarePool = rareCards;

    std::uniform_int_distribution<> rollDis(1, 100);

    for (int i = 0; i < count; ++i) {
        bool pickRare = !rarePool.empty() && (rollDis(gen) <= rareChance || commonPool.empty());
        std::vector<Card>& pool = pickRare ? rarePool : commonPool;

        if (pool.empty()) break;

        std::uniform_int_distribution<> idxDis(0, (int)pool.size() - 1);
        int index = idxDis(gen);
        choices.push_back(pool[index]);
        pool.erase(pool.begin() + index);
    }

    return choices;
}

std::vector<Card> RewardPool::generateRareRewards(int count) {
    std::vector<Card> choices;
    std::random_device rd;
    std::mt19937 gen(rd());

    std::vector<Card> pool = rareCards;
    for (int i = 0; i < count && !pool.empty(); ++i) {
        std::uniform_int_distribution<> dis(0, (int)pool.size() - 1);
        int index = dis(gen);
        choices.push_back(pool[index]);
        pool.erase(pool.begin() + index);
    }
    return choices;
}

void RewardPool::displayRewardChoices(const std::vector<Card>& choices) {
    std::cout << "\n" << Color::BOLD << Color::YELLOW << "========== CARD REWARDS ==========" << Color::RESET << "\n";
    std::cout << Color::DIM << "Choose 1 card to add to your deck:\n\n" << Color::RESET;

    for (size_t i = 0; i < choices.size(); ++i) {
        const Card& c = choices[i];

        const char* typeColor = Color::WHITE;
        if      (c.getTypeString() == "ATTACK")  typeColor = Color::CARD_ATTACK;
        else if (c.getTypeString() == "DEFEND")  typeColor = Color::CARD_DEFEND;
        else if (c.getTypeString() == "SPECIAL") typeColor = Color::CARD_SPECIAL;

        std::cout << "  " << Color::BOLD << (i + 1) << "." << Color::RESET
                  << " [" << typeColor << c.getTypeString() << Color::RESET << "] "
                  << Color::BOLD << Color::WHITE << c.getName() << Color::RESET << "\n";
        std::cout << "     Cost: " << Color::ENERGY_CLR << c.getCost() << Color::RESET
                  << " | Value: " << Color::GREEN << c.getValue() << Color::RESET << "\n";
        std::cout << "     " << Color::DIM << c.getDescription() << Color::RESET << "\n\n";
    }

    std::cout << "Enter your choice (1-" << choices.size() << "): ";
}
