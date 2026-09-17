#include "CardBar.h"
#include "Console.h"
#include "Platform.h"
#include "UIHelper.h"
#include "DrawUtil.h"

#include <algorithm>
#include <iostream>

namespace CardBar {
namespace {

// Energy left this turn. Declared up here because both the layout (which
// reserves a column for the pips) and the card painter (which reddens a badge
// you cannot pay for) are defined above the drawing code that sets it.
int gEnergy = 0, gEnergyMax = 0;
// True while the full-screen picker owns the display. drawCards is shared
// with it, and needs to know not to apply hand-only styling there.
bool gGridActive = false;

// Layout is derived from the console cell so the hand scales with the font -
// the same way everything else in this build does.
// Rows withheld for the hand. Derived rather than fixed: a fullscreen window
// has plenty to spare and fixed rows left the cards looking lost in it, while a
// small window cannot afford many at all.
// Measured against the whole window, not the rows left after the battle scene
// has taken its share - that is why the hand stayed small on a fullscreen
// display no matter how much empty space was below it. The text card list is
// gone now that the widgets replace it, so those rows are free.
int handRowsFor(int totalRows) {
    return std::max(9, std::min(17, (totalRows * 30) / 100));
}

struct Layout {
    int energyW = 0;   // reserved gutter on the left for the pips
    SDL_Rect band{ 0,0,0,0 };
    int cw = 0, ch = 0, gap = 0;
    // Distance from one card's left edge to the next. Normally cw + gap; on a
    // hand too wide for the band it drops below cw and the cards overlap.
    int step = 0;
    int x0 = 0, y0 = 0;
    int plusR = 0;
};

Layout gLayout;
std::vector<SDL_Rect> gCardRects;
std::vector<SDL_Rect> gPlusRects;
std::vector<SDL_Rect> gActionRects;

// Snapshots. The hand keeps drawing after select() returns, so a play's result
// lands with the cards still on screen; borrowing the caller's vector would
// dangle the moment it left scope.
std::vector<Card>   gCards;
std::vector<Action> gActions;
int gCurrent = 0;
bool gActive = false;      // drawing
bool gPicking = false;     // and accepting input

SDL_Color tone(int luma) { return Platform::groundTone(luma); }

Layout compute(int nCards, int nActions) {
    Layout L;
    L.band = Console::handRegion();
    L.ch = std::max(1, Platform::cellH());
    const int actionW = nActions ? 15 * std::max(1, Platform::cellW()) : 0;
    // A reserved column for the energy pips. Taken out of the usable width
    // like the action buttons are, so a full hand cannot overlap it.
    L.energyW = gEnergyMax > 0 ? 7 * std::max(1, Platform::cellW()) : 0;
    const int usable  = L.band.w - actionW - L.energyW - 24;

    // Width follows the band height at a card-like aspect. The old fixed 140px
    // cap is why the hand looked lost on a fullscreen display.
    L.ch = L.band.h - 14;
    L.cw = (int)(L.ch * 0.74f);
    const int fitByWidth = nCards ? (usable / std::max(1, nCards)) - 12 : L.cw;
    L.cw = std::max(58, std::min(L.cw, fitByWidth));
    L.gap = std::max(6, L.cw / 9);
    L.step = L.cw + L.gap;
    int total = nCards * L.cw + std::max(0, nCards - 1) * L.gap;
    // Endurance stacks, so a hand of ten or more is reachable, and below the
    // 58px floor a card is no longer readable anyway. Past that point the hand
    // overlaps like a held fan instead of running off the right edge: every
    // card keeps a visible strip, and the one under the cursor is drawn last
    // and raised, so it is fully readable.
    if (nCards > 1 && total > usable) {
        L.step = std::max(L.cw / 4, (usable - L.cw) / (nCards - 1));
        total  = L.cw + (nCards - 1) * L.step;
    }
    L.x0 = L.band.x + L.energyW + std::max(12, (usable - total) / 2);
    L.y0 = L.band.y + 8;
    L.plusR = std::max(9, L.cw / 10);
    return L;
}

void buildRects() {
    gCardRects.clear(); gPlusRects.clear(); gActionRects.clear();
    const Layout& L = gLayout;
    for (size_t i = 0; i < gCards.size(); i++) {
        int x = L.x0 + (int)i * L.step;
        gCardRects.push_back(SDL_Rect{ x, L.y0, L.cw, L.ch });
        gPlusRects.push_back(SDL_Rect{ x + L.cw - L.plusR*2 - 8,
                                       L.y0 + L.ch - L.plusR*2 - 8,
                                       L.plusR*2, L.plusR*2 });
    }
    if (!gActions.empty()) {
        int aw = 14 * std::max(1, Platform::cellW());
        int ax = L.band.x + L.band.w - aw - 16;
        int ah = std::max(22, L.ch / 5);
        int ay = L.y0 + (L.ch - ah * (int)gActions.size()) / 2;
        for (size_t i = 0; i < gActions.size(); i++)
            gActionRects.push_back(SDL_Rect{ ax, ay + (int)i * ah, aw, ah - 4 });
    }
}

void fill(SDL_Renderer* r, SDL_Rect q, SDL_Color c, Uint8 a = 255) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a);
    SDL_RenderFillRect(r, &q);
}
void frame(SDL_Renderer* r, SDL_Rect q, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_RenderDrawRect(r, &q);
}
int  rad() { return std::max(4, Platform::cellH() / 3); }
void fillR(SDL_Renderer* r, SDL_Rect q, SDL_Color c, Uint8 a = 255) { DrawUtil::fillRound(r, q, rad(), c, a); }
void frameR(SDL_Renderer* r, SDL_Rect q, SDL_Color c) { DrawUtil::frameRound(r, q, rad(), c); }

