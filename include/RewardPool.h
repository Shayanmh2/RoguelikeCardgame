#ifndef REWARDPOOL_H
#define REWARDPOOL_H

#include "Card.h"
#include "ConfigLoader.h"
#include <vector>

class RewardPool {
private:
    std::vector<Card> commonCards;
    std::vector<Card> rareCards;
    
    void initializeCardPool();
    
public:
    RewardPool();
    
    // Get 3 random cards from pool
    std::vector<Card> generateRewardChoices(int count = 3);
    
    // Weighted card selection (can bias towards rare cards at higher tiers)
    std::vector<Card> generateWeightedRewards(int encounterNumber, int count = 3, bool rarityBoost = false);

    // Boss reward: always pulls from rare pool
    std::vector<Card> generateRareRewards(int count = 2);

    void displayRewardChoices(const std::vector<Card>& choices);
};

#endif
