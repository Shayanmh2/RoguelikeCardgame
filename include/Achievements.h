#ifndef ACHIEVEMENTS_H
#define ACHIEVEMENTS_H

// Things done across every run, kept with the cleared roads in progress.dat.
// Three ranks, each with its medal, and a fourth, crimson, for the one that
// means everything is done (its page shows only once it is earned). Earning one slides a notice in at the top
// right, over everything, the full-screen pickers included.
namespace Achievements {

enum Rank { BRONZE, SILVER, GOLD, CRIMSON };

// In the order they were added, which is the order progress.dat keeps them
// in: never reorder or insert, only add on the end, or everything earned
// after the change shifts. Each one's rank is in INFO, and the order the pages
// list them in is DISPLAY_ORDER (Achievements.cpp).
enum Id {
    WIN_FIGHT, COLOSSUS, VIGIL, SIT, FORGE, RELIC, BOON, PARRY, STUN,
    WITCH, THUNDER, HYDRA, DRAGON, MOONSTRUCK, SEAR, BIG_HIT, THREAD, LEGEND,
    CLEAR, TRUE_FORM, HARD, ALL_FORMS, HUGE_HIT, TROPHY,
    HEAD_START, GEAR, TRAVEL_LIGHT, REST, MEET_MOON,
    FIRST_TURN, BY_A_HAIR, EVERY_VIGIL, COLLECTOR, ONE_HEAD, LAST_FIRE,
    NO_LEGENDS, RANDOM_HARD,
    HER_OWN, BACKDRAFT, UNSHAKEN, STORM_STOPS, SLOW_BURN, STONE_COLD,
    TWICE_STRUCK, FLEW_AWAY, COLD_FEET, SIDESTEP,
    CLEAN_CUTS,
    THOUSAND, NO_REST,
    NO_DISCARD, EVERY_SIT, DOT_ONLY,
    TUTORIAL_SLIME,
    FULL_REST, WALK_IT_OFF, SCRAP_DEATH, HELD_SACRIFICE, OWN_BLOW,
    JAB_FINISH, OVERKILL, LUCKY_CHARM, CURSED_RELIC, TICKLE,
    DOWN_TO_ONE, SENTIMENTAL, BAD_DAY, BUYERS_REMORSE, WIDE_OPEN,
    ONE_TRICK, MAGICIAN,
    NECROMANCER, NO_BOONS, NO_RELICS,
    SHORT_WAY,
    GRAVE_MISTAKE,
    FALSE_MOON,
    COUNT
};

struct Info { const char* name; const char* text; Rank rank; };
const Info& info(int id);

bool has(int id);
// Marks it earned and announces it; false if it already was.
bool earn(int id);

// The progress.dat round trip. Loading is not earning: nothing is announced.
// Two 64-bit words: the first is the ACH line, the second (ids 64 on) ACH2.
unsigned long long mask(int word = 0);
void setMask(unsigned long long m, int word = 0);
// The Moonstruck shapes met in any run, a bit per area.
int  formsSeen();
void setFormsSeen(int m);
// Records a shape met; true once all five have been.
bool seeForm(int zone);
// The shapes beaten in any run, a bit per area: all five open the way to the
// ruined church.
int  formsBeaten();
void setFormsBeaten(int m);
bool beatForm(int zone);
// The crimson one has two halves (the user, 2026-10-07): the False Moon put
// out in some run, kept in progress.dat, and every other achievement earned.
// It comes the moment both are done, in either order.
bool falseMoonBeaten();
void setFalseMoonBeaten(bool on);
bool crimsonDue();
// The frame in medals.png for a rank: crimson is the fifth, after the dark
// one for locked.
int  medalFrame(int rank);
// The Sit a while memories heard in any run, a bit each.
unsigned long long sitsHeard();
void setSitsHeard(unsigned long long m);
// Records one heard; true once every one of `total` has been.
bool hearSit(int index, int total);

int earnedCount();
int earnedCount(Rank r);
int totalCount(Rank r);
// How many there are, as far as the player knows: the crimson one is a
// secret, and nothing counts it, names it or shows its rank until the False
// Moon is beaten and it is earned.
int knownCount();

// Puts the notice layer in with Platform. Once, at start-up.
void install();
// That layer: the notice on screen, if there is one. install() registers it.
void draw();
// The list: the three ranks, then the achievements of the one picked.
void showScreen();

} // namespace Achievements

#endif
