#include "RewardPool.h"
#include <random>
#include <iostream>

RewardPool::RewardPool() {
    initializeCardPool();
}

void RewardPool::initializeCardPool() {
    // Common cards (frequently available, low cost)
    commonCards.push_back(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
    commonCards.push_back(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
    commonCards.push_back(Card("Defend", "Gain 5 armor", CardType::DEFEND, 1, 5));
    commonCards.push_back(Card("Defend", "Gain 5 armor", CardType::DEFEND, 1, 5));
    commonCards.push_back(Card("Bash", "Deal 8 damage", CardType::ATTACK, 2, 8));
    commonCards.push_back(Card("Bash", "Deal 8 damage", CardType::ATTACK, 2, 8));
    commonCards.push_back(Card("Block", "Gain 8 armor", CardType::DEFEND, 2, 8));
    commonCards.push_back(Card("Block", "Gain 8 armor", CardType::DEFEND, 2, 8));
    commonCards.push_back(Card("Lunge", "Deal 6 damage", CardType::ATTACK, 1, 6));
    commonCards.push_back(Card("Lunge", "Deal 6 damage", CardType::ATTACK, 1, 6));
    commonCards.push_back(Card("Brace", "Gain 6 armor", CardType::DEFEND, 1, 6));
    commonCards.push_back(Card("Brace", "Gain 6 armor", CardType::DEFEND, 1, 6));
    commonCards.push_back(Card("Jab", "Deal 4 damage (quick)", CardType::ATTACK, 1, 4));
    commonCards.push_back(Card("Jab", "Deal 4 damage (quick)", CardType::ATTACK, 1, 4));
    commonCards.push_back(Card("Dodge", "Gain 4 armor (evasion)", CardType::DEFEND, 1, 4));
    commonCards.push_back(Card("Dodge", "Gain 4 armor (evasion)", CardType::DEFEND, 1, 4));
    
    // Rare cards (expensive, powerful, strategic)
    rareCards.push_back(Card("Power Attack", "Deal 12 damage", CardType::ATTACK, 3, 12));
    rareCards.push_back(Card("Power Attack", "Deal 12 damage", CardType::ATTACK, 3, 12));
    rareCards.push_back(Card("Iron Skin", "Gain 12 armor", CardType::DEFEND, 3, 12));
    rareCards.push_back(Card("Iron Skin", "Gain 12 armor", CardType::DEFEND, 3, 12));
    rareCards.push_back(Card("Combo", "Deal 8 damage (special)", CardType::SPECIAL, 2, 8));
    rareCards.push_back(Card("Fortify", "Gain 10 armor (special)", CardType::SPECIAL, 2, 10));
    rareCards.push_back(Card("Cleave", "Deal 15 damage", CardType::ATTACK, 3, 15));
    rareCards.push_back(Card("Cleave", "Deal 15 damage", CardType::ATTACK, 3, 15));
    rareCards.push_back(Card("Rampart", "Gain 15 armor", CardType::DEFEND, 3, 15));
    rareCards.push_back(Card("Rampart", "Gain 15 armor", CardType::DEFEND, 3, 15));
    rareCards.push_back(Card("Execute", "Deal 10 damage", CardType::ATTACK, 2, 10));
    rareCards.push_back(Card("Execute", "Deal 10 damage", CardType::ATTACK, 2, 10));
    rareCards.push_back(Card("Fortification", "Gain 14 armor", CardType::DEFEND, 2, 14));
    rareCards.push_back(Card("Onslaught", "Deal 9 damage", CardType::ATTACK, 2, 9));
    rareCards.push_back(Card("Onslaught", "Deal 9 damage", CardType::ATTACK, 2, 9));
    rareCards.push_back(Card("Riposte", "Deal 7 damage (counter)", CardType::ATTACK, 1, 7));
    rareCards.push_back(Card("Riposte", "Deal 7 damage (counter)", CardType::ATTACK, 1, 7));
    rareCards.push_back(Card("Deflect", "Gain 7 armor (redirect)", CardType::DEFEND, 1, 7));
    rareCards.push_back(Card("Deflect", "Gain 7 armor (redirect)", CardType::DEFEND, 1, 7));
    rareCards.push_back(Card("Whirlwind", "Deal 11 damage", CardType::ATTACK, 3, 11));
    rareCards.push_back(Card("Last Stand", "Gain 13 armor", CardType::DEFEND, 3, 13));
    rareCards.push_back(Card("Slash", "Deal 6 damage (fast)", CardType::ATTACK, 1, 6));
    rareCards.push_back(Card("Shield Bash", "Deal 5 damage + gain 5 armor", CardType::SPECIAL, 2, 5));
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
