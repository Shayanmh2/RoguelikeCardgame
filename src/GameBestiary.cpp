// The Gallery: the bestiary, the Lore Book and the Card Ledger, off the title
// menu. Bestiary is a page per area, each enemy shown in the first frame of
// its idle (a shadow until you've met it), and picking one opens its entry.
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
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "GameShared.h"

namespace {

enum Area { DUNGEON, DARK_DUNGEON, FOREST, LAKE, MOUNTAIN, CHURCH, AREAS };
const char* const AREA_NAME[AREAS] = { "The Dungeon", "The Dark Dungeon", "The Wicked Forest",
                                       "The Dark Lake", "The Mountain", "The Ruined Church" };

enum Kind { REGULAR, BOSS, SHADE };

// Every enemy by area: the road, the boss, and that area's Moon Shade.
// IMPORTANT: an enemy's index here is its bit in progress.dat's MET record,
// so new ones go on the END or everyone's save shifts. Learned that one.
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
    { "Goblin", "Goblin", "melee_goblin", 30, T::MELEE, DUNGEON, REGULAR, "Goblins came down into the dungeon to scavenge the rubble the Stone Colossus leaves behind, and they liked the dark too much to leave. They take what they can carry and run from anything that fights back, which down here is most things." },
    { "Orc", "Orc", "tank_orc", 30, T::TANK, DUNGEON, REGULAR, "Orcs were brought down to haul stone back when the dungeon was a prison, and nobody ever took them back up. They work for whoever is strongest down here, and these days that is the bandits." },
    { "Wizard", "Wizard", "caster_wizard", 30, T::CASTER, DUNGEON, REGULAR, "He came down to study the old stone the dungeon was built on, years before it stood up as the Stone Colossus. He still has his notes, and he still thinks he was close to understanding it. The Sage studied the stone with him until the two of them fell out, and they have not spoken to each other since." },
    { "Skeleton", "Skeleton", "undead_skeleton", 30, T::UNDEAD, DUNGEON, REGULAR, "The bones of prisoners who died in their cells back when the dungeon was a prison. Something down here got them moving again, and now they attack anything that comes near their cells." },
    { "Spider", "Spider", "beast_spider", 30, T::BEAST, DUNGEON, REGULAR, "Cave spiders grow fat and pale down here, years away from any light. They spin webs across the halls nobody walks anymore and nest in old bones, which the prison left plenty of. The Moon Shade Weaver was one of them once." },
    { "Archer", "Archer", "ranged_archer", 30, T::RANGED, DUNGEON, REGULAR, "The bandits' lookout, posted in the old guard towers above the cells. It would rather put an arrow in you from across the room than come down and fight you up close." },
    { "Bandit", "Bandit", "melee_bandit", 30, T::MELEE, DUNGEON, REGULAR, "They robbed travelers on the road by the pond until you ran them off it, and then moved into the abandoned prison, where nobody comes looking. They still talk about the knight who ran them off, and they have no idea it is you. They know every blind corner down here, and keep a knife ready for each one." },
    { "Warden", "Warden", "tank_warden", 30, T::TANK, DUNGEON, REGULAR, "It kept these halls locked when the dungeon was a prison. The prison was abandoned years ago, but nobody told the Warden, and it still locks in anything that moves, the skeletons included." },
    { "Sage", "Sage", "caster_sage", 30, T::CASTER, DUNGEON, REGULAR, "He came down with the Wizard to study the stone. After they fell out, he stayed in the ruins on his own, and he throws fire at anyone who will not leave him alone." },
    { "Stone Colossus", "Stone Colossus", "boss_colossus", 30, T::TANK, DUNGEON, BOSS, "Long before anyone dug into the hill, the stone lay under it as one great block, too square to have gotten there on its own. The prison's builders cut their first cells into its back. There was a seam of yellow crystal in its face, and they tried to chip it out for gold. It never came loose, and it is the eye the Colossus sees through now. It is never in a hurry. It can stand still so long that moss grows over its shoulders, and when it is hurt, it settles back into the floor it came from." },
    { "Moon Shade Weaver", "Moon Shade Weaver", "moon_weaver", 30, T::UNDEAD, DUNGEON, SHADE, "Before the moon struck it, it was a cave spider that lived in the bones of a climber who died deep under the dungeon. The moon took the two together and wears them as one shape: a skeleton with a spider's legs, set on wrong. It was one of the first shapes the moon ever took, and it has followed you since the first gate, learning how you move." },

    { "Ghoul", "Ghoul", "undead_ghoul", 30, T::UNDEAD, DARK_DUNGEON, REGULAR, "It was one of the witch's guards, until it started eating out of her cauldrons. Hunger is all that is left of it now, and every bite it takes out of you heals it." },
    { "Basilisk", "Basilisk", "beast_basilisk", 30, T::BEAST, DARK_DUNGEON, REGULAR, "The witch keeps it for its stare. The statues along her halls are the people it has looked at, and she likes to walk among them." },
    { "Assassin", "Assassin", "ranged_assassin", 30, T::RANGED, DARK_DUNGEON, REGULAR, "The witch pays it to get rid of anyone who asks too many questions in her halls. The Enchanter is on its list, and now so are you. It waits in the dark until you have already committed to a move." },
    { "Knight", "Knight", "melee_knight", 30, T::MELEE, DARK_DUNGEON, REGULAR, "He came down into the Dark Dungeon to drive the witch out, and took her coin instead. He guards her halls now, and he has never looked back. The two of you were knighted on the same day, before the moon struck you. He does not know you now." },
    { "Sentinel", "Sentinel", "tank_sentinel", 30, T::TANK, DARK_DUNGEON, REGULAR, "The witch had it built to guard the room that held her altar. She moved the altar long ago, and the Sentinel still guards the empty room behind its door." },
    { "Enchanter", "Enchanter", "caster_enchanter", 30, T::CASTER, DARK_DUNGEON, REGULAR, "A young witch searching the Dark Dungeon for her older sister, a mage who came down here years ago and never came back up. She does not know that the moon wears her sister now, as the Moon Shade Beguiler. She would rather empty your hand than fight you for it. When she saw you, you reminded her of someone from her village, though she could not say who." },
    { "Wraith", "Wraith", "undead_wraith", 30, T::UNDEAD, DARK_DUNGEON, REGULAR, "Someone the witch wronged long ago. Nobody remembers who it was, not even the Wraith. Only the grudge is left, drifting through her halls." },
    { "Serpent", "Serpent", "beast_serpent", 30, T::BEAST, DARK_DUNGEON, REGULAR, "The witch feeds it whoever fails her, and it has grown huge on them. It waits in the dark where the torchlight ends." },
    { "Omneye", "Omneye", "ranged_omneye", 30, T::RANGED, DARK_DUNGEON, REGULAR, "A single vast eye the witch keeps chained up to watch her halls. Nothing happens down here that it does not see, and whatever it sees, the witch's Assassin hears about soon after." },
    { "Vile Witch", "Vile Witch", "boss_witch", 30, T::CASTER, DARK_DUNGEON, BOSS, "She was a village healer once, the best for miles around. Despite all her good deeds, her vanity proved stronger, and it led her to the opposite of healing: drawing the life out of others and keeping it for herself. She has lived on other people's years ever since, far longer than anyone should. The whispering in her halls is what is left of the people she took them from." },
    { "Moon Shade Beguiler", "Moon Shade Beguiler", "moon_beguiler", 30, T::CASTER, DARK_DUNGEON, SHADE, "Before the moon struck her, she was a mage, the sister the young Enchanter is still searching the dark dungeon for. She came down into the witch's halls years ago and never came back up. The moon wears her now and is learning her magic through her, and on some nights it sings with her voice. Lately the song has your name in it." },

    { "Raider", "Raider", "melee_raider", 30, T::MELEE, FOREST, REGULAR, "The forest tribe's raiders, who hunt the road through the wood. They strike from the treeline and are gone before the thunder fades, and they take the storm as a sign that the hunting is good." },
    { "Barbarian", "Barbarian", "tank_barbarian", 30, T::TANK, FOREST, REGULAR, "A warrior of the forest tribe, which follows the Thunder Beast's storm wherever it goes and believes the storm is what makes them strong. He has stood in it so long that his skin has gone hard as bark." },
    { "Mystic", "Mystic", "caster_mystic", 30, T::CASTER, FOREST, REGULAR, "The tribe's seer, who reads the future in the lightning. She saw you in it long before you came, a knight climbing the road to get himself back. She did not like what she saw, so she keeps her distance, and vanishes with every flash." },
    { "Banshee", "Banshee", "undead_banshee", 30, T::UNDEAD, FOREST, REGULAR, "She mourns the tribe's dead, and the storm gives her more of them every year. Her scream carries farther than the thunder, and cuts twice as deep." },
    { "Wolf", "Wolf", "beast_wolf", 30, T::BEAST, FOREST, REGULAR, "The forest's wolves used to hunt as one pack. Since the moon struck their leader, they have scattered, and each one hunts alone, which is why nobody ever sees more than one at a time." },
    { "Falcon", "Falcon", "ranged_falcon", 30, T::RANGED, FOREST, REGULAR, "A hunter and her trained falcon, moving as one. She hunts the forest's wolves, the same pack her partner was tracking when he went into the forest and did not come back." },
    { "Berserker", "Berserker", "melee_berserker", 30, T::MELEE, FOREST, REGULAR, "A tribesman who stood out in the storm too long. It drove the sense out of him, and he gets worse the longer a fight goes on." },
    { "Guardian", "Guardian", "tank_guardian", 30, T::TANK, FOREST, REGULAR, "An eagle-headed knight that has nested in the canopy longer than anyone can say. It guards the old shrine at the side of the road, the one with three hollows in its stone, though it no longer remembers why." },
    { "Vampire", "Vampire", "caster_vampire", 30, T::CASTER, FOREST, REGULAR, "She was in the forest long before the tribe came, and the tribe has learned to leave her alone. She hunts the travelers on the road instead. Elegant, patient, and never quite as far away as she seems." },
    { "Thunder Beast", "Thunder Beast", "boss_warlord", 30, T::MELEE, FOREST, BOSS, "It was a kobold once, one of the small diggers that live in burrows under the forest's roots. It came up into the worst storm the forest has ever had, and the lightning found it. The bolt did not kill it. The lightning stayed inside it instead, and it has not slept since. Every blow it lands stirs the lightning up, so each one hits harder than the last, and when the spark in its tail flares, the next blow is lightning." },
    { "Moon Shade Werewolf", "Moon Shade Werewolf", "moon_werewolf", 30, T::BEAST, FOREST, SHADE, "Before the moon struck it, it was a wolf, the leader of a pack in the Wicked Forest. It was one of the moon's early shapes, and it shows: the moon wears it standing up and walking like a man, which no wolf ever did, and up close the face and the fur are not quite right. What is left of its pack still hunts the forest, always one at a time." },

