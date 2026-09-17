#include "Card.h"
#include <algorithm>
#include <vector>
#include <sstream>

// Legendary is checked first because Bloodlust carries BOTH superRare and
// legendary; the old ladder never tested legendary at all, so it was quietly
// getting the super-rare number.
//
// Rare and Super Rare both sit at 2.0 on purpose. No rare-tier Strength card
// exists today, so nothing is indistinguishable in practice, but a new one
// would need its own step here.
int Card::healAmount(int value, int current, int maxHp) {
    if (maxHp <= 0) return 0;
    const int floorHp = maxHp * value / 100;      // "heal up to here"
    const int topUp   = maxHp * value * 2 / 500;  // two fifths of the floor
    int healed = floorHp - current;
    if (healed < topUp) healed = topUp;           // never worse than the top-up
    if (healed > maxHp - current) healed = maxHp - current;
    return healed < 0 ? 0 : healed;
}

int Card::minCost() const {
    // Legendaries first: you only ever see one or two in a run, so letting them
    // reach 1 is part of the payoff rather than a balance hole.
    // Stun Strike is the exception to the exception. A hard stun takes a whole
    // turn away from the enemy, which is worth more than any amount of damage,
    // so it never gets cheap enough to chain.
    if (getBaseName() == "Stun Strike") return 3;
    // A 25% heal for one energy was the best rate in the game.
    if (getBaseName() == "Heal") return 2;
    if (legendary) return 1;
    // Super Rare is the game's own marker for a standout card.
    if (superRare) return 2;
    // Plus two effects that are too good at 1 whatever their rarity: hitting
    // twice, and defence that outlives the turn it was played on.
    if (effect == CardEffect::DOUBLE_HIT || effect == CardEffect::TRUE_DOUBLE) return 2;
    if (effect == CardEffect::FORTIFY    || effect == CardEffect::WARD)        return 2;
    return 1;
}

double Card::strengthMultiplier() const {
    if (legendary) return 4.0;
    if (superRare) return 2.0;
    if (rare)      return 2.0;
    return 1.2;
}

CardEffect Card::effectFromString(const std::string& s) {
    if (s == "POISON")       return CardEffect::POISON;
    if (s == "BURN")         return CardEffect::BURN;
    if (s == "REND")         return CardEffect::REND;
    if (s == "STUN")         return CardEffect::STUN;
    if (s == "WEAK")         return CardEffect::WEAK;
    if (s == "COUNTER")      return CardEffect::COUNTER;
    if (s == "PARRY")        return CardEffect::PARRY;
    if (s == "PIERCE")       return CardEffect::PIERCE;
    if (s == "FORTIFY")      return CardEffect::FORTIFY;
    if (s == "STRENGTH")     return CardEffect::STRENGTH;
    if (s == "DOUBLE_HIT")   return CardEffect::DOUBLE_HIT;
    if (s == "IMPAIR")       return CardEffect::IMPAIR;
    if (s == "CHIP")         return CardEffect::CHIP;
    if (s == "HEAL")         return CardEffect::HEAL;
    if (s == "WARD")         return CardEffect::WARD;
    if (s == "TAUNT")        return CardEffect::TAUNT;
    if (s == "FEAR")         return CardEffect::FEAR;
    if (s == "TRUESTRIKE")   return CardEffect::TRUESTRIKE;
    if (s == "TRUE_DOUBLE")  return CardEffect::TRUE_DOUBLE;
    if (s == "SCRAP")          return CardEffect::SCRAP;
    if (s == "RECKLESS")       return CardEffect::RECKLESS;
    if (s == "SELFWEAK")       return CardEffect::SELFWEAK;
    if (s == "OVEREXTEND")     return CardEffect::OVEREXTEND;
    if (s == "BLOODPRICE")     return CardEffect::BLOODPRICE;
    if (s == "WILDCHARGE")     return CardEffect::WILDCHARGE;
    if (s == "BERSERK")        return CardEffect::BERSERK;
    if (s == "TURTLE")         return CardEffect::TURTLE;
    if (s == "EMBERBLADE")     return CardEffect::EMBERBLADE;
    if (s == "ADRENALINE")     return CardEffect::ADRENALINE;
    if (s == "SHATTERPOINT")   return CardEffect::SHATTERPOINT;
    if (s == "BLOODPACT")      return CardEffect::BLOODPACT;
    if (s == "UNSTABLEWARD")   return CardEffect::UNSTABLEWARD;
    if (s == "ALLIN")          return CardEffect::ALLIN;
    if (s == "LASTSTAND")      return CardEffect::LASTSTAND;
    if (s == "BORROWED")       return CardEffect::BORROWED;
    if (s == "PACTRUIN")       return CardEffect::PACTRUIN;
    if (s == "SACRIFICE")      return CardEffect::SACRIFICE;
    return CardEffect::NONE;
}

