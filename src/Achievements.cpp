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
    { "Something Following", "Beat a Moon Shade.",                                   SILVER },
    { "Nothing Grows Back",  "Sear a Hydra stump shut.",                             SILVER },
    { "Heavy Hand",          "Deal 200 damage in one hit.",                          BRONZE },
    { "Hanging by a Thread", "Be saved by the Red Thread.",                          SILVER },
    { "Legend in Hand",      "Carry a legendary card.",                              SILVER },

    { "Whole Again",         "Clear all 50 encounters.",                             GOLD },
    { "Long Slumber",        "Beat the true form.",                                  GOLD },
    { "Walked Twice",        "Clear the hard road.",                                 GOLD },
    { "Every Shape",         "Meet all five Moon Shades.",                           GOLD },
    { "Earthshaker",         "Deal 500 damage in one hit.",                          SILVER },
    { "Spoils",              "Claim a piece of trophy gear.",                        GOLD },

    { "Head Start",          "Clear all 50 encounters with an upgrade on.",          SILVER },
    { "Better Than Wood",    "Claim a piece of gear.",                               BRONZE },
    { "Travel Light",        "Discard a card at a rest site.",                       BRONZE },
    { "Catch Your Breath",   "Rest at a rest site.",                                 BRONZE },
    { "Something Out There", "Meet a Moon Shade.",                                   BRONZE },

    { "Before It Moves",     "Win a fight on your first turn.",                      SILVER },
    { "By a Hair",           "Win a fight on a tenth of your health or less.",       SILVER },
    { "All Four Out",        "Put out all four vigils in one run.",                  SILVER },
    { "Pockets Full",        "Hold five relics at once.",                            SILVER },
    { "One Head Left",       "Cut the Hydra down to one head.",                      SILVER },
    { "The Last Fire",       "Sit by the last fire before the peak.",                SILVER },

    { "No Legends Needed",   "Clear all 50 encounters with no legendary card.",      GOLD },
    { "The Long Way Round",  "Clear Randomized Hard.",                               GOLD },

    { "Her Own Medicine",    "Reverse the Witch's Toxic Eruption.",                  SILVER },
    { "Backdraft",           "Reverse the Dragon's Fire Breath.",                    SILVER },
    { "Unshaken",            "Parry the Colossus's Earthquake Slam.",                SILVER },
    { "The Storm Stops",     "Stun the Thunder Beast.",                              SILVER },
    { "Slow Burn",           "Finish an enemy with poison or burn.",                 BRONZE },
    { "Stone Cold",          "Win a fight while turning to stone.",                  SILVER },

    { "Lightning Strikes Twice", "Get stunned twice in one Thunder Beast fight.",    BRONZE },
    { "It Flew Away",        "Parry something that flies.",                          BRONZE },
    { "Cold Feet",           "Scare an enemy into bracing with Fear.",               BRONZE },
    { "Sidestep",            "Counter a blow with Dodge Reversal.",                  BRONZE },

    { "Clean Cuts",          "Beat the Hydra before it grows a head.",               SILVER },

    { "Skybreaker",          "Deal 1000 damage in one hit.",                         GOLD },
    { "Sleepless",           "Clear all 50 encounters without resting.",             GOLD },

    { "Kept Them All",       "Clear all 50 encounters, discarding nothing.",         SILVER },
    { "Nothing Forgotten",   "Hear every memory from Sit a while.",                  SILVER },
    { "Never Laid a Hand",   "Kill an enemy with only poison, burn and rend.",       SILVER },

    { "How did you even-",   "Die to the tutorial slime.",                           BRONZE },

    { "Just Resting My Eyes", "Rest while already at full health.",                  BRONZE },
    { "Walk It Off",         "Skip resting on a tenth of your health or less.",      BRONZE },
    { "Mind the Edge",       "Die to your own Scrap Shield.",                        BRONZE },
    { "It Was Right There",  "Die with Sacrifice still in your hand.",               BRONZE },
    { "Stop Hitting Yourself", "Let the true form turn your own blow on you.",       BRONZE },
    { "Just a Jab",          "Finish a boss with Quick Jab.",                        SILVER },
    { "Overkill",            "Finish an enemy with 100 damage to spare.",            BRONZE },
    { "Lucky Charm",         "Carry a starter card into your next run.",             BRONZE },
    { "It Said Cursed",      "Take the cursed relic.",                               BRONZE },
    { "Tickle",              "Hit an enemy for no damage at all.",                   BRONZE },
    { "Down to One",         "Discard every card but one.",                          BRONZE },
    { "Sentimental",         "Beat a boss in starter gear while owning better.",     BRONZE },
    { "Having a Bad Day",    "Suffer three ailments at once.",                       BRONZE },
    { "Buyer's Remorse",     "Die to your own Blood Price.",                         BRONZE },
    { "Wide Open",           "Die while Berserk Stance has you exposed.",            BRONZE },
    { "One Trick",           "Clear all 50 encounters with a deck of one card.",     GOLD },
    { "Magician",            "Clear all 50 using only poison, burn and rend cards from encounter 10 on.", GOLD },
    { "Necromancer",         "Finish a boss with an undead you raised.",             SILVER },
    { "No Respite",          "Clear all 50 encounters without taking a boon.",       SILVER },
    { "Left Where They Lie", "Clear all 50 encounters without taking a relic.",      SILVER },
    { "The Short Way",       "Clear the quick road.",                                SILVER },
    { "Grave Mistake",       "Die to your own Raise Undead.",                        BRONZE },
    { "Moonstruck",          "Put out the False Moon, with every other achievement earned.", CRIMSON },
};

