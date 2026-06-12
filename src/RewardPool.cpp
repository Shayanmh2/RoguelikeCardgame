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
        
        // Fallback to hardcoded cards.
        // Starter cards (Strike / Defend / Bash) are intentionally excluded here —
        // the player already has them and getting duplicates as rewards feels bad.

        // Common ATTACK
        commonCards.push_back(Card("Quick Jab",    "Deal 3 damage",                    CardType::ATTACK,  0, 3));
        commonCards.push_back(Card("Slice",        "Deal 6 damage",                    CardType::ATTACK,  1, 6));
        commonCards.push_back(Card("Heavy Blow",   "Deal 10 damage",                   CardType::ATTACK,  2, 10));
        commonCards.push_back(Card("Reckless Swing","Deal 13 damage",                  CardType::ATTACK,  3, 13));
        // Common DEFEND
        commonCards.push_back(Card("Iron Guard",   "Gain 8 armor",                     CardType::DEFEND,  1, 8));
        commonCards.push_back(Card("Fortify",      "Gain 14 armor",                    CardType::DEFEND,  2, 14));
        commonCards.push_back(Card("Bulwark",      "Gain 20 armor",                    CardType::DEFEND,  3, 20));
        // Common SPECIAL
        commonCards.push_back(Card("Poison Dart",  "Apply 3 Poison stacks",            CardType::SPECIAL, 1, 3, CardEffect::POISON));
        commonCards.push_back(Card("Torch",        "Apply 2 Burn (5 dmg x2 turns)",    CardType::SPECIAL, 1, 2, CardEffect::BURN));
        commonCards.push_back(Card("Stun Strike",  "Stun enemy for 1 turn",            CardType::SPECIAL, 2, 1, CardEffect::STUN));
        commonCards.push_back(Card("Weaken",       "Apply 3 Weak (-2 atk x3 turns)",   CardType::SPECIAL, 1, 3, CardEffect::WEAK));

        // Rare ATTACK
        rareCards.push_back(Card("Power Strike",   "Deal 15 damage",                   CardType::ATTACK,  2, 15));
        rareCards.push_back(Card("Cleave",         "Deal 18 damage",                   CardType::ATTACK,  3, 18));
        rareCards.push_back(Card("Annihilate",     "Deal 22 damage",                   CardType::ATTACK,  3, 22));
        // Rare DEFEND
        rareCards.push_back(Card("Iron Skin",      "Gain 18 armor",                    CardType::DEFEND,  2, 18));
        rareCards.push_back(Card("Diamond Wall",   "Gain 25 armor",                    CardType::DEFEND,  3, 25));
        // Rare SPECIAL
        rareCards.push_back(Card("Toxic Cloud",    "Apply 6 Poison stacks",            CardType::SPECIAL, 2, 6, CardEffect::POISON));
        rareCards.push_back(Card("Inferno",        "Apply 4 Burn (5 dmg x4 turns)",    CardType::SPECIAL, 2, 4, CardEffect::BURN));
        rareCards.push_back(Card("Paralysis",      "Stun enemy for 2 turns",           CardType::SPECIAL, 2, 2, CardEffect::STUN));
        rareCards.push_back(Card("Shatter",        "Apply 5 Weak (-2 atk x5 turns)",   CardType::SPECIAL, 2, 5, CardEffect::WEAK));
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

std::vector<Card> RewardPool::generateWeightedRewards(int encounterNumber, int count, bool rarityBoost, int maxCost) {
    std::vector<Card> choices;
    std::random_device rd;
    std::mt19937 gen(rd());

    int rareChance = 20 + (encounterNumber * 5);
    if (rarityBoost) rareChance += 20;
    if (rareChance > 80) rareChance = 80;

    // Local copies filtered to cards the player can actually play.
    std::vector<Card> commonPool, rarePool;
    for (const auto& c : commonCards) if (c.getCost() <= maxCost) commonPool.push_back(c);
    for (const auto& c : rareCards)   if (c.getCost() <= maxCost) rarePool.push_back(c);

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

std::vector<Card> RewardPool::generateRareRewards(int count, int maxCost) {
    std::vector<Card> choices;
    std::random_device rd;
    std::mt19937 gen(rd());

    std::vector<Card> pool;
    for (const auto& c : rareCards) if (c.getCost() <= maxCost) pool.push_back(c);
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
        const char* valLabel = (c.getTypeString() == "ATTACK") ? "DMG"
                            : (c.getTypeString() == "DEFEND") ? "ARM" : "STK";
        std::cout << "     Cost: " << Color::ENERGY_CLR << c.getCost() << Color::RESET
                  << " | " << valLabel << ": " << Color::GREEN << c.getValue() << Color::RESET << "\n";
        std::cout << "     " << Color::DIM << c.getDescription() << Color::RESET << "\n\n";
    }

    std::cout << "Enter your choice (1-" << choices.size() << "): ";
}
