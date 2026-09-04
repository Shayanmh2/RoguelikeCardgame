#ifndef HUD_H
#define HUD_H

#include <string>

// The combat status panel. Same information Game.cpp used to print as a text
// header, drawn with real geometry instead: a text bar is only ever as smooth
// as its character cell, and it cannot animate.
namespace Hud {

struct State {
    int  turn = 1;
    int  energy = 0, maxEnergy = 0;
    std::string encounter;          // "Encounter 12" / "BOSS"

    int  playerHp = 0, playerMax = 1, playerArmor = 0;
    std::string playerTags;         // fortified / bonuses / status summary

    std::string enemyName;
    int  enemyHp = 0, enemyMax = 1, enemyAtk = 0, enemyDef = 0;
    std::string enemyTags;

    // A summoned add (the Lich raises one). Its own bar, because it is a
    // health pool you have to track.
    std::string addName;
    int  addHp = 0, addMax = 0;
    bool addActive = false;

    std::string notice;             // curse countdown, taunt, bound
};

void set(const State& s);
void setActive(bool on);            // off outside battle - rewards, forge, menus
int  rowsNeeded();                  // console rows the panel occupies

} // namespace Hud

#endif