DamageType Card::damageTypeFromString(const std::string& s) {
    if (s == "SMASH")  return DamageType::SMASH;
    if (s == "PIERCE") return DamageType::PIERCE;
    if (s == "FIRE")   return DamageType::FIRE;
    if (s == "POISON") return DamageType::POISON;
    if (s == "WIND")   return DamageType::WIND;
    return DamageType::NONE;
}

Card::Card(std::string n, std::string desc, CardType t, int c, int v, CardEffect e, bool isRare,
           DamageType physT, DamageType elemT, bool isSuperRare, DamageType physT2, bool isLegendary, int savedUpgradeCount)
    : name(n), description(desc), type(t), effect(e), cost(c), value(v), upgradeCount(savedUpgradeCount), rare(isRare),
      superRare(isSuperRare), legendary(isLegendary), physType(physT), physType2(physT2), elemType(elemT) {}

std::string Card::getName() const {
    return name;
}

std::string Card::getDescription() const {
    return description;
}

CardType Card::getType() const {
    return type;
}

std::string Card::getTypeString() const {
    switch(type) {
        case CardType::ATTACK: return "ATTACK";
        case CardType::DEFEND: return "DEFEND";
        case CardType::SPECIAL: return "SPECIAL";
        default: return "UNKNOWN";
    }
}

int Card::getCost() const {
    return cost;
}

int Card::getValue() const {
    return value;
}

CardEffect Card::getEffect() const {
    return effect;
}

int Card::getUpgradeCount() const {
    return upgradeCount;
}

bool Card::isRare() const {
    return rare;
}

bool Card::isSuperRare() const {
    return superRare;
}

bool Card::isLegendary() const {
    return legendary;
}




bool Card::hasDrawback() const {
    switch (effect) {
        case CardEffect::SCRAP:        case CardEffect::RECKLESS:
        case CardEffect::SELFWEAK:     case CardEffect::OVEREXTEND:
        case CardEffect::BLOODPRICE:   case CardEffect::WILDCHARGE:
        case CardEffect::BERSERK:      case CardEffect::TURTLE:
        case CardEffect::EMBERBLADE:   case CardEffect::ADRENALINE:
        case CardEffect::SHATTERPOINT: case CardEffect::BLOODPACT:
        case CardEffect::UNSTABLEWARD: case CardEffect::ALLIN:
        case CardEffect::LASTSTAND:    case CardEffect::BORROWED:
        case CardEffect::PACTRUIN:     case CardEffect::SACRIFICE:
        case CardEffect::COUNTER:      // Dodge Reversal lets a quarter through
            return true;
        default:
            return false;
    }
}

static const std::vector<std::string>& starterCardNames() {
    // Name-keyed, so a rename here is not cosmetic: isStarter() drives the
    // upgrade cap and the white name tint.
    static const std::vector<std::string> names = {"Quick Jab", "Slash", "Bash", "Lunge",
                                                   "Scrap Shield", "Brace", "Parry"};
    return names;
}

bool Card::isStarter() const {
    std::string base = getBaseName();
    for (const auto& n : starterCardNames())
        if (base == n) return true;
    return false;
}

DamageType Card::getPhysType() const {
    return physType;
}

DamageType Card::getPhysType2() const {
    return physType2;
}

DamageType Card::getElemType() const {
    return elemType;
}

std::string Card::getTypeTag() const {
    std::string tag;
    if (physType  != DamageType::NONE) tag += std::string("[") + damageTypeName(physType) + "]";
    if (physType2 != DamageType::NONE) tag += std::string("[") + damageTypeName(physType2) + "]";
    if (elemType  != DamageType::NONE) tag += std::string("[") + damageTypeName(elemType) + "]";
    return tag;
}

std::string Card::getBaseName() const {
    std::string base = name;
    while (!base.empty() && base.back() == '+') base.pop_back();
    return base;
}