    { "Specter", "Specter", "undead_specter", 30, T::UNDEAD, LAKE, REGULAR, "One of the crew of the ship the Hydra sank. Most of them stayed at the bottom of the lake. This one drifts up at night, and you never quite get a clear look at it." },
    { "Cockatrice", "Cockatrice", "beast_cockatrice", 30, T::BEAST, LAKE, REGULAR, "Half bird, half serpent, hatched in the reeds along the shore the gorgon's stare turned to glass. Some of that stare got into it, and its bite turns flesh to stone." },
    { "Gladiator", "Gladiator", "melee_gladiator", 30, T::MELEE, LAKE, REGULAR, "He fought his way up from the lowlands, arena by arena, because in every one of them someone told him about you, a knight nobody had ever beaten. He came all this way to find you, and he does not know it is you he is fighting." },
    { "Bastion", "Bastion", "tank_bastion", 30, T::TANK, LAKE, REGULAR, "The captain of the ship the Hydra sank. He held the deck while his crew got off and went down with his ship, and he is still standing guard over it at the bottom of the lake." },
    { "Spellmaster", "Spellmaster", "caster_spellmaster", 30, T::CASTER, LAKE, REGULAR, "Once the Vile Witch's best student. The Spellmaster left her halls for the lake, where the stagnant shallows brew plague better than any cauldron, and now drinks it like water." },
    { "Warrior", "Warrior", "melee_warrior", 30, T::MELEE, LAKE, REGULAR, "The only one of the crew who came up alive when the Hydra sank their ship. He has been stranded on the lake ever since, and he does not trust anything that comes across the water." },
    { "Sorcerer", "Sorcerer", "caster_sorcerer", 30, T::CASTER, LAKE, REGULAR, "He came out to the lake to study the cold under its water, and never seemed to notice the shore freezing around him. Maybe he did that." },
    { "Fortress", "Fortress", "tank_fortress", 30, T::TANK, LAKE, REGULAR, "The lake's old keepers built it to hold the shore against the Hydra. The keepers are long gone, and the Fortress holds the shore against everyone. It is less a soldier than a wall that decided to start moving." },
    { "Wyvern", "Wyvern", "beast_wyvern", 30, T::RANGED, LAKE, REGULAR, "It nests in the reeds at the lake's edge, where the water is too shallow for the Hydra to reach it. It dives on anything that wades out past the reeds, and it has gone under after its prey so often that it is half-drowned, and twice as vicious for it." },
    { "Hydra", "Hydra", "boss_hydra", 30, T::BEAST, LAKE, BOSS, "It came into the lake as a serpent, back when the water was still clear, and it has lived at the bottom ever since. Its heads do not get along. They snap at each other over every meal, and only rise together when something is out on the water. Its venom has been seeping into the lake for so long that the water has gone black, which is how the Dark Lake got its name." },
    { "Moon Shade Gorgon", "Moon Shade Gorgon", "moon_gorgon", 30, T::BEAST, LAKE, SHADE, "Before the moon struck her, she was a gorgon who lived on the shore of the Dark Lake, and the black glass along that shore is said to be her work. She was one of the later shapes it took, and it wears her far better than the early ones: nothing about her looks wrong, except that she no longer blinks. Her stare still stiffens your joints, and when she falls, the stiffness goes out of you with her." },

    { "Deadeye", "Deadeye", "ranged_deadeye", 30, T::RANGED, MOUNTAIN, REGULAR, "The heretics' lookout, posted where the ledge is narrowest. With nothing to do up here but wait, it has perfected one shot." },
    { "Enforcer", "Enforcer", "melee_enforcer", 30, T::MELEE, MOUNTAIN, REGULAR, "When the heretics' templar went up the mountain to meet the moon, the Enforcer stayed behind to keep the heretics in line. It keeps discipline the way a hammer keeps discipline on stone." },
    { "Revenant", "Revenant", "undead_revenant", 30, T::UNDEAD, MOUNTAIN, REGULAR, "It drowned in the Dark Lake and rose from the lakebed still furious about how it got there, then climbed the mountain looking for someone to blame. It waits for your swing so it can give it back." },
    { "Manticore", "Manticore", "beast_manticore", 30, T::BEAST, MOUNTAIN, REGULAR, "It hunts the high crags between the heretics' camps, and the heretics leave it alone. It has a lion's hunger and a scorpion's patience." },
    { "Lich", "Lich", "undead_lich", 30, T::UNDEAD, MOUNTAIN, REGULAR, "The oldest thing on the mountain. It spent centuries trying to raise the dragon that died here and failed every time, until your soul lodged in the dragon's chest and did it in one night. It never fights alone for long." },
    { "Paladin", "Paladin", "tank_paladin", 30, T::TANK, MOUNTAIN, REGULAR, "A holy knight of the sun, sent to hunt the heretics who turned from the sun to the moon. You cast no shadow, and to him that marks you as the sun's enemy. Nothing you put on him stays for long. He kept a shrine to the sun on the mountain, with three hollows in its stone." },
    { "Archon", "Archon", "caster_archon", 30, T::CASTER, MOUNTAIN, REGULAR, "A servant of the sun, sent up the mountain before the Paladin to bring the heretics back. It listened to them instead, and fell far enough to land on this peak. The Paladin was sent to finish the job." },
    { "Unchosen", "The Unchosen", "melee_unchosen", 30, T::MELEE, MOUNTAIN, REGULAR, "One of the mountain's heretics. It begged the moon to strike it, but the moon chose you instead, and it has been trying to become you ever since. It swings the way you swing, and it was forging a sword like yours when you caught it." },
    { "Undead Dragon", "Undead Dragon", "boss_dragon", 30, T::RANGED, MOUNTAIN, BOSS, "In life it was the last dragon, and the mountain was its own. The people of the lowlands sent it cattle every spring to keep it up there. The old story says a hero finally climbed up to end it, and the two of them fought all night until both of them fell. The hero's sword and armor were never found. The dragon lay frozen in its cave for ages after that. It flies badly now, on wings that are mostly holes, and its fire came back cold, but it burns just the same." },
    { "Shadow Knight", "Shadow Knight", "boss_shadowknight", 30, T::UNDEAD, MOUNTAIN, BOSS, "It can only live where the moon is reflected, and a knight's polished plate holds a reflection as well as still water does. That is why it took your armor down into the pond. It climbed inside, walked back out of the water in it, and went straight up the mountain, as close to the moon as it could get. Nothing of it shows now but the red behind the visor. It fights the only way a reflection can, by doing back whatever is done in front of it." },
    { "Moonstruck Shadow Knight", "Shadow Knight", "moon_shadowknight", 56, T::UNDEAD, MOUNTAIN, BOSS, "Its long sleep is the moon's hold on it. Every time it drops off, the moon's light closes over it again and keeps it under until the next crimson night, and those come years apart. So when the last vigil goes out, it throws everything it has into staying awake. It is full of moonlight it has stolen to keep itself going. Break it open, and that light goes back to the moon it was taken from." },
    { "Moon Shade Templar", "Moon Shade Templar", "moon_templar", 30, T::TANK, MOUNTAIN, SHADE, "Before the moon struck him, he was the templar of the mountain's heretics, the one they followed when they turned from the sun to the moon. When the moon struck him, they took it as a sign they had chosen right. He was the last shape it took before you, and the one it wears best: it climbs after you like a man." },

    { "Sexton", "Sexton", "melee_sexton", 30, T::MELEE, CHURCH, REGULAR, "He dug the graves for the faithful the moon never chose, and when there was nobody left to bury, he kept digging. He is the one who dug the Exhumed Saint back up. There is a hole out back that fits you." },
    { "Mirror Nun", "Mirror Nun", "caster_mirror_nun", 30, T::CASTER, CHURCH, REGULAR, "Once the most beautiful woman in the country. When she gave herself to the moon's faith, she learned that the moon has no face of its own, so she carved hers out and offered it. It took yours instead, and she knows it. She carries a hand mirror, and prays to the moon in it." },
    { "Gargoyle", "Gargoyle", "beast_gargoyle", 30, T::BEAST, CHURCH, REGULAR, "Carved to watch the moon from the church roof. The faithful prayed beneath it for so long that the moon's light woke it. When they took the roof down beam by beam, they left its perch standing." },
    { "Bellringer", "Bellringer", "tank_bellringer", 30, T::TANK, CHURCH, REGULAR, "It rang the hours for the moon every night the church stood. The bell took its hearing first, and the moon never answered. It still does not know how to stop ringing." },
    { "Inquisitor", "Inquisitor", "ranged_inquisitor", 30, T::RANGED, CHURCH, REGULAR, "The faithful sent it after anyone who said the moon was false. For years it hunted the Paladin's order in the valley, the same way the Paladin hunts the heretics on the mountain. You are on its list, near the top." },
    { "Exhumed Saint", "Exhumed Saint", "undead_saint", 30, T::UNDEAD, CHURCH, REGULAR, "The faithful wanted a saint of their own, so the Sexton dug one out of the crypt and they stood it back up. It does not know it died, and it does not stay down the first time." },
    { "Confessor", "Confessor", "caster_confessor", 30, T::CASTER, CHURCH, REGULAR, "It heard every prayer the faithful made to the moon, and every wrong they owned up to. It still keeps count, and it reads your sins out of its book as if you were one of them." },
    { "Glass Templar", "Glass Templar", "tank_glass_templar", 30, T::TANK, CHURCH, REGULAR, "When the heretics' templar went up the mountain to meet the moon, he left his second-in-command to guard the church. When the windows came down, the Glass Templar took up the broken glass and wore it, and it is guarding the church still." },
    { "Fleshmass", "Fleshmass", "beast_fleshmass", 46, T::BEAST, CHURCH, REGULAR, "A body the faithful made for the moon to wear, so it would not need yours. It could not hold the moon, and it crumbled into this. Whatever it lashes, it holds." },
    { "False Moon", "False Moon", "boss_false_moon", 56, T::CASTER, CHURCH, BOSS, "It came from somewhere past the stars, long before anyone lived here to see it, and it came hungry. It went for the moon, the brightest thing in the night, and tried to eat its light. The light closed over it instead and held it, and has held it ever since. It can only reach out through the moon's reflection, in still water, glass or a mirror, and only while someone is looking. When it pushes hard enough, its red shows through, and the moon comes up crimson." },
};
const int COUNT = (int)(sizeof(BESTIARY) / sizeof(BESTIARY[0]));
static_assert(sizeof(BESTIARY) / sizeof(BESTIARY[0]) <= 128, "MET is two 64-bit words");

// Bestiary card icons: ICON0 + id * 2, +1 for the not-met-yet shadow.
// Medals start at 100.
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
// The bestiary as a book: the list on the left page, the picked enemy big on
// the right with its entry, areas as tabs along the top. Each area's book
// takes its colours from that area's backdrop.
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
    // A second phase shows as page 2 of its entry once you've met it (the
    // true form is page 2 of the Shadow Knight).
    std::vector<int> phase2;         // its table index, or -1
    std::vector<std::string> text2;
    std::vector<std::vector<std::string>> record;   // its record page's lines, once met
    int page = 0;                    // which of its pages is open
    int first = 0;                   // a long entry's first line on show
};