// Shared by the in-battle hand and the full-screen picker. They differ only in
// where the rects are.
void drawCards(SDL_Renderer* r, const std::vector<Card>& cards,
               const std::vector<SDL_Rect>& rects,
               const std::vector<SDL_Rect>& plus, int lift) {
    const SDL_Color cardBg   = tone(30);
    const SDL_Color cardLine = tone(66);
    const SDL_Color badgeBg  = tone(19);
    const SDL_Color badgeLn  = tone(60);
    const SDL_Color btnBg    = tone(34);
    const SDL_Color btnLn    = tone(84);
    const SDL_Color lime{ 166, 226, 46, 255 };
    const SDL_Color energy{ 249, 241, 165, 255 };   // Colors.h ENERGY_CLR
    const SDL_Color ink{ 228, 228, 238, 255 };
    const SDL_Color dim{ 150, 150, 168, 255 };
    const SDL_Color gold{ 232, 196, 84, 255 };

    // Two passes so the selected card is painted last. On a hand wide enough to
    // overlap, the card under the cursor would otherwise be half-covered by its
    // right-hand neighbour, and even in a spaced row this keeps a neighbour from
    // clipping the selection glow.
    const size_t nDraw = std::min(cards.size(), rects.size());
    for (size_t pass = 0; pass < 2; pass++)
    for (size_t i = 0; i < nDraw; i++) {
        const Card& c = cards[i];
        SDL_Rect q = rects[i];
        const bool sel = gPicking && ((int)i == gCurrent);
        if (sel != (pass == 1)) continue;
        if (sel) q.y -= lift;

        if (sel) DrawUtil::glowRound(r, q, rad(), lime, std::max(6, q.w / 12), 90);
        fillR(r, q, cardBg, c.disabled ? 150 : 255);
        frameR(r, q, sel ? lime : cardLine);
        if (sel) { SDL_Rect in{ q.x+1, q.y+1, q.w-2, q.h-2 }; frameR(r, in, lime); }
        if (c.risk) { SDL_Rect g{ q.x+3, q.y+3, q.w-6, q.h-6 }; frameR(r, g, gold); }
        DrawUtil::fillRound(r, SDL_Rect{ q.x+3, q.y+3, q.w-6, std::max(4, rad()) },
                            rad()-1, c.tint, c.disabled ? 140 : 255);

        const int pad = std::max(11, q.w / 12);
        const int nameRoom = q.w - pad * 2;
        const int gridFit = nameRoom / std::max(1, Platform::cellW());
        auto clip = [&](std::string t) {
            if ((int)t.size() > gridFit) t = t.substr(0, std::max(1, gridFit));
            return t;
        };
        const SDL_Color nameCol = c.disabled ? dim : c.nameColor;
        std::string nm = c.name;
        if ((int)nm.size() * Console::bigCellW() <= nameRoom) {
            Console::drawTextBigPx(r, q.x + pad, q.y + pad + 6, nm, nameCol, true);
        } else {
            Console::drawTextPx(r, q.x + pad, q.y + pad + 8, clip(nm), nameCol, true);
        }
        int ty = q.y + pad + 8 + Console::bigCellH();
        if (!c.elemTag.empty()) {
            Console::drawTextPx(r, q.x + pad, ty, clip(c.elemTag),
                                c.disabled ? dim : SDL_Color{ 249, 241, 165, 255 }, true);
            ty += Platform::cellH();
        }
        // Wrapped on word boundaries down the card's free space, rather than cut
        // at its width.
        int lineY = ty + 2;
        const int textBottom = q.y + q.h - std::max(18, Platform::cellH() + 4) - 14;
        auto drawWrapped = [&](const std::string& text, SDL_Color col) {
            if (text.empty()) return;
            std::string line;
            size_t i3 = 0;
            while (i3 <= text.size()) {
                const size_t sp = text.find(' ', i3);
                const std::string word = text.substr(i3, sp == std::string::npos ? std::string::npos : sp - i3);
                const std::string cand = line.empty() ? word : line + " " + word;
                if ((int)cand.size() > gridFit && !line.empty()) {
                    if (lineY > textBottom) return;
                    Console::drawTextPx(r, q.x + pad, lineY, line, col, false);
                    lineY += Platform::cellH();
                    line = word;
                } else {
                    line = cand;
                }
                if (sp == std::string::npos) break;
                i3 = sp + 1;
            }
            if (!line.empty() && lineY <= textBottom) {
                Console::drawTextPx(r, q.x + pad, lineY, clip(line), col, false);
                lineY += Platform::cellH();
            }
        };
        drawWrapped(c.effect, dim);
        drawWrapped(c.note, c.disabled ? dim : SDL_Color{ 79, 214, 214, 255 });

        const int cellW = std::max(1, Platform::cellW());
        int bh = std::max(18, Platform::cellH() + 4);
        int room = q.w - (plus.size() > i ? plus[i].w : 0) - 26;
        const bool longForm = room >= 6*cellW + 12;
        const std::string num = std::to_string(c.cost);
        int bw = (longForm ? (5 + (int)num.size()) : (int)num.size()) * cellW + 12;
        SDL_Rect badge{ q.x + 7, q.y + q.h - bh - 8, std::min(room, bw), bh };
        // A card you cannot pay for says so on the badge. Greying the whole
        // card told you it was unavailable but not why.
        // Only in the hand. drawCards is shared with the full-screen picker,
        // where energy means nothing - a reward you are choosing to OWN is not
        // unaffordable, and the leftover figure from the last turn made cards
        // look barred on the reward, forge and deck screens.
        const bool tooDear = !gGridActive && gEnergyMax > 0 && c.cost > gEnergy;
        fillR(r, badge, tooDear ? SDL_Color{ 58, 26, 26, 255 } : badgeBg);
        frameR(r, badge, tooDear ? SDL_Color{ 176, 68, 60, 255 } : badgeLn);
        int tx = badge.x + 6;
        if (longForm) { Console::drawTextPx(r, tx, badge.y + 2, "cost:", dim, false); tx += 5*cellW; }
        Console::drawTextPx(r, tx, badge.y + 2, num,
                            tooDear ? SDL_Color{ 238, 116, 106, 255 } : energy, true);

        if (i < plus.size()) {
            SDL_Rect pr = plus[i];
            if (sel) pr.y -= lift;
            fillR(r, pr, btnBg); frameR(r, pr, sel ? lime : btnLn);
            Console::drawTextPx(r, pr.x + pr.w/2 - Platform::cellW()/2,
                                pr.y + pr.h/2 - Platform::cellH()/2,
                                "+", sel ? lime : dim, true);
        }
    }
}

