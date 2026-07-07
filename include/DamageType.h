#ifndef DAMAGE_TYPE_H
#define DAMAGE_TYPE_H

// A card can carry a physical tag (SMASH/PIERCE) and/or an elemental tag
// (FIRE/POISON/WIND) independently, e.g. Whirlwind is PIERCE + WIND. Enemies
// have one weakness type; matching either tag against it grants bonus damage.
enum class DamageType {
    NONE,
    SMASH,
    PIERCE,
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
