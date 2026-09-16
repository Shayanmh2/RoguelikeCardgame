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
    // Legendary (Dodge Reversal) never drops here - boss rewards only.
    // maxRarityUnlocked gates which tiers can appear at all: 0 = Uncommon only,
    // 1 = + Rare, 2 = + Super Rare (unlocked by beating the 1st/2nd boss).
    // luck adds percentage points to the rare/super-rare slots, from the Fortune boon.
    std::vector<Card> generateWeightedRewards(int count = 3, bool rarityBoost = false, int maxCost = 99, const std::vector<std::string>& ownedNames = {}, int maxRarityUnlocked = 2, int luck = 0);

    // Boss reward: always Rare-or-better, weighted 70% Rare / 25% Super Rare / 5% Legendary per pick.
    // bossIndex selects the tier: bosses 3 and 4 (Hydra, Undead Dragon) are late
    // enough that a plain Rare is no longer a prize, so they roll Super Rare only
    // with a 2% Legendary. Earlier bosses keep the 70/30 Rare/Super Rare split.
    std::vector<Card> generateRareRewards(int count = 2, int maxCost = 99, const std::vector<std::string>& ownedNames = {}, int bossIndex = 0, int luck = 0);
    // One unowned Super Rare, for the ??? encounter. Falls back to a Rare if
    // every Super Rare is already in the deck; empty only if both are.
    std::vector<Card> generateSuperRareReward(const std::vector<std::string>& ownedNames = {});

    // Every legendary the player doesn't own yet - the Shadow Knight's guaranteed drop.
    std::vector<Card> getUnownedLegendaries(const std::vector<std::string>& ownedNames = {}) const;
};

#endif
