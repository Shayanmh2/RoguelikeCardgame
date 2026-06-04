#include "RewardPool.h"
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
        commonCards.push_back(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
        commonCards.push_back(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
        commonCards.push_back(Card("Defend", "Gain 5 armor", CardType::DEFEND, 1, 5));
        commonCards.push_back(Card("Defend", "Gain 5 armor", CardType::DEFEND, 1, 5));
        commonCards.push_back(Card("Bash", "Deal 8 damage", CardType::ATTACK, 2, 8));
        commonCards.push_back(Card("Bash", "Deal 8 damage", CardType::ATTACK, 2, 8));
        
        rareCards.push_back(Card("Power Strike", "Deal 12 damage", CardType::ATTACK, 3, 12));
        rareCards.push_back(Card("Iron Skin", "Gain 12 armor", CardType::DEFEND, 3, 12));
        rareCards.push_back(Card("Cleave", "Deal 15 damage", CardType::ATTACK, 3, 15));
    }
}


std::vector<Card> RewardPool::generateRewardChoices(int count) {
    std::vector<Card> choices;
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Combine pools
    std::vector<Card> allCards;
    allCards.insert(allCards.end(), commonCards.begin(), commonCards.end());
    allCards.insert(allCards.end(), rareCards.begin(), rareCards.end());
    
    // Pick random cards without replacement
    std::uniform_int_distribution<> dis(0, allCards.size() - 1);
    
    for (int i = 0; i < count && allCards.size() > 0; ++i) {
        int index = dis(gen);
        choices.push_back(allCards[index]);
        // Remove selected card to avoid duplicates
        allCards.erase(allCards.begin() + index);
    }
    
    return choices;
}

std::vector<Card> RewardPool::generateWeightedRewards(int encounterNumber, int count, bool rarityBoost) {
    std::vector<Card> choices;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> rareDis(0, 100);
    
    // Higher encounters have higher chance of rare cards
    int rareChance = 20 + (encounterNumber * 5);  // 20% at enc 1, 75% at enc 11+
    if (rarityBoost) rareChance += 20;  // +20% if Rarity Boost upgrade is active
    if (rareChance > 80) rareChance = 80;
    
    std::vector<Card> allCards;
    allCards.insert(allCards.end(), commonCards.begin(), commonCards.end());
    allCards.insert(allCards.end(), rareCards.begin(), rareCards.end());
    
    std::uniform_int_distribution<> cardDis(0, allCards.size() - 1);
    
    for (int i = 0; i < count && allCards.size() > 0; ++i) {
        int index = cardDis(gen);
        choices.push_back(allCards[index]);
        allCards.erase(allCards.begin() + index);
    }
    
    return choices;
}

void RewardPool::displayRewardChoices(const std::vector<Card>& choices) {
    std::cout << "\n========== CARD REWARDS ==========\n";
    std::cout << "Choose 1 card to add to your deck:\n\n";
    
    for (size_t i = 0; i < choices.size(); ++i) {
        std::cout << (i + 1) << ". [" << choices[i].getTypeString() << "] " << choices[i].getName() << "\n";
        std::cout << "   Cost: " << choices[i].getCost() << " | Value: " << choices[i].getValue() << "\n";
        std::cout << "   " << choices[i].getDescription() << "\n\n";
    }
    
    std::cout << "Enter your choice (1-" << choices.size() << "):\n";
}
