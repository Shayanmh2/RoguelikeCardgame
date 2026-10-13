#ifndef HUD_H
#define HUD_H

#include <functional>
#include <string>

// The combat status panel, drawn with real geometry so the bars are smooth
// and can animate.
namespace Hud {

struct State {
    int  turn = 1;
    int  energy = 0, maxEnergy = 0;
    std::string encounter;          // "Encounter 12" / "BOSS"

    int  playerHp = 0, playerMax = 1, playerArmor = 0;
    // A card spent the guard this turn. Armour reaching zero on its own is
    // ordinary; being stripped is something the player has to notice.
    bool armorBroken = false;
    // Flat bonuses the player adds to every attack / defend card. Always
    // shown, so the row reads against the enemy's ATK/DEF opposite it.
    int  playerAtk = 0, playerDef = 0;
    // Gear is a multiplier rather than a flat add, so it is shown separately.
    int  playerAtkPct = 0, playerDefPct = 0;
    std::string playerTags;         // fortified / bonuses / status summary

    std::string enemyName;
    std::string enemyTitle, enemyTitleTip;   // a champion's trait, beside the name, and what it does
    int  enemyHp = 0, enemyMax = 1, enemyAtk = 0, enemyDef = 0;
    std::string enemyTags;

    // A summoned add (the Lich raises one). Its own bar, because it is a
    // health pool you have to track.
    std::string addName;
    int  addHp = 0, addMax = 0;
    bool addActive = false;

    // Your own raised dead (Raise Undead): a bar under yours, for the same
    // reason, and what it strikes for, for its tooltip.
    std::string allyName;
    int  allyHp = 0, allyMax = 0, allyStrike = 0;
    bool allyActive = false;

    std::string notice;             // curse countdown, taunt, bound
};

void set(const State& s);
// Read every frame while the panel is up, so health, armour and statuses move
// the moment they change. The band keeps the size the last set() gave it.
void setSource(const std::function<State()>& fn);

// Damage the hovered card would take off the enemy, drawn as a pale bite
// out of the end of its health bar. 0 clears it.
void setPreview(int hpLoss);
void setActive(bool on);            // off outside battle - rewards, forge, menus
int  rowsNeeded();                  // console rows the panel occupies

// How the status tags are shown (Settings, "Status tags"): a symbol in each
// tag's bracket before its words, the words alone, or the symbols alone.
// Pointing at a tag says it in full whichever way it is shown.
enum TagStyle { TAGS_ICONS_AND_TEXT = 0, TAGS_TEXT_ONLY = 1, TAGS_ICONS_ONLY = 2 };
void setTagStyle(int style);

} // namespace Hud

#endif