// An entry's pages: its words, its second phase's once that is met, and its
// record once you've met it. Enter or a click turns them.
int pagesOf(const JournalView& v, size_t si) {
    if (si >= v.ids.size() || !v.met[si]) return 1;
    return 2 + (v.phase2[si] >= 0 && !v.text2[si].empty() ? 1 : 0);
}
bool onRecord(const JournalView& v, size_t si) { return pagesOf(v, si) > 1 && v.page == pagesOf(v, si) - 1; }
bool onSecond(const JournalView& v, size_t si) {
    return v.page == 1 && v.phase2[si] >= 0 && !v.text2[si].empty() && !onRecord(v, si);
}

// Second phases are rows in the table so they save like everything else,
// they just read as a page of the entry they belong to.
const char* phaseOf(const char* key) {
    return std::strcmp(key, "Moonstruck Shadow Knight") == 0 ? "Shadow Knight" : nullptr;
}
const JournalView* gView = nullptr;

struct JournalLayout {
    SDL_Rect book, left, right, pic;
    std::vector<SDL_Rect> tabs, rows;
    int pad = 12, capacity = 1;
    SDL_Rect listTrack{ 0, 0, 0, 0 };   // the list's slider, when it has more than fit
};

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
    const bool more = rows > L.capacity;
    rows = std::min(rows, L.capacity);
    const int rowH = std::max(ch + 6, std::min(ch * 2 + 8, room / std::max(1, rows)));
    const int trackW = std::max(6, cw * 2 / 3);
    const int rowW = L.left.w - L.pad - (more ? trackW + L.pad / 2 : 0);
    for (int i = 0; i < rows; ++i)
        L.rows.push_back(SDL_Rect{ L.left.x + L.pad / 2, listTop + i * rowH, rowW, rowH - 4 });
    if (more) L.listTrack = SDL_Rect{ L.left.x + L.left.w - L.pad / 2 - trackW, listTop, trackW, rows * rowH - 4 };
    const int side = std::min(L.right.w - 2 * L.pad, L.right.h * 40 / 100);
    L.pic = SDL_Rect{ L.right.x + (L.right.w - side) / 2, L.right.y + L.pad, side, side };
    return L;
}

std::vector<std::string> areaTabs(int areas) { return std::vector<std::string>(AREA_TAB, AREA_TAB + areas); }
JournalLayout layoutJournal(int areas, int rows) { return layoutBook(areaTabs(areas), rows); }

// The book itself: background, tabs (the open one joined to the book), the
// binding, two pages and the gutter.
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

// Words in a box that scroll under a slider when they run past it. The Lore
// Book's pages and the Card Ledger's text use it; the bestiary has its own
// below because its facts line stays pinned at the bottom.
struct TextBlock {
    std::vector<std::string> lines;
    int x = 0, top = 0, bottom = 0, lineH = 1;
    int maxFirst = 0, whole = 0;   // the furthest line it can start on, and its height
    SDL_Rect track{ 0, 0, 0, 0 };
};

TextBlock layoutBlock(const std::string& text, int x, int top, int right, int bottom, int trackRight) {
    TextBlock b;
    const int cw = std::max(1, Platform::cellW()), ch = std::max(1, Platform::cellH());
    b.lineH = ch * 4 / 3;
    b.x = x; b.top = top; b.bottom = bottom;
    b.lines = wrapTo(text, std::max(12, (right - x) / cw));
    auto from = [&](size_t i) {   // the last line only needs its glyph
        int h = 0;
        for (size_t j = i; j < b.lines.size(); ++j) h += b.lines[j].empty() ? b.lineH / 2 : b.lineH;
        return h - b.lineH + ch;
    };
    b.whole = from(0);
    if (top + b.whole <= bottom) return b;
    while (b.maxFirst + 1 < (int)b.lines.size() && top + from((size_t)b.maxFirst) > bottom) ++b.maxFirst;
    const int trackW = std::max(6, cw * 2 / 3);
    b.track = SDL_Rect{ trackRight - trackW, top, trackW, bottom - top };
    return b;
}

// Draws it from line `first` with its slider, and says where the words ended.
int drawBlock(SDL_Renderer* r, const TextBlock& b, int first, SDL_Color ink, bool italic, SDL_Color groove,
              SDL_Color thumb) {
    const int ch = std::max(1, Platform::cellH());
    first = std::max(0, std::min(first, b.maxFirst));
    int y = b.top;
    for (size_t i = (size_t)first; i < b.lines.size(); ++i) {
        if (y + ch > b.bottom) break;
        if (b.lines[i].empty()) { y += b.lineH / 2; continue; }
        Console::drawTextPx(r, b.x, y, b.lines[i], ink, false, italic);
        y += b.lineH;
    }
    DrawUtil::drawScroll(r, b.track, first, b.maxFirst, b.bottom - b.top, b.whole, groove, thumb);
    return y;
}

// An entry's words on the right page. They go under the name with the facts
// after them, and when that runs past the page, the facts stay at the bottom
// and the words above them scroll with a slider in the margin. The picture
// keeps its size. The input loop lays this out too, for the slider.
struct EntryText {
    std::vector<std::string> lore, facts;
    int x = 0, top = 0, loreBottom = 0, factsTop = 0, lineH = 1;
    int maxFirst = 0;              // the furthest line the words can start on, 0 when they fit
    int whole = 0;                 // the words' whole height, for the thumb
    SDL_Rect track{ 0, 0, 0, 0 };
};

EntryText layoutEntry(const JournalLayout& L, const std::string& text) {
    EntryText e;
    const int cw = std::max(1, Platform::cellW()), ch = std::max(1, Platform::cellH());
    e.lineH = ch * 4 / 3;
    e.x = L.right.x + L.pad * 2;
    e.top = L.pic.y + L.pic.h + L.pad + Console::bigCellH() + 6 + ch + L.pad;   // under the name and tag
    const int bottom = L.right.y + L.right.h - L.pad / 2;
    const int cols = std::max(16, (L.right.w - L.pad * 4) / cw);
    const size_t cut = text.rfind("\n\n");
    e.lore = wrapTo(cut == std::string::npos ? text : text.substr(0, cut), cols);
    if (cut != std::string::npos) e.facts = wrapTo(text.substr(cut + 2), cols);
    // From line i to the end of the words; the last one only needs its glyph.
    auto from = [&](size_t i) {
        int h = 0;
        for (size_t j = i; j < e.lore.size(); ++j) h += e.lore[j].empty() ? e.lineH / 2 : e.lineH;
        return h - e.lineH + ch;
    };
    e.whole = from(0);
    e.loreBottom = bottom;
    const int factsH = e.facts.empty() ? 0 : e.lineH / 2 + (int)e.facts.size() * e.lineH;
    if (e.top + e.whole + factsH <= bottom) return e;   // it all fits
    e.factsTop = bottom - ch - ((int)e.facts.size() - 1) * e.lineH;
    if (!e.facts.empty()) e.loreBottom = e.factsTop - e.lineH / 2;
    while (e.maxFirst + 1 < (int)e.lore.size() && e.top + from((size_t)e.maxFirst) > e.loreBottom) ++e.maxFirst;
    const int trackW = std::max(6, cw * 2 / 3);
    e.track = SDL_Rect{ L.right.x + L.right.w - L.pad - trackW, e.top, trackW, e.loreBottom - e.top };
    return e;
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
    bool scrolls = false;
    if (v.sel >= 0 && v.sel < (int)v.ids.size()) {
        const size_t si = (size_t)v.sel;
        const bool second = onSecond(v, si), record = onRecord(v, si);
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
        // Scale by height, not width. The wide sheets (true form, False Moon,
        // Fleshmass) came out tiny next to everyone else otherwise.
        const int fh = b.frameW == 30 ? 32 : 36;
        const int k = std::max(1, std::min((L.pic.h - gh / 2) / fh, (L.right.w - 2 * L.pad) / b.frameW));
        const SDL_Rect to{ L.pic.x + L.pic.w / 2 - b.frameW * k / 2, L.pic.y + L.pic.h - gh / 2 - fh * k,
                           b.frameW * k, fh * k };
        EnemyArt::drawSheetFrame(b.sheet, b.frameW, 0, to, !met);
        x = L.right.x + L.pad * 2;
        y = L.pic.y + L.pic.h + L.pad;
        Console::drawTextBigPx(r, x, y, met ? b.name : "???", met ? t.ink : t.faint, true);
        if (pagesOf(v, si) > 1) {
            const std::string pager = "<  " + std::to_string(v.page + 1) + " / " + std::to_string(pagesOf(v, si)) + "  >";
            Console::drawTextPx(r, L.right.x + L.right.w - L.pad * 2 - (int)pager.size() * cw,
                                y + (Console::bigCellH() - ch) / 2, pager, t.accent, true);
        }
        y += Console::bigCellH() + 6;
        std::string tag = met ? upper(kindName(b)) : std::string("NOT MET");
        tag += std::string("    ") + upper(AREA_NAME[b.area]);
        Console::drawTextPx(r, x, y, tag, met && b.kind == BOSS ? GOLD : met && b.kind == SHADE ? MOON_RED : t.faint, true);
        if (record) {
            // Where the words would start, under the tag: a label and its count on each line.
            y += ch + L.pad;
            const int lineH = ch * 4 / 3, valueX = x + 16 * cw;
            for (const std::string& ln : v.record[si]) {
                if (ln.empty()) { y += lineH / 2; continue; }
                const size_t bar = ln.find('|');
                if (ln[0] == '#') Console::drawTextPx(r, x, y, ln.substr(1), t.accent, true);
                else if (bar == std::string::npos) Console::drawTextPx(r, x, y, ln, t.ink, false);
                else {
                    Console::drawTextPx(r, x, y, ln.substr(0, bar), t.faint, false);
                    Console::drawTextPx(r, valueX, y, ln.substr(bar + 1), t.ink, false);
                }
                y += lineH;
            }
        }
        // The words, then the facts in the area's accent.
        const EntryText e = layoutEntry(L, record ? std::string() : second ? v.text2[si] : v.text[si]);
        const int first = std::min(v.first, e.maxFirst);
        int ly = e.top;
        for (size_t i = (size_t)first; i < e.lore.size(); ++i) {
            if (ly + ch > e.loreBottom) break;
            if (e.lore[i].empty()) { ly += e.lineH / 2; continue; }
            Console::drawTextPx(r, e.x, ly, e.lore[i], t.ink, false);
            ly += e.lineH;
        }
        int fy = e.maxFirst > 0 ? e.factsTop : ly + e.lineH / 2;
        for (const std::string& ln : e.facts) {
            Console::drawTextPx(r, e.x, fy, ln, t.accent, false);
            fy += e.lineH;
        }
        DrawUtil::drawScroll(r, e.track, first, e.maxFirst, e.loreBottom - e.top, e.whole, dark(t.page), t.faint);
        scrolls = e.maxFirst > 0;
    }

    const bool paged = v.sel >= 0 && v.sel < (int)v.ids.size() && pagesOf(v, (size_t)v.sel) > 1;
    const std::string hint = std::string("up and down to choose     left and right for another area")
                           + (paged ? "     Enter to turn the page" : "")
                           + (scrolls ? "     PgUp and PgDn to scroll" : "") + "     Esc to close";
    Console::drawTextPx(r, (W - (int)hint.size() * cw) / 2, L.book.y + L.book.h + 10, hint,
                        SDL_Color{ 120, 116, 132, 255 }, false);
}

