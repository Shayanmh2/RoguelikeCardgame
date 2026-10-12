#include "StatusIcons.h"

#include <cstring>

namespace StatusIcons {
namespace {

// '#' is drawn in the tag's colour, 'o' a step lighter, '.' is left clear.
// The first sixteen are the set the user saw on the Road Ahead page
// (2026-10-11); the rest are the enemy's tags, drawn to match.
struct Symbol { const char* rows[8]; };
const Symbol SYMBOLS[] = {
    {{ "...#....", "...##...", "..####..", ".##o###.", ".#o####.", ".######.", "..####..", "........" }}, // droplet: Poison
    {{ "....#...", "...##...", "..###.#.", ".#####..", ".##o###.", ".#ooo##.", "..####..", "........" }}, // flame: Burn
    {{ "....#..#", "...#..#.", "..#..#..", ".#..#..#", "#..#..#.", "..#..#..", ".#..#...", "........" }}, // claws: Rend
    {{ "...#....", "..###...", "#######.", ".#####..", "..#.#...", ".#...#..", "........", "........" }}, // star: Stunned
    {{ "...##...", "...##...", "...##...", "########", ".######.", "..####..", "...##...", "........" }}, // down: Weak
    {{ "...##...", "..####..", ".######.", "########", "...##...", "...##...", "...##...", "........" }}, // up: Strength
    {{ "########", "#oooooo#", "#ooo.oo#", "#oo..oo#", ".#oooo#.", ".#oooo#.", "..#oo#..", "...##..." }}, // shield: Fortified
    {{ "..####..", ".#....#.", "#..##..#", "#.####.#", "#.####.#", "#..##..#", ".#....#.", "..####.." }}, // ring: Guard
    {{ ".##..##.", "########", "#####.##", "####.###", ".##.###.", "..#.##..", "...##...", "........" }}, // broken heart: No healing
    {{ "....##..", "...##...", "..##....", ".######.", "....##..", "...##...", "..##....", ".#......" }}, // bolt: energy next turn
    {{ "..####..", ".###....", "###.....", "###.....", "###.....", ".###....", "..####..", "........" }}, // crescent: Lost
    {{ "########", "###.####", "####.###", "###.####", ".###.##.", ".##.###.", "..#.##..", "...##..." }}, // cracked shield: Exposed
    {{ ".#......", "########", ".#......", "........", "......#.", "########", "......#.", "........" }}, // swap: Armor turned, Reversed
    {{ "........", "........", "...##...", "..####..", ".######.", "########", "########", "........" }}, // mound: Dirt
    {{ "..####..", ".######.", "##.##.##", "##.##.##", ".######.", "..#.#.#.", "..#####.", "........" }}, // skull: PACT
    {{ "......#.", ".....##.", "....##..", "...##...", ".###....", "..#.....", ".#.#....", "........" }}, // dagger: less damage
    {{ ".####...", "#o...#..", "#o...#..", "#....#..", ".####...", "....##..", ".....##.", "......##" }}, // lens: Next
    {{ "........", "..####..", ".#oo###.", "#oo#####", "#o###.##", "####.###", ".######.", "........" }}, // rock: Stone
    {{ "..####..", ".######.", "##o##o##", "##o##o##", "########", "########", "##.##.##", "........" }}, // ghost: Phased
    {{ "..####..", ".#....#.", ".#....#.", ".#....#.", "###...#.", ".#....#.", "......#.", "......#." }}, // turn-back arrow: Reversal set
    {{ "o......o", ".o....o.", "..o..o..", "...oo...", "...##...", "..#..#..", "##....##", "#......#" }}, // crossed swords: Parry stance
    {{ "#..#..#.", ".#.#.#..", "..###...", "###o###.", "..###...", ".#.#.#..", "#..#..#.", "........" }}, // burst: Blows
    {{ "##.##.##", "##.##.##", ".#.##.#.", "..####..", "...##...", "..####..", ".######.", "........" }}, // three necks: heads
    {{ "........", ".######.", "#oooooo#", ".######.", ".######.", ".######.", ".######.", "........" }}, // stump: open stumps
    {{ "...##...", "..####..", "..#o##..", ".##o###.", ".######.", "########", "...##...", "........" }}, // bell: Toll
    {{ "...#....", ".##.##..", ".#...#..", "##.#.##.", ".#...#..", ".##.##..", "...#....", "........" }}, // crosshair: Marked
    {{ "...##...", "..#o##..", ".#oo###.", "#ooo####", ".#o####.", "..####..", "...##...", "........" }}, // shard: Glass
    {{ "..####..", ".######.", ".##..##.", ".#....#.", ".##..##.", ".##..##.", "########", "........" }}, // headstone: Rises once, Risen
    {{ "..###...", "..###...", "..###...", "..###...", "...#....", "........", "..###...", "........" }}, // "!": Taunt
    {{ ".######.", "#oooooo#", "#o#oo#o#", "#oooooo#", "#oo##oo#", "#oo##oo#", ".######.", "........" }}, // wide-eyed face: Fear
};
enum Id {
    DROPLET, FLAME, CLAWS, STAR, DOWN, UP, SHIELD, RING, NOHEAL, BOLT, CRESCENT, CRACKED, SWAP, MOUND,
    SKULL, DAGGER, LENS, ROCK, GHOST, TURN_BACK, SWORDS, BURST, NECKS, STUMP, BELL, CROSSHAIR, SHARD,
    HEADSTONE, SHOUT, FACE, COUNT
};
static_assert(sizeof(SYMBOLS) / sizeof(SYMBOLS[0]) == COUNT, "one pattern per symbol");

SDL_Texture* gWhole = nullptr;   // every pixel of every symbol, white, to be tinted
SDL_Texture* gLight = nullptr;   // only the lighter ones
bool gBuilt = false;

// All the symbols side by side, white where a pixel's mark is in `marks`.
SDL_Texture* strip(SDL_Renderer* r, const char* marks) {
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 8 * COUNT, 8, 32, SDL_PIXELFORMAT_RGBA32);
    if (!s) return nullptr;
    SDL_LockSurface(s);
    for (int i = 0; i < COUNT; ++i)
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const char m = SYMBOLS[i].rows[y][x];
                Uint32* px = (Uint32*)((Uint8*)s->pixels + y * s->pitch) + i * 8 + x;
                *px = SDL_MapRGBA(s->format, 255, 255, 255, std::strchr(marks, m) ? 255 : 0);
            }
    SDL_UnlockSurface(s);
    SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
    SDL_FreeSurface(s);
    if (t) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
    }
    return t;
}