void drawActions(SDL_Renderer* r, const std::vector<SDL_Rect>& rects, int indexOffset) {
    const SDL_Color btnBg = tone(34), btnLn = tone(84);
    const SDL_Color lime{ 166,226,46,255 }, ink{ 228,228,238,255 }, dim{ 150,150,168,255 };
    // The longest name sets the description column, so descriptions line
    // up down the list however long each name is.
    int nameCells = 0;
    bool anyDesc = false;
    for (const Action& a : gActions) {
        nameCells = std::max(nameCells, (int)a.label.size());
        if (!a.desc.empty()) anyDesc = true;
    }
    for (size_t i = 0; i < rects.size() && i < gActions.size(); i++) {
        const Action& a = gActions[i];
        const bool sel = gPicking && ((int)(indexOffset + i) == gCurrent);
        SDL_Rect q = rects[i];
        if (sel) DrawUtil::glowRound(r, q, rad(), lime, 7, 70);
        fillR(r, q, sel ? tone(46) : btnBg, a.disabled ? 140 : 255);
        frameR(r, q, sel ? lime : btnLn);
        const int cellW = std::max(1, Platform::cellW());
        const int ty = q.y + (q.h - Platform::cellH()) / 2;
        const SDL_Color nameCol = a.disabled ? dim : (sel ? lime : ink);
        if (anyDesc) {
            // Name bold on the left, description dim in its own column.
            const int pad = std::max(14, cellW * 2);
            Console::drawTextPx(r, q.x + pad, ty, a.label, nameCol, true);
            if (!a.desc.empty())
                Console::drawTextPx(r, q.x + pad + (nameCells + 3) * cellW, ty,
                                    a.desc, dim, false);
        } else {
            // No descriptions anywhere in this set (a plain Yes/No): centre it.
            const int lw = (int)a.label.size() * cellW;
            Console::drawTextPx(r, q.x + std::max(10, (q.w - lw) / 2), ty,
                                a.label, nameCol, sel);
        }
    }
}

