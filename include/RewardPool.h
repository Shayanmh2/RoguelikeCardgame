#ifndef REWARDPOOL_H
#define REWARDPOOL_H

#include "Card.h"
#include "ConfigLoader.h"
#include <vector>
#include <string>

class RewardPool {
private:
    std::vector<Card> commonCards;
    std::vector<Card> rareCards;
    
    void initializeCardPool();
    
public:
    RewardPool();
    
    // Get 3 random cards from pool
    std::vector<Card> generateRewardChoices(int count = 3);
    
    // Weighted card selection: fixed 80% Uncommon / 15% Rare / 5% Super Rare odds
    // per slot (60/25/15 with rarityBoost). maxCost filters unplayable cards.
    std::vector<Card> generateWeightedRewards(int count = 3, bool rarityBoost = false, int maxCost = 99, const std::vector<std::string>& ownedNames = {});

    // Boss reward: always Rare-or-better, weighted 75% Rare / 25% Super Rare per pick.
    std::vector<Card> generateRareRewards(int count = 2, int maxCost = 99, const std::vector<std::string>& ownedNames = {});

    void displayRewardChoices(const std::vector<Card>& choices);
};

#endif
