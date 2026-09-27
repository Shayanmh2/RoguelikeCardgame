#include "Achievements.h"
#include "Audio.h"
#include "CardBar.h"
#include "Console.h"
#include "DrawUtil.h"
#include "EnemyArt.h"
#include "Platform.h"

#include <SDL.h>
#include <algorithm>
#include <deque>
#include <string>
#include <vector>

namespace Achievements {
namespace {

const Info INFO[COUNT] = {
    { "Wooden Sword",        "Win a fight.",                                         BRONZE },
    { "Strength Back",       "Beat the Stone Colossus.",                             BRONZE },
    { "Lights Out",          "Put out a vigil.",                                     BRONZE },
    { "By the Fire",         "Sit a while at a rest site.",                          BRONZE },
    { "Tempered",            "Forge a card at a rest site.",                         BRONZE },
    { "Left for You",        "Take a relic.",                                        BRONZE },
    { "A Moment's Respite",  "Take a boon.",                                         BRONZE },
    { "Riposte",             "Parry a blow and hit back.",                           BRONZE },
    { "Out Cold",            "Stun an enemy.",                                       BRONZE },

    { "Clear Head",          "Beat the Vile Witch.",                                 SILVER },
    { "Quick Again",         "Beat the Thunder Beast.",                              SILVER },
    { "Still Water",         "Beat the Hydra.",                                      SILVER },
    { "Soul Back",           "Beat the Undead Dragon.",                              SILVER },
    { "Something Following", "Beat the Moonstruck.",                                 SILVER },
    { "Nothing Grows Back",  "Sear a Hydra stump shut.",                             SILVER },
    { "Heavy Hand",          "Deal 200 damage in one hit.",                          SILVER },
    { "Hanging by a Thread", "Be saved by the Red Thread.",                          SILVER },
    { "Legend in Hand",      "Carry a legendary card.",                              SILVER },

    { "Whole Again",         "Clear the fifty.",                                     GOLD },
    { "Long Slumber",        "Beat the true form.",                                  GOLD },
    { "Walked Twice",        "Clear the hard road.",                                 GOLD },
    { "Every Shape",         "Meet all five Moonstruck.",                            GOLD },
    { "Earthshaker",         "Deal 500 damage in one hit.",                          GOLD },
    { "Spoils",              "Claim a piece of trophy gear.",                        GOLD },

    { "Head Start",          "Clear the fifty with an upgrade switched on.",         BRONZE },
    { "Better Than Wood",    "Claim a piece of gear.",                               BRONZE },
    { "Travel Light",        "Throw a card away at a rest site.",                    BRONZE },
    { "Catch Your Breath",   "Rest at a rest site.",                                 BRONZE },
    { "Something Out There", "Meet the Moonstruck.",                                 BRONZE },

    { "Before It Moves",     "Win a fight on your first turn.",                      SILVER },
    { "By a Hair",           "Win a fight on a tenth of your health or less.",       SILVER },
    { "All Four Out",        "Put out all four vigils in one run.",                  SILVER },
    { "Pockets Full",        "Hold five relics at once.",                            SILVER },
    { "One Head Left",       "Cut the Hydra down to one head.",                      SILVER },
    { "The Last Fire",       "Sit by the last fire before the peak.",                SILVER },

    { "No Legends Needed",   "Beat the last boss with no legendary card.",           GOLD },
    { "The Long Way Round",  "Clear Random Hard.",                                   GOLD },

    { "Her Own Medicine",    "Reverse the Witch's Toxic Eruption.",                  SILVER },
    { "Backdraft",           "Reverse the Dragon's Fire Breath.",                    SILVER },
    { "Unshaken",            "Parry the Colossus's Earthquake Slam.",                SILVER },
    { "The Storm Stops",     "Stun the Thunder Beast.",                              SILVER },
    { "Slow Burn",           "Finish an enemy with poison or burn.",                 SILVER },
    { "Stone Cold",          "Win a fight while turning to stone.",                  SILVER },

    { "Lightning Strikes Twice", "Get stunned twice in one Thunder Beast fight.",    BRONZE },
    { "It Flew Away",        "Parry something that flies.",                          BRONZE },
    { "Cold Feet",           "Scare an enemy into bracing with Fear.",               BRONZE },
    { "Sidestep",            "Counter a blow with Dodge Reversal.",                  BRONZE },

    { "Clean Cuts",          "Beat the Hydra before it grows a head.",               GOLD },
};

const char* RANK_NAME[3] = { "Bronze", "Silver", "Gold" };
// Lettering, and the card stripe, in each medal's own metal.
const SDL_Color RANK_INK[3]    = { { 226, 160, 100, 255 }, { 212, 218, 230, 255 }, { 250, 222, 130, 255 } };
const SDL_Color RANK_STRIPE[3] = { { 192, 118,  64, 255 }, { 170, 178, 194, 255 }, { 232, 190,  84, 255 } };
const SDL_Color LOCKED_INK{ 120, 120, 132, 255 };

unsigned long long gMask = 0;
int gForms = 0;

// Notices waiting their turn, and the one on screen.
std::deque<int> gQueue;
int    gShowing = -1;
Uint32 gShownAt = 0;
constexpr Uint32 SLIDE_MS = 280, HOLD_MS = 3200;

int rad() { return std::max(4, Platform::cellH() / 3); }

// Slides in from the right edge, holds, and slides back out; the next one in
// the queue follows it.
void drawToast() {
    if (gShowing < 0) {
        if (gQueue.empty()) return;
        gShowing = gQueue.front();
        gQueue.pop_front();
        gShownAt = SDL_GetTicks();
    }
    const Uint32 t = SDL_GetTicks() - gShownAt;
    if (t >= SLIDE_MS * 2 + HOLD_MS) { gShowing = -1; return; }
    float k = t < SLIDE_MS ? t / (float)SLIDE_MS
            : t < SLIDE_MS + HOLD_MS ? 1.0f
            : 1.0f - (t - SLIDE_MS - HOLD_MS) / (float)SLIDE_MS;
    k = 1.0f - (1.0f - k) * (1.0f - k);

    SDL_Renderer* r = Platform::renderer();
    const Info& a = INFO[gShowing];
    const int cw = std::max(1, Platform::cellW()), ch = std::max(1, Platform::cellH());
    const int icon = 24 * std::max(3, ch * 3 / 24);
    const std::string label = std::string(RANK_NAME[a.rank]) + " achievement";
    const int textW = std::max((int)label.size() * cw,
                               (int)std::string(a.name).size() * Console::bigCellW());
    const int pad = std::max(10, cw);
    const int w = pad + icon + pad + textW + pad * 2;
    const int h = std::max(icon, ch + Console::bigCellH() + 4) + pad * 2;
    const int margin = 22;
    const SDL_Rect box{ Platform::screenW() - (int)(k * (w + margin)), margin, w, h };

    DrawUtil::glowRound(r, box, rad(), SDL_Color{ 0, 0, 0, 255 }, 8, 110);
    DrawUtil::fillRound(r, box, rad(), Platform::groundTone(26));
    DrawUtil::frameRound(r, box, rad(), RANK_STRIPE[a.rank]);
    EnemyArt::drawMedal(a.rank, SDL_Rect{ box.x + pad, box.y + (h - icon) / 2, icon, icon });
    const int tx = box.x + pad * 2 + icon;
    const int ty = box.y + (h - (ch + Console::bigCellH() + 4)) / 2;
    Console::drawTextPx(r, tx, ty, label, SDL_Color{ 150, 150, 168, 255 }, false);
    Console::drawTextBigPx(r, tx, ty + ch + 4, a.name, RANK_INK[a.rank], true);
}

// Until the game has been cleared, the ones not yet earned keep their secret.
const char* HIDDEN_TEXT = "Hidden until you clear the game.";
bool revealed() { return has(CLEAR); }

// One rank's achievements as cards: its medal when earned, the dark one when
// not. Two rows at most to a screen, in pages of even size: a whole rank on
// one screen squeezed the cards until the medals and the words fell off them.
void showRank(int rank) {
    std::vector<int> ids;
    for (int i = 0; i < COUNT; i++)
        if (INFO[i].rank == rank) ids.push_back(i);
    std::vector<CardBar::Card> cards;
    for (int id : ids) {
        const bool got = has(id);
        const bool shown = got || revealed();
        CardBar::Card c;
        c.name      = shown ? INFO[id].name : "???";
        c.elemTag   = std::string("[") + RANK_NAME[rank] + "]";
        c.effect    = shown ? INFO[id].text : HIDDEN_TEXT;
        c.note      = got ? "earned" : "not yet";
        c.noteColor = got ? SDL_Color{ 79, 214, 214, 255 } : LOCKED_INK;
        c.icon      = EnemyArt::MEDAL_ICON0 + (got ? rank : EnemyArt::MEDAL_LOCKED);
        c.tint      = got ? RANK_STRIPE[rank] : SDL_Color{ 70, 70, 80, 255 };
        c.nameColor = got ? RANK_INK[rank] : LOCKED_INK;
        c.item      = true;
        cards.push_back(c);
    }
    const int COLUMNS = 5, MOST = 2 * COLUMNS;
    const int total = (int)cards.size();
    const int pages = std::max(1, (total + MOST - 1) / MOST);
    const int perPage = std::max(1, (total + pages - 1) / pages);
    int page = 0;
    while (true) {
        const int first = page * perPage;
        const int last = std::min(total, first + perPage);
        const std::vector<CardBar::Card> shown(cards.begin() + first, cards.begin() + last);
        std::string title = std::string(RANK_NAME[rank]) + "   "
            + std::to_string(earnedCount((Rank)rank)) + " of " + std::to_string(total) + " earned";
        if (pages > 1)
            title += "   page " + std::to_string(page + 1) + " of " + std::to_string(pages);
        std::vector<CardBar::Action> acts;
        int prevAt = -1, nextAt = -1;
        if (page > 0)         { prevAt = (int)acts.size(); acts.push_back(CardBar::Action{ "Previous page", false }); }
        if (page < pages - 1) { nextAt = (int)acts.size(); acts.push_back(CardBar::Action{ "Next page", false }); }
        acts.push_back(CardBar::Action{ "Back", "to the ranks", false });

        const int choice = CardBar::pick(title, shown, acts, COLUMNS);
        const int ci = choice <= -2 ? -2 - choice : choice;
        if (ci < 0) return;
        if (ci < (int)shown.size()) {
            CardBar::showDetail(shown[ci], shown[ci].effect, "ACHIEVEMENT",
                                has(ids[first + ci]) ? "EARNED" : "NOT YET", 0);
            continue;
        }
        const int ai = ci - (int)shown.size();
        if (ai == prevAt)      page--;
        else if (ai == nextAt) page++;
        else return;
    }
}

} // namespace

const Info& info(int id) { return INFO[std::max(0, std::min(COUNT - 1, id))]; }

bool has(int id) { return id >= 0 && id < COUNT && ((gMask >> id) & 1ULL); }

bool earn(int id) {
    if (id < 0 || id >= COUNT || has(id)) return false;
    gMask |= 1ULL << id;
    gQueue.push_back(id);
    Audio::playSFX(INFO[id].rank == GOLD ? "legendary" : "upgrade");
    return true;
}

unsigned long long mask() { return gMask; }
void setMask(unsigned long long m) { gMask = m & ((1ULL << COUNT) - 1); }
int  formsSeen() { return gForms; }
void setFormsSeen(int m) { gForms = m & 31; }
bool seeForm(int zone) {
    if (zone >= 0 && zone < 5) gForms |= 1 << zone;
    return gForms == 31;
}

int earnedCount() {
    int n = 0;
    for (int i = 0; i < COUNT; i++) n += has(i) ? 1 : 0;
    return n;
}
int earnedCount(Rank r) {
    int n = 0;
    for (int i = 0; i < COUNT; i++) n += (INFO[i].rank == r && has(i)) ? 1 : 0;
    return n;
}
int totalCount(Rank r) {
    int n = 0;
    for (int i = 0; i < COUNT; i++) n += INFO[i].rank == r ? 1 : 0;
    return n;
}

void install() { Platform::setToastRenderer(&draw); }
void draw() { drawToast(); }

void showScreen() {
    while (true) {
        std::vector<CardBar::Card> ranks;
        for (int r = 0; r < 3; r++) {
            CardBar::Card c;
            c.name      = RANK_NAME[r];
            c.effect    = std::to_string(earnedCount((Rank)r)) + " of "
                        + std::to_string(totalCount((Rank)r)) + " earned";
            c.icon      = EnemyArt::MEDAL_ICON0 + r;
            c.tint      = RANK_STRIPE[r];
            c.nameColor = RANK_INK[r];
            c.item      = true;
            ranks.push_back(c);
        }
        const std::string title = "Achievements   " + std::to_string(earnedCount()) + " of "
                                + std::to_string((int)COUNT) + " earned";
        const std::vector<CardBar::Action> back{ CardBar::Action{ "Back", false } };
        const int choice = CardBar::pick(title, ranks, back, 3);
        const int ri = choice <= -2 ? -2 - choice : choice;
        if (ri < 0 || ri >= 3) return;
        showRank(ri);
    }
}

} // namespace Achievements