// Energy as pips: a filled disc per point available, a hollow ring per point
// spent, with the figure underneath. Reads at a glance without being parsed.
void drawEnergy(const Layout& L) {
    if (gEnergyMax <= 0 || L.energyW <= 0) return;
    SDL_Renderer* r = Platform::renderer();
    const SDL_Color lit{ 249, 241, 165, 255 };
    const SDL_Color spent{ 92, 88, 70, 255 };

    const int cx = L.band.x + L.energyW / 2 + 4;
    // Sized off the band and the number of pips, so raising max energy
    // shrinks them to fit rather than running out of the band.
    const int room = L.band.h - Platform::cellH() - 14;
    int rad = std::max(3, std::min(9, L.ch / 12));
    while (gEnergyMax * (rad * 2 + 6) - 6 > room && rad > 3) rad--;
    const int step = rad * 2 + 6;
    const int totalH = gEnergyMax * step - 6;
    int py = L.band.y + (L.band.h - totalH) / 2 - Platform::cellH() / 2;

    for (int i = 0; i < gEnergyMax; i++) {
        const bool have = i < gEnergy;
        SDL_Rect dot{ cx - rad, py, rad * 2, rad * 2 };
        if (have) {
            DrawUtil::fillRound(r, dot, rad, lit);
        } else {
            DrawUtil::fillRound(r, dot, rad, SDL_Color{ 34, 31, 26, 255 });
            DrawUtil::frameRound(r, dot, rad, spent);
        }
        py += step;
    }
    const std::string txt = std::to_string(gEnergy) + "/" + std::to_string(gEnergyMax);
    Console::drawTextPx(r, cx - (int)txt.size() * Platform::cellW() / 2, py + 2,
                        txt, gEnergy > 0 ? lit : spent, true);
}

void drawHand() {
    // No reserved band means some other screen owns the display now. clear()
    // drops the band, so this covers every full-screen view without each of
    // them having to remember to hide the hand.
    if (!gActive || gCards.empty() || Console::handRows() <= 0) return;
    SDL_Renderer* r = Platform::renderer();
    drawEnergy(gLayout);
    drawCards(r, gCards, gCardRects, gPlusRects, 10);
    drawActions(r, gActionRects, (int)gCards.size());
}

