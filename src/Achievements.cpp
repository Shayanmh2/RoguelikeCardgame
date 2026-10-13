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
    { "By a Hair",           "Win a fight with a tenth of your health or less.",       SILVER },
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

    { "Just Resting My Eyes", "Rest when you are already at full health.",                  BRONZE },
    { "Walk It Off",         "Skip resting with a tenth of your health or less.",      BRONZE },
    { "Mind the Edge",       "Die to your own Scrap Shield.",                        BRONZE },
    { "It Was Right There",  "Die with Sacrifice still in your hand.",               BRONZE },
    { "Stop Hitting Yourself", "Let the true form turn your own blow on you.",       BRONZE },
    { "Just a Jab",          "Finish a boss with Quick Jab.",                        SILVER },
    { "Overkill",            "Finish an enemy with 100 damage to spare.",            BRONZE },
    { "Lucky Charm",         "Carry a starter card into your next run.",             BRONZE },
    { "It Said Cursed",      "Take the cursed relic.",                               BRONZE },
    { "Tickle",              "Hit an enemy for no damage at all.",                   BRONZE },
    { "Down to One",         "Discard every card but one.",                          BRONZE },
    { "Sentimental",         "Beat a boss in your starter gear when you own better.",     BRONZE },
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
    { "A Full Ledger",       "Hold every card in the game, over any number of runs.", GOLD },
    { "Lorekeeper",          "Meet every enemy in the Bestiary and find every page of the Lore Book.", GOLD },
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
    THOUSAND, EVERY_CARD, LOREKEEPER,
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

// Slides in from the right, holds, slides out, and the next one follows.
// Each toast has its own chime pitched above every reward sound (those run
// 0.8 to 1.15), and waits a moment after a reward's chime instead of landing
// on top of it.
const Uint32 CHIME_GAP_MS = 450;
Uint32 gChimeAt = 0;
bool gChimed = true;

