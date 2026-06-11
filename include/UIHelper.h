#pragma once

#include <string>
#include <vector>

class UIHelper {
public:
    // Draw decorative elements
    static void printLine(int width = 60, char c = '-');
    static void printBox(const std::string& title, int width = 60);
    static void printBoxEnd(int width = 60);
    
    // Health bar visualization
    static std::string createHealthBar(int current, int max, int width = 20);
    static std::string createArmorBar(int armor, int width = 15);
    
    // Centered text
    static void printCentered(const std::string& text, int width = 60);
    
    // Status display helpers
    static void printStatusHeader();
    static void printEncounterHeader(int encounterNum, const std::string& difficulty, const std::string& tierLabel);
    static void printBossHeader(int encounterNum, const std::string& bossName);
    static void printCombatStatus(int playerHP, int playerMaxHP, int playerArmor, int playerEnergy, int maxEnergy,
                                   const std::string& enemyName, int enemyHP, int enemyMaxHP, int enemyArmor, 
                                   int enemyAttack, int enemyDefense);
    
    // Card display
    static void printCardHeader(int count);
    static void printCardRow(int index, const std::string& type, const std::string& name, 
                             int cost, int value, const std::string& description);
    
    // Title screen
    static void printTitle();
    static void printGameOverScreen(bool won, int encountersWon, int cardsCollected);

    // Animated output — handles ANSI codes and UTF-8 characters correctly.
    // msPerChar=0 prints instantly (useful for toggling from call sites).
    static void typeWrite(const std::string& text, int msPerChar = 10);

    // Sleep for ms milliseconds (cross-platform wrapper).
    static void pause(int ms);
};
