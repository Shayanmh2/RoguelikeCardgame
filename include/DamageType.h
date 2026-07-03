#ifndef DAMAGE_TYPE_H
#define DAMAGE_TYPE_H

// Damage typing used for the enemy-weakness system. A card can carry a
// physical school tag (SMASH/PIERCE) and/or an elemental tag (FIRE/POISON/WIND)
// independently — e.g. Whirlwind is PIERCE + WIND. Enemies have a single
// weakness drawn from this same enum; matching either of a card's tags
// against it grants bonus damage.
enum class DamageType {
    NONE,
    SMASH,   // heavy blunt/crushing strikes — punishes armored, sturdy enemies
    PIERCE,  // precise, gap-finding strikes — punishes mobile, lightly-armored enemies
    FIRE,
    POISON,
    WIND
};

inline const char* damageTypeName(DamageType t) {
    switch (t) {
        case DamageType::SMASH:  return "Smash";
        case DamageType::PIERCE: return "Pierce";
        case DamageType::FIRE:   return "Fire";
        case DamageType::POISON: return "Poison";
        case DamageType::WIND:   return "Wind";
        default:                 return "";
    }
}

#endif