// --- the Lore Book ----------------------------------------------------------
// The story so far, every fire you've sat by, the camp letters, and the
// cutscenes. Same book as the bestiary, just in night colours.
const int LORE_TABS = 4;
const char* const LORE_TAB[LORE_TABS] = { "The Story", "By the Fire", "Letters", "Cutscenes" };
const Theme NIGHT = { { 30, 30, 58, 255 }, { 80, 82, 132, 255 }, { 28, 28, 40, 255 }, { 60, 60, 92, 255 },
                      { 232, 232, 244, 255 }, { 140, 140, 166, 255 }, { 220, 90, 96, 255 } };

struct LoreView {
    int tab = 0, sel = 0, top = 0;
    int first = 0;                              // the open page's first line on show
    std::vector<const Game::LoreEntry*> rows;   // the open tab's entries
};
const LoreView* gLore = nullptr;

std::vector<std::string> loreTabs() { return std::vector<std::string>(LORE_TAB, LORE_TAB + LORE_TABS); }

// The open page's words, where drawLore puts them. A cutscene's page keeps
// room at the bottom for "Enter to watch it again".
TextBlock lorePage(const JournalLayout& L, const LoreView& v) {
    if (v.sel < 0 || v.sel >= (int)v.rows.size()) return TextBlock{};
    const Game::LoreEntry& e = *v.rows[(size_t)v.sel];
    const int ch = std::max(1, Platform::cellH());
    std::string text;
    if (!e.seen) text = v.tab == 3 ? "Not seen yet." : v.tab == 2 ? "Not found yet."
                      : v.tab == 1 ? "Not heard yet." : "Not reached yet.";
    else for (const std::string& l : e.lines) text += (text.empty() ? "" : "\n\n") + l;
    const int top = L.right.y + L.pad * 2 + Console::bigCellH() + L.pad;
    const int bottom = L.right.y + L.right.h - L.pad - (e.cutscene >= 0 && e.seen ? ch * 2 : 0);
    return layoutBlock(text, L.right.x + L.pad * 2, top, L.right.x + L.right.w - L.pad * 2, bottom,
                       L.right.x + L.right.w - L.pad);
}

void drawLore() {
    if (!gLore) return;
    const LoreView& v = *gLore;
    SDL_Renderer* r = Platform::renderer();
    const Theme& t = NIGHT;
    const int W = Platform::screenW();
    const int cw = std::max(1, Platform::cellW()), ch = std::max(1, Platform::cellH());
    const JournalLayout L = layoutBook(loreTabs(), (int)v.rows.size());
    drawBookFrame(r, L, loreTabs(), { &NIGHT, &NIGHT, &NIGHT, &NIGHT }, v.tab, t);

    // The left page: the part, how much of it has been reached, and its list.
    int x = L.left.x + L.pad, y = L.left.y + L.pad;
    Console::drawTextBigPx(r, x, y, LORE_TAB[v.tab], t.ink, true);
    int seen = 0;
    for (const Game::LoreEntry* e : v.rows) seen += e->seen ? 1 : 0;
    Console::drawTextPx(r, x, y + Console::bigCellH() + 4,
                        std::to_string(seen) + " of " + std::to_string(v.rows.size())
                        + (v.tab == 3 ? " to watch" : v.tab == 2 ? " read" : v.tab == 1 ? " heard" : " seen"),
                        t.faint, false);
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
    // More than fit: a slider down the right of the list.
    DrawUtil::drawScroll(r, L.listTrack, v.top, std::max(0, (int)v.rows.size() - L.capacity), L.capacity,
                         (int)v.rows.size(), dark(t.page), t.faint);

    // The right page: the chosen one's words, or what it shows. A long one
    // scrolls under a slider instead of dropping to the small text.
    bool scrolls = false;
    if (v.sel >= 0 && v.sel < (int)v.rows.size()) {
        const Game::LoreEntry& e = *v.rows[(size_t)v.sel];
        x = L.right.x + L.pad * 2;
        y = L.right.y + L.pad * 2;
        Console::drawTextBigPx(r, x, y, e.seen ? e.title : "???", e.seen ? t.ink : t.faint, true);
        const TextBlock b = lorePage(L, v);
        y = drawBlock(r, b, v.first, t.ink, v.tab == 2 && e.seen, dark(t.page), t.faint);   // letters lean
        scrolls = b.maxFirst > 0;
        const int bottom = L.right.y + L.right.h - L.pad;
        if (e.cutscene >= 0 && e.seen)
            Console::drawTextPx(r, x, std::min(bottom - ch, y + L.pad), "Enter to watch it again", t.accent, true);
    }
    const std::string hint = std::string("up and down to choose     left and right for another part")
                           + (scrolls ? "     PgUp and PgDn to scroll" : "") + "     Esc to close";
    Console::drawTextPx(r, (W - (int)hint.size() * cw) / 2, L.book.y + L.book.h + 10, hint,
                        SDL_Color{ 120, 116, 132, 255 }, false);
}

// --- the Card Ledger ---------------------------------------------------------
// Every card in the game, a tab per rarity. Ones you've held in any run show
// their name and face, the rest stay ??? until you get one.
const int LEDGER_TABS = 5;
const char* const LEDGER_TAB[LEDGER_TABS] = { "Starter", "Uncommon", "Rare", "Super Rare", "Legendary" };

struct CardLedgerView {
    int tab = 0, sel = 0, top = 0;
    int first = 0;                        // the open card's first line of text on show
    std::vector<Card> rows;               // the open rarity's cards, by name
    std::vector<bool> held;
    std::vector<CardBar::Card> faces;     // each at its printed numbers
    int heldAll = 0, total = 0;           // across every rarity
};
const CardLedgerView* gLedger = nullptr;

std::vector<std::string> ledgerTabs() { return std::vector<std::string>(LEDGER_TAB, LEDGER_TAB + LEDGER_TABS); }

// The open card's full text, beside its face, where drawCardLedger puts it.
TextBlock ledgerPage(const JournalLayout& L, const CardLedgerView& v) {
    if (v.sel < 0 || v.sel >= (int)v.rows.size() || !v.held[(size_t)v.sel]) return TextBlock{};
    const int ch = std::max(1, Platform::cellH());
    const int cardW = std::min(L.right.w * 2 / 5, (L.right.h - L.pad * 4) * 100 / 136);
    const int tx = L.right.x + L.pad * 2 + cardW + L.pad * 2;
    return layoutBlock(v.rows[(size_t)v.sel].getDescription(), tx, L.right.y + L.pad * 2 + ch * 2,
                       L.right.x + L.right.w - L.pad * 2, L.right.y + L.right.h - L.pad,
                       L.right.x + L.right.w - L.pad);
}

void drawCardLedger() {
    if (!gLedger) return;
    const CardLedgerView& v = *gLedger;
    SDL_Renderer* r = Platform::renderer();
    const Theme& t = NIGHT;
    const int W = Platform::screenW();
    const int cw = std::max(1, Platform::cellW()), ch = std::max(1, Platform::cellH());
    const JournalLayout L = layoutBook(ledgerTabs(), (int)v.rows.size());
    drawBookFrame(r, L, ledgerTabs(), { &NIGHT, &NIGHT, &NIGHT, &NIGHT, &NIGHT }, v.tab, t);

    // The left page: the rarity, how many of it you have held, and its list.
    int x = L.left.x + L.pad, y = L.left.y + L.pad;
    Console::drawTextBigPx(r, x, y, LEDGER_TAB[v.tab], t.ink, true);
    int held = 0;
    for (const bool h : v.held) held += h ? 1 : 0;
    Console::drawTextPx(r, x, y + Console::bigCellH() + 4,
                        std::to_string(held) + " of " + std::to_string(v.rows.size()) + " held     "
                        + std::to_string(v.heldAll) + " of " + std::to_string(v.total) + " in all",
                        t.faint, false);
    for (size_t i = 0; i < L.rows.size(); ++i) {
        const size_t at = (size_t)v.top + i;
        if (at >= v.rows.size()) break;
        const SDL_Rect& row = L.rows[i];
        const bool on = (int)at == v.sel;
        if (on) {
            DrawUtil::fillRound(r, row, 5, lift(t.page, 14));
            DrawUtil::frameRound(r, row, 5, LIME);
        }
        Console::drawTextPx(r, row.x + 14, row.y + (row.h - ch) / 2, v.held[at] ? v.rows[at].getName() : "???",
                            v.held[at] ? v.faces[at].nameColor : t.faint, on);
    }
    DrawUtil::drawScroll(r, L.listTrack, v.top, std::max(0, (int)v.rows.size() - L.capacity), L.capacity,
                         (int)v.rows.size(), dark(t.page), t.faint);

    // The right page: the card as it is printed, and beside it its full text.
    bool scrolls = false;
    if (v.sel >= 0 && v.sel < (int)v.rows.size()) {
        x = L.right.x + L.pad * 2;
        y = L.right.y + L.pad * 2;
        if (!v.held[(size_t)v.sel]) {
            Console::drawTextBigPx(r, x, y, "???", t.faint, true);
            Console::drawTextPx(r, x, y + Console::bigCellH() + L.pad, "Not held yet.", t.ink, false);
        } else {
            const Card& c = v.rows[(size_t)v.sel];
            const CardBar::Card& face = v.faces[(size_t)v.sel];
            const int cardW = std::min(L.right.w * 2 / 5, (L.right.h - L.pad * 4) * 100 / 136);
            const SDL_Rect at{ x, y, cardW, cardW * 136 / 100 };
            CardBar::drawCardAt(r, face, at);
            const int tx = at.x + at.w + L.pad * 2;
            Console::drawTextPx(r, tx, y, face.typeLabel + (face.elemTag.empty() ? "" : "  " + face.elemTag),
                                t.faint, true);
            const TextBlock b = ledgerPage(L, v);
            drawBlock(r, b, v.first, t.ink, false, dark(t.page), t.faint);
            scrolls = b.maxFirst > 0;
            (void)c;
        }
    }
    const std::string hint = std::string("up and down to choose     left and right for another rarity")
                           + (scrolls ? "     PgUp and PgDn to scroll" : "") + "     Esc to close";
    Console::drawTextPx(r, (W - (int)hint.size() * cw) / 2, L.book.y + L.book.h + 10, hint,
                        SDL_Color{ 120, 116, 132, 255 }, false);
}

// --- the Chronicle -------------------------------------------------------------
// Every run you've finished, newest first, and the best on each road. One
// record a block in chronicle.dat, read back here.
struct RunRecord {
    std::string when, road, by, weapon, armor;
    int result = 0, at = 0, length = 0, area = 0;
    std::vector<std::string> relics, keepsakes;
    struct Held { std::string name; int rarity = 0, count = 1; };
    std::vector<Held> deck;
    int won = 0, bosses = 0, shades = 0, dealt = 0, taken = 0, biggest = 0, cards = 0, turns = 0;
    int rests = 0, forges = 0, sits = 0, seconds = 0, mostCount = 0;
    std::string most;
};

std::vector<std::string> splitBar(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream in(s);
    for (std::string p; std::getline(in, p, '|'); ) if (!p.empty()) out.push_back(p);
    return out;
}

