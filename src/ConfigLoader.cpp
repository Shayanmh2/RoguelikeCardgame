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

std::string ConfigLoader::extractValue(const std::string& line, const std::string& key) {
    size_t pos = line.find(key);
    if (pos == std::string::npos) return "";
    
    pos = line.find(':', pos);
    if (pos == std::string::npos) return "";
    
    size_t start = line.find('"', pos);
    if (start == std::string::npos) {
        // Try parsing as number
        size_t numStart = pos + 1;
        size_t numEnd = line.find_first_of(",}", numStart);
        if (numEnd == std::string::npos) numEnd = line.length();
        return trim(line.substr(numStart, numEnd - numStart));
    }
    
    size_t end = line.find('"', start + 1);
    if (end == std::string::npos) return "";
    
    return line.substr(start + 1, end - start - 1);
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

std::vector<ConfigLoader::EnemyTypeData> ConfigLoader::loadEnemyTypes(const std::string& configPath) {
    std::vector<EnemyTypeData> types;
    std::ifstream file(configPath);
    
    if (!file.is_open()) {
        std::cerr << "Warning: Could not load enemy config from " << configPath << std::endl;
        return types;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    // Find enemyTypes array
    size_t typesPos = content.find("\"enemyTypes\"");
    if (typesPos == std::string::npos) return types;
    
    size_t arrayStart = content.find('[', typesPos);
    size_t arrayEnd = content.find(']', arrayStart);
    
    std::string typesSection = content.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
    
    // Parse individual type objects
    size_t pos = 0;
    while (pos < typesSection.length()) {
        size_t objStart = typesSection.find('{', pos);
        if (objStart == std::string::npos) break;
        
        size_t objEnd = typesSection.find('}', objStart);
        if (objEnd == std::string::npos) break;
        
        std::string typeStr = typesSection.substr(objStart, objEnd - objStart + 1);
        
        EnemyTypeData type;
        
        // Extract type name
        size_t typeStart = typeStr.find("\"type\"");
        if (typeStart != std::string::npos) {
            size_t quoteStart = typeStr.find('"', typeStart + 6);
            size_t quoteEnd = typeStr.find('"', quoteStart + 1);
            if (quoteStart != std::string::npos && quoteEnd != std::string::npos) {
                type.type = typeStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
            }
        }
        
        // Extract attack probability
        size_t attackStart = typeStr.find("\"baseAttackProbability\"");
        if (attackStart != std::string::npos) {
            size_t numStart = typeStr.find(':', attackStart);
            size_t numEnd = typeStr.find(',', numStart);
            std::string attackStr = trim(typeStr.substr(numStart + 1, numEnd - numStart - 1));
            try {
                type.baseAttackProb = std::stoi(attackStr);
            } catch (...) {
                type.baseAttackProb = 50;
            }
        }
        
        // Extract defend probability
        size_t defendStart = typeStr.find("\"baseDefendProbability\"");
        if (defendStart != std::string::npos) {
            size_t numStart = typeStr.find(':', defendStart);
            size_t numEnd = typeStr.find(',', numStart);
            std::string defendStr = trim(typeStr.substr(numStart + 1, numEnd - numStart - 1));
            try {
                type.baseDefendProb = std::stoi(defendStr);
            } catch (...) {
                type.baseDefendProb = 50;
            }
        }
        
        // Extract heal threshold (default to 33 if not present)
        type.healThreshold = 33;
        size_t healStart = typeStr.find("\"healThreshold\"");
        if (healStart != std::string::npos) {
            size_t numStart = typeStr.find(':', healStart);
            size_t numEnd = typeStr.find(',', numStart);
            std::string healStr = trim(typeStr.substr(numStart + 1, numEnd - numStart - 1));
            try {
                type.healThreshold = std::stoi(healStr);
            } catch (...) {
                type.healThreshold = 33;
            }
        }
        
        // Extract names array
        size_t namesStart = typeStr.find("\"names\"");
        if (namesStart != std::string::npos) {
            size_t namesArrayStart = typeStr.find('[', namesStart);
            size_t namesArrayEnd = typeStr.find(']', namesArrayStart);
            std::string namesStr = typeStr.substr(namesArrayStart + 1, namesArrayEnd - namesArrayStart - 1);
            
            size_t namePos = 0;
            while (namePos < namesStr.length()) {
                size_t quoteStart = namesStr.find('"', namePos);
                if (quoteStart == std::string::npos) break;
                size_t quoteEnd = namesStr.find('"', quoteStart + 1);
                if (quoteEnd == std::string::npos) break;
                
                type.names.push_back(namesStr.substr(quoteStart + 1, quoteEnd - quoteStart - 1));
                namePos = quoteEnd + 1;
            }
        }
        
        if (!type.type.empty()) {
            types.push_back(type);
        }
        
        pos = objEnd + 1;
    }
    
    return types;
}

std::unordered_map<std::string, std::string> ConfigLoader::loadTierPrefixes(const std::string& configPath) {
    std::unordered_map<std::string, std::string> prefixes;
    std::ifstream file(configPath);
    
    if (!file.is_open()) {
        return prefixes;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    // Find tierPrefixes object
    size_t prefixPos = content.find("\"tierPrefixes\"");
    if (prefixPos == std::string::npos) return prefixes;
    
    size_t objStart = content.find('{', prefixPos);
    size_t objEnd = content.find('}', objStart);
    
    std::string prefixStr = content.substr(objStart + 1, objEnd - objStart - 1);
    
    // Parse each prefix pair
    size_t pos = 0;
    while (pos < prefixStr.length()) {
        size_t keyStart = prefixStr.find('"', pos);
        if (keyStart == std::string::npos) break;
        size_t keyEnd = prefixStr.find('"', keyStart + 1);
        if (keyEnd == std::string::npos) break;
        
        std::string key = prefixStr.substr(keyStart + 1, keyEnd - keyStart - 1);
        
        size_t valStart = prefixStr.find('"', keyEnd + 1);
        if (valStart == std::string::npos) break;
        size_t valEnd = prefixStr.find('"', valStart + 1);
        if (valEnd == std::string::npos) break;
        
        std::string value = prefixStr.substr(valStart + 1, valEnd - valStart - 1);
        prefixes[key] = value;
        
        pos = valEnd + 1;
    }
    
    return prefixes;
}
