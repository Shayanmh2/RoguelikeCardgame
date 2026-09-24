#ifndef CARD_BAR_H
#define CARD_BAR_H

#include <SDL.h>
#include <functional>
#include <string>
#include <vector>

// The player's hand, drawn as card widgets in the band Console reserves at
// the bottom of the screen (Console::setHandRows), with its own input loop.
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
    // Gold border: marks the cards that come with a cost, where a warning is
    // worth something.
    bool        risk = false;
    bool        disabled = false;            // unaffordable or blocked this turn
    // An item icon (EnemyArt::drawItemIcon index) drawn in the space under the
    // text. Gear has no card art of its own, and a name alone did not say
    // sword or shield at a glance. -1 draws nothing.
    int         icon = -1;
    // Gear, relics and the like: nothing to pay, so no cost badge. A "cost: 0"
    // on a relic read as if it were a card you could play.
    bool        item = false;
    // The note's colour. Cyan says "this is what it is"; red says "choosing
    // this destroys something", which the deck screen needs and the forge,
    // one screen over and identical in shape, must not borrow.
    SDL_Color   noteColor{ 79, 214, 214, 255 };
};

// A button beside the hand: End Turn and friends. Same selector, so there is
// one keyboard path to maintain.
struct Action {
    std::string label;
    // Optional second column: what the choice does. Kept separate rather
    // than padded into the label so it can be dimmed and aligned.
    std::string desc;
    bool        disabled = false;

    // A slider row: with `value` set, left and right move it between lo and hi,
    // and the bar is drawn from the live value.
    int* value = nullptr;
    int  lo = 0, hi = 100, step = 5;
    // For a value where bigger means less, like the combat pace (a longer beat
    // is a slower game): the bar fills as the number falls.
    bool invert = false;
    // What to print beside the bar, given the current value.
    std::function<std::string(int)> readout;
    // Called after every nudge, so a volume can be heard while it is set.
    std::function<void()> onChange;

    // Two constructors so the dozen existing { "Skip", false } call sites stay
    // valid alongside the new { "Rest", "heal to full", false } form.
    Action(std::string l, bool d = false)
        : label(std::move(l)), disabled(d) {}
    Action(std::string l, std::string ds, bool d = false)
        : label(std::move(l)), desc(std::move(ds)), disabled(d) {}
    // And a slider: label, the value it moves, its range and its reading.
    Action(std::string l, int* v, int vlo, int vhi, int vstep,
           std::function<std::string(int)> rd, std::function<void()> ch = {})
        : label(std::move(l)), value(v), lo(vlo), hi(vhi), step(vstep),
          readout(std::move(rd)), onChange(std::move(ch)) {}
};

// Called with the hand index under the cursor whenever it changes, -1 on an
// action. Set before select().
void setHoverCallback(const std::function<void(int)>& fn);

// Energy left this turn, drawn as pips beside the hand.
void setEnergy(int current, int max);

// Returns the chosen index: cards first, then actions. -1 is a cancel.
// onIdleTick keeps the battle scene moving while the player thinks.
int select(const std::vector<Card>& cards,
           const std::vector<Action>& actions,
           const std::function<void()>& onIdleTick = nullptr,
           int idleTickMs = 600);

// Draws Card::icon. Set once by the game, so this file needs no art layer.
void setIconRenderer(std::function<void(SDL_Renderer*, int, const SDL_Rect&)> fn);
// For the next pick() only: veil=false skips the dim, and cardlessTopPct sets
// how far down a card-less screen's buttons sit. The seal screen uses both.
void setNextGridStyle(bool veil, int cardlessTopPct);

// Full-screen picker: a centred grid of cards, prompt above, actions below.
// `columns` caps the row width (0 fits the window). Returns as select() does,
// plus -2-i for a card's "+" button.
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