bool inside(const SDL_Rect& q, int x, int y) {
    return x >= q.x && x < q.x + q.w && y >= q.y && y < q.y + q.h;
}

std::function<void(int)> gOnHover;

} // namespace

void setHoverCallback(const std::function<void(int)>& fn) { gOnHover = fn; }
void setEnergy(int current, int max) { gEnergy = current; gEnergyMax = max; }

int select(const std::vector<Card>& cards, const std::vector<Action>& actions,
           const std::function<void()>& onIdleTick, int idleTickMs) {
    const int n = (int)cards.size() + (int)actions.size();
    if (n == 0) return -1;

    Console::setHandRows(handRowsFor(Console::totalRows()));
    gCards = cards; gActions = actions; gActive = true; gPicking = true;

    auto usable = [&](int i) {
        if (i < (int)cards.size()) return !cards[i].disabled;
        return !actions[i - (int)cards.size()].disabled;
    };
    gCurrent = 0;
    while (gCurrent < n && !usable(gCurrent)) gCurrent++;
    if (gCurrent >= n) gCurrent = 0;
    // Report the highlighted card so the panel can show what it would do.
    int lastHover = -2;
    auto tellHover = [&]() {
        int h = (gCurrent < (int)cards.size()) ? gCurrent : -1;
        if (h != lastHover) { lastHover = h; if (gOnHover) gOnHover(h); }
    };
    tellHover();

    Platform::setHandRenderer(&drawHand);
    Platform::flushKeys();

    int lastMx = -1, lastMy = -1;
    Platform::mousePos(lastMx, lastMy);
    Uint32 lastTick = SDL_GetTicks();
    int result = -1;

    auto step = [&](int dir) {
        int i = gCurrent;
        for (int guard = 0; guard < n; guard++) {
            i = (i + dir + n) % n;
            if (usable(i)) { gCurrent = i; return; }
        }
    };

    while (true) {
        // Recomputed every frame: the window can be resized mid-turn, and the
        // font refits with it, so the band and every rect move.
        gLayout = compute((int)cards.size(), (int)actions.size());
        buildRects();

        int mx, my;
        Platform::mousePos(mx, my);
        if (mx != lastMx || my != lastMy) {
            lastMx = mx; lastMy = my;
            // Back to front. A fanned hand overlaps, and the card on top is the
            // last one drawn, so the highest index under the cursor is the one
            // the player can actually see and means to point at.
            for (int i = n - 1; i >= 0; i--) {
                const SDL_Rect& q = (i < (int)cards.size())
                                        ? gCardRects[i] : gActionRects[i - (int)cards.size()];
                if (inside(q, mx, my) && usable(i)) { gCurrent = i; break; }
            }
        }

        Platform::KeyEvent k = Platform::pollKey();
        if (k.key == Platform::Key::LEFT  || k.key == Platform::Key::UP)   step(-1);
        else if (k.key == Platform::Key::RIGHT || k.key == Platform::Key::DOWN) step(1);
        else if (k.key == Platform::Key::ENTER) { result = gCurrent; break; }
        else if (k.key == Platform::Key::ESCAPE) { result = -1; break; }
        else if (k.key == Platform::Key::CHAR && k.ch >= '1' && k.ch <= '9') {
            int i = k.ch - '1';
            if (i < (int)cards.size() && usable(i)) { result = i; break; }
        }

        int cx, cy;
        if (Platform::takeClick(cx, cy)) {
            bool handled = false;
            for (int i = (int)gPlusRects.size() - 1; i >= 0 && !handled; i--) {
                SDL_Rect p = gPlusRects[i];
                if (i == gCurrent) p.y -= 10;
                if (inside(p, cx, cy)) { gCurrent = i; result = -2 - i; handled = true; }
            }
            for (int i = n - 1; i >= 0 && !handled; i--) {   // topmost first, as above
                const SDL_Rect& q = (i < (int)cards.size())
                                        ? gCardRects[i] : gActionRects[i - (int)cards.size()];
                if (inside(q, cx, cy) && usable(i)) { gCurrent = i; result = i; handled = true; }
            }
            if (handled) break;
        }

        tellHover();

        if (onIdleTick && idleTickMs > 0 && SDL_GetTicks() - lastTick >= (Uint32)idleTickMs) {
            lastTick = SDL_GetTicks();
            onIdleTick();
        }
        Platform::frame();
    }

    // Keep drawing, stop accepting input: the played card's result now happens
    // with the hand still visible rather than the board going empty.
    gPicking = false;
    if (gOnHover) { gOnHover(-1); gOnHover = nullptr; }  // no stale ghost on the bar
    return result;
}


