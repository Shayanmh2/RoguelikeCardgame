// The bestiary: every enemy met, in a gallery off the title menu (the user,
// 2026-10-11: each enemy's first idle sprite, with the option to read its
// entry). One page per area, each enemy a card in the first frame of its
// idle, a shadow until it has been met. Choosing one opens its entry.
#include "Game.h"
#include "Achievements.h"
#include "CardBar.h"
#include "EnemyArt.h"
#include "Audio.h"
#include "Console.h"
#include "DrawUtil.h"
#include "Platform.h"
#include "UIHelper.h"
#include <SDL.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include "GameShared.h"

namespace {

enum Area { DUNGEON, DARK_DUNGEON, FOREST, LAKE, MOUNTAIN, CHURCH, AREAS };
const char* const AREA_NAME[AREAS] = { "The Dungeon", "The Dark Dungeon", "The Wicked Forest",
                                       "The Dark Lake", "The Mountain", "The Ruined Church" };

enum Kind { REGULAR, BOSS, SHADE };

// Every enemy, by area: its road, its boss, and the Moon Shade that walks it.
// An enemy's place in this list is its bit in progress.dat's MET record, so a
// new one goes on the end; it is shown with its area wherever it sits.
struct Beast {
    const char* key;      // found in the fight's name; the longest match wins
    const char* name;     // as the gallery shows it
    const char* sheet;    // assets/sprites/<sheet>.png
    int frameW;
    EnemyType type;
    Area area;
    Kind kind;
    const char* entry;    // its own entry; until one is written, View Enemy's line
};
using T = EnemyType;
const Beast BESTIARY[] = {
    { "Goblin", "Goblin", "melee_goblin", 30, T::MELEE, DUNGEON, REGULAR, "" },
    { "Orc", "Orc", "tank_orc", 30, T::TANK, DUNGEON, REGULAR, "" },
    { "Wizard", "Wizard", "caster_wizard", 30, T::CASTER, DUNGEON, REGULAR, "" },
    { "Skeleton", "Skeleton", "undead_skeleton", 30, T::UNDEAD, DUNGEON, REGULAR, "" },
    { "Spider", "Spider", "beast_spider", 30, T::BEAST, DUNGEON, REGULAR, "" },
    { "Archer", "Archer", "ranged_archer", 30, T::RANGED, DUNGEON, REGULAR, "" },
    { "Bandit", "Bandit", "melee_bandit", 30, T::MELEE, DUNGEON, REGULAR, "" },
    { "Warden", "Warden", "tank_warden", 30, T::TANK, DUNGEON, REGULAR, "" },
    { "Sage", "Sage", "caster_sage", 30, T::CASTER, DUNGEON, REGULAR, "" },
    { "Stone Colossus", "Stone Colossus", "boss_colossus", 30, T::TANK, DUNGEON, BOSS, "The Stone Colossus is the dungeon's foundation, the old stone its halls were built on. When your strength came loose at the pond, the stone was the first thing it fell into, and the stone stood up. The moon set one of its vigils in its chest as well, a small cold flame, and as long as it burned, the moon never had to sleep." },
    { "Moon Shade Weaver", "Moon Shade Weaver", "moon_weaver", 30, T::UNDEAD, DUNGEON, SHADE, "Before the moon struck it, it was a cave spider that lived in the bones of a climber who died deep under the dungeon. The moon took the two together and wears them as one shape: a skeleton with a spider's legs, set on wrong. It was one of the first shapes the moon ever took, and it has followed you since the first gate, learning how you move." },

    { "Ghoul", "Ghoul", "undead_ghoul", 30, T::UNDEAD, DARK_DUNGEON, REGULAR, "" },
    { "Basilisk", "Basilisk", "beast_basilisk", 30, T::BEAST, DARK_DUNGEON, REGULAR, "" },
    { "Assassin", "Assassin", "ranged_assassin", 30, T::RANGED, DARK_DUNGEON, REGULAR, "" },
    { "Knight", "Knight", "melee_knight", 30, T::MELEE, DARK_DUNGEON, REGULAR, "" },
    { "Sentinel", "Sentinel", "tank_sentinel", 30, T::TANK, DARK_DUNGEON, REGULAR, "" },
    { "Enchanter", "Enchanter", "caster_enchanter", 30, T::CASTER, DARK_DUNGEON, REGULAR, "" },
    { "Wraith", "Wraith", "undead_wraith", 30, T::UNDEAD, DARK_DUNGEON, REGULAR, "" },
    { "Serpent", "Serpent", "beast_serpent", 30, T::BEAST, DARK_DUNGEON, REGULAR, "" },
    { "Omneye", "Omneye", "ranged_omneye", 30, T::RANGED, DARK_DUNGEON, REGULAR, "" },
    { "Vile Witch", "Vile Witch", "boss_witch", 30, T::CASTER, DARK_DUNGEON, BOSS, "The Vile Witch runs the dark dungeon. She pays its guards, keeps the Omneye to watch her halls, and mixes her own poisons. Your wits ended up on her altar, and she spent her nights studying them without ever learning what they were." },
    { "Moon Shade Beguiler", "Moon Shade Beguiler", "moon_beguiler", 30, T::CASTER, DARK_DUNGEON, SHADE, "Before the moon struck her, she was a mage, the sister the young Enchanter is still searching the dark dungeon for. She came down into the witch's halls years ago and never came back up. The moon wears her now and is learning her magic through her, and on some nights it sings with her voice. Lately the song has your name in it." },

    { "Raider", "Raider", "melee_raider", 30, T::MELEE, FOREST, REGULAR, "" },
    { "Barbarian", "Barbarian", "tank_barbarian", 30, T::TANK, FOREST, REGULAR, "" },
    { "Mystic", "Mystic", "caster_mystic", 30, T::CASTER, FOREST, REGULAR, "" },
    { "Banshee", "Banshee", "undead_banshee", 30, T::UNDEAD, FOREST, REGULAR, "" },
    { "Wolf", "Wolf", "beast_wolf", 30, T::BEAST, FOREST, REGULAR, "" },
    { "Falcon", "Falcon", "ranged_falcon", 30, T::RANGED, FOREST, REGULAR, "" },
    { "Berserker", "Berserker", "melee_berserker", 30, T::MELEE, FOREST, REGULAR, "" },
    { "Guardian", "Guardian", "tank_guardian", 30, T::TANK, FOREST, REGULAR, "" },
    { "Vampire", "Vampire", "caster_vampire", 30, T::CASTER, FOREST, REGULAR, "" },
    { "Thunder Beast", "Thunder Beast", "boss_warlord", 30, T::MELEE, FOREST, BOSS, "The storm over the Wicked Forest is not weather. It follows the Thunder Beast, and it gets louder the longer a fight lasts. Since the moon struck you, the beast has carried your speed in its chest, and every blow it lands makes the next one heavier." },
    { "Moon Shade Werewolf", "Moon Shade Werewolf", "moon_werewolf", 30, T::BEAST, FOREST, SHADE, "Before the moon struck it, it was a wolf, the leader of a pack in the Wicked Forest. It was one of the moon's early shapes, and it shows: the moon wears it standing up and walking like a man, which no wolf ever did, and up close the face and the fur are not quite right. What is left of its pack still hunts the forest, always one at a time." },

    { "Specter", "Specter", "undead_specter", 30, T::UNDEAD, LAKE, REGULAR, "" },
    { "Cockatrice", "Cockatrice", "beast_cockatrice", 30, T::BEAST, LAKE, REGULAR, "" },
    { "Gladiator", "Gladiator", "melee_gladiator", 30, T::MELEE, LAKE, REGULAR, "" },
    { "Bastion", "Bastion", "tank_bastion", 30, T::TANK, LAKE, REGULAR, "" },
    { "Spellmaster", "Spellmaster", "caster_spellmaster", 30, T::CASTER, LAKE, REGULAR, "" },
    { "Warrior", "Warrior", "melee_warrior", 30, T::MELEE, LAKE, REGULAR, "" },
    { "Sorcerer", "Sorcerer", "caster_sorcerer", 30, T::CASTER, LAKE, REGULAR, "" },
    { "Fortress", "Fortress", "tank_fortress", 30, T::TANK, LAKE, REGULAR, "" },
    { "Wyvern", "Wyvern", "beast_wyvern", 30, T::RANGED, LAKE, REGULAR, "" },
    { "Hydra", "Hydra", "boss_hydra", 30, T::BEAST, LAKE, BOSS, "The Hydra lives at the bottom of the Dark Lake and lets nothing cross. Cut off one of its heads and two more grow back in its place, unless fire seals the wound. Your hands, the sure way they knew a blade, came to rest in this lake, and the Hydra kept them without knowing." },
    { "Moon Shade Gorgon", "Moon Shade Gorgon", "moon_gorgon", 30, T::BEAST, LAKE, SHADE, "Before the moon struck her, she was a gorgon who lived on the shore of the Dark Lake, and the black glass along that shore is said to be her work. She was one of the later shapes it took, and it wears her far better than the early ones: nothing about her looks wrong, except that she no longer blinks. Her stare still stiffens your joints, and when she falls, the stiffness goes out of you with her." },

    { "Deadeye", "Deadeye", "ranged_deadeye", 30, T::RANGED, MOUNTAIN, REGULAR, "" },
    { "Enforcer", "Enforcer", "melee_enforcer", 30, T::MELEE, MOUNTAIN, REGULAR, "" },
    { "Revenant", "Revenant", "undead_revenant", 30, T::UNDEAD, MOUNTAIN, REGULAR, "" },
    { "Manticore", "Manticore", "beast_manticore", 30, T::BEAST, MOUNTAIN, REGULAR, "" },
    { "Lich", "Lich", "undead_lich", 30, T::UNDEAD, MOUNTAIN, REGULAR, "" },
    { "Paladin", "Paladin", "tank_paladin", 30, T::TANK, MOUNTAIN, REGULAR, "" },
    { "Archon", "Archon", "caster_archon", 30, T::CASTER, MOUNTAIN, REGULAR, "" },
    { "Unchosen", "The Unchosen", "melee_unchosen", 30, T::MELEE, MOUNTAIN, REGULAR, "" },
    { "Undead Dragon", "Undead Dragon", "boss_dragon", 30, T::RANGED, MOUNTAIN, BOSS, "The dragon died on the mountain ages ago and did not stay down. When your soul came loose, it lodged in the dragon's chest, and that is what has kept it moving since: something dead, held on its feet by a soul that is not its own." },
    { "Shadow Knight", "Shadow Knight", "boss_shadowknight", 30, T::UNDEAD, MOUNTAIN, BOSS, "The thing from the moon's reflection, in your armor and holding your sword, both taken from you at the pond. It has lived in them so long that it has corrupted them: the plate has gone dark, and the blade with it. It waited on the peak through the whole climb, practicing being you, and it answers every card you play with one of your own." },
    { "Moonstruck Shadow Knight", "Moonstruck Shadow Knight", "moon_shadowknight", 56, T::UNDEAD, MOUNTAIN, BOSS, "When the last vigil goes out, it stops pretending to be you. It still has your armor on and your sword in its hand, but its own shape is breaking out through the plates. Everything it has taken in has malformed it: it is bone-white and spiked, with the moon's red in its eyes, and shaped around its memory of you. It fights to stay awake with your own cards, and takes up your stances before your blows land." },
    { "Moon Shade Templar", "Moon Shade Templar", "moon_templar", 30, T::TANK, MOUNTAIN, SHADE, "Before the moon struck him, he was the templar of the mountain's heretics, the one they followed when they turned from the sun to the moon. When the moon struck him, they took it as a sign they had chosen right. He was the last shape it took before you, and the one it wears best: it climbs after you like a man." },

    { "Sexton", "Sexton", "melee_sexton", 30, T::MELEE, CHURCH, REGULAR, "" },
    { "Mirror Nun", "Mirror Nun", "caster_mirror_nun", 30, T::CASTER, CHURCH, REGULAR, "" },
    { "Gargoyle", "Gargoyle", "beast_gargoyle", 30, T::BEAST, CHURCH, REGULAR, "" },
    { "Bellringer", "Bellringer", "tank_bellringer", 30, T::TANK, CHURCH, REGULAR, "" },
    { "Inquisitor", "Inquisitor", "ranged_inquisitor", 30, T::RANGED, CHURCH, REGULAR, "" },
    { "Exhumed Saint", "Exhumed Saint", "undead_saint", 30, T::UNDEAD, CHURCH, REGULAR, "" },
    { "Confessor", "Confessor", "caster_confessor", 30, T::CASTER, CHURCH, REGULAR, "" },
    { "Glass Templar", "Glass Templar", "tank_glass_templar", 30, T::TANK, CHURCH, REGULAR, "" },
    { "Fleshmass", "Fleshmass", "beast_fleshmass", 46, T::BEAST, CHURCH, REGULAR, "" },
    { "False Moon", "False Moon", "boss_false_moon", 56, T::CASTER, CHURCH, BOSS, "Its own malformed shape, the one that broke out of your armor on the peak, with nothing of yours left on it: no armor, no sword, only the moon's red light. It has been latched onto the real moon since the night at the pond, and in the church, where the faithful prayed to it every night, it is at its strongest. It fights with every move you have ever made, and every two turns it takes back a piece of you. When it has them all, you are its vessel." },
};
const int COUNT = (int)(sizeof(BESTIARY) / sizeof(BESTIARY[0]));
static_assert(sizeof(BESTIARY) / sizeof(BESTIARY[0]) <= 128, "MET is two 64-bit words");

// Card icons from here on are the bestiary's: ICON0 + id * 2, plus one for
// the shadow of one not met yet. The medals start at 100.
constexpr int ICON0 = 1000;

const char* typeName(EnemyType t) {
    switch (t) {
        case T::MELEE:  return "Melee";
        case T::RANGED: return "Ranged";
        case T::TANK:   return "Tank";
        case T::CASTER: return "Caster";
        case T::BEAST:  return "Beast";
        default:        return "Undead";
    }
}
const char* kindName(const Beast& b) {
    return b.kind == BOSS ? "Boss" : b.kind == SHADE ? "Moon Shade" : typeName(b.type);
}
std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

// --- the journal ------------------------------------------------------------
// The bestiary as a book (the user, 2026-10-11, picked from three looks): the
// area's list on the left page, the chosen one large on the right with its
// entry, the areas as tabs along the top, and each area's book in its own
// colours, taken from its backdrop.
const char* const AREA_TAB[AREAS] = { "Dungeon", "Dark Dungeon", "Forest", "Lake", "Mountain", "Church" };

struct Theme { SDL_Color binding, edge, page, pageEdge, ink, faint, accent; };
const Theme THEMES[AREAS] = {
    // the Dungeon: torchlight on old stone
    { { 66, 44, 30, 255 }, { 116, 82, 54, 255 }, { 40, 34, 30, 255 }, { 78, 64, 52, 255 },
      { 236, 224, 204, 255 }, { 150, 136, 120, 255 }, { 214, 160, 96, 255 } },
    // the Dark Dungeon: purple stone and the green light on its runes
    { { 52, 24, 66, 255 }, { 98, 60, 122, 255 }, { 34, 26, 44, 255 }, { 70, 54, 88, 255 },
      { 230, 222, 240, 255 }, { 146, 134, 160, 255 }, { 150, 210, 130, 255 } },
    // the Wicked Forest: the canopy
    { { 30, 52, 32, 255 }, { 68, 98, 62, 255 }, { 30, 36, 30, 255 }, { 60, 74, 58, 255 },
      { 226, 232, 214, 255 }, { 140, 150, 132, 255 }, { 170, 206, 140, 255 } },
    // the Dark Lake: black water under fog
    { { 24, 38, 62, 255 }, { 62, 86, 122, 255 }, { 28, 34, 46, 255 }, { 56, 68, 88, 255 },
      { 222, 230, 240, 255 }, { 132, 144, 162, 255 }, { 130, 172, 222, 255 } },
    // the Mountain: slate and ice
    { { 52, 62, 80, 255 }, { 112, 130, 152, 255 }, { 38, 44, 54, 255 }, { 76, 88, 104, 255 },
      { 232, 238, 246, 255 }, { 150, 160, 176, 255 }, { 196, 214, 236, 255 } },
    // the Ruined Church: the moon's red
    { { 70, 20, 28, 255 }, { 132, 52, 60, 255 }, { 40, 28, 32, 255 }, { 84, 56, 62, 255 },
      { 238, 222, 222, 255 }, { 160, 134, 138, 255 }, { 226, 112, 112, 255 } },
};
const SDL_Color LIME{ 166, 226, 46, 255 }, GOLD{ 232, 196, 84, 255 }, MOON_RED{ 220, 80, 90, 255 };

SDL_Color lift(SDL_Color c, int by) {
    return SDL_Color{ (Uint8)std::min(255, c.r + by), (Uint8)std::min(255, c.g + by), (Uint8)std::min(255, c.b + by), 255 };
}
SDL_Color dark(SDL_Color c) { return SDL_Color{ (Uint8)(c.r / 2), (Uint8)(c.g / 2), (Uint8)(c.b / 2), 255 }; }

// What the journal shows, while it is open.
struct JournalView {
    int area = 0, sel = 0, areas = CHURCH;
    std::vector<int> ids;            // the area's entries, in the table's order
    std::vector<bool> met;
    std::vector<std::string> text;   // each one's words
    int metCount = 0;
    // An entry's second phase, shown as its second page once met: the
    // Moonstruck Shadow Knight is the Shadow Knight's (the user, 2026-10-11).
    std::vector<int> phase2;         // its table index, or -1
    std::vector<std::string> text2;
    int page = 0;                    // 0 the first, 1 the second
};

// Second phases live in the table so they can be met and saved like the
// rest, but are read as a page of the entry they belong to.
const char* phaseOf(const char* key) {
    return std::strcmp(key, "Moonstruck Shadow Knight") == 0 ? "Shadow Knight" : nullptr;
}
const JournalView* gView = nullptr;

struct JournalLayout { SDL_Rect book, left, right, pic; std::vector<SDL_Rect> tabs, rows; int pad = 12, capacity = 1; };

JournalLayout layoutBook(const std::vector<std::string>& tabs, int rows) {
    JournalLayout L;
    const int W = Platform::screenW(), H = Platform::screenH();
    const int cw = std::max(1, Platform::cellW()), ch = std::max(1, Platform::cellH());
    L.pad = std::max(10, W / 64);
    const int tabH = ch + 12;
    const int mx = std::max(16, W * 5 / 100);
    const int top = std::max(tabH + 20, H * 10 / 100);
    const int bottom = H - std::max(ch * 2 + 10, H * 7 / 100);
    L.book = SDL_Rect{ mx, top, W - 2 * mx, bottom - top };
    int tx = L.book.x + L.pad * 2;
    for (size_t a = 0; a < tabs.size(); ++a) {
        const int w = ((int)tabs[a].size() + 2) * cw + 8;
        L.tabs.push_back(SDL_Rect{ tx, top - tabH + 6, w, tabH });
        tx += w + 6;
    }
    const int half = L.book.w / 2;
    L.left  = SDL_Rect{ L.book.x + L.pad, L.book.y + L.pad, half - L.pad - L.pad / 2, L.book.h - 2 * L.pad };
    L.right = SDL_Rect{ L.book.x + half + L.pad / 2, L.book.y + L.pad, half - L.pad - L.pad / 2, L.book.h - 2 * L.pad };
    const int listTop = L.left.y + L.pad + Console::bigCellH() + ch + L.pad + 6;
    const int room = L.left.y + L.left.h - L.pad / 2 - listTop;
    L.capacity = std::max(1, room / (ch + 6));
    rows = std::min(rows, L.capacity);
    const int rowH = std::max(ch + 6, std::min(ch * 2 + 8, room / std::max(1, rows)));
    for (int i = 0; i < rows; ++i)
        L.rows.push_back(SDL_Rect{ L.left.x + L.pad / 2, listTop + i * rowH, L.left.w - L.pad, rowH - 4 });
    const int side = std::min(L.right.w - 2 * L.pad, L.right.h * 40 / 100);
    L.pic = SDL_Rect{ L.right.x + (L.right.w - side) / 2, L.right.y + L.pad, side, side };
    return L;
}

std::vector<std::string> areaTabs(int areas) { return std::vector<std::string>(AREA_TAB, AREA_TAB + areas); }
JournalLayout layoutJournal(int areas, int rows) { return layoutBook(areaTabs(areas), rows); }

// The book itself: the room behind it, the tabs (each in its own colours, the
// open one joined to the book), the binding, two pages and the gutter.
void drawBookFrame(SDL_Renderer* r, const JournalLayout& L, const std::vector<std::string>& tabs,
                   const std::vector<const Theme*>& tabTheme, int open, const Theme& t) {
    const int W = Platform::screenW(), H = Platform::screenH();
    const int cw = std::max(1, Platform::cellW());
    SDL_SetRenderDrawColor(r, 10, 9, 12, 255);
    SDL_Rect full{ 0, 0, W, H };
    SDL_RenderFillRect(r, &full);
    for (size_t a = 0; a < tabs.size() && a < L.tabs.size(); ++a) {
        const Theme& ta = *tabTheme[a];
        const bool on = (int)a == open;
        DrawUtil::fillRound(r, L.tabs[a], 6, on ? ta.binding : dark(ta.binding));
        DrawUtil::frameRound(r, L.tabs[a], 6, on ? ta.edge : dark(ta.edge));
        Console::drawTextPx(r, L.tabs[a].x + cw + 4, L.tabs[a].y + 4, tabs[a], on ? ta.ink : ta.faint, on);
    }
    DrawUtil::fillRound(r, L.book, 12, t.binding);
    DrawUtil::frameRound(r, L.book, 12, t.edge);
    for (const SDL_Rect& pg : { L.left, L.right }) {
        DrawUtil::fillRound(r, pg, 6, t.page);
        DrawUtil::frameRound(r, pg, 6, t.pageEdge);
    }
    const SDL_Color g = dark(t.binding);
    SDL_SetRenderDrawColor(r, g.r, g.g, g.b, 255);
    SDL_Rect gutter{ L.book.x + L.book.w / 2 - 3, L.book.y + 8, 6, L.book.h - 16 };
    SDL_RenderFillRect(r, &gutter);
}

// Words broken to `cols` characters: "\n" starts a new line, "\n\n" leaves an
// empty one between paragraphs.
std::vector<std::string> wrapTo(const std::string& text, int cols) {
    std::vector<std::string> out;
    for (size_t start = 0; ; ) {
        const size_t nl = text.find('\n', start);
        const std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string cur, word;
        std::istringstream in(para);
        while (in >> word) {
            if (!cur.empty() && (int)(cur.size() + 1 + word.size()) > cols) { out.push_back(cur); cur.clear(); }
            cur += (cur.empty() ? "" : " ") + word;
        }
        if (!cur.empty()) out.push_back(cur);
        else if (nl != std::string::npos) out.push_back("");
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

void drawJournal() {
    if (!gView) return;
    const JournalView& v = *gView;
    SDL_Renderer* r = Platform::renderer();
    const Theme& t = THEMES[v.area];
    const int W = Platform::screenW(), H = Platform::screenH();
    const int cw = std::max(1, Platform::cellW()), ch = std::max(1, Platform::cellH());
    const JournalLayout L = layoutJournal(v.areas, (int)v.ids.size());

    std::vector<const Theme*> tabTheme;
    for (int a = 0; a < v.areas; ++a) tabTheme.push_back(&THEMES[a]);
    drawBookFrame(r, L, areaTabs(v.areas), tabTheme, v.area, t);

    // The left page: the area, how much of it has been met, and its list.
    int x = L.left.x + L.pad, y = L.left.y + L.pad;
    Console::drawTextBigPx(r, x, y, AREA_NAME[v.area], t.ink, true);
    Console::drawTextPx(r, x, y + Console::bigCellH() + 4,
                        std::to_string(v.metCount) + " of " + std::to_string(v.ids.size()) + " met", t.faint, false);
    for (size_t i = 0; i < v.ids.size() && i < L.rows.size(); ++i) {
        const Beast& b = BESTIARY[v.ids[i]];
        const SDL_Rect& row = L.rows[i];
        const bool on = (int)i == v.sel;
        if (on) {
            DrawUtil::fillRound(r, row, 5, lift(t.page, 14));
            DrawUtil::frameRound(r, row, 5, LIME);
        }
        const int side = std::max(8, row.h - 4);
        EnemyArt::drawSheetFrame(b.sheet, b.frameW, 0, SDL_Rect{ row.x + 6, row.y + 2, side, side }, !v.met[i]);
        const int ty = row.y + (row.h - ch) / 2;
        Console::drawTextPx(r, row.x + side + 18, ty, v.met[i] ? b.name : "???", v.met[i] ? t.ink : t.faint, on);
        const std::string kind = v.met[i] ? kindName(b) : "not met";
        const SDL_Color kc = !v.met[i] ? t.faint : b.kind == BOSS ? GOLD : b.kind == SHADE ? MOON_RED : t.faint;
        Console::drawTextPx(r, row.x + row.w - ((int)kind.size() + 1) * cw, ty, kind, kc, false);
    }

    // The right page: the one chosen, large, on a little ground, then its words.
    if (v.sel >= 0 && v.sel < (int)v.ids.size()) {
        const size_t si = (size_t)v.sel;
        const bool second = v.page == 1 && v.phase2[si] >= 0;
        const Beast& b = BESTIARY[second ? v.phase2[si] : v.ids[si]];
        const bool met = v.met[si];
        const int gw = L.pic.w * 2 / 3, gh = std::max(6, L.pic.h / 18);
        const SDL_Color sh = dark(t.page);
        SDL_SetRenderDrawColor(r, sh.r, sh.g, sh.b, 255);
        for (int k = 0; k < 3; ++k) {   // a flat stepped oval, not a soft one
            SDL_Rect band{ L.pic.x + (L.pic.w - gw) / 2 + k * gw / 8, L.pic.y + L.pic.h - gh + k * gh / 3 - gh / 2,
                           gw - k * gw / 4, gh / 3 + 1 };
            SDL_RenderFillRect(r, &band);
        }
        // Scaled by height, so the wide frames (the true form, the False Moon,
        // the Fleshmass) are not drawn at half the size of the rest. They are
        // 36 tall where the standard frame is 32.
        const int fh = b.frameW == 30 ? 32 : 36;
        const int k = std::max(1, std::min((L.pic.h - gh / 2) / fh, (L.right.w - 2 * L.pad) / b.frameW));
        const SDL_Rect to{ L.pic.x + L.pic.w / 2 - b.frameW * k / 2, L.pic.y + L.pic.h - gh / 2 - fh * k,
                           b.frameW * k, fh * k };
        EnemyArt::drawSheetFrame(b.sheet, b.frameW, 0, to, !met);
        x = L.right.x + L.pad * 2;
        y = L.pic.y + L.pic.h + L.pad;
        Console::drawTextBigPx(r, x, y, met ? b.name : "???", met ? t.ink : t.faint, true);
        if (v.phase2[si] >= 0 && !v.text2[si].empty()) {
            const std::string pager = second ? "<  2 / 2  >" : "<  1 / 2  >";
            Console::drawTextPx(r, L.right.x + L.right.w - L.pad * 2 - (int)pager.size() * cw,
                                y + (Console::bigCellH() - ch) / 2, pager, t.accent, true);
        }
        y += Console::bigCellH() + 6;
        std::string tag = met ? upper(kindName(b)) : std::string("NOT MET");
        tag += std::string("    ") + upper(AREA_NAME[b.area]);
        Console::drawTextPx(r, x, y, tag, met && b.kind == BOSS ? GOLD : met && b.kind == SHADE ? MOON_RED : t.faint, true);
        y += ch + L.pad;
        const int cols = std::max(16, (L.right.w - L.pad * 4) / cw);
        const int lineH = ch * 4 / 3;
        const std::vector<std::string> lines = wrapTo(second ? v.text2[si] : v.text[si], cols);
        // the facts, after the empty line, in the area's accent
        bool facts = false;
        for (const std::string& ln : lines) {
            if (y + ch > L.right.y + L.right.h - L.pad / 2) break;
            if (ln.empty()) { facts = true; y += lineH / 2; continue; }
            Console::drawTextPx(r, x, y, ln, facts ? t.accent : t.ink, false);
            y += lineH;
        }
    }

    const bool paged = v.sel >= 0 && v.sel < (int)v.ids.size() && v.phase2[(size_t)v.sel] >= 0
                    && !v.text2[(size_t)v.sel].empty();
    const std::string hint = std::string("up and down to choose     left and right for another area")
                           + (paged ? "     Enter for its other phase" : "") + "     Esc to close";
    Console::drawTextPx(r, (W - (int)hint.size() * cw) / 2, L.book.y + L.book.h + 10, hint,
                        SDL_Color{ 120, 116, 132, 255 }, false);
}

// --- the Lore Book ----------------------------------------------------------
// The story so far, every fire sat by, and the cutscenes to watch again (the
// user, 2026-10-11), in the same book as the bestiary, bound in the night.
const char* const LORE_TAB[3] = { "The Story", "By the Fire", "Cutscenes" };
const Theme NIGHT = { { 30, 30, 58, 255 }, { 80, 82, 132, 255 }, { 28, 28, 40, 255 }, { 60, 60, 92, 255 },
                      { 232, 232, 244, 255 }, { 140, 140, 166, 255 }, { 220, 90, 96, 255 } };

struct LoreView {
    int tab = 0, sel = 0, top = 0;
    std::vector<const Game::LoreEntry*> rows;   // the open tab's entries
};
const LoreView* gLore = nullptr;

std::vector<std::string> loreTabs() { return std::vector<std::string>(LORE_TAB, LORE_TAB + 3); }

void drawLore() {
    if (!gLore) return;
    const LoreView& v = *gLore;
    SDL_Renderer* r = Platform::renderer();
    const Theme& t = NIGHT;
    const int W = Platform::screenW();
    const int cw = std::max(1, Platform::cellW()), ch = std::max(1, Platform::cellH());
    const JournalLayout L = layoutBook(loreTabs(), (int)v.rows.size());
    drawBookFrame(r, L, loreTabs(), { &NIGHT, &NIGHT, &NIGHT }, v.tab, t);

    // The left page: the part, how much of it has been reached, and its list.
    int x = L.left.x + L.pad, y = L.left.y + L.pad;
    Console::drawTextBigPx(r, x, y, LORE_TAB[v.tab], t.ink, true);
    int seen = 0;
    for (const Game::LoreEntry* e : v.rows) seen += e->seen ? 1 : 0;
    Console::drawTextPx(r, x, y + Console::bigCellH() + 4,
                        std::to_string(seen) + " of " + std::to_string(v.rows.size())
                        + (v.tab == 2 ? " to watch" : v.tab == 1 ? " heard" : " seen"), t.faint, false);
    for (size_t i = 0; i < L.rows.size(); ++i) {
        const size_t at = (size_t)v.top + i;
        if (at >= v.rows.size()) break;
        const Game::LoreEntry& e = *v.rows[at];
        const SDL_Rect& row = L.rows[i];
        const bool on = (int)at == v.sel;
        if (on) {
            DrawUtil::fillRound(r, row, 5, lift(t.page, 14));
            DrawUtil::frameRound(r, row, 5, LIME);
        }
        Console::drawTextPx(r, row.x + 14, row.y + (row.h - ch) / 2, e.seen ? e.title : "???",
                            e.seen ? t.ink : t.faint, on);
    }
    // More above or below the part of the list shown.
    if (v.top > 0)
        Console::drawTextPx(r, L.left.x + L.left.w - L.pad - cw, L.rows.front().y - ch, "^", t.faint, true);
    if (v.top + (int)L.rows.size() < (int)v.rows.size())
        Console::drawTextPx(r, L.left.x + L.left.w - L.pad - cw, L.rows.back().y + L.rows.back().h, "v", t.faint, true);

    // The right page: the chosen one's words, or what it shows.
    if (v.sel >= 0 && v.sel < (int)v.rows.size()) {
        const Game::LoreEntry& e = *v.rows[(size_t)v.sel];
        x = L.right.x + L.pad * 2;
        y = L.right.y + L.pad * 2;
        Console::drawTextBigPx(r, x, y, e.seen ? e.title : "???", e.seen ? t.ink : t.faint, true);
        y += Console::bigCellH() + L.pad;
        std::string text;
        if (!e.seen) text = v.tab == 2 ? "Not seen yet." : v.tab == 1 ? "Not heard yet." : "Not reached yet.";
        else for (const std::string& l : e.lines) text += (text.empty() ? "" : "\n\n") + l;
        const int bottom = L.right.y + L.right.h - L.pad;
        // Too long for the page at the grid's size, it goes down to the card face's.
        int cols = std::max(16, (L.right.w - L.pad * 4) / cw);
        int lineH = ch * 4 / 3;
        std::vector<std::string> lines = wrapTo(text, cols);
        bool small = false;
        if (y + (int)lines.size() * lineH > bottom - (e.cutscene >= 0 ? ch * 2 : 0)) {
            small = true;
            const int scw = std::max(1, Console::smallCellW()), sch = std::max(1, Console::smallCellH());
            cols = std::max(16, (L.right.w - L.pad * 4) / scw);
            lineH = sch * 5 / 4;
            lines = wrapTo(text, cols);
        }
        for (const std::string& ln : lines) {
            if (y + lineH > bottom) break;
            if (ln.empty()) { y += lineH / 2; continue; }
            if (small) Console::drawTextSmallPx(r, x, y, ln, t.ink);
            else       Console::drawTextPx(r, x, y, ln, t.ink, false);
            y += lineH;
        }
        if (e.cutscene >= 0 && e.seen)
            Console::drawTextPx(r, x, std::min(bottom - ch, y + L.pad), "Enter to watch it again", t.accent, true);
    }
    const std::string hint = "up and down to choose     left and right for another part     Esc to close";
    Console::drawTextPx(r, (W - (int)hint.size() * cw) / 2, L.book.y + L.book.h + 10, hint,
                        SDL_Color{ 120, 116, 132, 255 }, false);
}

} // namespace

bool Game::churchFound() const {
    for (int i = 0; i < COUNT; ++i)
        if (BESTIARY[i].area == CHURCH && hasMet(i)) return true;
    return Achievements::falseMoonBeaten();
}

int Game::bestiaryIndexOf(const std::string& enemyName) {
    int best = -1;
    size_t bestLen = 0;
    for (int i = 0; i < COUNT; ++i) {
        const size_t len = std::strlen(BESTIARY[i].key);
        if (len > bestLen && enemyName.find(BESTIARY[i].key) != std::string::npos) { best = i; bestLen = len; }
    }
    return best;
}

bool Game::hasMet(int id) const {
    return id >= 0 && id < COUNT && ((metMask[id / 64] >> (id % 64)) & 1ULL);
}

void Game::markMet(const std::string& enemyName) {
    const int id = bestiaryIndexOf(enemyName);
    if (id < 0 || hasMet(id)) return;
    metMask[id / 64] |= 1ULL << (id % 64);
    saveProgress();
}

// A save from before the bestiary has been places it never saw: a boss beaten
// enters its area's road, and each Moon Shade seen enters itself. Only adds.
void Game::seedMetFromProgress() {
    namespace A = Achievements;
    const bool through[AREAS] = { A::has(A::COLOSSUS), A::has(A::WITCH), A::has(A::THUNDER),
                                  A::has(A::HYDRA), A::has(A::DRAGON), A::falseMoonBeaten() };
    for (int i = 0; i < COUNT; ++i) {
        const Beast& b = BESTIARY[i];
        bool met = b.kind == SHADE ? ((A::formsSeen() >> b.area) & 1) != 0 : through[b.area];
        // The peak's two come after the dragon: met by finishing the road.
        if (!std::strcmp(b.key, "Shadow Knight"))            met = A::has(A::CLEAR) || A::has(A::TRUE_FORM);
        if (!std::strcmp(b.key, "Moonstruck Shadow Knight")) met = A::has(A::TRUE_FORM);
        if (met) metMask[i / 64] |= 1ULL << (i % 64);
    }
}

void Game::drawCardIcon(SDL_Renderer* r, int icon, const SDL_Rect& dst) {
    if (icon < ICON0) { EnemyArt::drawItemIcon(r, icon, dst); return; }
    const int id = (icon - ICON0) / 2;
    if (id >= COUNT) return;
    EnemyArt::drawSheetFrame(BESTIARY[id].sheet, BESTIARY[id].frameW, 0, dst, (icon - ICON0) % 2 != 0);
}

// One enemy's words: its entry (View Enemy's line until its own is written),
// then what View Enemy would say of its kind.
std::string Game::bestiaryEntryText(int id) const {
    const Beast& b = BESTIARY[id];
    if (!hasMet(id)) return "You have not met it yet.";
    const std::string text = *b.entry ? std::string(b.entry) : flavorTextOf(b.key);
    const Enemy kind(b.key, 1, 1, 0, b.type);
    return text + "\n\nWeak to " + kind.getWeaknessLabel() + ". Resists " + kind.getResistanceLabel()
         + ". Its blows are " + typeWord(blowTypeOf(b.key, b.type)) + ".";
}

void Game::showBestiary() {
    JournalView v;
    // The church is a secret until it has been found.
    v.areas = churchFound() ? AREAS : CHURCH;
    int selFor[AREAS] = { 0, 0, 0, 0, 0, 0 };   // where you were in each area
    auto open = [&](int area) {
        v.area = area;
        v.ids.clear(); v.met.clear(); v.text.clear(); v.phase2.clear(); v.text2.clear();
        v.metCount = 0;
        v.page = 0;
        for (int i = 0; i < COUNT; ++i) {
            if (BESTIARY[i].area != area || phaseOf(BESTIARY[i].key)) continue;
            v.ids.push_back(i);
            v.met.push_back(hasMet(i));
            v.text.push_back(bestiaryEntryText(i));
            v.metCount += hasMet(i) ? 1 : 0;
            int second = -1;
            for (int j = 0; j < COUNT; ++j)
                if (phaseOf(BESTIARY[j].key) && std::strcmp(phaseOf(BESTIARY[j].key), BESTIARY[i].key) == 0) second = j;
            v.phase2.push_back(second);
            // Not there to turn to until it has been met: the second phase is a secret too.
            v.text2.push_back(second >= 0 && hasMet(second) ? bestiaryEntryText(second) : std::string());
        }
        v.sel = std::min(selFor[area], (int)v.ids.size() - 1);
    };
    open(0);
    gView = &v;
    Platform::setModalRenderer(&drawJournal);
    Platform::flushKeys();
    int lastMx = -1, lastMy = -1;
    Platform::mousePos(lastMx, lastMy);
    auto turn = [&](int d) {
        const int to = (v.area + d + v.areas) % v.areas;
        if (to == v.area) return;
        selFor[v.area] = v.sel;
        open(to);
        Audio::menuHover();
    };
    auto choose = [&](int i) {
        if (i < 0 || i >= (int)v.ids.size() || i == v.sel) return;
        v.sel = i;
        v.page = 0;
        Audio::menuHover();
    };
    auto flip = [&]() {
        const size_t si = (size_t)v.sel;
        if (si >= v.ids.size() || v.phase2[si] < 0 || v.text2[si].empty()) return;
        v.page = 1 - v.page;
        Audio::menuHover();
    };
    while (true) {
        const JournalLayout L = layoutJournal(v.areas, (int)v.ids.size());
        int mx, my;
        Platform::mousePos(mx, my);
        if ((mx != lastMx || my != lastMy) && !Platform::pointerHidden()) {
            lastMx = mx; lastMy = my;
            for (size_t i = 0; i < L.rows.size(); ++i) {
                const SDL_Rect& q = L.rows[i];
                if (mx >= q.x && mx < q.x + q.w && my >= q.y && my < q.y + q.h) choose((int)i);
            }
        }
        const Platform::KeyEvent k = Platform::pollKey();
        const int n = (int)v.ids.size();
        if (k.key == Platform::Key::UP)        choose((v.sel - 1 + n) % n);
        else if (k.key == Platform::Key::DOWN) choose((v.sel + 1) % n);
        else if (k.key == Platform::Key::LEFT || k.key == Platform::Key::TO_CARDS)    turn(-1);
        else if (k.key == Platform::Key::RIGHT || k.key == Platform::Key::TO_ACTIONS) turn(1);
        else if (k.key == Platform::Key::ENTER) flip();
        else if (k.key == Platform::Key::ESCAPE) break;
        const int wheel = Platform::takeWheel();
        if (wheel) choose(std::max(0, std::min(n - 1, v.sel - wheel)));
        int cx, cy;
        if (Platform::takeClick(cx, cy)) {
            for (int a = 0; a < v.areas; ++a) {
                const SDL_Rect& q = L.tabs[(size_t)a];
                if (cx >= q.x && cx < q.x + q.w && cy >= q.y && cy < q.y + q.h) turn(a - v.area);
            }
            for (size_t i = 0; i < L.rows.size(); ++i) {
                const SDL_Rect& q = L.rows[i];
                if (cx >= q.x && cx < q.x + q.w && cy >= q.y && cy < q.y + q.h) choose((int)i);
            }
            // A click on the picture side turns to the other phase.
            if (cx >= L.right.x && cx < L.right.x + L.right.w && cy >= L.right.y && cy < L.right.y + L.right.h) flip();
        }
        Platform::frame();
    }
    Platform::setModalRenderer(nullptr);
    gView = nullptr;
    Audio::menuSelect(true);
}

// The Gallery, off the title menu: the bestiary and the Lore Book.
void Game::showGallery() {
    int last = 0;   // back from a book, the highlight stays on it
    while (true) {
        const std::vector<CardBar::Action> acts{
            CardBar::Action{ "Bestiary",  "every enemy you have met", false },
            CardBar::Action{ "Lore Book", "the story so far, every fire, and the cutscenes", false },
            CardBar::Action{ "Back", false } };
        UIHelper::clearScreen();
        CardBar::setNextHighlight(last);
        const int c = CardBar::pick("Gallery", {}, acts, 0);
        if (c == 0)      showBestiary();
        else if (c == 1) showLoreBook();
        else return;
        last = c;
    }
}

void Game::showLoreBook() {
    const std::vector<LoreEntry> all = loreEntries();
    LoreView v;
    int selFor[3] = { 0, 0, 0 }, topFor[3] = { 0, 0, 0 };
    auto open = [&](int tab) {
        v.tab = tab;
        v.rows.clear();
        for (const LoreEntry& e : all)
            if (e.tab == tab) v.rows.push_back(&e);
        v.sel = std::min(selFor[tab], (int)v.rows.size() - 1);
        v.top = topFor[tab];
    };
    // The list scrolls to keep the chosen one in view.
    auto keepInView = [&]() {
        const JournalLayout L = layoutBook(loreTabs(), (int)v.rows.size());
        if (v.sel < v.top) v.top = v.sel;
        if (v.sel >= v.top + L.capacity) v.top = v.sel - L.capacity + 1;
        v.top = std::max(0, std::min(v.top, std::max(0, (int)v.rows.size() - L.capacity)));
    };
    auto turn = [&](int d) {
        selFor[v.tab] = v.sel; topFor[v.tab] = v.top;
        open((v.tab + d + 3) % 3);
        Audio::menuHover();
    };
    auto choose = [&](int i) {
        if (i < 0 || i >= (int)v.rows.size() || i == v.sel) return;
        v.sel = i;
        keepInView();
        Audio::menuHover();
    };
    open(0);
    gLore = &v;
    Platform::setModalRenderer(&drawLore);
    Platform::flushKeys();
    int lastMx = -1, lastMy = -1;
    Platform::mousePos(lastMx, lastMy);
    while (true) {
        const JournalLayout L = layoutBook(loreTabs(), (int)v.rows.size());
        int mx, my;
        Platform::mousePos(mx, my);
        if ((mx != lastMx || my != lastMy) && !Platform::pointerHidden()) {
            lastMx = mx; lastMy = my;
            for (size_t i = 0; i < L.rows.size(); ++i) {
                const SDL_Rect& q = L.rows[i];
                if (mx >= q.x && mx < q.x + q.w && my >= q.y && my < q.y + q.h) choose(v.top + (int)i);
            }
        }
        const Platform::KeyEvent k = Platform::pollKey();
        const int n = (int)v.rows.size();
        bool watch = false;
        if (k.key == Platform::Key::UP)        choose((v.sel - 1 + n) % n);
        else if (k.key == Platform::Key::DOWN) choose((v.sel + 1) % n);
        else if (k.key == Platform::Key::LEFT || k.key == Platform::Key::TO_CARDS)    turn(-1);
        else if (k.key == Platform::Key::RIGHT || k.key == Platform::Key::TO_ACTIONS) turn(1);
        else if (k.key == Platform::Key::ENTER) watch = true;
        else if (k.key == Platform::Key::ESCAPE) break;
        const int wheel = Platform::takeWheel();
        if (wheel) choose(std::max(0, std::min(n - 1, v.sel - wheel)));
        int cx, cy;
        if (Platform::takeClick(cx, cy)) {
            for (size_t a = 0; a < L.tabs.size(); ++a) {
                const SDL_Rect& q = L.tabs[a];
                if (cx >= q.x && cx < q.x + q.w && cy >= q.y && cy < q.y + q.h) turn((int)a - v.tab);
            }
            for (size_t i = 0; i < L.rows.size(); ++i) {
                const SDL_Rect& q = L.rows[i];
                if (cx >= q.x && cx < q.x + q.w && cy >= q.y && cy < q.y + q.h) choose(v.top + (int)i);
            }
            if (cx >= L.right.x && cx < L.right.x + L.right.w && cy >= L.right.y && cy < L.right.y + L.right.h)
                watch = true;
        }
        // A cutscene plays over the book, which comes back after it.
        if (watch && v.sel >= 0 && v.sel < n && v.rows[(size_t)v.sel]->seen && v.rows[(size_t)v.sel]->cutscene >= 0) {
            Platform::setModalRenderer(nullptr);
            replayCutscene(v.rows[(size_t)v.sel]->cutscene);
            UIHelper::clearScreen();
            Platform::setModalRenderer(&drawLore);
            Platform::flushKeys();
        }
        Platform::frame();
    }
    Platform::setModalRenderer(nullptr);
    gLore = nullptr;
    Audio::menuSelect(true);
}
