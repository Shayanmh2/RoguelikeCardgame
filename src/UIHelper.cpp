#include "UIHelper.h"
#include <iostream>
#include <iomanip>
#include <cmath>

void UIHelper::printLine(int width, char c) {
    for (int i = 0; i < width; ++i) {
        std::cout << c;
    }
    std::cout << "\n";
}

void UIHelper::printBox(const std::string& title, int width) {
    printLine(width, '=');
    if (!title.empty()) {
        int padding = (width - title.length() - 2) / 2;
        std::cout << std::string(padding, ' ') << title << "\n";
        printLine(width, '=');
    }
}

void UIHelper::printBoxEnd(int width) {
    printLine(width, '=');
}

std::string UIHelper::createHealthBar(int current, int max, int width) {
    if (max <= 0) return "";
    
    int filled = static_cast<int>((static_cast<double>(current) / max) * width);
    if (current > 0 && filled == 0) filled = 1;  // Minimum 1 char if alive
    
    std::string bar = "[";
    for (int i = 0; i < width; ++i) {
        if (i < filled) bar += "█";
        else bar += " ";
    }
    bar += "]";
    return bar;
}

std::string UIHelper::createArmorBar(int armor, int width) {
    if (armor < 0) armor = 0;
    
    int filled = std::min(armor / 2, width);  // Scale: 2 armor per character
    std::string bar = "[";
    for (int i = 0; i < width; ++i) {
        if (i < filled) bar += "■";
        else bar += " ";
    }
    bar += "]";
    return bar;
}

void UIHelper::printCentered(const std::string& text, int width) {
    int padding = (width - text.length()) / 2;
    if (padding < 0) padding = 0;
    std::cout << std::string(padding, ' ') << text << "\n";
}

void UIHelper::printStatusHeader() {
    printBox("ROGUELIKE CARDGAME", 60);
}

void UIHelper::printEncounterHeader(int encounterNum, const std::string& difficulty, const std::string& tierLabel) {
    printLine(60, '=');
    std::cout << "  ENCOUNTER " << encounterNum << " | " << difficulty << " [" << tierLabel << "]\n";
    printLine(60, '-');
}

void UIHelper::printBossHeader(int encounterNum, const std::string& bossName) {
    printLine(60, '*');
    printCentered("!!! BOSS ENCOUNTER !!!", 60);
    printCentered("Encounter " + std::to_string(encounterNum), 60);
    printLine(60, '*');
    printCentered(bossName, 60);
    printLine(60, '*');
}

void UIHelper::printCombatStatus(int playerHP, int playerMaxHP, int playerArmor, int playerEnergy, int maxEnergy,
                                  const std::string& enemyName, int enemyHP, int enemyMaxHP, int enemyArmor,
                                  int enemyAttack, int enemyDefense) {
    printLine(60, '-');
    
    // Player status
    std::cout << "PLAYER\n";
    std::cout << "  HP:  " << playerHP << "/" << playerMaxHP << " " << createHealthBar(playerHP, playerMaxHP) << "\n";
    std::cout << "  ARM: " << playerArmor << "        " << createArmorBar(playerArmor) << "\n";
    std::cout << "  NRG: " << playerEnergy << "/" << maxEnergy << "\n\n";
    
    // Enemy status
    std::cout << "ENEMY: " << enemyName << "\n";
    std::cout << "  HP:  " << enemyHP << "/" << enemyMaxHP << " " << createHealthBar(enemyHP, enemyMaxHP) << "\n";
    std::cout << "  ARM: " << enemyArmor << "        " << createArmorBar(enemyArmor) << "\n";
    std::cout << "  ATK: " << enemyAttack << " | DEF: " << enemyDefense << "\n";
    
    printLine(60, '-');
}

void UIHelper::printCardHeader(int count) {
    printBox("HAND (" + std::to_string(count) + " cards)", 60);
}

void UIHelper::printCardRow(int index, const std::string& type, const std::string& name, 
                             int cost, int value, const std::string& description) {
    std::cout << "  " << index << ". [" << std::setw(6) << std::left << type << "] " 
              << std::setw(18) << std::left << name << " (Cost: " << cost << ", Val: " << value << ")\n";
    std::cout << "     " << description << "\n\n";
}

void UIHelper::printTitle() {
    std::cout << "\n";
    printLine(60, '=');
    printCentered("ROGUELIKE CARDGAME", 60);
    printCentered("A turn-based deckbuilder roguelike", 60);
    printLine(60, '=');
    std::cout << "Type 'help' for commands.\n\n";
}

void UIHelper::printGameOverScreen(bool won, int encountersWon, int cardsCollected) {
    std::cout << "\n";
    printLine(60, '=');
    if (won) {
        printCentered("YOU WIN!", 60);
    } else {
        printCentered("YOU LOST!", 60);
    }
    printLine(60, '=');
    std::cout << "\nRun Summary:\n";
    std::cout << "  Encounters Won: " << encountersWon << "\n";
    std::cout << "  Cards Collected: " << cardsCollected << "\n";
    printLine(60, '-');
}