// --- full-screen grid picker ------------------------------------------------
namespace {
std::string gTitle;
std::vector<SDL_Rect> gGridRects, gGridPlus, gGridActions;

void layoutGrid(int columns) {
    gGridRects.clear(); gGridPlus.clear(); gGridActions.clear();
    const int cw = std::max(1, Platform::cellW());
    const int ch = std::max(1, Platform::cellH());
    const int W = Platform::screenW(), H = Platform::screenH();

    const int nCards = (int)gCards.size();
    int cols = columns > 0 ? columns : nCards;
    if (cols <= 0) cols = 1;
    // Fit the row: never wider than the window allows, never sillier than 6.
    cols = std::min(cols, std::max(1, (W - 80) / 150));
    cols = std::min(cols, 6);
    int rows = (nCards + cols - 1) / std::max(1, cols);
    if (rows < 1) rows = 1;
    // An actions-only screen (the rest site) has no grid to reserve space for.
    const bool cardless = (nCards == 0);

    const int actionH = ch + 14;
    const int topY = ch * 3;
    const int availH = H - topY - actionH * (int)gActions.size() - ch * 3;
    int cardH = std::max(120, std::min(360, (availH - (rows - 1) * 14) / rows));
    int cardW = (int)(cardH * 0.74f);
    int maxW = (W - 80 - (cols - 1) * 16) / cols;
    if (cardW > maxW) { cardW = maxW; cardH = (int)(cardW / 0.74f); }

    const int gap = 16;
    int gridH = cardless ? 0 : rows * cardH + (rows - 1) * 14;
    int y0 = cardless ? (H / 2 - ch * 2)
                      : topY + std::max(0, (availH - gridH) / 2);

    for (int i = 0; i < nCards; i++) {
        int rIdx = i / cols, cIdx = i % cols;
        int inRow = std::min(cols, nCards - rIdx * cols);
        int rowW = inRow * cardW + (inRow - 1) * gap;
        int x = (W - rowW) / 2 + cIdx * (cardW + gap);
        int y = y0 + rIdx * (cardH + 14);
        gGridRects.push_back(SDL_Rect{ x, y, cardW, cardH });
        int pr = std::max(9, cardW / 10);
        gGridPlus.push_back(SDL_Rect{ x + cardW - pr*2 - 8, y + cardH - pr*2 - 8, pr*2, pr*2 });
    }

    int ay = cardless ? y0 : y0 + gridH + ch;
    int nameCells = 0, descCells = 0;
    for (const Action& a : gActions) {
        nameCells = std::max(nameCells, (int)a.label.size());
        descCells = std::max(descCells, (int)a.desc.size());
    }
    int aw = descCells > 0 ? nameCells + 3 + descCells : nameCells;
    // Longer and taller than the label strictly needs. A "Yes"/"No" pair
    // sized to its text is a tiny box in the middle of an empty screen;
    // these are the most-clicked controls in the game.
    aw = std::max((aw + 10) * cw, 26 * cw);
    aw = std::min(aw, W - 80);
    const int btnH = actionH + ch / 2;
    for (size_t i = 0; i < gActions.size(); i++)
        gGridActions.push_back(SDL_Rect{ (W - aw) / 2, ay + (int)i * (btnH + 4),
                                         aw, btnH });
}

void drawGrid() {
    if (!gGridActive) return;
    SDL_Renderer* r = Platform::renderer();
    const int cw = std::max(1, Platform::cellW());
    const int ch = std::max(1, Platform::cellH());

    SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
    SDL_Rect full{ 0, 0, Platform::screenW(), Platform::screenH() };
    SDL_RenderFillRect(r, &full);

    if (!gTitle.empty()) {
        int tw = (int)gTitle.size() * Console::bigCellW();
        Console::drawTextBigPx(r, (Platform::screenW() - tw) / 2, ch,
                               gTitle, SDL_Color{ 228,228,238,255 }, true);
    }
    drawCards(r, gCards, gGridRects, gGridPlus, 0);
    drawActions(r, gGridActions, (int)gCards.size());
    (void)cw;
}
} // namespace

