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
    // Resolve relative to the exe's own folder, not the current working directory —
    // cwd can differ depending on how the exe was launched (shortcut, opened from
    // inside an unextracted zip, etc.), which was silently dropping us into the
    // old hardcoded fallback deck below.
    std::string configPath = Audio::exeDir() + "config/cards.json";
    
    // Check if config file exists
    if (std::filesystem::exists(configPath)) {
        std::cout << "Loading cards from " << configPath << "\n";
        
        auto commonData = ConfigLoader::loadCommonCards(configPath);
        auto rareData = ConfigLoader::loadRareCards(configPath);
        
        auto toEffect = [](const std::string& e) -> CardEffect {
            if (e == "POISON")     return CardEffect::POISON;
            if (e == "BURN")       return CardEffect::BURN;
            if (e == "STUN")       return CardEffect::STUN;
            if (e == "WEAK")       return CardEffect::WEAK;
            if (e == "COUNTER")    return CardEffect::COUNTER;
            if (e == "PARRY")      return CardEffect::PARRY;
            if (e == "PIERCE")     return CardEffect::PIERCE;
            if (e == "FORTIFY")    return CardEffect::FORTIFY;
            if (e == "STRENGTH")   return CardEffect::STRENGTH;
            if (e == "DOUBLE_HIT") return CardEffect::DOUBLE_HIT;
            if (e == "IMPAIR")     return CardEffect::IMPAIR;
            if (e == "CHIP")       return CardEffect::CHIP;
            if (e == "HEAL")       return CardEffect::HEAL;
            return CardEffect::NONE;
        };
        auto toPhysType = [](const std::string& s) -> DamageType {
            if (s == "SMASH")  return DamageType::SMASH;
            if (s == "PIERCE") return DamageType::PIERCE;
            return DamageType::NONE;
        };
        auto toElemType = [](const std::string& s) -> DamageType {
            if (s == "FIRE")   return DamageType::FIRE;
            if (s == "POISON") return DamageType::POISON;
            if (s == "WIND")   return DamageType::WIND;
            return DamageType::NONE;
        };

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
        
        std::cout << "Loaded " << commonCards.size() << " common cards and " << rareCards.size() << " rare cards.\n";
    } else {
        std::cout << Color::YELLOW << "Warning: " << configPath << " not found — using a reduced built-in fallback deck "
                   << "(no Parry/Dodge, no damage types). Reinstall or verify config/ sits next to the exe." << Color::RESET << "\n";

        // Starter cards excluded — player already has them

        // Common ATTACK
        commonCards.push_back(Card("Quick Jab",     "Deal 4 damage",                    CardType::ATTACK,  0, 4));
        commonCards.push_back(Card("Slice",         "Deal 9 damage",                    CardType::ATTACK,  1, 9));
        commonCards.push_back(Card("Heavy Blow",    "Deal 16 damage",                   CardType::ATTACK,  2, 16));
        commonCards.push_back(Card("Reckless Swing","Deal 25 damage",                   CardType::ATTACK,  3, 25));
        // Common DEFEND
        commonCards.push_back(Card("Iron Guard",    "Gain 9 armor",                     CardType::DEFEND,  1, 9));
        commonCards.push_back(Card("Fortify",       "Gain 16 armor",                    CardType::DEFEND,  2, 16));
        commonCards.push_back(Card("Bulwark",       "Gain 25 armor",                    CardType::DEFEND,  3, 25));
        // Common SPECIAL
        commonCards.push_back(Card("Poison Dart",   "Apply 3 Poison stacks",            CardType::SPECIAL, 1, 3, CardEffect::POISON));
        commonCards.push_back(Card("Torch",         "Apply 2 Burn (5 dmg x2 turns)",    CardType::SPECIAL, 1, 2, CardEffect::BURN));
        commonCards.push_back(Card("Stun Strike",   "Stun enemy for 1 turn",            CardType::SPECIAL, 2, 1, CardEffect::STUN));
        commonCards.push_back(Card("Weaken",        "Apply 3 Weak (-2 atk x3 turns)",   CardType::SPECIAL, 1, 3, CardEffect::WEAK));

        // Rare ATTACK
        rareCards.push_back(Card("Power Strike",    "Deal 20 damage",                   CardType::ATTACK,  2, 20, CardEffect::NONE, true));
        rareCards.push_back(Card("Cleave",          "Deal 28 damage",                   CardType::ATTACK,  3, 28, CardEffect::NONE, true));
        rareCards.push_back(Card("Annihilate",      "Deal 35 damage",                   CardType::ATTACK,  3, 35, CardEffect::NONE, true));
        // Rare DEFEND
        rareCards.push_back(Card("Iron Skin",       "Gain 20 armor",                    CardType::DEFEND,  2, 20, CardEffect::NONE, true));
        rareCards.push_back(Card("Diamond Wall",    "Gain 32 armor",                    CardType::DEFEND,  3, 32, CardEffect::NONE, true));
        // Rare SPECIAL
        rareCards.push_back(Card("Toxic Cloud",     "Apply 6 Poison stacks",            CardType::SPECIAL, 2, 6, CardEffect::POISON, true));
        rareCards.push_back(Card("Inferno",         "Apply 4 Burn (5 dmg x4 turns)",    CardType::SPECIAL, 2, 4, CardEffect::BURN, true));
        rareCards.push_back(Card("Paralysis",       "Stun enemy for 2 turns",           CardType::SPECIAL, 2, 2, CardEffect::STUN, true));
        rareCards.push_back(Card("Shatter",         "Apply 5 Weak (-2 atk x5 turns)",   CardType::SPECIAL, 2, 5, CardEffect::WEAK, true));
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

std::vector<Card> RewardPool::generateWeightedRewards(int count, bool rarityBoost, int maxCost, const std::vector<std::string>& ownedNames) {
    std::vector<Card> choices;
    std::random_device rd;
    std::mt19937 gen(rd());

    // Fixed odds per slot: 80% Uncommon / 15% Rare / 5% Super Rare, or
    // 60% / 25% / 15% with the "Fortunate Soul" rarity boost active.
    // Legendary (Dodge) is intentionally excluded — it only ever drops from boss rewards.
    int superRareChance  = rarityBoost ? 15 : 5;
    int rareChance       = rarityBoost ? 25 : 15;

    std::unordered_set<std::string> owned(ownedNames.begin(), ownedNames.end());

    std::vector<Card> uncommonPool, rarePool, superRarePool;
    for (const auto& c : commonCards) if (c.getCost() <= maxCost && owned.find(c.getName()) == owned.end()) uncommonPool.push_back(c);
    for (const auto& c : rareCards) {
        if (c.isLegendary() || c.getCost() > maxCost || owned.find(c.getName()) != owned.end()) continue;
        if (c.isSuperRare()) superRarePool.push_back(c);
        else rarePool.push_back(c);
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

std::vector<Card> RewardPool::generateRareRewards(int count, int maxCost, const std::vector<std::string>& ownedNames) {
    std::vector<Card> choices;
    std::random_device rd;
    std::mt19937 gen(rd());

    std::unordered_set<std::string> owned(ownedNames.begin(), ownedNames.end());

    // Boss rewards are always Rare-or-better: 70% Rare / 25% Super Rare / 5% Legendary.
    // This is the ONLY place Legendary (Dodge) can drop — regular rewards never roll it.
    std::vector<Card> rarePool, superRarePool, legendaryPool;
    for (const auto& c : rareCards) {
        if (c.getCost() > maxCost || owned.find(c.getName()) != owned.end()) continue;
        if (c.isLegendary()) legendaryPool.push_back(c);
        else if (c.isSuperRare()) superRarePool.push_back(c);
        else rarePool.push_back(c);
    }

    std::uniform_int_distribution<> rollDis(1, 100);

    for (int i = 0; i < count; ++i) {
        int roll = rollDis(gen);
        std::vector<Card>* pool;
        if (roll <= 5) pool = &legendaryPool;
        else if (roll <= 30) pool = &superRarePool;
        else pool = &rarePool;

        if (pool->empty()) {
            if (!rarePool.empty()) pool = &rarePool;
            else if (!superRarePool.empty()) pool = &superRarePool;
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

void RewardPool::displayRewardChoices(const std::vector<Card>& choices) {
    std::cout << "\n" << Color::BOLD << Color::YELLOW << "Card reward" << Color::RESET << " — pick one to add to your deck:\n\n";

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

}
