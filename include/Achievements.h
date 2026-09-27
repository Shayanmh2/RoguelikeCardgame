#ifndef ACHIEVEMENTS_H
#define ACHIEVEMENTS_H

// Things done across every run, kept with the cleared roads in progress.dat.
// Three ranks, each with its medal. Earning one slides a notice in at the top
// right, over everything, the full-screen pickers included.
namespace Achievements {

enum Rank { BRONZE, SILVER, GOLD };

enum Id {
    // bronze
    WIN_FIGHT, COLOSSUS, VIGIL, SIT, FORGE, RELIC, BOON, PARRY, STUN,
    // silver
    WITCH, THUNDER, HYDRA, DRAGON, MOONSTRUCK, SEAR, BIG_HIT, THREAD, LEGEND,
    // gold
    CLEAR, TRUE_FORM, HARD, ALL_FORMS, HUGE_HIT, TROPHY,
    // Added later, so on the end: progress.dat keeps them by position, and a
    // new one in the middle would shift everything earned after it.
    HEAD_START, GEAR, TRAVEL_LIGHT, REST, MEET_MOON,                 // bronze
    FIRST_TURN, BY_A_HAIR, EVERY_VIGIL, COLLECTOR, ONE_HEAD, LAST_FIRE, // silver
    NO_LEGENDS, RANDOM_HARD,                                         // gold
    HER_OWN, BACKDRAFT, UNSHAKEN, STORM_STOPS, SLOW_BURN, STONE_COLD, // silver
    TWICE_STRUCK, FLEW_AWAY, COLD_FEET, SIDESTEP,                    // bronze
    CLEAN_CUTS,                                                      // gold
    COUNT
};

struct Info { const char* name; const char* text; Rank rank; };
const Info& info(int id);

bool has(int id);
// Marks it earned and announces it; false if it already was.
bool earn(int id);

// The progress.dat round trip. Loading is not earning: nothing is announced.
unsigned long long mask();
void setMask(unsigned long long m);
// The Moonstruck shapes met in any run, a bit per area.
int  formsSeen();
void setFormsSeen(int m);
// Records a shape met; true once all five have been.
bool seeForm(int zone);

int earnedCount();
int earnedCount(Rank r);
int totalCount(Rank r);

// Puts the notice layer in with Platform. Once, at start-up.
void install();
// That layer: the notice on screen, if there is one. install() registers it.
void draw();
// The list: the three ranks, then the achievements of the one picked.
void showScreen();

} // namespace Achievements

#endif
