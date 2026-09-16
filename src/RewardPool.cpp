#include "RewardPool.h"
#include "Colors.h"
#include "Audio.h"
#include <random>
#include <iostream>
#include <filesystem>
#include <unordered_set>

RewardPool::RewardPool() {
    initializeCardPool();
}

void RewardPool::initializeCardPool() {
    // Resolve relative to the exe's folder, not cwd (which varies by launch method)
    std::string configPath = Audio::exeDir() + "config/cards.json";

    if (!std::filesystem::exists(configPath)) {
        std::cout << Color::YELLOW << "Warning: " << configPath << " not found - no card rewards will be available. "
                   << "Reinstall or verify config/ sits next to the exe." << Color::RESET << "\n";
        return;
    }

    // The card load is silent on success - it printed a path banner over the
    // title screen with nothing actionable in it. The missing-file warning
    // above still shows, since that one the player can act on.
    auto commonData = ConfigLoader::loadCommonCards(configPath);
    auto rareData = ConfigLoader::loadRareCards(configPath);

    // Delegates. This table used to be duplicated here, and REND went missing
    // from the copy, so every wind card handed out was an inert SPECIAL with
    // CardEffect::NONE: no message, no status applied, no matching sound.
    auto toEffect = [](const std::string& e) { return Card::effectFromString(e); };
    auto toPhysType = [](const std::string& s) { return Card::damageTypeFromString(s); };
    // Physical and elemental read the same table; a card carries at most one
    // of each kind, so one converter serves both slots.
    auto toElemType = [](const std::string& s) { return Card::damageTypeFromString(s); };

    for (const auto& data : commonData) {
        CardType type = CardType::ATTACK;
        if (data.type == "DEFEND")  type = CardType::DEFEND;
        else if (data.type == "SPECIAL") type = CardType::SPECIAL;
        commonCards.push_back(Card(data.name, data.description, type, data.cost, data.value, toEffect(data.effect), false,
                                   toPhysType(data.physType), toElemType(data.elemType)));
    }

    for (const auto& data : rareData) {
        CardType type = CardType::ATTACK;
        if (data.type == "DEFEND")  type = CardType::DEFEND;
        else if (data.type == "SPECIAL") type = CardType::SPECIAL;
        rareCards.push_back(Card(data.name, data.description, type, data.cost, data.value, toEffect(data.effect), true,
                                  toPhysType(data.physType), toElemType(data.elemType), data.superRare,
                                  toPhysType(data.physType2), data.legendary));
    }

    // (No "loaded N cards" line - it was terminal-startup noise that just sat
    // on top of the title screen with nothing useful to say.)
}


std::vector<Card> RewardPool::generateWeightedRewards(int count, bool rarityBoost, int maxCost, const std::vector<std::string>& ownedNames, int maxRarityUnlocked, int luck) {
    std::vector<Card> choices;
    std::random_device rd;
    std::mt19937 gen(rd());

    // Fixed odds per slot: 80% Uncommon / 15% Rare / 5% Super Rare, or
    // 60% / 25% / 15% with the "Fortunate Soul" rarity boost active.
    // Legendary (Dodge Reversal) is intentionally excluded - it only ever drops from boss rewards.
    // Luck widens both good slots at the uncommon slot's expense.
    int superRareChance  = (rarityBoost ? 15 : 5)  + luck;
    int rareChance       = (rarityBoost ? 25 : 15) + luck;

    std::unordered_set<std::string> owned(ownedNames.begin(), ownedNames.end());

    std::vector<Card> uncommonPool, rarePool, superRarePool;
    for (const auto& c : commonCards) if (c.getCost() <= maxCost && owned.find(c.getName()) == owned.end()) uncommonPool.push_back(c);
    for (const auto& c : rareCards) {
        if (c.isLegendary() || c.getCost() > maxCost || owned.find(c.getName()) != owned.end()) continue;
        if (c.isSuperRare()) { if (maxRarityUnlocked >= 2) superRarePool.push_back(c); }
        else { if (maxRarityUnlocked >= 1) rarePool.push_back(c); }
    }

    std::uniform_int_distribution<> rollDis(1, 100);

    for (int i = 0; i < count; ++i) {
        int roll = rollDis(gen);
        std::vector<Card>* pool;
        if (roll <= superRareChance) pool = &superRarePool;
        else if (roll <= superRareChance + rareChance) pool = &rarePool;
        else pool = &uncommonPool;

        // Fall back to whichever tier still has cards left, favoring the rolled tier first.
        if (pool->empty()) {
            if (!uncommonPool.empty()) pool = &uncommonPool;
            else if (!rarePool.empty()) pool = &rarePool;
            else if (!superRarePool.empty()) pool = &superRarePool;
            else break;
        }

        std::uniform_int_distribution<> idxDis(0, (int)pool->size() - 1);
        int index = idxDis(gen);
        choices.push_back((*pool)[index]);
        pool->erase(pool->begin() + index);
    }

    return choices;
}

