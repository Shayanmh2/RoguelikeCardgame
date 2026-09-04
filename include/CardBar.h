#ifndef CARD_BAR_H
#define CARD_BAR_H

#include <SDL.h>
#include <functional>
#include <string>
#include <vector>

// The player's hand, drawn as real card widgets in the band Console reserves at
// the bottom of the screen (Console::setHandRows).
//
// Its own selector, with its own input loop. The text menus it replaced were
// row-based, and cards are rectangles on a pixel grid; the two geometries did
// not want to share code.
namespace CardBar {

struct Card {
    std::string name;
    std::string effect;      // short line: "6 dmg", "+8 armor"
    std::string typeLabel;   // ATTACK / DEFEND / SPECIAL - details panel only
    std::string elemTag;     // damage school: [Smash], [Pierce][Wind], [Fire]...
    std::string note;        // per-screen state, e.g. the Forge's "2 upgrades left"
    int         cost = 0;
    SDL_Color   tint{ 240, 101, 92, 255 };   // type stripe along the top edge
    SDL_Color   nameColor{ 228, 228, 238, 255 };  // rarity tint, as the text UI had
    bool        rare = false;                // gold border
    bool        disabled = false;            // unaffordable or blocked this turn
};

// A button beside the hand: End Turn and friends. Same selector, so there is
// one keyboard path to maintain.
struct Action {
    std::string label;
    // Optional second column: what the choice does. Kept separate rather
    // than padded into the label so it can be dimmed and aligned.
    std::string desc;
    bool        disabled = false;

    // Two constructors so the dozen existing { "Skip", false } call sites stay
    // valid alongside the new { "Rest", "heal to full", false } form.
    Action(std::string l, bool d = false)
        : label(std::move(l)), disabled(d) {}
    Action(std::string l, std::string ds, bool d = false)
        : label(std::move(l)), desc(std::move(ds)), disabled(d) {}
};

// Returns the chosen index: cards first, then actions. -1 is a cancel.
// onIdleTick keeps the battle scene breathing while the player thinks.
// Called with the hand index under the cursor whenever it changes, and -1
// when the highlight is on an action instead. Set before select().
void setHoverCallback(const std::function<void(int)>& fn);

// Energy left this turn, drawn as pips beside the hand. The panel readout
// is in the opposite corner from where anyone is looking while choosing.
void setEnergy(int current, int max);

// Energy left this turn, drawn as pips beside the hand. The panel readout
// is in the opposite corner from where anyone is looking while choosing.
void setEnergy(int current, int max);

int select(const std::vector<Card>& cards,
           const std::vector<Action>& actions,
           const std::function<void()>& onIdleTick = nullptr,
           int idleTickMs = 600);

// Full-screen picker: a centred grid of cards, prompt above, actions below.
// The reward choice, View Deck, the Forge and the rest site all use it.
//
// `columns` caps the row width; 0 fits as many as the window allows. Returns
// as select() does, plus -2-i for a card's "+" button.
int pick(const std::string& title,
         const std::vector<Card>& cards,
         const std::vector<Action>& actions,
         int columns = 0);

// Details panel for one card, opened by its "+" button.
void showDetail(const Card& c, const std::string& description,
                const std::string& typeLabel, const std::string& rarityLabel,
                int upgrades);

} // namespace CardBar

#endif