// The order each rank's page lists them in: grouped by what they are rather
// than by when they were added. Every id appears exactly once.
const int DISPLAY_ORDER[COUNT] = {
    // bronze: the road's firsts, tricks in a fight, odd choices, odd deaths
    WIN_FIGHT, GEAR, REST, SIT, FORGE, TRAVEL_LIGHT, RELIC, BOON, VIGIL, COLOSSUS, MEET_MOON,
    PARRY, SIDESTEP, STUN, COLD_FEET, FLEW_AWAY, SLOW_BURN, BIG_HIT, OVERKILL, TICKLE,
    FULL_REST, WALK_IT_OFF, DOWN_TO_ONE, LUCKY_CHARM, CURSED_RELIC, SENTIMENTAL, BAD_DAY, TWICE_STRUCK,
    TUTORIAL_SLIME, SCRAP_DEATH, HELD_SACRIFICE, BUYERS_REMORSE, GRAVE_MISTAKE, WIDE_OPEN, OWN_BLOW,
    // silver: the bosses, their tricks in road order, feats in a fight, the run
    WITCH, THUNDER, HYDRA, DRAGON, MOONSTRUCK,
    UNSHAKEN, HER_OWN, STORM_STOPS, ONE_HEAD, SEAR, CLEAN_CUTS, BACKDRAFT,
    FIRST_TURN, BY_A_HAIR, STONE_COLD, HUGE_HIT, JAB_FINISH, NECROMANCER, DOT_ONLY, THREAD,
    LEGEND, COLLECTOR, EVERY_VIGIL, LAST_FIRE, EVERY_SIT, SHORT_WAY, HEAD_START, NO_DISCARD, NO_BOONS, NO_RELICS,
    // gold: the clears, then the long hauls
    CLEAR, TRUE_FORM, HARD, RANDOM_HARD, NO_LEGENDS, NO_REST, ONE_TRICK, MAGICIAN, ALL_FORMS, TROPHY,
    THOUSAND,
    // crimson: everything
    FALSE_MOON,
};

const char* RANK_NAME[4] = { "Bronze", "Silver", "Gold", "Crimson" };
// Lettering, and the card stripe, in each medal's own metal.
const SDL_Color RANK_INK[4]    = { { 226, 160, 100, 255 }, { 212, 218, 230, 255 }, { 250, 222, 130, 255 },
                                   { 236, 104,  92, 255 } };
const SDL_Color RANK_STRIPE[4] = { { 192, 118,  64, 255 }, { 170, 178, 194, 255 }, { 232, 190,  84, 255 },
                                   { 176,  40,  46, 255 } };
const SDL_Color LOCKED_INK{ 120, 120, 132, 255 };