// Newest first.
std::vector<RunRecord> readChronicle(const std::string& path) {
    std::vector<RunRecord> all;
    std::ifstream in(path);
    RunRecord rec;
    for (std::string line; std::getline(in, line); ) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t sp = line.find(' ');
        const std::string tag = line.substr(0, sp), rest = sp == std::string::npos ? std::string() : line.substr(sp + 1);
        std::istringstream v(rest);
        if (tag == "RUN") rec = RunRecord{};
        else if (tag == "WHEN") rec.when = rest;
        else if (tag == "ROAD") rec.road = rest;
        else if (tag == "RESULT") v >> rec.result;
        else if (tag == "FAR") v >> rec.at >> rec.length >> rec.area;
        else if (tag == "BY") rec.by = rest;
        else if (tag == "WEAPON") rec.weapon = rest;
        else if (tag == "ARMOR") rec.armor = rest;
        else if (tag == "RELICS") rec.relics = splitBar(rest);
        else if (tag == "KEEPS") rec.keepsakes = splitBar(rest);
        else if (tag == "DECK") {
            for (const std::string& p : splitBar(rest)) {
                const size_t a = p.rfind('='), b = a == std::string::npos ? a : p.rfind('=', a - 1);
                if (b == std::string::npos) continue;
                rec.deck.push_back(RunRecord::Held{ p.substr(0, b), std::atoi(p.c_str() + b + 1), std::atoi(p.c_str() + a + 1) });
            }
        }
        else if (tag == "NUMBERS") v >> rec.won >> rec.bosses >> rec.shades >> rec.dealt >> rec.taken >> rec.biggest
                                      >> rec.cards >> rec.turns >> rec.rests >> rec.forges >> rec.sits >> rec.seconds;
        else if (tag == "MOST") { v >> rec.mostCount; std::getline(v, rec.most); if (!rec.most.empty() && rec.most[0] == ' ') rec.most.erase(0, 1); }
        else if (tag == "END") all.push_back(rec);
    }
    std::reverse(all.begin(), all.end());
    return all;
}

const char* resultWord(const RunRecord& r) {
    return r.result == 3 ? "the long night ends" : r.result == 2 ? "cleared the road"
         : r.result == 4 ? "ended early" : "died";
}
// Better: the true ending, then a clear, then how far it got.
int runScore(const RunRecord& r) { return (r.result == 3 ? 2 : r.result == 2 ? 1 : 0) * 1000 + r.at; }

const int CHRON_TABS = 2;
const char* const CHRON_TAB[CHRON_TABS] = { "Recent", "Best runs" };
struct ChronView {
    int tab = 0, sel = 0, top = 0;
    int scroll = 0;                // the open run's page, in pixels
    std::vector<RunRecord> all;    // newest first
    std::vector<int> rows;         // the open tab's, as places in `all`
};
const ChronView* gChron = nullptr;

std::vector<std::string> chronTabs() { return std::vector<std::string>(CHRON_TAB, CHRON_TAB + CHRON_TABS); }

std::string withCommas(int n) {
    std::string s = std::to_string(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return s;
}
std::string timeWords(int seconds) {
    const int h = seconds / 3600, m = (seconds % 3600) / 60;
    if (h > 0) return std::to_string(h) + (h == 1 ? " hour " : " hours ") + std::to_string(m) + (m == 1 ? " minute" : " minutes");
    return std::to_string(std::max(1, m)) + (m == 1 ? " minute" : " minutes");
}
std::string joinComma(const std::vector<std::string>& v, const char* none) {
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : ", ") + x;
    return s.empty() ? std::string(none) : s;
}

SDL_Rect chronBox(const JournalLayout& L) {
    return SDL_Rect{ L.right.x + L.pad * 2, L.right.y + L.pad, L.right.w - L.pad * 4, L.right.h - L.pad * 2 };
}

// The open run's page: drawn (draw) or only measured. Says how tall it is.
int chronPage(SDL_Renderer* r, const JournalLayout& L, const RunRecord& rec, int scroll, bool draw, const Theme& t) {
    const int cw = std::max(1, Platform::cellW()), ch = std::max(1, Platform::cellH());
    const SDL_Rect box = chronBox(L);
    const int lineH = ch * 4 / 3, valueX = box.x + 13 * cw, cols = std::max(12, (box.x + box.w - valueX) / cw);
    int y = box.y + L.pad - scroll;
    auto text = [&](int x, const std::string& s, SDL_Color c, bool bold) { if (draw) Console::drawTextPx(r, x, y, s, c, bold); };
    // A label and its value, the value wrapping under itself.
    auto row = [&](const std::string& label, const std::string& value) {
        text(box.x, label, t.faint, false);
        const std::vector<std::string> lines = wrapTo(value, cols);
        for (size_t i = 0; i < lines.size(); ++i) {
            if (draw) Console::drawTextPx(r, valueX, y, lines[i], t.ink, false);
            y += lineH;
        }
        if (lines.empty()) y += lineH;
    };
    if (draw) Console::drawTextBigPx(r, box.x, y, rec.road + " road", t.ink, true);
    y += Console::bigCellH() + 4;
    text(box.x, rec.when + "     " + timeWords(rec.seconds), t.faint, false);
    y += lineH + L.pad / 2;
    std::string result = rec.result == 1 && !rec.by.empty() ? "died to the " + rec.by : std::string(resultWord(rec));
    result[0] = (char)std::toupper((unsigned char)result[0]);
    row("Result", result);
    static const char* const AREA_WORD[AREAS] = { "the Dungeon", "the Dark Dungeon", "the Wicked Forest", "the Dark Lake",
                                                  "the Mountain", "the Ruined Church" };
    row("How far", rec.area == CHURCH ? std::string("In the ruined church")
                   : "Encounter " + std::to_string(rec.at) + " of " + std::to_string(rec.length) + ", "
                     + AREA_WORD[std::max(0, std::min(AREAS - 1, rec.area))]);
    row("Gear", rec.weapon + ", " + rec.armor);
    row("Relics", joinComma(rec.relics, "none"));
    row("Keepsakes", joinComma(rec.keepsakes, "none"));
    y += lineH / 2;

    // The deck, as a grid of tiles in their rarity's colour.
    int deckSize = 0;
    for (const RunRecord::Held& h : rec.deck) deckSize += h.count;
    text(box.x, "THE DECK, " + std::to_string(deckSize) + " CARDS", t.accent, true);
    y += lineH;
    static const SDL_Color RARITY[5] = { { 200, 200, 208, 255 }, { 135, 215, 135, 255 }, { 175, 215, 255, 255 },
                                         { 255, 175, 215, 255 }, { 255, 215, 0, 255 } };
    const int perRow = box.w >= 40 * cw ? 3 : 2, gap = 8;
    const int tileW = (box.w - gap * (perRow - 1)) / perRow, tileH = ch + 12;
    for (size_t i = 0; i < rec.deck.size(); ++i) {
        const int col = (int)i % perRow;
        const SDL_Rect tile{ box.x + col * (tileW + gap), y, tileW, tileH };
        if (draw) {
            DrawUtil::fillRound(r, tile, 4, lift(t.page, 10));
            const RunRecord::Held& h = rec.deck[i];
            std::string name = h.name + (h.count > 1 ? "  x" + std::to_string(h.count) : "");
            const int fit = std::max(4, (tileW - 16) / cw);
            if ((int)name.size() > fit) name = name.substr(0, (size_t)fit - 1) + ".";
            Console::drawTextPx(r, tile.x + 8, tile.y + (tileH - ch) / 2, name,
                                RARITY[std::max(0, std::min(4, h.rarity))], false);
        }
        if (col == perRow - 1 || i + 1 == rec.deck.size()) y += tileH + gap;
    }
    y += lineH / 2;

    text(box.x, "THE RUN IN NUMBERS", t.accent, true);
    y += lineH;
    row("Won", std::to_string(rec.won) + (rec.won == 1 ? " fight, " : " fights, ") + std::to_string(rec.bosses)
               + (rec.bosses == 1 ? " boss, " : " bosses, ") + std::to_string(rec.shades)
               + (rec.shades == 1 ? " Moon Shade" : " Moon Shades"));
    row("Damage", withCommas(rec.dealt) + " dealt, " + withCommas(rec.taken) + " taken");
    row("Biggest hit", withCommas(rec.biggest));
    row("Cards", withCommas(rec.cards) + " played" + (rec.most.empty() ? std::string()
               : ", " + rec.most + " the most (" + std::to_string(rec.mostCount) + ")"));
    row("Rest sites", std::to_string(rec.rests) + (rec.rests == 1 ? " rest, " : " rests, ") + std::to_string(rec.forges)
                      + (rec.forges == 1 ? " forge, " : " forges, ") + std::to_string(rec.sits)
                      + (rec.sits == 1 ? " fire sat by" : " fires sat by"));
    row("Turns", withCommas(rec.turns));
    return y + scroll - box.y;
}

void drawChronicle() {
    if (!gChron) return;
    const ChronView& v = *gChron;
    SDL_Renderer* r = Platform::renderer();
    const Theme& t = NIGHT;
    const int W = Platform::screenW();
    const int cw = std::max(1, Platform::cellW()), ch = std::max(1, Platform::cellH());
    const JournalLayout L = layoutBook(chronTabs(), (int)v.rows.size());
    drawBookFrame(r, L, chronTabs(), { &NIGHT, &NIGHT }, v.tab, t);

    int x = L.left.x + L.pad, y = L.left.y + L.pad;
    Console::drawTextBigPx(r, x, y, CHRON_TAB[v.tab], t.ink, true);
    Console::drawTextPx(r, x, y + Console::bigCellH() + 4,
                        v.tab == 0 ? std::to_string(v.all.size()) + (v.all.size() == 1 ? " run kept" : " runs kept")
                                   : std::string("the best on each road"), t.faint, false);
    for (size_t i = 0; i < L.rows.size(); ++i) {
        const size_t at = (size_t)v.top + i;
        if (at >= v.rows.size()) break;
        const RunRecord& rec = v.all[(size_t)v.rows[at]];
        const SDL_Rect& row = L.rows[i];
        const bool on = (int)at == v.sel;
        if (on) {
            DrawUtil::fillRound(r, row, 5, lift(t.page, 14));
            DrawUtil::frameRound(r, row, 5, LIME);
        }
        std::string road = rec.road;
        road.resize(std::max(road.size(), (size_t)17), ' ');
        const std::string how = rec.result == 3 ? "true ending" : rec.result == 2 ? "cleared"
                              : rec.area == CHURCH ? "the church" : "encounter " + std::to_string(rec.at);
        Console::drawTextPx(r, row.x + 14, row.y + (row.h - ch) / 2, road + how,
                            rec.result >= 2 && rec.result <= 3 ? GOLD : t.ink, on);
    }
    DrawUtil::drawScroll(r, L.listTrack, v.top, std::max(0, (int)v.rows.size() - L.capacity), L.capacity,
                         (int)v.rows.size(), dark(t.page), t.faint);

    bool scrolls = false;
    const SDL_Rect box = chronBox(L);
    if (v.sel >= 0 && v.sel < (int)v.rows.size()) {
        const RunRecord& rec = v.all[(size_t)v.rows[(size_t)v.sel]];
        const int whole = chronPage(r, L, rec, 0, false, t);
        const int maxScroll = std::max(0, whole - box.h);
        SDL_RenderSetClipRect(r, &box);
        chronPage(r, L, rec, std::min(v.scroll, maxScroll), true, t);
        SDL_RenderSetClipRect(r, nullptr);
        const int trackW = std::max(6, cw * 2 / 3);
        const SDL_Rect track{ L.right.x + L.right.w - L.pad - trackW, box.y, trackW, box.h };
        DrawUtil::drawScroll(r, track, std::min(v.scroll, maxScroll), maxScroll, box.h, whole, dark(t.page), t.faint);
        scrolls = maxScroll > 0;
    } else {
        Console::drawTextPx(r, box.x, box.y + L.pad, "No runs yet. Every run you finish is kept here.", t.ink, false);
    }
    const std::string hint = std::string("up and down to choose     left and right for the other list")
                           + (scrolls ? "     PgUp and PgDn to scroll" : "") + "     Esc to close";
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
    noteLorekeeper();
}