int pick(const std::string& title, const std::vector<Card>& cards,
         const std::vector<Action>& actions, int columns) {
    const int n = (int)cards.size() + (int)actions.size();
    if (n == 0) return -1;

    gTitle = title;
    gCards = cards; gActions = actions;
    gGridActive = true;

    auto usable = [&](int i) {
        if (i < (int)cards.size()) return !cards[i].disabled;
        return !actions[i - (int)cards.size()].disabled;
    };
    gCurrent = 0;
    while (gCurrent < n && !usable(gCurrent)) gCurrent++;
    if (gCurrent >= n) gCurrent = 0;
    gPicking = true;

    Platform::setModalRenderer(&drawGrid);
    Platform::flushKeys();
    int lastMx = -1, lastMy = -1;
    Platform::mousePos(lastMx, lastMy);
    int result = -1;

    auto step = [&](int d) {
        int i = gCurrent;
        for (int g = 0; g < n; g++) { i = (i + d + n) % n; if (usable(i)) { gCurrent = i; return; } }
    };
    auto rectFor = [&](int i) -> const SDL_Rect& {
        return i < (int)gGridRects.size() ? gGridRects[i]
                                          : gGridActions[i - (int)gGridRects.size()];
    };

    while (true) {
        layoutGrid(columns);
        int mx, my;
        Platform::mousePos(mx, my);
        if (mx != lastMx || my != lastMy) {
            lastMx = mx; lastMy = my;
            for (int i = 0; i < n; i++)
                if (inside(rectFor(i), mx, my) && usable(i)) { gCurrent = i; break; }
        }

        Platform::KeyEvent k = Platform::pollKey();
        if (k.key == Platform::Key::LEFT || k.key == Platform::Key::UP) step(-1);
        else if (k.key == Platform::Key::RIGHT || k.key == Platform::Key::DOWN) step(1);
        else if (k.key == Platform::Key::ENTER) { result = gCurrent; break; }
        else if (k.key == Platform::Key::ESCAPE) { result = -1; break; }
        else if (k.key == Platform::Key::CHAR && k.ch >= '1' && k.ch <= '9') {
            int i = k.ch - '1';
            if (i < (int)cards.size() && usable(i)) { result = i; break; }
        }

        int cx, cy;
        if (Platform::takeClick(cx, cy)) {
            bool done = false;
            for (size_t i = 0; i < gGridPlus.size() && !done; i++)
                if (inside(gGridPlus[i], cx, cy)) { gCurrent = (int)i; result = -2 - (int)i; done = true; }
            for (int i = 0; i < n && !done; i++)
                if (inside(rectFor(i), cx, cy) && usable(i)) { gCurrent = i; result = i; done = true; }
            if (done) break;
        }
        Platform::frame();
    }

    gGridActive = false;
    gPicking = false;
    Platform::setModalRenderer(nullptr);
    return result;
}