int Card::getMaxUpgrades() const {
    // Weaken/Stun cards aren't upgradable at all - Weak's duration is already
    // generous, and Stun is fixed at 1 turn regardless of value, so there'd be
    // nothing upgrading them actually improves. Same logic for a pure-buff
    // SPECIAL Strength card (its multiplier is fixed by rarity, not value) -
    // but NOT for an ATTACK card like Bloodlust, where value still raises damage.
    if (effect == CardEffect::WEAK || effect == CardEffect::STUN
        || effect == CardEffect::TAUNT || effect == CardEffect::FEAR) return 0;
    if (effect == CardEffect::STRENGTH && type == CardType::SPECIAL) return 0;
    // Starters are not upgradable: the forge is for cards you chose to build
    // around, and grinding a Strike upward gave every deck the same floor.
    // Everything you actually picked up still upgrades.
    // Uncommon 2; Rare 3; Super Rare 4; Legendary 5.
    if (isStarter()) return 0;
    if (legendary)   return 5;
    if (superRare)   return 4;
    if (rare)        return 3;
    return 2;
}

void Card::upgrade() {
    // The step scales with rarity: at a flat +3 a maxed uncommon reached 1
    // energy for 18 damage, which is a printed rare at a third of the cost.
    // 2/3/4/5 puts the ceilings at 16/28/46/55, one tier under the next.
    const int step = legendary ? 5 : superRare ? 4 : rare ? 3 : 2;
    // Heals are a percentage of max HP, so the same step runs away with them.
    value = (effect == CardEffect::HEAL) ? std::min(70, value + 2) : value + step;
    // Cost still drops, but only to minCost(). A flat floor of 1 let every strong
    // card bottom out there, and a fully upgraded deck then bought extra ACTIONS
    // per turn on top of bigger numbers - five plays against the enemy's one.
    // Commons still reach 1; rare and above stop at 2.
    if (cost > minCost()) cost--;
    name += "+";
    upgradeCount++;
    // keep description text in sync with the new value
    // Any ATTACK carrying an elemental tag has a 10% on-hit chance to leave its
    // matching status behind (Game::playCardFromHand). The rebuild below flattens
    // attack text to "Deal N damage.", so without re-appending this here the note
    // would survive in cards.json and then vanish on the first upgrade.
    auto elementalNote = [](DamageType e) -> const char* {
        switch (e) {
            case DamageType::FIRE:   return " 10% chance on hit to also apply Burn 3: 4 damage a turn for 2 turns.";
            case DamageType::POISON: return " 10% chance on hit to also apply Poison 3: 2 damage a turn for 6 turns.";
            case DamageType::WIND:   return " 10% chance on hit to also apply Rend 3: 3 damage the next 3 times it attacks.";
            default:                 return "";
        }
    };

    if (type == CardType::ATTACK) {
        if (effect == CardEffect::DOUBLE_HIT)
            description = "Deal " + std::to_string(value) + " damage twice (" + std::to_string(value * 2) + " total).";
        else if (effect == CardEffect::PIERCE)
            description = "Deal " + std::to_string(value) + " damage: ignoring enemy defense.";
        else if (effect == CardEffect::TRUESTRIKE)
            description = "Deal " + std::to_string(value) + " damage that nothing reduces. Ignores armor, "
                          "defense, resistance, and any stance or phase the enemy is hiding behind.";
        else if (effect == CardEffect::TRUE_DOUBLE)
            description = "Strike twice for " + std::to_string(value) + " damage each, "
                          + std::to_string(value * 2) + " total, and nothing reduces either hit. "
                          "Ignores armor, defense, resistance, and any stance or phase.";
        else if (effect == CardEffect::STRENGTH) {
            double buff = strengthMultiplier();
            std::ostringstream buffStr;
            buffStr << buff;
            description = "Deal " + std::to_string(value) + " damage. Gain x" + buffStr.str() + " damage for 2 turns.";
        }
        else if (effect == CardEffect::RECKLESS)
            description = "Deal " + std::to_string(value) + " damage. Your cards deal 2 less next turn.";
        else if (effect == CardEffect::OVEREXTEND)
            description = "Deal " + std::to_string(value) + " damage, ignoring enemy defense. "
                          "You draw one fewer card next turn.";
        else if (effect == CardEffect::WILDCHARGE)
            description = "Deal " + std::to_string(value) + " damage. You lose all your armor.";
        else if (effect == CardEffect::EMBERBLADE)
            description = "Deal " + std::to_string(value) + " damage and apply 4 Burn. You gain 2 Burn.";
        else if (effect == CardEffect::SHATTERPOINT)
            description = "Deal " + std::to_string(value) + " damage. You may play only one more card this turn.";
        else if (effect == CardEffect::ALLIN)
            description = "Deal twice your current armor in damage, then lose all of it. "
                          "You are Weakened for 2 turns.";
        else if (effect == CardEffect::PACTRUIN)
            description = "Deal " + std::to_string(value) + " damage. For the rest of the encounter every "
                          "attack you play also applies 3 Burn and 2 Rend, you cannot heal, and every card "
                          "you play costs you 6 HP.";
        else if (effect == CardEffect::RECKLESS)
            description = "Deal " + std::to_string(value) + " damage. Your cards deal 2 less next turn.";
        else if (effect == CardEffect::OVEREXTEND)
            description = "Deal " + std::to_string(value) + " damage, ignoring enemy defense. "
                          "You draw one fewer card next turn.";
        else if (effect == CardEffect::WILDCHARGE)
            description = "Deal " + std::to_string(value) + " damage. You lose all your armor.";
        else if (effect == CardEffect::EMBERBLADE)
            description = "Deal " + std::to_string(value) + " damage and apply 4 Burn. You gain 2 Burn.";
        else if (effect == CardEffect::SHATTERPOINT)
            description = "Deal " + std::to_string(value) + " damage. You may play only one more card this turn.";
        else if (effect == CardEffect::ALLIN)
            description = "Deal twice your current armor in damage, then lose all of it. "
                          "You are Weakened for 2 turns.";
        else if (effect == CardEffect::PACTRUIN)
            description = "Deal " + std::to_string(value) + " damage. For the rest of the encounter every "
                          "attack you play also applies 3 Burn and 2 Rend, you cannot heal, and every card "
                          "you play costs you 6 HP.";
        else if (effect == CardEffect::RECKLESS)
            description = "Deal " + std::to_string(value) + " damage. Your cards deal 2 less next turn.";
        else if (effect == CardEffect::OVEREXTEND)
            description = "Deal " + std::to_string(value) + " damage, ignoring enemy defense. "
                          "You draw one fewer card next turn.";
        else if (effect == CardEffect::WILDCHARGE)
            description = "Deal " + std::to_string(value) + " damage. You lose all your armor.";
        else if (effect == CardEffect::EMBERBLADE)
            description = "Deal " + std::to_string(value) + " damage and apply 4 Burn. You gain 2 Burn.";
        else if (effect == CardEffect::SHATTERPOINT)
            description = "Deal " + std::to_string(value) + " damage. You may play only one more card this turn.";
        else if (effect == CardEffect::ALLIN)
            description = "Deal twice your current armor in damage, then lose all of it. "
                          "You are Weakened for 2 turns.";
        else if (effect == CardEffect::PACTRUIN)
            description = "Deal " + std::to_string(value) + " damage. For the rest of the encounter every "
                          "attack you play also applies 3 Burn and 2 Rend, you cannot heal, and every card "
                          "you play costs you 6 HP.";
        else if (effect == CardEffect::RECKLESS)
            description = "Deal " + std::to_string(value) + " damage. Your cards deal 2 less next turn.";
        else if (effect == CardEffect::OVEREXTEND)
            description = "Deal " + std::to_string(value) + " damage, ignoring enemy defense. "
                          "You draw one fewer card next turn.";
        else if (effect == CardEffect::WILDCHARGE)
            description = "Deal " + std::to_string(value) + " damage. You lose all your armor.";
        else if (effect == CardEffect::EMBERBLADE)
            description = "Deal " + std::to_string(value) + " damage and apply 4 Burn. You gain 2 Burn.";
        else if (effect == CardEffect::SHATTERPOINT)
            description = "Deal " + std::to_string(value) + " damage. You may play only one more card this turn.";
        else if (effect == CardEffect::ALLIN)
            description = "Deal twice your current armor in damage, then lose all of it. "
                          "You are Weakened for 2 turns.";
        else if (effect == CardEffect::PACTRUIN)
            description = "Deal " + std::to_string(value) + " damage. For the rest of the encounter every "
                          "attack you play also applies 3 Burn and 2 Rend, you cannot heal, and every card "
                          "you play costs you 6 HP.";
        else
            description = "Deal " + std::to_string(value) + " damage.";
        description += elementalNote(elemType);
    } else if (type == CardType::DEFEND) {
        if (effect == CardEffect::FORTIFY)
            description = "Gain " + std::to_string(value) + " armor that persists for 3 turns (until broken).";
        else if (effect == CardEffect::IMPAIR)
            description = "Gain " + std::to_string(value) + " armor. 50% chance to Weaken the enemy.";
        else if (effect == CardEffect::CHIP)
            description = "Gain " + std::to_string(value) + " armor. Deal 3 damage.";
        else if (effect == CardEffect::WARD)
            description = "Gain " + std::to_string(value) + " armor and ward yourself for 2 turns. "
                          "Every ailment the enemy would inflict (Poison, Burn, Weak or Stun) is blocked while it holds.";
        else if (effect == CardEffect::SCRAP)
            description = "Gain " + std::to_string(value) + " armor. You take 1 damage.";
        else if (effect == CardEffect::SELFWEAK)
            description = "Gain " + std::to_string(value) + " armor. Your attacks deal 10% less this turn.";
        else if (effect == CardEffect::TURTLE)
            description = "Gain " + std::to_string(value) + " armor that persists for 3 turns (until broken). "
                          "You are Weakened for 3 turns.";
        else if (effect == CardEffect::UNSTABLEWARD)
            description = "Gain " + std::to_string(value) + " armor and ward yourself for 5 turns against every "
                          "ailment. You draw two fewer cards next turn.";
        else if (effect == CardEffect::LASTSTAND)
            description = "Gain armor equal to the health you are missing, plus " + std::to_string(value)
                        + ", and it persists. You cannot heal for the rest of the encounter.";
        else
            description = "Gain " + std::to_string(value) + " armor.";
    } else if (type == CardType::SPECIAL) {
        if (effect == CardEffect::COUNTER)
            description = "Counter: reverses the enemy's next attack or ailment back at them, doubled, +" + std::to_string(value)
                        + ". Fizzles if they do neither.";
        else if (effect == CardEffect::PARRY) {
            // This used to collapse to one line on upgrade and silently drop
            // the stun, the armour threshold and the ranged caveat - so Parry+
            // told you less than Parry did. Both now say the same things, with
            // the value-derived numbers filled in.
            description = "Block the enemy's next attack and riposte for 1.5x their attack "
                          "plus " + std::to_string(value) + ", ignoring their defense, with a chance to stun them. "
                          "How big a blow you can catch is your armor plus " + std::to_string(value * 3) + ": "
                          "too heavy a hit breaks the guard. Ranged enemies are blocked but "
                          "stand too far away to riposte.";
        }
        // The tick is not the card value: poison pays half of it over six turns,
        // burn half again over two. Both formulas mirror StatusEffects::apply().
        else if (effect == CardEffect::POISON)
            description = "Apply " + std::to_string(value) + " Poison ("
                        + std::to_string((value + 1) / 2) + " dmg/turn for 6 turns).";
        else if (effect == CardEffect::BURN)
            description = "Apply " + std::to_string(value) + " Burn ("
                        + std::to_string(value + value / 2) + " dmg/turn for 2 turns).";
        else if (effect == CardEffect::REND)
            description = "Apply " + std::to_string(value) + " Rend ("
                        + std::to_string(value) + " damage the next 3 times the enemy attacks).";
        else if (effect == CardEffect::WEAK) {
            // Duration is always 3; the multiplier is set by rarity, not by value.
            const char* m = superRare ? "2" : rare ? "1.75" : "1.5";
            description = std::string("Weaken the enemy for 3 turns: their attacks land for ")
                        + m + "x less damage.";
        }
        else if (effect == CardEffect::HEAL)
            description = "Restore yourself to " + std::to_string(value)
                          + "% of maximum HP. If you are already above that, heal "
                          + std::to_string(value * 2 / 5) + "% instead.";
        else if (effect == CardEffect::STRENGTH) {
            double buff = strengthMultiplier();
            std::ostringstream buffStr;
            buffStr << buff;
            description = "Gain x" + buffStr.str() + " damage for 2 turns.";
        }
        // STUN is left as-is - duration no longer scales with value, only cost drops
    }
}