// Lorekeeper: every enemy met and every Lore Book page found. Everything it
// counts saves progress the moment it happens, so each of those spots calls
// this right after, and loading checks once for anyone who already had it all.
void Game::noteLorekeeper() {
    if (Achievements::has(Achievements::LOREKEEPER)) return;
    for (int i = 0; i < COUNT; ++i)
        if (!hasMet(i)) return;
    for (const LoreEntry& e : loreEntries(true))
        if (!e.seen) return;
    earn(Achievements::LOREKEEPER);
}

// The true form keeps the Shadow Knight's name in the fight, so it is picked
// out by the phase instead.
int Game::recordIdNow() const {
    return trueFormPhase ? bestiaryIndexOf("Moonstruck Shadow Knight") : bestiaryIndexOf(enemy.getName());
}

void Game::recordFight(int outcome) {
    const int id = recordIdNow();
    if (id < 0 || id >= 128) return;
    BeastRecord& rec = beastRecords[id];
    if (outcome == 0) rec.fought++;
    else if (outcome == 1) { rec.won++; if (rec.fastest == 0 || turnNumber < rec.fastest) rec.fastest = turnNumber; }
    else rec.lost++;
    // The Chronicle's damage dealt: what stuck on it by the end of the fight.
    if (outcome != 0) runTally.dealt += std::max(0, enemy.getMaxHealth() - enemy.getHealth());
    saveProgress();
}

// Its record page: two columns, and the true form's under its own heading on
// the Shadow Knight's.
std::vector<std::string> Game::recordLines(int id, int id2) const {
    auto times = [](int n) {
        return n == 1 ? std::string("once") : n == 2 ? std::string("twice") : std::to_string(n) + " times";
    };
    std::vector<std::string> out;
    auto block = [&](int i) {
        const BeastRecord& rec = beastRecords[i];
        if (rec.fought == 0) { out.push_back("No fights on record yet."); return; }
        out.push_back("Fought|" + times(rec.fought));
        out.push_back("Beaten|" + (rec.won ? times(rec.won) : std::string("not yet")));
        out.push_back("Beaten by it|" + (rec.lost ? times(rec.lost) : std::string("never")));
        if (rec.fastest > 0) out.push_back("Fastest win|" + std::to_string(rec.fastest) + (rec.fastest == 1 ? " turn" : " turns"));
    };
    if (id >= 0 && id < 128) block(id);
    if (id2 >= 0 && id2 < 128 && hasMet(id2)) {
        out.push_back("");
        out.push_back("#Its true form");
        block(id2);
    }
    return out;
}

void Game::loreProgress(int& have, int& need) const {
    have = need = 0;
    for (int i = 0; i < COUNT; ++i) { ++need; have += hasMet(i) ? 1 : 0; }
    for (const LoreEntry& e : loreEntries(true)) { ++need; have += e.seen ? 1 : 0; }
}

// Floors, so it only says 100% once every last thing is in.
int Game::galleryPercent() const {
    int have = 0, need = 0;
    loreProgress(have, need);
    for (const Card& c : allCards()) { ++need; have += cardsHeld.count(c.getBaseName()) ? 1 : 0; }
    return need > 0 ? have * 100 / need : 100;
}

// Saves from before the bestiary existed have no MET record, so fill it in
// from what they do have: a beaten boss means you walked its area, and a Moon
// Shade seen is a Moon Shade met. Only ever adds.
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

// An enemy's sprite as a card icon, for the quick road's forks.
int Game::bestiaryIcon(const std::string& enemyName) const {
    const int id = bestiaryIndexOf(enemyName);
    return id < 0 ? -1 : ICON0 + id * 2;
}

void Game::drawCardIcon(SDL_Renderer* r, int icon, const SDL_Rect& dst) {
    if (icon < ICON0) { EnemyArt::drawItemIcon(r, icon, dst); return; }
    const int id = (icon - ICON0) / 2;
    if (id >= COUNT) return;
    EnemyArt::drawSheetFrame(BESTIARY[id].sheet, BESTIARY[id].frameW, 0, dst, (icon - ICON0) % 2 != 0);
}

