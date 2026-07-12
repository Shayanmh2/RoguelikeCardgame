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

    // Weighted card selection: fixed 80% Uncommon / 15% Rare / 5% Super Rare odds
    // per slot (60/25/15 with rarityBoost). maxCost filters unplayable cards.
    // Legendary (Dodge) never drops here - boss rewards only.
    // maxRarityUnlocked gates which tiers can appear at all: 0 = Uncommon only,
    // 1 = + Rare, 2 = + Super Rare (unlocked by beating the 1st/2nd boss).
    std::vector<Card> generateWeightedRewards(int count = 3, bool rarityBoost = false, int maxCost = 99, const std::vector<std::string>& ownedNames = {}, int maxRarityUnlocked = 2);

    // Boss reward: always Rare-or-better, weighted 70% Rare / 25% Super Rare / 5% Legendary per pick.
    std::vector<Card> generateRareRewards(int count = 2, int maxCost = 99, const std::vector<std::string>& ownedNames = {});
};

#endif