void drawToast() {
    if (gShowing < 0) {
        if (gQueue.empty()) return;
        gShowing = gQueue.front();
        gQueue.pop_front();
        gShownAt = SDL_GetTicks();
        const Uint32 last = Audio::lastPlayed("upgrade");
        gChimeAt = std::max(gShownAt, last ? last + CHIME_GAP_MS : 0u);
        gChimed = false;
    }
    if (!gChimed && SDL_GetTicks() >= gChimeAt) {
        Audio::playSFXPitched("upgrade", 1.3f);
        gChimed = true;
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

// --- the ledger --------------------------------------------------------------
// One rank at a time, a row each: medal, name, what it asks, earned or not,
// with the ranks as tabs along the top with their counts. I mocked up three
// layouts and this one won. Crimson's tab only shows once it's earned, and
// until you've cleared the game the unearned ones stay ???.
struct Ledger {
    int tab = 0, sel = 0, top = 0;
    std::vector<int> ranks;   // the tabs shown
    std::vector<int> ids;     // the open rank's achievements, in DISPLAY_ORDER
};
const Ledger* gLedger = nullptr;
std::function<bool(int, int&, int&)> gProgress;   // setProgress()

struct LedgerLayout {
    std::vector<SDL_Rect> tabs, rows;
    int capacity = 1, pad = 16, titleY = 0, countY = 0, hintY = 0, nameX = 0, textX = 0;
    SDL_Rect track{ 0, 0, 0, 0 };   // the slider, when a rank has more than fit
};

LedgerLayout layoutLedger(const Ledger& v) {
    LedgerLayout L;
    const int W = Platform::screenW(), H = Platform::screenH();
    const int ch = std::max(1, Platform::cellH());
    L.pad = std::max(16, W / 48);
    const int left = L.pad, width = W - L.pad * 2;
    L.titleY = L.pad;
    L.countY = L.titleY + ch + 4;
    int y = L.countY + Console::bigCellH() + L.pad;
    const int n = std::max(1, (int)v.ranks.size());
    const int gap = L.pad / 2;
    const int tabW = (width - gap * (n - 1)) / n, tabH = ch * 2;
    for (int i = 0; i < n; ++i) L.tabs.push_back(SDL_Rect{ left + i * (tabW + gap), y, tabW, tabH });
    y += tabH + L.pad;
    L.hintY = H - L.pad - ch;
    const int rowH = ch * 2, rowGap = 6;
    const int room = L.hintY - L.pad / 2 - y;
    L.capacity = std::max(1, (room + rowGap) / (rowH + rowGap));
    const int count = std::min(L.capacity, (int)v.ids.size());
    // More than fit: the rows give up a strip on the right for the slider.
    const int trackW = std::max(6, std::max(1, Platform::cellW()) * 2 / 3);
    const bool more = (int)v.ids.size() > L.capacity;
    const int rowW = more ? width - trackW - L.pad / 2 : width;
    for (int i = 0; i < count; ++i) L.rows.push_back(SDL_Rect{ left, y + i * (rowH + rowGap), rowW, rowH });
    if (more) L.track = SDL_Rect{ left + width - trackW, y, trackW, L.capacity * (rowH + rowGap) - rowGap };
    L.nameX = left + rowH + L.pad;      // after the medal
    L.textX = left + width * 34 / 100;  // what it asks
    return L;
}

void drawLedger() {
    if (!gLedger) return;
    const Ledger& v = *gLedger;
    SDL_Renderer* r = Platform::renderer();
    const int W = Platform::screenW(), H = Platform::screenH();
    const int cw = std::max(1, Platform::cellW()), ch = std::max(1, Platform::cellH());
    const LedgerLayout L = layoutLedger(v);
    const SDL_Color panel{ 28, 26, 32, 255 }, raised{ 40, 37, 46, 255 }, line{ 58, 54, 66, 255 },
                    lime{ 166, 226, 46, 255 }, earnedInk{ 79, 214, 214, 255 }, bright{ 214, 214, 226, 255 },
                    dim{ 150, 150, 168, 255 };
    SDL_SetRenderDrawColor(r, 10, 9, 12, 255);
    SDL_Rect full{ 0, 0, W, H };
    SDL_RenderFillRect(r, &full);

    // What this is, and how many of the ones the player knows of are earned.
    Console::drawTextPx(r, L.pad, L.titleY, "ACHIEVEMENTS", RANK_INK[GOLD], true);
    Console::drawTextBigPx(r, L.pad, L.countY, std::to_string(earnedCount()) + " of " + std::to_string(knownCount())
                           + " earned", SDL_Color{ 232, 232, 240, 255 }, true);

    // The ranks, each with its count and a bar.
    for (size_t i = 0; i < L.tabs.size(); ++i) {
        const int rank = v.ranks[i];
        const SDL_Rect& t = L.tabs[i];
        const bool on = (int)i == v.tab;
        DrawUtil::fillRound(r, t, rad(), on ? raised : panel);
        DrawUtil::frameRound(r, t, rad(), on ? lime : line);
        const int icon = std::max(8, t.h - 12);
        EnemyArt::drawMedal(medalFrame(rank), SDL_Rect{ t.x + 8, t.y + (t.h - icon) / 2, icon, icon });
        const int got = earnedCount((Rank)rank), all = totalCount((Rank)rank);
        const std::string label = std::string(RANK_NAME[rank]) + "  " + std::to_string(got) + " / "
                                + std::to_string(all);
        const int tx = t.x + 8 + icon + 10;
        Console::drawTextPx(r, tx, t.y + (t.h - ch) / 2, label, RANK_INK[rank], on);
        const int bx = tx + ((int)label.size() + 2) * cw, bw = t.x + t.w - 14 - bx;
        if (bw > 20) {
            const SDL_Rect back{ bx, t.y + t.h / 2 - 3, bw, 6 };
            SDL_SetRenderDrawColor(r, 18, 17, 22, 255);
            SDL_RenderFillRect(r, &back);
            SDL_Rect fill = back;
            fill.w = all > 0 ? bw * got / all : 0;
            SDL_SetRenderDrawColor(r, RANK_STRIPE[rank].r, RANK_STRIPE[rank].g, RANK_STRIPE[rank].b, 255);
            if (fill.w > 0) SDL_RenderFillRect(r, &fill);
        }
    }

    // A row each: the medal (dark until earned), the name, what it asks, and
    // whether it is earned.
    const int rank = v.ranks[(size_t)v.tab];
    for (size_t i = 0; i < L.rows.size(); ++i) {
        const int at = v.top + (int)i;
        if (at >= (int)v.ids.size()) break;
        const int id = v.ids[(size_t)at];
        const SDL_Rect& q = L.rows[i];
        const bool sel = at == v.sel, got = has(id), shown = got || revealed();
        DrawUtil::fillRound(r, q, rad(), sel ? raised : panel);
        DrawUtil::frameRound(r, q, rad(), sel ? lime : line);
        const int icon = std::max(8, q.h - 10);
        EnemyArt::drawMedal(got ? medalFrame(rank) : EnemyArt::MEDAL_LOCKED, SDL_Rect{ q.x + 10, q.y + 5, icon, icon });
        const int ty = q.y + (q.h - ch) / 2;
        Console::drawTextPx(r, L.nameX, ty, shown ? INFO[id].name : "???", got ? RANK_INK[rank] : LOCKED_INK, true);
        const std::string text = shown ? INFO[id].text : HIDDEN_TEXT;
        // The long ones say how far along you are instead of just "not yet".
        int have = 0, need = 0;
        const bool counted = !got && shown && gProgress && gProgress(id, have, need) && need > 0;
        const std::string status = got ? "earned" : counted ? std::to_string(have * 100 / need) + "%" : "not yet";
        const int statusX = q.x + q.w - 14 - (int)status.size() * cw;
        const SDL_Color ink = got ? bright : dim;
        if ((int)text.size() * cw <= statusX - L.textX - cw * 2)
            Console::drawTextPx(r, L.textX, ty, text, ink, false);
        else   // a long one in the card face's smaller letters rather than over the status
            Console::drawTextSmallPx(r, L.textX, q.y + (q.h - Console::smallCellH()) / 2, text, ink);
        Console::drawTextPx(r, statusX, ty, status, got ? earnedInk : counted ? RANK_INK[rank] : LOCKED_INK, counted);
    }
    // Where the list is when there's more than fits: drag it, or click along it.
    const int n = (int)v.ids.size();
    DrawUtil::drawScroll(r, L.track, v.top, std::max(0, n - L.capacity), L.capacity, n, panel,
                         SDL_Color{ 104, 100, 118, 255 });

    const std::string hint = std::string("up and down to choose     left and right, or L2 and R2, for another rank")
                           + (n > L.capacity ? "     PgUp and PgDn to scroll" : "") + "     Esc to close";
    Console::drawTextPx(r, (W - (int)hint.size() * cw) / 2, L.hintY, hint, SDL_Color{ 120, 116, 132, 255 }, false);
}

} // namespace

const Info& info(int id) { return INFO[std::max(0, std::min(COUNT - 1, id))]; }

bool has(int id) { return id >= 0 && id < COUNT && ((gMask[id / 64] >> (id % 64)) & 1ULL); }

bool earn(int id) {
    if (id < 0 || id >= COUNT || has(id)) return false;
    gMask[id / 64] |= 1ULL << (id % 64);
    gQueue.push_back(id);
    // The chime plays when the toast comes in (drawToast), not here. Earned along
    // with a reward, or several at the end of a run, they all landed at once as
    // one muddy noise.
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
void setProgress(std::function<bool(int, int&, int&)> fn) { gProgress = std::move(fn); }
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
    Ledger v;
    v.ranks = { BRONZE, SILVER, GOLD };
    if (earnedCount(CRIMSON) > 0) v.ranks.push_back(CRIMSON);   // crimson keeps its secret
    int selFor[4] = { 0, 0, 0, 0 }, topFor[4] = { 0, 0, 0, 0 };
    auto open = [&](int tab) {
        v.tab = tab;
        v.ids.clear();
        for (int i = 0; i < COUNT; i++)
            if (INFO[DISPLAY_ORDER[i]].rank == v.ranks[(size_t)tab]) v.ids.push_back(DISPLAY_ORDER[i]);
        v.sel = std::max(0, std::min(selFor[tab], (int)v.ids.size() - 1));
        v.top = topFor[tab];
    };
    // The list scrolls to keep the chosen one in view.
    auto keepInView = [&]() {
        const LedgerLayout L = layoutLedger(v);
        if (v.sel < v.top) v.top = v.sel;
        if (v.sel >= v.top + L.capacity) v.top = v.sel - L.capacity + 1;
        v.top = std::max(0, std::min(v.top, std::max(0, (int)v.ids.size() - L.capacity)));
    };
    auto turn = [&](int to) {
        const int n = (int)v.ranks.size();
        to = (to % n + n) % n;
        if (to == v.tab) return;
        selFor[v.tab] = v.sel;
        topFor[v.tab] = v.top;
        open(to);
        keepInView();
        Audio::menuHover();
    };
    auto choose = [&](int i) {
        if (i < 0 || i >= (int)v.ids.size() || i == v.sel) return;
        v.sel = i;
        keepInView();
        Audio::menuHover();
    };
    open(0);
    gLedger = &v;
    Platform::setModalRenderer(&drawLedger);
    Platform::flushKeys();
    int lastMx = -1, lastMy = -1, lastRow = -1;
    Platform::mousePos(lastMx, lastMy);
    auto inside = [](const SDL_Rect& q, int x, int y) { return x >= q.x && x < q.x + q.w && y >= q.y && y < q.y + q.h; };
    DrawUtil::ScrollGrip grip;   // the slider, while it's held
    while (true) {
        const LedgerLayout L = layoutLedger(v);
        const int maxTop = std::max(0, (int)v.ids.size() - L.capacity);
        int mx, my;
        Platform::mousePos(mx, my);
        // A held slider moves the list, and the highlight keeps to a row in view.
        if (grip.held) {
            DrawUtil::scrollDrag(grip, L.track, v.top, maxTop, L.capacity, (int)v.ids.size(), my, Platform::mouseDown());
            v.sel = std::max(v.top, std::min(v.sel, v.top + L.capacity - 1));
        }
        // The highlight follows the pointer onto another row, not the pointer
        // twitching on the row it was already on: after the wheel or the keys moved
        // the highlight, a nudge of the mouse used to snap it straight back.
        if ((mx != lastMx || my != lastMy) && !Platform::pointerHidden() && !grip.held) {
            lastMx = mx;
            lastMy = my;
            int row = -1;
            for (size_t i = 0; i < L.rows.size(); ++i)
                if (inside(L.rows[i], mx, my)) row = (int)i;
            if (row != lastRow) {
                lastRow = row;
                if (row >= 0) choose(v.top + row);
            }
        }
        const Platform::KeyEvent k = Platform::pollKey();
        const int n = (int)v.ids.size();
        if (k.key == Platform::Key::UP && n > 0)        choose((v.sel - 1 + n) % n);
        else if (k.key == Platform::Key::DOWN && n > 0) choose((v.sel + 1) % n);
        else if (k.key == Platform::Key::LEFT || k.key == Platform::Key::TO_CARDS)    turn(v.tab - 1);
        else if (k.key == Platform::Key::RIGHT || k.key == Platform::Key::TO_ACTIONS) turn(v.tab + 1);
        else if (k.key == Platform::Key::PAGE_UP && n > 0)   choose(std::max(0, v.sel - L.capacity));
        else if (k.key == Platform::Key::PAGE_DOWN && n > 0) choose(std::min(n - 1, v.sel + L.capacity));
        else if (k.key == Platform::Key::ESCAPE) break;
        const int wheel = Platform::takeWheel();
        if (wheel && n > 0) choose(std::max(0, std::min(n - 1, v.sel - wheel)));
        int cx, cy;
        if (Platform::takeClick(cx, cy)) {
            if (DrawUtil::scrollPress(grip, L.track, v.top, maxTop, L.capacity, n, cx, cy)) {
                v.sel = std::max(v.top, std::min(v.sel, v.top + L.capacity - 1));
                Platform::frame();
                continue;
            }
            for (size_t t = 0; t < L.tabs.size(); ++t)
                if (inside(L.tabs[t], cx, cy)) turn((int)t);
            for (size_t i = 0; i < L.rows.size(); ++i)
                if (inside(L.rows[i], cx, cy)) choose(v.top + (int)i);
        }
        Platform::frame();
    }
    Platform::setModalRenderer(nullptr);
    gLedger = nullptr;
    Audio::menuSelect(true);
}

} // namespace Achievements