std::vector<Card> RewardPool::generateRareRewards(int count, int maxCost, const std::vector<std::string>& ownedNames, int bossIndex, int luck) {
    std::vector<Card> choices;
    std::random_device rd;
    std::mt19937 gen(rd());

    std::unordered_set<std::string> owned(ownedNames.begin(), ownedNames.end());

    // Early bosses: 70% Rare / 30% Super Rare, no Legendary.
    //
    // Bosses 3 and 4 (Hydra, Undead Dragon) sit late enough that a plain Rare is
    // not worth a boss kill any more, so they drop Super Rare only, with a 2%
    // Legendary. That is far narrower than the old flat 5% on every boss, which
    // is what made Legendaries stop feeling special.
    const bool lateBoss = (bossIndex == 3 || bossIndex == 4);
    std::vector<Card> rarePool, superRarePool, legendaryPool;
    for (const auto& c : rareCards) {
        if (c.getCost() > maxCost || owned.find(c.getName()) != owned.end()) continue;
        if (c.isLegendary()) { if (lateBoss) legendaryPool.push_back(c); }
        else if (c.isSuperRare()) superRarePool.push_back(c);
        else rarePool.push_back(c);
    }

    std::uniform_int_distribution<> rollDis(1, 100);

    for (int i = 0; i < count; ++i) {
        int roll = rollDis(gen);
        std::vector<Card>* pool;
        if (lateBoss) pool = (roll <= 2 + luck && !legendaryPool.empty()) ? &legendaryPool : &superRarePool;
        else          pool = (roll <= 30) ? &superRarePool : &rarePool;

        if (pool->empty()) {
            if (!superRarePool.empty()) pool = &superRarePool;
            else if (!rarePool.empty()) pool = &rarePool;
            else if (!legendaryPool.empty()) pool = &legendaryPool;
            else break;
        }
        std::uniform_int_distribution<> idxDis(0, (int)pool->size() - 1);
        int index = idxDis(gen);
        choices.push_back((*pool)[index]);
        pool->erase(pool->begin() + index);
    }
    return choices;
}

std::vector<Card> RewardPool::generateSuperRareReward(const std::vector<std::string>& ownedNames) {
    std::unordered_set<std::string> owned(ownedNames.begin(), ownedNames.end());
    std::vector<Card> superRare, rare;
    for (const auto& c : rareCards) {
        if (c.isLegendary() || owned.find(c.getName()) != owned.end()) continue;
        (c.isSuperRare() ? superRare : rare).push_back(c);
    }
    std::vector<Card>& pool = superRare.empty() ? rare : superRare;
    if (pool.empty()) return {};
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> pick(0, (int)pool.size() - 1);
    return { pool[pick(gen)] };
}

std::vector<Card> RewardPool::getUnownedLegendaries(const std::vector<std::string>& ownedNames) const {
    std::unordered_set<std::string> owned(ownedNames.begin(), ownedNames.end());
    std::vector<Card> result;
    for (const auto& c : rareCards)
        if (c.isLegendary() && owned.find(c.getName()) == owned.end())
            result.push_back(c);
    return result;
}