void showDetail(const Card& c, const std::string& description,
                const std::string& typeLabel, const std::string& rarityLabel,
                int upgrades) {
    // A drawn modal, not printed text. The previous version std::cout'd into
    // the log region, which is why pressing "+" just looked like more log.
    SDL_Renderer* r = Platform::renderer();
    const int cw = std::max(1, Platform::cellW());
    const int ch = std::max(1, Platform::cellH());

    // Wrap the description to the panel, on word boundaries.
    const int panelW = std::max(320, std::min(560, Platform::screenW() / 3));
    const int textCols = std::max(12, (panelW - 6 * cw) / cw);
    std::vector<std::string> lines;
    {
        std::string cur;
        size_t i2 = 0;
        while (i2 <= description.size()) {
            size_t sp = description.find(' ', i2);
            std::string word = description.substr(i2, sp == std::string::npos ? std::string::npos : sp - i2);
            if (cur.empty()) cur = word;
            else if ((int)(cur.size() + 1 + word.size()) <= textCols) cur += " " + word;
            else { lines.push_back(cur); cur = word; }
            if (sp == std::string::npos) break;
            i2 = sp + 1;
        }
        if (!cur.empty()) lines.push_back(cur);
    }

    // Description lines are drawn at 1.45x line height and the panel gets
    // more padding: at a flat ch per line the text was set solid and read
    // as a block rather than as sentences.
    const int lineH = (ch * 29) / 20;
    const int panelH = lineH * (int)lines.size() + ch * 7 + 28;
    SDL_Rect panel{ (Platform::screenW() - panelW) / 2,
                    std::max(20, (Platform::screenH() - panelH) / 2 - ch * 2),
                    panelW, panelH };

    auto drawModal = [&]() {
        // Dim everything behind it so the panel reads as on top.
        SDL_SetRenderDrawColor(r, 8, 8, 11, 170);
        SDL_Rect full{ 0, 0, Platform::screenW(), Platform::screenH() };
        SDL_RenderFillRect(r, &full);

        DrawUtil::glowRound(r, panel, rad(), SDL_Color{ 0, 0, 0, 255 }, 10, 120);
        fillR(r, panel, tone(30));
        frameR(r, panel, c.risk ? SDL_Color{ 232, 196, 84, 255 } : tone(80));
        DrawUtil::fillRound(r, SDL_Rect{ panel.x + 3, panel.y + 3, panel.w - 6, std::max(4, rad()) },
                            rad() - 1, c.tint);

        int x = panel.x + cw * 2;
        int y = panel.y + ch;
        Console::drawTextBigPx(r, x, y, c.name, c.nameColor, true);

        // cost badge, top right, same wording as the card face
        std::string num = std::to_string(c.cost);
        int bw = (5 + (int)num.size()) * cw + 12;
        SDL_Rect badge{ panel.x + panel.w - bw - cw * 2, y, bw, ch + 4 };
        fillR(r, badge, tone(19)); frameR(r, badge, tone(60));
        Console::drawTextPx(r, badge.x + 6, badge.y + 2, "cost:", SDL_Color{ 150,150,168,255 }, false);
        Console::drawTextPx(r, badge.x + 6 + 5 * cw, badge.y + 2, num,
                            SDL_Color{ 249,241,165,255 }, true);

        y += Console::bigCellH() + 4;
        Console::drawTextPx(r, x, y, typeLabel, c.tint, true);
        int mx = x + ((int)typeLabel.size() + 2) * cw;
        if (!c.elemTag.empty()) {
            Console::drawTextPx(r, mx, y, c.elemTag, SDL_Color{ 249,241,165,255 }, true);
            mx += ((int)c.elemTag.size() + 2) * cw;
        }
        if (!rarityLabel.empty()) {
            Console::drawTextPx(r, mx, y, rarityLabel, c.nameColor, true);
            mx += ((int)rarityLabel.size() + 3) * cw;
        }
        if (upgrades > 0)
            Console::drawTextPx(r, mx, y, "upgraded " + std::string((size_t)upgrades, '+'),
                                SDL_Color{ 166,226,46,255 }, true);

        y += ch + 6;
        SDL_SetRenderDrawColor(r, tone(70).r, tone(70).g, tone(70).b, 255);
        SDL_RenderDrawLine(r, x, y, panel.x + panel.w - cw * 2, y);
        y += 8;
        for (const std::string& ln : lines) {
            Console::drawTextPx(r, x, y, ln, SDL_Color{ 228,228,238,255 }, false);
            y += lineH;
        }

        Console::drawTextPx(r, x, panel.y + panel.h - ch - 6,
                            "(any key to close)", SDL_Color{ 120,120,138,255 }, false);
    };

    // Draws above the hand, which keeps rendering underneath - the card you
    // opened stays visible next to its own details.
    Platform::setModalRenderer(drawModal);
    Platform::flushKeys();
    while (true) {
        Platform::KeyEvent k = Platform::pollKey();
        if (k.key != Platform::Key::NONE) break;
        int cx, cy;
        if (Platform::takeClick(cx, cy)) break;
        Platform::frame();
    }
    Platform::setModalRenderer(nullptr);
}

} // namespace CardBar
