#include "Hud.h"
#include "Console.h"
#include "Platform.h"
#include "DrawUtil.h"

#include <algorithm>
#include <string>

namespace Hud {
namespace {

State gState;
bool  gActive = false;
bool  gInstalled = false;

// Lagging values for the "damage ghost": the bar drops immediately, a pale
// trail follows it down over a few hundred ms so you can see how big the hit
// was rather than just where it left you.
float gPlayerLag = -1.0f, gEnemyLag = -1.0f;
Uint32 gLastTick = 0;

SDL_Color tone(int l) { return Platform::groundTone(l); }

// Colors.h hpColor(): >60% green, >30% yellow, else red.
SDL_Color hpColor(int hp, int max) {
    int pct = max > 0 ? hp * 100 / max : 0;
    if (pct > 60) return SDL_Color{ 134, 209, 107, 255 };
    if (pct > 30) return SDL_Color{ 223, 193,  64, 255 };
    return SDL_Color{ 214, 74, 66, 255 };
}

void fill(SDL_Renderer* r, SDL_Rect q, SDL_Color c, Uint8 a = 255) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a);
    SDL_RenderFillRect(r, &q);
}
void frame(SDL_Renderer* r, SDL_Rect q, SDL_Color c) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    SDL_RenderDrawRect(r, &q);
}
int  radius() { return std::max(3, Platform::cellH() / 4); }
void panelFill(SDL_Renderer* r, SDL_Rect q, SDL_Color c) { DrawUtil::fillRound(r, q, radius(), c); }
void panelEdge(SDL_Renderer* r, SDL_Rect q, SDL_Color c) { DrawUtil::frameRound(r, q, radius(), c); }

// Small caps tag sitting on the panel's top edge, as in the mock-up. The
// border is broken behind it so the label reads as part of the frame.
void panelLabel(SDL_Renderer* r, SDL_Rect panel, const std::string& text) {
    const int cw = std::max(1, Platform::cellW());
    const int ch = std::max(1, Platform::cellH());
    int w = (int)text.size() * cw + cw;
    SDL_Rect gapRect{ panel.x + cw + cw/2, panel.y - 1, w, 3 };
    fill(r, gapRect, tone(26));
    Console::drawTextPx(r, panel.x + cw*2, panel.y - ch/2, text,
                        SDL_Color{ 120,120,138,255 }, true);
}

void bar(SDL_Renderer* r, SDL_Rect q, float frac, float lag, SDL_Color col) {
    frac = std::max(0.0f, std::min(1.0f, frac));
    fill(r, q, tone(20));
    frame(r, q, tone(58));
    const int inner = q.w - 4;
    if (lag > frac) {
        SDL_Rect g{ q.x + 2 + (int)(inner * frac), q.y + 2,
                    (int)(inner * (lag - frac)), q.h - 4 };
        fill(r, g, SDL_Color{ 235, 235, 245, 255 }, 200);
    }
    int w = (int)(inner * frac);
    // A left-to-right lift across the fill, so the bar has some form rather
    // than reading as a flat rectangle.
    for (int i = 0; i < w; i++) {
        float t = w > 1 ? (float)i / (float)(w - 1) : 0.0f;
        SDL_SetRenderDrawColor(r,
            (Uint8)std::min(255.0f, col.r * (0.78f + 0.32f * t)),
            (Uint8)std::min(255.0f, col.g * (0.78f + 0.32f * t)),
            (Uint8)std::min(255.0f, col.b * (0.78f + 0.32f * t)), 255);
        SDL_Rect s{ q.x + 2 + i, q.y + 2, 1, q.h - 4 };
        SDL_RenderFillRect(r, &s);
    }
}

void ease(float& lag, float target, float dt) {
    if (lag < 0.0f) { lag = target; return; }
    if (lag > target) { lag -= dt * 0.9f; if (lag < target) lag = target; }
    else lag = target;                       // healing snaps, damage trails
}