Uint8 lighter(Uint8 v) { return (Uint8)(v + (255 - v) * 45 / 100); }

} // namespace

int forTag(const std::string& t) {
    auto starts = [&](const char* p) { return t.rfind(p, 0) == 0; };
    auto has = [&](const char* p) { return t.find(p) != std::string::npos; };
    if (starts("Poison"))       return DROPLET;
    if (starts("Burn"))         return FLAME;
    if (starts("Rend"))         return CLAWS;
    if (starts("Stunned"))      return STAR;
    if (starts("Weak"))         return DOWN;
    if (starts("Strength"))     return UP;
    if (starts("Fortified"))    return SHIELD;
    if (starts("Guard"))        return RING;
    if (starts("No healing"))   return NOHEAL;
    if (starts("-") && has("energy")) return BOLT;
    if (starts("-") && has("dmg"))    return DAGGER;
    if (starts("Lost:"))        return CRESCENT;
    if (starts("Exposed"))      return CRACKED;
    if (starts("Armor turned") || starts("Reversed")) return SWAP;
    if (starts("Dirt"))         return MOUND;
    if (starts("PACT"))         return SKULL;
    if (starts("Next:"))        return LENS;
    if (starts("Stone"))        return ROCK;
    if (starts("Phased"))       return GHOST;
    if (starts("Reversal set")) return TURN_BACK;
    if (starts("Parry stance")) return SWORDS;
    if (starts("Blows:"))       return BURST;
    if (has("heads"))           return NECKS;
    if (has("stump"))           return STUMP;
    if (starts("Toll"))         return BELL;
    if (starts("Marked"))       return CROSSHAIR;
    if (starts("Glass"))        return SHARD;
    if (starts("Rises once") || starts("Risen")) return HEADSTONE;
    if (starts("Taunt"))        return SHOUT;
    if (starts("Fear"))         return FACE;
    return -1;
}

void draw(SDL_Renderer* r, int id, int x, int y, int z, SDL_Color c) {
    if (!r || id < 0 || id >= COUNT) return;
    // The renderer lives as long as the window, so the strips are made once.
    if (!gBuilt) {
        gWhole = strip(r, "#o");
        gLight = strip(r, "o");
        gBuilt = true;
    }
    if (!gWhole) return;
    const SDL_Rect src{ id * 8, 0, 8, 8 }, dst{ x, y, 8 * z, 8 * z };
    SDL_SetTextureColorMod(gWhole, c.r, c.g, c.b);
    SDL_RenderCopy(r, gWhole, &src, &dst);
    if (gLight) {
        SDL_SetTextureColorMod(gLight, lighter(c.r), lighter(c.g), lighter(c.b));
        SDL_RenderCopy(r, gLight, &src, &dst);
    }
}

} // namespace StatusIcons
