#include "Hud.h"
#include "Console.h"
#include "Platform.h"
#include "DrawUtil.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace Hud {
namespace {

State gState;
std::function<State()> gSource;   // the live fight, read every frame
int   gPreview = 0;   // hovered card's damage, drawn on the enemy bar
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

void bar(SDL_Renderer* r, SDL_Rect q, float frac, float lag, SDL_Color col,
         float preview = 0.0f) {
    frac = std::max(0.0f, std::min(1.0f, frac));
    fill(r, q, tone(20));
    frame(r, q, tone(58));
    const int inner = q.w - 4;
    if (lag > frac) {
        SDL_Rect g{ q.x + 2 + (int)(inner * frac), q.y + 2,
                    (int)(inner * (lag - frac)), q.h - 4 };
        fill(r, g, SDL_Color{ 235, 235, 245, 255 }, 200);
    }
    // The slice a hovered card would remove: drawn over the fill from where
    // the bar would end, so you read the bite and the remainder at once.
    if (preview > 0.0f) {
        float from = std::max(0.0f, frac - preview);
        SDL_Rect p{ q.x + 2 + (int)(inner * from), q.y + 2,
                    std::max(1, (int)(inner * (frac - from))), q.h - 4 };
        fill(r, p, SDL_Color{ 255, 236, 170, 255 }, 235);
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

// ---- what a status does -------------------------------------------------
// Point at a tag in the panel, or click it, and a box says what it does. The
// panel records where each tag landed as it draws, and the tip layer, drawn
// over the log and the hand, puts the box up under the one the mouse is on.
struct Tip { SDL_Rect at; std::string title, text; };
std::vector<Tip> gTips;

// The numbers in a tag, in order: "Poison 3/turn x2" gives 3 and 2.
std::vector<double> numbersIn(const std::string& t) {
    std::vector<double> out;
    for (size_t i = 0; i < t.size(); ) {
        if (std::isdigit((unsigned char)t[i])) {
            size_t j = i;
            while (j < t.size() && (std::isdigit((unsigned char)t[j]) || t[j] == '.')) j++;
            out.push_back(std::atof(t.substr(i, j - i).c_str()));
            i = j;
        } else {
            i++;
        }
    }
    return out;
}

std::string num(double v) { std::ostringstream o; o << v; return o.str(); }
std::string turns(double n) { return num(n) + (n == 1 ? " more turn" : " more turns"); }
std::string times(double n) { return num(n) + (n == 1 ? " more time" : " more times"); }

// What a tag in the panel means, said for you or for the enemy. Empty for a
// tag that needs no explaining.
std::string describeTag(const std::string& t, bool you) {
    auto starts = [&](const char* p) { return t.rfind(p, 0) == 0; };
    auto has = [&](const char* p) { return t.find(p) != std::string::npos; };
    const std::vector<double> n = numbersIn(t);
    auto at = [&](size_t i) { return i < n.size() ? n[i] : 0.0; };
    if (starts("Poison") || starts("Burn"))
        return (you ? "You lose " : "It loses ") + num(at(0)) + " health at the start of each of "
             + (you ? "your" : "its") + " turns, " + times(at(1)) + ". Armor does not stop it.";
    if (starts("Rend"))
        return you ? "Every attack card you play tears the wound open: " + num(at(0)) + " damage to you, "
                     + times(at(1)) + "."
                   : "Every time it attacks, the wound tears for " + num(at(0)) + ", " + times(at(1)) + ".";
    if (starts("Stunned"))
        return you ? "You lose your next turn." : "It loses its next turn.";
    if (starts("Weak")) {
        const int pct = at(0) > 0 ? (int)(100.0 / at(0) + 0.5) : 100;
        return std::string(you ? "Your" : "Its") + " attacks deal " + std::to_string(pct)
             + "% of their damage for " + turns(at(1)) + ".";
    }
    if (starts("Strength"))
        return std::string(you ? "Your" : "Its") + " attacks deal x" + num(at(0)) + " damage for "
             + turns(at(1)) + ".";
    if (starts("Reversed"))
        return "Its weakness and resistance have swapped for the rest of the fight: it is weak to what it "
               "resisted, and resists what it was weak to.";
    if (starts("Blows:"))
        return "Its blows land as " + t.substr(7) + " for the rest of the fight.";
    if (starts("Armor turned"))
        return "Your armor's resistances and its weakness have swapped places until this fight ends.";
    if (starts("Exposed"))
        return you ? "You take x1.5 damage until your next turn."
                   : "Your attacks deal x1.5 to it until its next turn.";
    if (starts("Fortified"))
        return "Your armor does not fade at the end of your turn. It holds for " + turns(at(0))
             + ", or until it is broken.";
    if (starts("-") && has("energy"))
        return "You start your next turn with " + num(at(0)) + " less energy.";
    if (starts("-") && has("%"))
        return "Your attacks deal " + num(at(0)) + "% less this turn.";
    if (starts("-") && has("dmg"))
        return "Your attacks deal " + num(at(0)) + " less this turn.";
    if (starts("PACT"))
        return "Pact of Ruin: for the rest of this fight your attacks add Burn and Rend, every card "
               "costs 2 HP, and you cannot heal.";
    if (starts("No healing"))
        return "Nothing heals you for the rest of this fight.";
    if (starts("Guard"))
        return "Any ailment an enemy tries to put on you is blocked, for " + turns(at(0)) + ".";
    if (starts("Next:"))
        return "Your Scholar's Lens has read its next move:" + t.substr(5) + ".";
    if (starts("Phased"))
        return "Your attacks pass straight through it this turn.";
    // The ruined church.
    if (starts("Stone"))
        return "It is stone this turn: nothing you do hurts it. It wakes on its turn and drops on you.";
    if (starts("Toll"))
        return "The bell has tolled " + (at(0) == 1 ? std::string("once") : at(0) == 2 ? std::string("twice")
                                         : num(at(0)) + " times")
             + ". The third toll is the Great Toll, a huge blow. "
               "A stun or a taunt makes it miss one.";
    if (starts("Marked"))
        return "Its marks on you. The bolt after the third ignores your armor. A turn it does not fire clears them.";
    if (starts("Glass"))
        return "Its armor is glass. Break it with an attack and the shards cut you for half of it. "
               "Pierce hits go round it.";
    if (starts("Rises once"))
        return "When it falls it gets back up once, at half its health, unless Fire finishes it or it is burning.";
    if (starts("Risen"))
        return "It has got up once already. Next time it stays down.";
    if (starts("Lost:"))
        return "What the False Moon has taken back. Strength: your blows land a quarter softer. Wits: a card "
               "fewer. Speed: one energy fewer. The way you move: your guard half as strong.";
    if (starts("Dirt"))
        return "Grave Dirt in your deck: cards that do nothing, gone when the fight ends. Three at most.";
    if (starts("Reversal set"))
        return "It has your Dodge Reversal up: your next attack is turned back on you, and it takes "
               "only a quarter.";
    if (starts("Parry stance"))
        return "Your next attack is caught: it lands for half, and it hits you back.";
    if (has("heads"))
        return "It bites once per head, each bite at half weight. A Pierce hit cuts a head off, and a Rend tear does half the time.";
    if (has("stump"))
        return "When it regrows, two heads come back from each open stump. A Fire hit or a Burn on it sears them shut.";
    if (starts("Taunt"))
        return "It is taunted: it can only make a plain attack, for " + turns(at(0)) + ".";
    if (starts("Fear"))
        return "It is afraid: on its turn it may flinch, raise its guard and give up its move, for "
             + turns(at(0)) + ".";
    return "";
}

// The warning line under the panel.
std::string describeNotice(const std::string& t) {
    if (t.rfind("CURSED", 0) == 0)
        return "Kill it before the count reaches zero, or you turn to stone and the run ends.";
    if (t.rfind("TAUNTED", 0) == 0)
        return "You can only play attack cards this turn.";
    if (t.rfind("BOUND", 0) == 0)
        return "You can play only one card this turn.";
    if (t.rfind("MOONSTRUCK", 0) == 0)
        return "Every two turns it takes back a piece of you, and at zero you are its vessel. Under five "
               "turns left, every quarter of its health you take wins a turn back. Sacrifice and Last "
               "Stand each win a turn back.";
    if (t.rfind("CONFESSED", 0) == 0)
        return "Playing that card heals the Confessor instead of hurting it. Hold it back this turn.";
    if (t.rfind("STONE", 0) == 0)
        return "Your attacks cannot hurt it this turn. Spend the turn on armor and setting up.";
    if (t.rfind("GREAT TOLL", 0) == 0)
        return "Its next turn is the Great Toll. Stun it or taunt it to make it miss the toll, or get your armor up.";
    if (t.rfind("MARKED", 0) == 0)
        return "Its next bolt ignores your armor. A stun or a taunt clears the marks.";
    return "";
}

// Records where each [tag] of a coloured tag line lands, drawn from x. The
// escapes take no room, so only the visible characters are counted.
void recordTags(const std::string& ansi, int x, int y, bool you) {
    const int cw = std::max(1, Platform::cellW()), ch = std::max(1, Platform::cellH());
    int col = 0, open = -1;
    std::string inner;
    for (size_t i = 0; i < ansi.size(); i++) {
        if (ansi[i] == '\x1b') {                 // an escape: skip to its final letter
            while (i < ansi.size() && !std::isalpha((unsigned char)ansi[i])) i++;
            continue;
        }
        const char c = ansi[i];
        if (c == '[') { open = col; inner.clear(); }
        else if (c == ']' && open >= 0) {
            const std::string text = describeTag(inner, you);
            if (!text.empty())
                gTips.push_back(Tip{ SDL_Rect{ x + open * cw, y, (col - open + 1) * cw, ch }, inner, text });
            open = -1;
        }
        else if (open >= 0) inner += c;
        col++;
    }
}

// Word-wrapped to `width` characters.
std::vector<std::string> wrapText(const std::string& s, int width) {
    std::vector<std::string> lines;
    std::istringstream in(s);
    std::string word, line;
    while (in >> word) {
        if (!line.empty() && (int)(line.size() + 1 + word.size()) > width) { lines.push_back(line); line.clear(); }
        line += (line.empty() ? "" : " ") + word;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// The box for the tag under the mouse, under that tag, kept on the screen.
void drawTip() {
    if (!gActive || gTips.empty()) return;
    int mx = 0, my = 0;
    Platform::mousePos(mx, my);
    const Tip* hit = nullptr;
    for (const Tip& t : gTips)
        if (mx >= t.at.x && mx < t.at.x + t.at.w && my >= t.at.y - 2 && my < t.at.y + t.at.h + 2) { hit = &t; break; }
    if (!hit) return;

    SDL_Renderer* r = Platform::renderer();
    const int cw = std::max(1, Platform::cellW()), ch = std::max(1, Platform::cellH());
    const std::vector<std::string> lines = wrapText(hit->text, 46);
    int cells = (int)hit->title.size();
    for (const std::string& l : lines) cells = std::max(cells, (int)l.size());
    const int pad = std::max(10, cw);
    const int w = cells * cw + pad * 2;
    const int h = (int)(lines.size() + 1) * (ch + 3) + pad * 2 + 4;
    int x = std::max(8, std::min(hit->at.x, Platform::screenW() - w - 8));
    int y = hit->at.y + hit->at.h + 8;
    if (y + h > Platform::screenH() - 8) y = std::max(8, hit->at.y - h - 8);

    // The tag itself, underlined, so it is plain which one is being explained.
    fill(r, SDL_Rect{ hit->at.x, hit->at.y + hit->at.h, hit->at.w, 2 }, SDL_Color{ 166, 226, 46, 255 });
    const SDL_Rect box{ x, y, w, h };
    DrawUtil::glowRound(r, box, radius(), SDL_Color{ 0, 0, 0, 255 }, 6, 110);
    DrawUtil::fillRound(r, box, radius(), tone(24));
    DrawUtil::frameRound(r, box, radius(), tone(96));
    Console::drawTextPx(r, x + pad, y + pad, hit->title, SDL_Color{ 166, 226, 46, 255 }, true);
    for (size_t i = 0; i < lines.size(); i++)
        Console::drawTextPx(r, x + pad, y + pad + (int)(i + 1) * (ch + 3) + 4, lines[i],
                            SDL_Color{ 222, 222, 232, 255 }, false);
}

void ease(float& lag, float target, float dt) {
    if (lag < 0.0f) { lag = target; return; }
    if (lag > target) { lag -= dt * 0.9f; if (lag < target) lag = target; }
    else lag = target;                       // healing snaps, damage trails
}

void draw() {
    gTips.clear();
    if (!gActive) return;
    // Live, except the two fields that size the band: changing those here
    // would move the log halfway through a frame, so they wait for set().
    if (gSource) {
        State s = gSource();
        s.addActive = gState.addActive;
        s.allyActive = gState.allyActive;
        s.notice    = gState.notice;
        if (s.enemyName != gState.enemyName) gEnemyLag = -1.0f;
        gState = s;
    }
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

    // Advance by what was actually drawn, not a fixed column count: the
    // numbers grow ("ATK +0   DEF +0" is 15 cells) and a fixed step made ARM
    // print on top of them.
    auto put = [&](int& cx, const std::string& t, SDL_Color col, bool bold = false) {
        Console::drawTextPx(r, cx, y, t, col, bold);
        cx += ((int)t.size() + 3) * cw;
    };

    // --- row 1: turn, energy, where you are -------------------------------
    int col = x;
    put(col, "Turn " + std::to_string(gState.turn), ink, true);
    put(col, std::to_string(gState.energy) + "/" + std::to_string(gState.maxEnergy) + " energy",
        energy);
    put(col, gState.encounter, dim);

    // --- row 2: you -------------------------------------------------------
    const int barW = std::max(120, std::min(360, panel.w / 4));
    const int barH = std::max(12, ch - 4);
    y += ch + 4;
    Console::drawTextPx(r, x, y, "YOU", ink, true);
    SDL_Rect pb{ x + 6 * cw, y + 1, barW, barH };
    bar(r, pb, pf, gPlayerLag, hpColor(gState.playerHp, gState.playerMax));
    int tx = pb.x + pb.w + cw;
    put(tx, std::to_string(gState.playerHp) + "/" + std::to_string(gState.playerMax),
        hpColor(gState.playerHp, gState.playerMax));
    // Flat bonus, then the gear percentage; a zero part is left out rather than
    // printed as "+0".
    auto stat = [](const char* label, int flat, int pct) {
        std::string s = label;
        if (flat) s += " +" + std::to_string(flat);
        if (pct)  s += " +" + std::to_string(pct) + "%";
        return s;
    };
    put(tx, stat("ATK", gState.playerAtk, gState.playerAtkPct) + "   "
          + stat("DEF", gState.playerDef, gState.playerDefPct), dim);
    if (gState.playerArmor > 0)
        put(tx, "ARM " + std::to_string(gState.playerArmor), armor);
    else if (gState.armorBroken)
        put(tx, "ARM 0", SDL_Color{ 226, 96, 88, 255 });
    if (!gState.playerTags.empty()) {
        Console::drawAnsiPx(r, tx, y, gState.playerTags, dim, false);
        recordTags(gState.playerTags, tx, y, true);
    }

    // --- your raised dead, when one is standing -----------------------------
    if (gState.allyActive) {
        y += ch + 4;
        const SDL_Color allyInk{ 150, 214, 214, 255 };
        Console::drawTextPx(r, x, y, gState.allyName, allyInk, true);
        SDL_Rect ab{ x + 6 * cw, y + 1, barW * 2 / 3, barH };
        if ((int)gState.allyName.size() + 1 > 6)
            ab.x += ((int)gState.allyName.size() + 1 - 6) * cw;
        bar(r, ab, gState.allyMax > 0 ? (float)gState.allyHp / gState.allyMax : 0.0f,
            0.0f, hpColor(gState.allyHp, gState.allyMax));
        int ax = ab.x + ab.w + cw;
        put(ax, std::to_string(gState.allyHp) + "/" + std::to_string(gState.allyMax),
            hpColor(gState.allyHp, gState.allyMax));
        put(ax, "fights beside you", dim);
        gTips.push_back(Tip{ SDL_Rect{ x, y, (int)gState.allyName.size() * cw, ch }, gState.allyName,
            "Your raised " + gState.allyName + " takes the enemy's attacks for you until it falls, and "
            "strikes for " + std::to_string(gState.allyStrike) + " at the end of each of your turns, "
            "past their defense." });
    }

    // --- row 3: the enemy -------------------------------------------------
    y += ch + 4;
    Console::drawTextPx(r, x, y, gState.enemyName, ink, true);
    SDL_Rect eb{ x + 6 * cw, y + 1, barW, barH };
    // Give the name room when it is longer than the label column allows.
    if ((int)gState.enemyName.size() + 1 > 6) {
        int shift = ((int)gState.enemyName.size() + 1 - 6) * cw;
        eb.x += shift;
    }
    const float pv = (gPreview > 0 && gState.enemyMax > 0)
                   ? std::min(ef, (float)gPreview / gState.enemyMax) : 0.0f;
    bar(r, eb, ef, gEnemyLag, hpColor(gState.enemyHp, gState.enemyMax), pv);
    tx = eb.x + eb.w + cw;
    put(tx, std::to_string(gState.enemyHp) + "/" + std::to_string(gState.enemyMax),
        hpColor(gState.enemyHp, gState.enemyMax));
    if (gPreview > 0)
        put(tx, gPreview >= gState.enemyHp ? "LETHAL" : ("-" + std::to_string(gPreview)),
            SDL_Color{ 255, 236, 170, 255 }, true);
    put(tx, "ATK " + std::to_string(gState.enemyAtk)
          + "   DEF " + std::to_string(gState.enemyDef), dim);
    if (!gState.enemyTags.empty()) {
        Console::drawAnsiPx(r, tx, y, gState.enemyTags, dim, false);
        recordTags(gState.enemyTags, tx, y, false);
    }

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
        put(tx, std::to_string(gState.addHp) + "/" + std::to_string(gState.addMax),
            hpColor(gState.addHp, gState.addMax));
        put(tx, "fights beside the " + gState.enemyName, dim);
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
        const std::string why = describeNotice(gState.notice);
        if (!why.empty())
            gTips.push_back(Tip{ SDL_Rect{ x, y, (int)note.size() * cw, ch }, note, why });
    }
    panelLabel(r, panel, "COMBAT");

    // --- the log gets a frame of its own -----------------------------------
    // The text between the panel and the hand, where combat results scroll.
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
// panel margins. Each optional line below them needs a row of its own, or
// the curse notice would push out through the border.
int rowsNeeded() {
    return 5 + (gState.addActive ? 1 : 0) + (gState.allyActive ? 1 : 0)
             + (gState.notice.empty() ? 0 : 1);
}

void set(const State& s) {
    // A new enemy resets the ghost, or the bar would trail from the last fight.
    if (s.enemyName != gState.enemyName) gEnemyLag = -1.0f;
    gState = s;
    // A summon or a curse arrives mid-fight, so the band is re-measured on
    // every update rather than only when the panel is switched on.
    if (gActive) Console::setHudRows(rowsNeeded());
}

void setSource(const std::function<State()>& fn) { gSource = fn; }

void setPreview(int hpLoss) { gPreview = std::max(0, hpLoss); }

void setActive(bool on) {
    gActive = on;
    if (!gInstalled) {
        Platform::setHudRenderer(&draw);
        Platform::setTipRenderer(&drawTip);
        gInstalled = true;
    }
    Console::setHudRows(on ? rowsNeeded() : 0);
    if (!on) { gPlayerLag = gEnemyLag = -1.0f; gPreview = 0; }
}

} // namespace Hud
