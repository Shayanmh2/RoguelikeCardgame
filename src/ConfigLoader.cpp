#include "ConfigLoader.h"
#include <iostream>
#include <cctype>
#include <algorithm>

std::string ConfigLoader::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

std::vector<ConfigLoader::CardData> ConfigLoader::loadCommonCards(const std::string& configPath) {
    std::vector<CardData> cards;
    std::ifstream file(configPath);
    
    if (!file.is_open()) {
        std::cerr << "Warning: Could not load config from " << configPath << std::endl;
        return cards;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    // Find commonCards array
    size_t commonPos = content.find("\"commonCards\"");
    if (commonPos == std::string::npos) return cards;
    
    size_t arrayStart = content.find('[', commonPos);
    size_t arrayEnd = content.find(']', arrayStart);
    
    std::string commonSection = content.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
    
    // Parse individual card objects
    size_t pos = 0;
    while (pos < commonSection.length()) {
        size_t objStart = commonSection.find('{', pos);
        if (objStart == std::string::npos) break;
        
        size_t objEnd = commonSection.find('}', objStart);
        if (objEnd == std::string::npos) break;
        
        std::string cardStr = commonSection.substr(objStart, objEnd - objStart + 1);
        CardData card = parseCard(cardStr);
        if (!card.name.empty()) {
            cards.push_back(card);
        }
        
        pos = objEnd + 1;
    }
    
    return cards;
}

std::vector<ConfigLoader::CardData> ConfigLoader::loadRareCards(const std::string& configPath) {
    std::vector<CardData> cards;
    std::ifstream file(configPath);
    
    if (!file.is_open()) {
        std::cerr << "Warning: Could not load config from " << configPath << std::endl;
        return cards;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    // Find rareCards array
    size_t rarePos = content.find("\"rareCards\"");
    if (rarePos == std::string::npos) return cards;
    
    size_t arrayStart = content.find('[', rarePos);
    size_t arrayEnd = content.rfind(']');  // Last ] in file
    
    std::string rareSection = content.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
    
    // Parse individual card objects
    size_t pos = 0;
    while (pos < rareSection.length()) {
        size_t objStart = rareSection.find('{', pos);
        if (objStart == std::string::npos) break;
        
        size_t objEnd = rareSection.find('}', objStart);
        if (objEnd == std::string::npos) break;
        
        std::string cardStr = rareSection.substr(objStart, objEnd - objStart + 1);
        CardData card = parseCard(cardStr);
        if (!card.name.empty()) {
            cards.push_back(card);
        }
        
        pos = objEnd + 1;
    }
    
    return cards;
}

ConfigLoader::CardData ConfigLoader::parseCard(const std::string& cardStr) {
    CardData card;
    
    // Extract name
    size_t nameStart = cardStr.find("\"name\"");
    if (nameStart != std::string::npos) {
        size_t quoteStart = cardStr.find('"', nameStart + 6);
        size_t quoteEnd = cardStr.find('"', quoteStart + 1);
        if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
            card.name = cardStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        }
    }
    
    // Extract description
    size_t descStart = cardStr.find("\"description\"");
    if (descStart != std::string::npos) {
        size_t quoteStart = cardStr.find('"', descStart + 13);
        size_t quoteEnd = cardStr.find('"', quoteStart + 1);
        if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
            card.description = cardStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        }
    }
    
    // Extract effect (optional)
    size_t effectStart = cardStr.find("\"effect\"");
    if (effectStart != std::string::npos) {
        size_t quoteStart = cardStr.find('"', effectStart + 8);
        size_t quoteEnd   = cardStr.find('"', quoteStart + 1);
        if (quoteStart != std::string::npos && quoteEnd != std::string::npos)
            card.effect = cardStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    }

    // Extract physType (optional)
    size_t physStart = cardStr.find("\"physType\"");
    if (physStart != std::string::npos) {
        size_t quoteStart = cardStr.find('"', physStart + 10);
        size_t quoteEnd   = cardStr.find('"', quoteStart + 1);
        if (quoteStart != std::string::npos && quoteEnd != std::string::npos)
            card.physType = cardStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    }

    // Extract physType2 (optional — only Finishing Blow uses both)
    size_t phys2Start = cardStr.find("\"physType2\"");
    if (phys2Start != std::string::npos) {
        size_t quoteStart = cardStr.find('"', phys2Start + 11);
        size_t quoteEnd   = cardStr.find('"', quoteStart + 1);
        if (quoteStart != std::string::npos && quoteEnd != std::string::npos)
            card.physType2 = cardStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    }

    // Extract elemType (optional)
    size_t elemStart = cardStr.find("\"elemType\"");
    if (elemStart != std::string::npos) {
        size_t quoteStart = cardStr.find('"', elemStart + 10);
        size_t quoteEnd   = cardStr.find('"', quoteStart + 1);
        if (quoteStart != std::string::npos && quoteEnd != std::string::npos)
            card.elemType = cardStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    }

    // Extract superRare (optional boolean)
    size_t superRareStart = cardStr.find("\"superRare\"");
    if (superRareStart != std::string::npos) {
        size_t colonPos = cardStr.find(':', superRareStart);
        if (colonPos != std::string::npos)
            card.superRare = cardStr.find("true", colonPos) != std::string::npos &&
                              cardStr.find("true", colonPos) < cardStr.find_first_of(",}", colonPos);
    }

    // Extract legendary (optional boolean)
    size_t legendaryStart = cardStr.find("\"legendary\"");
    if (legendaryStart != std::string::npos) {
        size_t colonPos = cardStr.find(':', legendaryStart);
        if (colonPos != std::string::npos)
            card.legendary = cardStr.find("true", colonPos) != std::string::npos &&
                              cardStr.find("true", colonPos) < cardStr.find_first_of(",}", colonPos);
    }

    // Extract type
    size_t typeStart = cardStr.find("\"type\"");
    if (typeStart != std::string::npos) {
        size_t quoteStart = cardStr.find('"', typeStart + 6);
        size_t quoteEnd = cardStr.find('"', quoteStart + 1);
        if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
            card.type = cardStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        }
    }
    
    // Extract cost
    size_t costStart = cardStr.find("\"cost\"");
    if (costStart != std::string::npos) {
        size_t numStart = cardStr.find(':', costStart);
        size_t numEnd = cardStr.find(',', numStart);
        if (numEnd == std::string::npos) numEnd = cardStr.find('}', numStart);
        std::string costStr = trim(cardStr.substr(numStart + 1, numEnd - numStart - 1));
        try {
            card.cost = std::stoi(costStr);
        } catch (...) {
            card.cost = 1;
        }
    }
    
    // Extract value
    size_t valueStart = cardStr.find("\"value\"");
    if (valueStart != std::string::npos) {
        size_t numStart = cardStr.find(':', valueStart);
        size_t numEnd = cardStr.find(',', numStart);
        if (numEnd == std::string::npos) numEnd = cardStr.find('}', numStart);
        std::string valueStr = trim(cardStr.substr(numStart + 1, numEnd - numStart - 1));
        try {
            card.value = std::stoi(valueStr);
        } catch (...) {
            card.value = 5;
        }
    }
    
    return card;
}
