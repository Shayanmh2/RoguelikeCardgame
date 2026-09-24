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

    // Weighted card rewards: 80/15/5 Uncommon/Rare/Super Rare per slot, 60/25/15
    // with rarityBoost, and `luck` adds points to the two good slots. maxCost
    // filters unplayable cards; maxRarityUnlocked caps the tier (0 Uncommon,
    // 1 Rare, 2 Super Rare). Legendaries only come from bosses.
    std::vector<Card> generateWeightedRewards(int count = 3, bool rarityBoost = false, int maxCost = 99, const std::vector<std::string>& ownedNames = {}, int maxRarityUnlocked = 2, int luck = 0);

    // Boss reward: always Rare-or-better. bossIndex selects the tier: early
    // bosses split 70% Rare / 30% Super Rare; bosses 3 and 4 (Hydra, Undead
    // Dragon) roll Super Rare only, with a 4% Legendary.
    std::vector<Card> generateRareRewards(int count = 2, int maxCost = 99, const std::vector<std::string>& ownedNames = {}, int bossIndex = 0, int luck = 0);
    // One unowned Super Rare, for the ??? encounter. Falls back to a Rare if
    // every Super Rare is already in the deck; empty only if both are.
    std::vector<Card> generateSuperRareReward(const std::vector<std::string>& ownedNames = {});

    // Every legendary the player doesn't own yet - the Shadow Knight's guaranteed drop.
    std::vector<Card> getUnownedLegendaries(const std::vector<std::string>& ownedNames = {}) const;
};

#endif