void draw() {
    if (!gActive) return;
    SDL_Renderer* r = Platform::renderer();
    SDL_Rect band = Console::hudRegion();
    if (band.h <= 0) return;

    Uint32 now = SDL_GetTicks();
    float dt = gLastTick ? (now - gLastTick) / 1000.0f : 0.0f;
    gLastTick = now;
    dt = std::min(dt, 0.1f);

    const float pf = gState.playerMax > 0 ? (float)gState.playerHp / gState.playerMax : 0.0f;
    const float ef = gState.enemyMax  > 0 ? (float)gState.enemyHp  / gState.enemyMax  : 0.0f;
    ease(gPlayerLag, pf, dt);
    ease(gEnemyLag,  ef, dt);

    const int cw = std::max(1, Platform::cellW());
    const int ch = std::max(1, Platform::cellH());
    const SDL_Color ink{ 228,228,238,255 }, dim{ 150,150,168,255 };
    const SDL_Color energy{ 249,241,165,255 }, armor{ 106,169,240,255 };

    SDL_Rect panel{ band.x, band.y + 6, band.w, band.h - 14 };
    panelFill(r, panel, tone(38));
    panelEdge(r, panel, tone(74));

    const int padX = std::max(10, cw);
    int x = panel.x + padX;
    int y = panel.y + std::max(4, ch / 3);

    // --- row 1: turn, energy, where you are -------------------------------
    Console::drawTextPx(r, x, y, "Turn " + std::to_string(gState.turn), ink, true);
    int col = x + 10 * cw;
    Console::drawTextPx(r, col, y,
        std::to_string(gState.energy) + "/" + std::to_string(gState.maxEnergy) + " energy",
        energy, false);
    col += 14 * cw;
    Console::drawTextPx(r, col, y, gState.encounter, dim, false);

    // --- row 2: you -------------------------------------------------------
    const int barW = std::max(120, std::min(360, panel.w / 4));
    const int barH = std::max(12, ch - 4);
    y += ch + 4;
    Console::drawTextPx(r, x, y, "YOU", ink, true);
    SDL_Rect pb{ x + 6 * cw, y + 1, barW, barH };
    bar(r, pb, pf, gPlayerLag, hpColor(gState.playerHp, gState.playerMax));
    int tx = pb.x + pb.w + cw;
    Console::drawTextPx(r, tx, y,
        std::to_string(gState.playerHp) + "/" + std::to_string(gState.playerMax),
        hpColor(gState.playerHp, gState.playerMax), false);
    tx += 9 * cw;
    if (gState.playerArmor > 0) {
        Console::drawTextPx(r, tx, y, "ARM " + std::to_string(gState.playerArmor), armor, false);
        tx += 8 * cw;
    }
    if (!gState.playerTags.empty()) Console::drawAnsiPx(r, tx, y, gState.playerTags, dim, false);

    // --- row 3: the enemy -------------------------------------------------
    y += ch + 4;
    Console::drawTextPx(r, x, y, gState.enemyName, ink, true);
    SDL_Rect eb{ x + 6 * cw, y + 1, barW, barH };
    // Give the name room when it is longer than the label column allows.
    if ((int)gState.enemyName.size() + 1 > 6) {
        int shift = ((int)gState.enemyName.size() + 1 - 6) * cw;
        eb.x += shift;
    }
    bar(r, eb, ef, gEnemyLag, hpColor(gState.enemyHp, gState.enemyMax));
    tx = eb.x + eb.w + cw;
    Console::drawTextPx(r, tx, y,
        std::to_string(gState.enemyHp) + "/" + std::to_string(gState.enemyMax),
        hpColor(gState.enemyHp, gState.enemyMax), false);
    tx += 9 * cw;
    Console::drawTextPx(r, tx, y,
        "ATK " + std::to_string(gState.enemyAtk) + "   DEF " + std::to_string(gState.enemyDef),
        dim, false);
    tx += 14 * cw;
    if (!gState.enemyTags.empty()) Console::drawAnsiPx(r, tx, y, gState.enemyTags, dim, false);

    // --- the summoned add, when one is standing ----------------------------
    if (gState.addActive) {
        y += ch + 4;
        const SDL_Color addInk{ 214, 138, 226, 255 };
        Console::drawTextPx(r, x, y, gState.addName, addInk, true);
        SDL_Rect ab{ x + 6 * cw, y + 1, barW * 2 / 3, barH };
        if ((int)gState.addName.size() + 1 > 6)
            ab.x += ((int)gState.addName.size() + 1 - 6) * cw;
        bar(r, ab, gState.addMax > 0 ? (float)gState.addHp / gState.addMax : 0.0f,
            0.0f, hpColor(gState.addHp, gState.addMax));
        tx = ab.x + ab.w + cw;
        Console::drawTextPx(r, tx, y,
            std::to_string(gState.addHp) + "/" + std::to_string(gState.addMax),
            hpColor(gState.addHp, gState.addMax), false);
        tx += 9 * cw;
        Console::drawTextPx(r, tx, y, "fights beside the " + gState.enemyName, dim, false);
    }

    // --- last row: whatever is unusual about this fight right now ----------
    if (!gState.notice.empty()) {
        y += ch + 2;
        // Clipped to the panel. The curse line names the enemy, so a long name
        // pushed it straight out through the right hand border.
        const int room = (panel.x + panel.w - padX - x) / cw;
        std::string note = gState.notice;
        if ((int)note.size() > room && room > 1) note = note.substr(0, (size_t)room - 1);
        Console::drawTextPx(r, x, y, note, SDL_Color{ 240,180,41,255 }, true);
    }
    panelLabel(r, panel, "COMBAT");

    // --- the log gets a frame of its own -----------------------------------
    // The text between the panel and the hand is where combat results scroll.
    // Framing it stops the screen reading as "two widgets floating in a void"
    // and matches the mock-up's second panel.
    SDL_Rect text = Console::textRegion();
    if (text.h > ch * 3) {
        SDL_Rect logPanel{ text.x, text.y + 2, text.w, text.h - 12 };
        panelFill(r, logPanel, tone(32));
        panelEdge(r, logPanel, tone(68));
        panelLabel(r, logPanel, "LOG");
    }
}

} // namespace

// Five rows hold the three fixed lines (turn, you, the enemy) with the
// panel margins. Each optional line below them needs a row of its own: a
// fixed height is what pushed the curse notice out through the border.
int rowsNeeded() {
    return 5 + (gState.addActive ? 1 : 0) + (gState.notice.empty() ? 0 : 1);
}

void set(const State& s) {
    // A new enemy resets the ghost, or the bar would trail from the last fight.
    if (s.enemyName != gState.enemyName) gEnemyLag = -1.0f;
    gState = s;
    // A summon or a curse arrives mid-fight, so the band is re-measured on
    // every update rather than only when the panel is switched on.
    if (gActive) Console::setHudRows(rowsNeeded());
}

void setActive(bool on) {
    gActive = on;
    if (!gInstalled) { Platform::setHudRenderer(&draw); gInstalled = true; }
    Console::setHudRows(on ? rowsNeeded() : 0);
    if (!on) { gPlayerLag = gEnemyLag = -1.0f; }
}

} // namespace Hud
