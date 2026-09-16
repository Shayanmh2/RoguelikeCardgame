// Projectile frames: which frame of projectiles.png each enemy throws,
// and where on its sprite the shot leaves from.
// One projectile frame per firing enemy. The shape comes from the kind of
// move; the muzzle and colour were measured from the effect each sprite
// paints into its own attack frame, so the shot leaves where the art shows it.
#ifndef PROJECTILE_TABLE_H
#define PROJECTILE_TABLE_H

namespace ProjectileTable {

// muzzleX / muzzleY are percentages of the sprite frame: where the shot
// leaves the creature. Derived from the attack frame, hand-tuned where the
// automatic answer was wrong.
struct Entry { const char* enemy; int frame; int muzzleX; int muzzleY; };

// Matched with the same substring test enemyTurn() uses for enemy names.
static const Entry kByName[] = {
    { "Bandit",        0, 10, 45 },   // blade rgb(214,210,200)
    { "Assassin",      1, 14, 30 },   // blade rgb(236,232,196)
    { "Deadeye",       2,  6, 42 },   // arrow rgb(244,150, 92)
    { "Archer",        3,  6, 46 },   // arrow rgb(230,224,196)
    { "Omneye",        4, 12, 42 },   // beam  rgb(236,138, 72)
    { "Sage",          5,  8, 48 },   // orb   rgb(240,150, 60)
    { "Archon",       -2, 50, 55 },   // none  rgb(200,117, 65)
    { "Spellmaster",   7, 10, 27 },   // orb   rgb( 96,168, 88)
    { "Sorcerer",      8,  8, 22 },   // shard rgb(140,190,214)
    { "Wizard",        9,  6, 40 },   // orb   rgb( 90,215,235)
    { "Enchanter",    10, 10, 52 },   // orb   rgb(176,140,226)
    { "Mystic",       11,  6, 45 },   // orb   rgb( 90,215,235)
    { "Spider",       12,  8, 45 },   // web   rgb(222,222,216)
    { "Serpent",      13,  7, 54 },   // orb   rgb(150,226,110)
    { "Banshee",      14,  8, 42 },   // wisp  rgb(200,238,246)
    { "Specter",      15,  6, 26 },   // wisp  rgb(190,232,246)
    { "Wraith",       16,  8, 46 },   // wisp  rgb(190,220,236)
    { "Lich",         17,  8, 40 },   // orb   rgb( 90,215,235)
    { "Vampire",      18, 14, 33 },   // wisp  rgb(206, 46, 52)
    { "Vile Witch",   19, 20, 27 },   // orb   rgb(150,225,110)
    { "Dragon",       20,  8, 42 },   // breath rgb(226,236,246)
};
static const int kByNameCount = 21;

// Fallbacks for an enemy with no entry above, then the knight's spell orbs.
static const int GEN_MELEE = 21;   // blade from melee_goblin
static const int GEN_RANGED = 22;   // arrow from ranged_archer
static const int GEN_CASTER = 23;   // orb   from caster_wizard
static const int GEN_BEAST = 24;   // fang  from beast
static const int GEN_UNDEAD = 25;   // wisp  from undead_skeleton
static const int PC_POISON = 26;   // orb   from player
static const int PC_BURN = 27;   // orb   from player
static const int PC_STUN = 28;   // orb   from player
static const int PC_WEAK = 29;   // orb   from player
static const int PC_REND = 30;   // orb   from player
static const int FX_PILLAR = 31;   // splice:4:9:13 from caster_archon
static const int FX_WIND = 32;   // wind  from player
static const int FX_BEAM = 33;   // splice:4:0:10:10:15 from ranged_omneye


// NONE lives in EnemyArt::Proj, not here: one definition, and the art layer
// is what interprets it.

} // namespace ProjectileTable

#endif