// Earned ones, a bit each: ids 0-63 in the first word, 64 on in the second.
static_assert(COUNT <= 128);
unsigned long long gMask[2] = { 0, 0 };
int gForms = 0;
int gBeaten = 0;
unsigned long long gSits = 0;

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
    EnemyArt::drawMedal(medalFrame(a.rank), SDL_Rect{ box.x + pad, box.y + (h - icon) / 2, icon, icon });
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
        if (INFO[DISPLAY_ORDER[i]].rank == rank) ids.push_back(DISPLAY_ORDER[i]);
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
        c.icon      = EnemyArt::MEDAL_ICON0 + (got ? medalFrame(rank) : EnemyArt::MEDAL_LOCKED);
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
        acts.push_back(CardBar::Action{ "Back", false });

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

bool has(int id) { return id >= 0 && id < COUNT && ((gMask[id / 64] >> (id % 64)) & 1ULL); }

bool earn(int id) {
    if (id < 0 || id >= COUNT || has(id)) return false;
    gMask[id / 64] |= 1ULL << (id % 64);
    gQueue.push_back(id);
    // The reward sound, pitched above every reward (they run 0.8 to 1.15), so
    // an achievement is its own thing. The legendary cue would read as a hit
    // landing.
    Audio::playSFXPitched("upgrade", 1.3f);
    return true;
}

unsigned long long mask(int word) { return word == 0 || word == 1 ? gMask[word] : 0; }
void setMask(unsigned long long m, int word) {
    if (word != 0 && word != 1) return;
    // Only the bits that name an achievement: the rest of the word stays clear.
    const int bits = std::max(0, std::min(64, (int)COUNT - word * 64));
    gMask[word] = bits == 64 ? m : (m & ((1ULL << bits) - 1));
}
int  medalFrame(int rank) { return rank == CRIMSON ? 4 : rank; }
static bool gMoonOut = false;
bool falseMoonBeaten() { return gMoonOut; }
void setFalseMoonBeaten(bool on) { gMoonOut = on; }
bool crimsonDue() {
    if (!gMoonOut || has(FALSE_MOON)) return false;
    for (int i = 0; i < COUNT; i++)
        if (i != FALSE_MOON && !has(i)) return false;
    return true;
}
int  formsBeaten() { return gBeaten; }
void setFormsBeaten(int m) { gBeaten = m & 31; }
bool beatForm(int zone) {
    if (zone >= 0 && zone < 5) gBeaten |= 1 << zone;
    return gBeaten == 31;
}
int  formsSeen() { return gForms; }
void setFormsSeen(int m) { gForms = m & 31; }
bool seeForm(int zone) {
    if (zone >= 0 && zone < 5) gForms |= 1 << zone;
    return gForms == 31;
}
unsigned long long sitsHeard() { return gSits; }
void setSitsHeard(unsigned long long m) { gSits = m; }
bool hearSit(int index, int total) {
    if (index >= 0 && index < 64) gSits |= 1ULL << index;
    const int n = std::max(0, std::min(63, total));
    const unsigned long long all = (1ULL << n) - 1;
    return n > 0 && (gSits & all) == all;
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
int knownCount() {
    int n = 0;
    for (int i = 0; i < COUNT; i++) n += (INFO[i].rank != CRIMSON || has(i)) ? 1 : 0;
    return n;
}

void install() { Platform::setToastRenderer(&draw); }
void draw() { drawToast(); }

void showScreen() {
    while (true) {
        std::vector<CardBar::Card> ranks;
        // Crimson keeps its secret: its page is only there once it is earned.
        std::vector<int> shownRanks{ BRONZE, SILVER, GOLD };
        if (earnedCount(CRIMSON) > 0) shownRanks.push_back(CRIMSON);
        for (int r : shownRanks) {
            CardBar::Card c;
            c.name      = RANK_NAME[r];
            c.effect    = std::to_string(earnedCount((Rank)r)) + " of "
                        + std::to_string(totalCount((Rank)r)) + " earned";
            c.icon      = EnemyArt::MEDAL_ICON0 + medalFrame(r);
            c.tint      = RANK_STRIPE[r];
            c.nameColor = RANK_INK[r];
            c.item      = true;
            ranks.push_back(c);
        }
        const std::string title = "Achievements   " + std::to_string(earnedCount()) + " of "
                                + std::to_string(knownCount()) + " earned";
        const std::vector<CardBar::Action> back{ CardBar::Action{ "Back", false } };
        const int choice = CardBar::pick(title, ranks, back, 3);
        const int ri = choice <= -2 ? -2 - choice : choice;
        if (ri < 0 || ri >= (int)shownRanks.size()) return;
        showRank(shownRanks[ri]);
    }
}

} // namespace Achievements