// An enemy's text: its entry (or its View Enemy line if it hasn't got one
// yet), then the weakness/resistance facts.
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
        v.ids.clear(); v.met.clear(); v.text.clear(); v.phase2.clear(); v.text2.clear(); v.record.clear();
        v.metCount = 0;
        v.page = 0;
        v.first = 0;
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
            v.record.push_back(hasMet(i) ? recordLines(i, second) : std::vector<std::string>{});
        }
        v.sel = std::min(selFor[area], (int)v.ids.size() - 1);
    };
    open(0);
    gView = &v;
    Platform::setModalRenderer(&drawJournal);
    Platform::flushKeys();
    int lastMx = -1, lastMy = -1, lastRow = -1;
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
        v.first = 0;
        Audio::menuHover();
    };
    auto flip = [&]() {
        const size_t si = (size_t)v.sel;
        if (si >= v.ids.size() || pagesOf(v, si) <= 1) return;
        v.page = (v.page + 1) % pagesOf(v, si);
        v.first = 0;
        Audio::menuHover();
    };
    DrawUtil::ScrollGrip grip;   // the entry's slider, while it's held
    while (true) {
        const JournalLayout L = layoutJournal(v.areas, (int)v.ids.size());
        const size_t cur = (size_t)std::max(0, v.sel);
        const EntryText e = layoutEntry(L, onRecord(v, cur) ? std::string() : onSecond(v, cur) ? v.text2[cur] : v.text[cur]);
        const int view = e.loreBottom - e.top;
        v.first = std::min(v.first, e.maxFirst);   // the window may have grown
        int mx, my;
        Platform::mousePos(mx, my);
        DrawUtil::scrollDrag(grip, e.track, v.first, e.maxFirst, view, e.whole, my, Platform::mouseDown());
        // The highlight follows the pointer onto another row, not the pointer
        // twitching on the row it was already on: after the wheel or the keys moved
        // the highlight, a nudge of the mouse used to snap it straight back.
        if ((mx != lastMx || my != lastMy) && !Platform::pointerHidden() && !grip.held) {
            lastMx = mx; lastMy = my;
            int row = -1;
            for (size_t i = 0; i < L.rows.size(); ++i) {
                const SDL_Rect& q = L.rows[i];
                if (mx >= q.x && mx < q.x + q.w && my >= q.y && my < q.y + q.h) row = (int)i;
            }
            if (row != lastRow) {
                lastRow = row;
                if (row >= 0) choose(row);
            }
        }
        const Platform::KeyEvent k = Platform::pollKey();
        const int n = (int)v.ids.size();
        if (k.key == Platform::Key::UP)        choose((v.sel - 1 + n) % n);
        else if (k.key == Platform::Key::DOWN) choose((v.sel + 1) % n);
        else if (k.key == Platform::Key::LEFT || k.key == Platform::Key::TO_CARDS)    turn(-1);
        else if (k.key == Platform::Key::RIGHT || k.key == Platform::Key::TO_ACTIONS) turn(1);
        else if (k.key == Platform::Key::ENTER) flip();
        else if (k.key == Platform::Key::PAGE_UP)   v.first = std::max(0, v.first - std::max(1, view / e.lineH - 1));
        else if (k.key == Platform::Key::PAGE_DOWN) v.first = std::min(e.maxFirst, v.first + std::max(1, view / e.lineH - 1));
        else if (k.key == Platform::Key::ESCAPE) break;
        // Over the words, or from a pad's right stick, the wheel scrolls a long
        // entry. Anywhere else it moves through the list.
        const int wheel = Platform::takeWheel();
        const bool overWords = mx >= L.right.x && mx < L.right.x + L.right.w && my >= L.right.y && my < L.right.y + L.right.h;
        if (wheel && e.maxFirst > 0 && (overWords || Platform::pointerHidden()))
            v.first = std::max(0, std::min(e.maxFirst, v.first - wheel));
        else if (wheel) choose(std::max(0, std::min(n - 1, v.sel - wheel)));
        int cx, cy;
        if (Platform::takeClick(cx, cy) && !DrawUtil::scrollPress(grip, e.track, v.first, e.maxFirst, view, e.whole, cx, cy)) {
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

// The Gallery menu.
void Game::showGallery() {
    int last = 0;   // back from a book, the highlight stays on it
    while (true) {
        const std::vector<CardBar::Action> acts{
            CardBar::Action{ "Bestiary",  "every enemy you have met", false },
            CardBar::Action{ "Lore Book", "the story so far, every fire, the letters and the cutscenes", false },
            CardBar::Action{ "Card Ledger", "every card in the game, and the ones you have held", false },
            CardBar::Action{ "Chronicle", "every run you have finished, how far it got and what you had", false },
            CardBar::Action{ "Back", false } };
        UIHelper::clearScreen();
        CardBar::setNextHighlight(last);
        // Short of 100% is the hint that there is more out there than you've seen.
        CardBar::setNextCorner(std::to_string(galleryPercent()) + "% found");
        const int c = CardBar::pick("Gallery", {}, acts, 0);
        if (c == 0)      showBestiary();
        else if (c == 1) showLoreBook();
        else if (c == 2) showCardLedger();
        else if (c == 3) showChronicle();
        else return;
        last = c;
    }
}

void Game::showLoreBook() {
    const std::vector<LoreEntry> all = loreEntries();
    LoreView v;
    int selFor[LORE_TABS] = { 0, 0, 0, 0 }, topFor[LORE_TABS] = { 0, 0, 0, 0 };
    auto open = [&](int tab) {
        v.tab = tab;
        v.rows.clear();
        for (const LoreEntry& e : all)
            if (e.tab == tab) v.rows.push_back(&e);
        v.sel = std::min(selFor[tab], (int)v.rows.size() - 1);
        v.top = topFor[tab];
        v.first = 0;
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
        open((v.tab + d + LORE_TABS) % LORE_TABS);
        Audio::menuHover();
    };
    auto choose = [&](int i) {
        if (i < 0 || i >= (int)v.rows.size() || i == v.sel) return;
        v.sel = i;
        v.first = 0;
        keepInView();
        Audio::menuHover();
    };
    open(0);
    gLore = &v;
    Platform::setModalRenderer(&drawLore);
    Platform::flushKeys();
    int lastMx = -1, lastMy = -1, lastRow = -1;
    Platform::mousePos(lastMx, lastMy);
    DrawUtil::ScrollGrip listGrip, pageGrip;   // the two sliders, while one is held
    while (true) {
        const JournalLayout L = layoutBook(loreTabs(), (int)v.rows.size());
        const TextBlock page = lorePage(L, v);
        const int maxTop = std::max(0, (int)v.rows.size() - L.capacity);
        // Moving the list keeps the highlight on a row you can see.
        auto selInView = [&]() {
            const int s = std::max(v.top, std::min(v.sel, v.top + L.capacity - 1));
            if (s != v.sel) { v.sel = s; v.first = 0; }
        };
        v.first = std::min(v.first, page.maxFirst);
        int mx, my;
        Platform::mousePos(mx, my);
        DrawUtil::scrollDrag(pageGrip, page.track, v.first, page.maxFirst, page.bottom - page.top, page.whole, my,
                             Platform::mouseDown());
        if (listGrip.held) {
            DrawUtil::scrollDrag(listGrip, L.listTrack, v.top, maxTop, L.capacity, (int)v.rows.size(), my,
                                 Platform::mouseDown());
            selInView();
        }
        // The highlight follows the pointer onto another row, not the pointer
        // twitching on the row it was already on: after the wheel or the keys moved
        // the highlight, a nudge of the mouse used to snap it straight back.
        if ((mx != lastMx || my != lastMy) && !Platform::pointerHidden() && !listGrip.held && !pageGrip.held) {
            lastMx = mx; lastMy = my;
            int row = -1;
            for (size_t i = 0; i < L.rows.size(); ++i) {
                const SDL_Rect& q = L.rows[i];
                if (mx >= q.x && mx < q.x + q.w && my >= q.y && my < q.y + q.h) row = (int)i;
            }
            if (row != lastRow) {
                lastRow = row;
                if (row >= 0) choose(v.top + row);
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
        else if (k.key == Platform::Key::PAGE_UP)
            v.first = std::max(0, v.first - std::max(1, (page.bottom - page.top) / page.lineH - 1));
        else if (k.key == Platform::Key::PAGE_DOWN)
            v.first = std::min(page.maxFirst, v.first + std::max(1, (page.bottom - page.top) / page.lineH - 1));
        else if (k.key == Platform::Key::ESCAPE) break;
        // Over the page, or from a pad's right stick, the wheel scrolls a long
        // page. Anywhere else it moves through the list.
        const int wheel = Platform::takeWheel();
        const bool overPage = mx >= L.right.x && mx < L.right.x + L.right.w && my >= L.right.y && my < L.right.y + L.right.h;
        if (wheel && page.maxFirst > 0 && (overPage || Platform::pointerHidden()))
            v.first = std::max(0, std::min(page.maxFirst, v.first - wheel));
        else if (wheel) choose(std::max(0, std::min(n - 1, v.sel - wheel)));
        int cx, cy;
        if (Platform::takeClick(cx, cy)) {
            if (DrawUtil::scrollPress(pageGrip, page.track, v.first, page.maxFirst, page.bottom - page.top, page.whole,
                                      cx, cy)) {
                Platform::frame();
                continue;
            }
            if (DrawUtil::scrollPress(listGrip, L.listTrack, v.top, maxTop, L.capacity, n, cx, cy)) {
                selInView();
                Platform::frame();
                continue;
            }
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

// --- the Card Ledger -----------------------------------------------------------
std::vector<Card> Game::allCards() const {
    std::vector<Card> all = starterCards();
    for (int rank = 1; rank <= 4; ++rank) {
        const std::vector<Card> more = rewardPool.cardsOfRarity(rank);
        all.insert(all.end(), more.begin(), more.end());
    }
    return all;
}

// Anything in the deck counts as held. Both halves of a weld count, and a
// forged card counts as the base card.
void Game::noteCardsHeld() {
    bool fresh = false;
    for (const Card& c : playerDeck.getAllCardsOrdered()) {
        if (c.isWeld()) {
            for (const Card& h : c.getParts()) fresh = cardsHeld.insert(h.getBaseName()).second || fresh;
        } else if (!isGraveDirt(c)) {
            fresh = cardsHeld.insert(c.getBaseName()).second || fresh;
        }
    }
    if (!fresh) return;
    saveProgress();
    for (const Card& c : allCards())
        if (!cardsHeld.count(c.getBaseName())) return;
    earn(Achievements::EVERY_CARD);
}

void Game::showCardLedger() {
    const std::vector<Card> all = allCards();
    CardLedgerView v;
    v.total = (int)all.size();
    for (const Card& c : all) v.heldAll += cardsHeld.count(c.getBaseName()) ? 1 : 0;
    int selFor[LEDGER_TABS] = { 0, 0, 0, 0, 0 }, topFor[LEDGER_TABS] = { 0, 0, 0, 0, 0 };
    auto open = [&](int tab) {
        v.tab = tab;
        v.rows.clear(); v.held.clear(); v.faces.clear();
        for (const Card& c : all)
            if (rarityOf(c) == tab) v.rows.push_back(c);
        std::stable_sort(v.rows.begin(), v.rows.end(),
                         [](const Card& a, const Card& b) { return a.getName() < b.getName(); });
        for (const Card& c : v.rows) {
            v.held.push_back(cardsHeld.count(c.getBaseName()) != 0);
            v.faces.push_back(printedWidget(c));
        }
        v.sel = std::min(selFor[tab], (int)v.rows.size() - 1);
        v.top = topFor[tab];
        v.first = 0;
    };
    auto keepInView = [&]() {
        const JournalLayout L = layoutBook(ledgerTabs(), (int)v.rows.size());
        if (v.sel < v.top) v.top = v.sel;
        if (v.sel >= v.top + L.capacity) v.top = v.sel - L.capacity + 1;
        v.top = std::max(0, std::min(v.top, std::max(0, (int)v.rows.size() - L.capacity)));
    };
    auto turn = [&](int d) {
        selFor[v.tab] = v.sel; topFor[v.tab] = v.top;
        open((v.tab + d + LEDGER_TABS) % LEDGER_TABS);
        Audio::menuHover();
    };
    auto choose = [&](int i) {
        if (i < 0 || i >= (int)v.rows.size() || i == v.sel) return;
        v.sel = i;
        v.first = 0;
        keepInView();
        Audio::menuHover();
    };
    open(0);
    gLedger = &v;
    Platform::setModalRenderer(&drawCardLedger);
    Platform::flushKeys();
    int lastMx = -1, lastMy = -1, lastRow = -1;
    Platform::mousePos(lastMx, lastMy);
    DrawUtil::ScrollGrip listGrip, pageGrip;   // the two sliders, while one is held
    while (true) {
        const JournalLayout L = layoutBook(ledgerTabs(), (int)v.rows.size());
        const TextBlock page = ledgerPage(L, v);
        const int maxTop = std::max(0, (int)v.rows.size() - L.capacity);
        auto selInView = [&]() {
            const int s = std::max(v.top, std::min(v.sel, v.top + L.capacity - 1));
            if (s != v.sel) { v.sel = s; v.first = 0; }
        };
        v.first = std::min(v.first, page.maxFirst);
        int mx, my;
        Platform::mousePos(mx, my);
        DrawUtil::scrollDrag(pageGrip, page.track, v.first, page.maxFirst, page.bottom - page.top, page.whole, my,
                             Platform::mouseDown());
        if (listGrip.held) {
            DrawUtil::scrollDrag(listGrip, L.listTrack, v.top, maxTop, L.capacity, (int)v.rows.size(), my,
                                 Platform::mouseDown());
            selInView();
        }
        // The highlight follows the pointer onto another row, not the pointer
        // twitching on the row it was already on: after the wheel or the keys moved
        // the highlight, a nudge of the mouse used to snap it straight back.
        if ((mx != lastMx || my != lastMy) && !Platform::pointerHidden() && !listGrip.held && !pageGrip.held) {
            lastMx = mx; lastMy = my;
            int row = -1;
            for (size_t i = 0; i < L.rows.size(); ++i) {
                const SDL_Rect& q = L.rows[i];
                if (mx >= q.x && mx < q.x + q.w && my >= q.y && my < q.y + q.h) row = (int)i;
            }
            if (row != lastRow) {
                lastRow = row;
                if (row >= 0) choose(v.top + row);
            }
        }
        const Platform::KeyEvent k = Platform::pollKey();
        const int n = (int)v.rows.size();
        if (k.key == Platform::Key::UP)        choose((v.sel - 1 + n) % n);
        else if (k.key == Platform::Key::DOWN) choose((v.sel + 1) % n);
        else if (k.key == Platform::Key::LEFT || k.key == Platform::Key::TO_CARDS)    turn(-1);
        else if (k.key == Platform::Key::RIGHT || k.key == Platform::Key::TO_ACTIONS) turn(1);
        else if (k.key == Platform::Key::PAGE_UP)
            v.first = std::max(0, v.first - std::max(1, (page.bottom - page.top) / page.lineH - 1));
        else if (k.key == Platform::Key::PAGE_DOWN)
            v.first = std::min(page.maxFirst, v.first + std::max(1, (page.bottom - page.top) / page.lineH - 1));
        else if (k.key == Platform::Key::ESCAPE) break;
        const int wheel = Platform::takeWheel();
        const bool overPage = mx >= L.right.x && mx < L.right.x + L.right.w && my >= L.right.y && my < L.right.y + L.right.h;
        if (wheel && page.maxFirst > 0 && (overPage || Platform::pointerHidden()))
            v.first = std::max(0, std::min(page.maxFirst, v.first - wheel));
        else if (wheel) choose(std::max(0, std::min(n - 1, v.sel - wheel)));
        int cx, cy;
        if (Platform::takeClick(cx, cy)) {
            if (DrawUtil::scrollPress(pageGrip, page.track, v.first, page.maxFirst, page.bottom - page.top, page.whole,
                                      cx, cy)) {
                Platform::frame();
                continue;
            }
            if (DrawUtil::scrollPress(listGrip, L.listTrack, v.top, maxTop, L.capacity, n, cx, cy)) {
                selInView();
                Platform::frame();
                continue;
            }
            for (size_t a = 0; a < L.tabs.size(); ++a) {
                const SDL_Rect& q = L.tabs[a];
                if (cx >= q.x && cx < q.x + q.w && cy >= q.y && cy < q.y + q.h) turn((int)a - v.tab);
            }
            for (size_t i = 0; i < L.rows.size(); ++i) {
                const SDL_Rect& q = L.rows[i];
                if (cx >= q.x && cx < q.x + q.w && cy >= q.y && cy < q.y + q.h) choose(v.top + (int)i);
            }
        }
        Platform::frame();
    }
    Platform::setModalRenderer(nullptr);
    gLedger = nullptr;
    Audio::menuSelect(true);
}

// --- the Chronicle: writing a run down, and the book ---------------------------
// In its own file next to the progress file, so it can never touch your progress.
std::string Game::chroniclePath() const { return Audio::saveDir() + "chronicle.dat"; }

void Game::tallySeconds() {
    const unsigned now = SDL_GetTicks();
    runTally.seconds += (int)((now - runClock) / 1000);
    runClock = now;
}

// At the end of a run that really ended: how far, what ended it, what you
// carried, and the numbers. The last fifty are kept.
void Game::writeChronicle() {
    tallySeconds();
    std::vector<std::vector<std::string>> blocks;
    {
        std::ifstream in(chroniclePath());
        std::vector<std::string> cur;
        for (std::string line; std::getline(in, line); ) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            cur.push_back(line);
            if (line == "END") { blocks.push_back(cur); cur.clear(); }
        }
    }
    while (blocks.size() >= 50) blocks.erase(blocks.begin());

    std::time_t now = std::time(nullptr);
    std::tm when{};
#ifdef _WIN32
    localtime_s(&when, &now);
#else
    localtime_r(&now, &when);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &when);
    std::string by = trueFormPhase ? std::string("Shadow Knight's true form") : enemy.getName();
    const size_t paren = by.find(" (");
    if (paren != std::string::npos) by.resize(paren);
    std::vector<std::string> b;
    b.push_back("RUN");
    b.push_back(std::string("WHEN ") + stamp);
    b.push_back(std::string("ROAD ") + modeName());
    b.push_back("RESULT " + std::to_string(runResult == 0 ? 4 : runResult));
    b.push_back("FAR " + std::to_string(currentRun.getCurrentEncounter()) + " " + std::to_string(currentRun.getLength())
                + " " + std::to_string(currentRun.getAreaIndex()));
    if (runResult == 1) b.push_back("BY " + by);
    b.push_back("WEAPON " + (wornWeapon == 0 ? std::string("Wooden Sword") : weaponTierAt(wornWeapon - 1).name));
    b.push_back("ARMOR " + (wornArmor == 0 ? std::string("Leather Armor") : armorTierAt(wornArmor - 1).name));
    std::string relics, keeps, deck;
    for (int i = 0; i < Relic::COUNT; ++i)
        if (hasRelic(i)) relics += (relics.empty() ? "" : "|") + std::string(Relic::INFO[i].name);
    for (int z = 0; z < KEEPSAKE_COUNT; ++z)
        if (hasKeepsake(z)) keeps += (keeps.empty() ? "" : "|") + std::string(keepsakeName(z));
    // The deck by name, copies counted, in the order you'd read it.
    std::vector<std::pair<std::string, std::pair<int, int>>> held;
    for (const Card& c : playerDeck.getAllCardsOrdered()) {
        if (isGraveDirt(c)) continue;
        auto it = std::find_if(held.begin(), held.end(), [&](const auto& h) { return h.first == c.getName(); });
        if (it != held.end()) it->second.second++;
        else held.push_back({ c.getName(), { rarityOf(c), 1 } });
    }
    for (const auto& h : held)
        deck += (deck.empty() ? "" : "|") + h.first + "=" + std::to_string(h.second.first) + "=" + std::to_string(h.second.second);
    b.push_back("RELICS " + relics);
    b.push_back("KEEPS " + keeps);
    b.push_back("DECK " + deck);
    int cards = 0, most = 0;
    std::string mostName;
    for (const auto& p : runTally.played) {
        cards += p.second;
        if (p.second > most) { most = p.second; mostName = p.first; }
    }
    int shades = 0;
    for (int z = 0; z < 5; ++z) shades += (moonZonesBeaten >> z) & 1;
    const int bosses = currentRun.areaBossesCleared() + (runResult >= 2 ? 1 : 0) + (runResult == 3 ? 1 : 0);
    std::ostringstream n;
    n << "NUMBERS " << currentRun.getEncountersWon() << " " << bosses << " " << shades << " " << runTally.dealt << " "
      << runTally.taken << " " << runTally.biggest << " " << cards << " " << runTally.turns << " " << runTally.rests << " "
      << runTally.forges << " " << runTally.sits << " " << runTally.seconds;
    b.push_back(n.str());
    if (most > 0) b.push_back("MOST " + std::to_string(most) + " " + mostName);
    b.push_back("END");
    blocks.push_back(b);

    std::ofstream out(chroniclePath(), std::ios::trunc);
    for (const auto& block : blocks)
        for (const std::string& line : block) out << line << "\n";
}

void Game::showChronicle() {
    ChronView v;
    v.all = readChronicle(chroniclePath());
    int selFor[CHRON_TABS] = { 0, 0 }, topFor[CHRON_TABS] = { 0, 0 };
    auto open = [&](int tab) {
        v.tab = tab;
        v.rows.clear();
        if (tab == 0) {
            for (int i = 0; i < (int)v.all.size(); ++i) v.rows.push_back(i);
        } else {
            // The best on each road, in the order the roads open.
            static const char* const ROADS[5] = { "Normal", "Quick", "Randomized", "Hard", "Randomized Hard" };
            for (const char* road : ROADS) {
                int best = -1;
                for (int i = 0; i < (int)v.all.size(); ++i)
                    if (v.all[(size_t)i].road == road && (best < 0 || runScore(v.all[(size_t)i]) > runScore(v.all[(size_t)best])))
                        best = i;
                if (best >= 0) v.rows.push_back(best);
            }
        }
        v.sel = std::max(0, std::min(selFor[tab], (int)v.rows.size() - 1));
        v.top = topFor[tab];
        v.scroll = 0;
    };
    auto keepInView = [&]() {
        const JournalLayout L = layoutBook(chronTabs(), (int)v.rows.size());
        if (v.sel < v.top) v.top = v.sel;
        if (v.sel >= v.top + L.capacity) v.top = v.sel - L.capacity + 1;
        v.top = std::max(0, std::min(v.top, std::max(0, (int)v.rows.size() - L.capacity)));
    };
    auto turn = [&](int d) {
        selFor[v.tab] = v.sel; topFor[v.tab] = v.top;
        open((v.tab + d + CHRON_TABS) % CHRON_TABS);
        Audio::menuHover();
    };
    auto choose = [&](int i) {
        if (i < 0 || i >= (int)v.rows.size() || i == v.sel) return;
        v.sel = i;
        v.scroll = 0;
        keepInView();
        Audio::menuHover();
    };
    open(0);
    gChron = &v;
    Platform::setModalRenderer(&drawChronicle);
    Platform::flushKeys();
    int lastMx = -1, lastMy = -1, lastRow = -1;
    Platform::mousePos(lastMx, lastMy);
    DrawUtil::ScrollGrip listGrip, pageGrip;
    while (true) {
        const JournalLayout L = layoutBook(chronTabs(), (int)v.rows.size());
        const SDL_Rect box = chronBox(L);
        const int whole = v.sel >= 0 && v.sel < (int)v.rows.size()
                        ? chronPage(nullptr, L, v.all[(size_t)v.rows[(size_t)v.sel]], 0, false, NIGHT) : 0;
        const int maxScroll = std::max(0, whole - box.h);
        const int trackW = std::max(6, Platform::cellW() * 2 / 3);
        const SDL_Rect track{ L.right.x + L.right.w - L.pad - trackW, box.y, trackW, box.h };
        const int n = (int)v.rows.size();
        const int maxTop = std::max(0, n - L.capacity);
        const int step = std::max(1, Platform::cellH() * 4 / 3);
        auto selInView = [&]() {
            const int s = std::max(v.top, std::min(v.sel, v.top + L.capacity - 1));
            if (s != v.sel) { v.sel = s; v.scroll = 0; }
        };
        v.scroll = std::min(v.scroll, maxScroll);
        int mx, my;
        Platform::mousePos(mx, my);
        DrawUtil::scrollDrag(pageGrip, track, v.scroll, maxScroll, box.h, whole, my, Platform::mouseDown());
        if (listGrip.held) {
            DrawUtil::scrollDrag(listGrip, L.listTrack, v.top, maxTop, L.capacity, n, my, Platform::mouseDown());
            selInView();
        }
        // The highlight follows the pointer onto another row, not the pointer
        // twitching on the row it was already on: after the wheel or the keys moved
        // the highlight, a nudge of the mouse used to snap it straight back.
        if ((mx != lastMx || my != lastMy) && !Platform::pointerHidden() && !listGrip.held && !pageGrip.held) {
            lastMx = mx; lastMy = my;
            int row = -1;
            for (size_t i = 0; i < L.rows.size(); ++i) {
                const SDL_Rect& q = L.rows[i];
                if (mx >= q.x && mx < q.x + q.w && my >= q.y && my < q.y + q.h) row = (int)i;
            }
            if (row != lastRow) {
                lastRow = row;
                if (row >= 0) choose(v.top + row);
            }
        }
        const Platform::KeyEvent k = Platform::pollKey();
        if (k.key == Platform::Key::UP && n > 0)        choose((v.sel - 1 + n) % n);
        else if (k.key == Platform::Key::DOWN && n > 0) choose((v.sel + 1) % n);
        else if (k.key == Platform::Key::LEFT || k.key == Platform::Key::TO_CARDS)    turn(-1);
        else if (k.key == Platform::Key::RIGHT || k.key == Platform::Key::TO_ACTIONS) turn(1);
        else if (k.key == Platform::Key::PAGE_UP)   v.scroll = std::max(0, v.scroll - (box.h - step));
        else if (k.key == Platform::Key::PAGE_DOWN) v.scroll = std::min(maxScroll, v.scroll + (box.h - step));
        else if (k.key == Platform::Key::ESCAPE) break;
        // Over the run, or from a pad's right stick, the wheel reads down it.
        const int wheel = Platform::takeWheel();
        const bool overPage = mx >= L.right.x && mx < L.right.x + L.right.w && my >= L.right.y && my < L.right.y + L.right.h;
        if (wheel && maxScroll > 0 && (overPage || Platform::pointerHidden()))
            v.scroll = std::max(0, std::min(maxScroll, v.scroll - wheel * step * 2));
        else if (wheel && n > 0) choose(std::max(0, std::min(n - 1, v.sel - wheel)));
        int cx, cy;
        if (Platform::takeClick(cx, cy)) {
            if (DrawUtil::scrollPress(pageGrip, track, v.scroll, maxScroll, box.h, whole, cx, cy)) { Platform::frame(); continue; }
            if (DrawUtil::scrollPress(listGrip, L.listTrack, v.top, maxTop, L.capacity, n, cx, cy)) {
                selInView();
                Platform::frame();
                continue;
            }
            for (size_t a = 0; a < L.tabs.size(); ++a) {
                const SDL_Rect& q = L.tabs[a];
                if (cx >= q.x && cx < q.x + q.w && cy >= q.y && cy < q.y + q.h) turn((int)a - v.tab);
            }
            for (size_t i = 0; i < L.rows.size(); ++i) {
                const SDL_Rect& q = L.rows[i];
                if (cx >= q.x && cx < q.x + q.w && cy >= q.y && cy < q.y + q.h) choose(v.top + (int)i);
            }
        }
        Platform::frame();
    }
    Platform::setModalRenderer(nullptr);
    gChron = nullptr;
    Audio::menuSelect(true);
}
