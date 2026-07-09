#include "Game.h"
#include "Audio.h"
#include "Colors.h"
#include "UIHelper.h"
#include <algorithm>
#include <iostream>
#include <random>

Game::Game() : playerDeck(), enemy("Enemy", 50, 8, 4, EnemyType::MELEE), currentRun(), playerHealth(100), maxPlayerHealth(100), playerArmor(0), playerArmorPersistTurns(0), playerEnergy(3), maxEnergy(3), turnNumber(1), playerTurnActive(true), running(false), inEncounter(false), equipDamageBonus(0), equipArmorBonus(0), weaponTier(0), armorTier(0), counterAttackActive(false), parryActive(false), counterBonusValue(0), parryBonusValue(0) {}

// Elemental effects get a matching sound; everything else uses "special".
static const char* effectSoundName(CardEffect effect) {
    switch (effect) {
        case CardEffect::POISON: return "poison";
        case CardEffect::BURN:   return "fire";
        case CardEffect::STUN:   return "volt";
        case CardEffect::HEAL:   return "heal";
        default:                 return "special";
    }
}

// Card name tint by rarity; legendary gets bold gold instead of a pastel.
static const char* rarityTint(const Card& c) {
    if (c.isLegendary()) return Color::LEGENDARY_TINT;
    if (c.isStarter())   return Color::CARD_NAME;
    if (c.isSuperRare()) return Color::SUPER_RARE_TINT;
    if (c.isRare())      return Color::RARE_TINT;
    return Color::COMMON_TINT;
}

struct EquipTier { std::string name; int bonus; };

// Gear name/bonus escalates per tier claimed; the last tier repeats after that.
static EquipTier weaponTierAt(int tier) {
    static const std::vector<EquipTier> tiers = {
        {"Rusty Blade", 3}, {"Iron Sword", 4}, {"Steel Blade", 5},
        {"War Axe", 6}, {"Mythril Edge", 8}, {"Legendary Blade", 10}
    };
    int idx = std::min(tier, (int)tiers.size() - 1);
    return tiers[idx];
}
static EquipTier armorTierAt(int tier) {
    static const std::vector<EquipTier> tiers = {
        {"Iron Plating", 3}, {"Steel Plating", 4}, {"Chainmail", 5},
        {"Plate Armor", 6}, {"Mythril Plating", 8}, {"Legendary Aegis", 10}
    };
    int idx = std::min(tier, (int)tiers.size() - 1);
    return tiers[idx];
}

void Game::init() {
    // One of each starter card; duplicates only ever come from later card rewards.
    playerDeck.addCard(Card("Quick Jab", "Deal 3 damage", CardType::ATTACK, 0, 3));
    playerDeck.addCard(Card("Jab", "Deal 4 damage", CardType::ATTACK, 1, 4));
    playerDeck.addCard(Card("Bash", "Deal 6 damage", CardType::ATTACK, 2, 6, CardEffect::NONE, false, DamageType::SMASH));
    playerDeck.addCard(Card("Lunge", "Deal 6 damage", CardType::ATTACK, 2, 6, CardEffect::NONE, false, DamageType::PIERCE));
    playerDeck.addCard(Card("Defend", "Gain 8 armor", CardType::DEFEND, 1, 8));
    playerDeck.addCard(Card("Brace", "Gain 8 armor", CardType::DEFEND, 1, 8));
    playerDeck.addCard(Card("Parry",
        "Parries the attack, then riposte for 1.5x damage.",
        CardType::SPECIAL, 3, 3, CardEffect::PARRY));

    applyUpgrades();
    
    playerDeck.shuffle();
    
    running = true;
}

void Game::displayStatus() const {
    UIHelper::printCombatStatus(playerHealth, maxPlayerHealth, playerArmor, playerEnergy, maxEnergy,
                                 enemy.getName(), enemy.getHealth(), enemy.getMaxHealth(), 
                                 enemy.getArmor(), enemy.getBaseAttack(), enemy.getBaseDefense());
    
    playerStatus.display("  YOU:   ");
    enemy.displayStatusEffects("  ENEMY: ");
    if (playerArmorPersistTurns > 0)
        std::cout << "  " << Color::CYAN << "[Fortified armor - " << playerArmorPersistTurns << " turns left]" << Color::RESET << "\n";

    // show bonuses only when they exist
    int totalDmg = upgrades.getDamageBonus() + equipDamageBonus;
    int totalArm = upgrades.getArmorBonus()  + equipArmorBonus;
    if (totalDmg > 0 || totalArm > 0) {
        std::cout << "BONUSES: ";
        if (totalDmg > 0) std::cout << Color::CARD_ATTACK << "Damage +" << totalDmg << Color::RESET << " ";
        if (totalArm > 0) std::cout << Color::ARMOR_CLR   << "Armor +"  << totalArm  << Color::RESET << " ";
        std::cout << "\n";
    }

}

void Game::displayEnemyInfo() const {
    int atk = enemy.getBaseAttack();
    int def = enemy.getBaseDefense(); // used below for move-estimate formulas, not the live display

    std::cout << "\n" << Color::BOLD << Color::RED << enemy.getName() << Color::RESET << "\n";
    std::cout << "  HP:  " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
              << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << "\n";
    // ARM and DEF are both flat damage reduction, so they're shown as one combined
    // DEF figure - a "defend" action just temporarily raises this number.
    std::cout << "  ATK: " << Color::RED << atk << Color::RESET
               << "  | DEF: " << Color::BLUE << (def + enemy.getArmor()) << Color::RESET
               << " (flat damage reduction on every hit you land)\n";
    if (enemy.getWeakness() != DamageType::NONE)
        std::cout << "  " << Color::YELLOW << "Weakness: " << enemy.getWeaknessLabel()
                   << " (+50% dmg from matching attacks)" << Color::RESET << "\n";
    if (enemy.getResistance() != DamageType::NONE)
        std::cout << "  " << Color::DIM << "Resistant: " << enemy.getResistanceLabel()
                   << " (-50% dmg from matching attacks)" << Color::RESET << "\n";
    enemy.displayStatusEffects("  ");

    std::cout << "\n" << Color::DIM << "Possible moves:" << Color::RESET << "\n";

    if (enemy.isBoss()) {
        switch (enemy.getBossType()) {
            case BossType::STONE_COLOSSUS:
                std::cout << "  " << Color::RED    << "Earthquake Slam" << Color::RESET << " (rare)    - hits you twice, ignores armor\n";
                std::cout << "  " << Color::ARMOR_CLR << "Fortify"      << Color::RESET << " (common)  - gains a lot of armor\n";
                std::cout << "  " << Color::RED    << "Crush"           << Color::RESET << " (common)  - heavy melee strike\n";
                break;
            case BossType::VILE_WITCH:
                std::cout << "  " << Color::CARD_SPECIAL << "Poison Cloud" << Color::RESET << " (common)  - poisons you heavily\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Hex"          << Color::RESET << " (common)  - burns you\n";
                std::cout << "  " << Color::RED         << "Strike"       << Color::RESET << " (uncommon)- direct attack\n";
                break;
            case BossType::WARLORD:
                std::cout << "  " << Color::STUN_CLR << "Thunderstrike" << Color::RESET << " (rare)    - stuns you, then attacks\n";
                std::cout << "  " << Color::CARD_SPECIAL << "War Cry"   << Color::RESET << " (uncommon)- weakens you, then attacks\n";
                std::cout << "  " << Color::RED      << "Heavy Strike"  << Color::RESET << " (common)  - attacks, grows stronger each turn\n";
                break;
            case BossType::HYDRA:
                std::cout << "  " << Color::CARD_SPECIAL << "Venomous Bite" << Color::RESET << " (common)  - poisons you heavily\n";
                std::cout << "  " << Color::RED         << "Twin Strike"   << Color::RESET << " (common)  - hits you twice\n";
                std::cout << "  " << Color::HEAL        << "Regrowth"      << Color::RESET << " (uncommon)- regrows a head, heals HP\n";
                std::cout << "  " << Color::RED         << "Bite"          << Color::RESET << " (uncommon)- direct attack\n";
                break;
            case BossType::DRAGON:
                std::cout << "  " << Color::CARD_SPECIAL << "Fire Breath" << Color::RESET << " (common)  - burns you heavily\n";
                std::cout << "  " << Color::RED       << "Claw Rake"   << Color::RESET << " (uncommon)- heavy strike, ignores armor\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Wing Buffet" << Color::RESET << " (uncommon)- knocks you off balance, weakens you\n";
                std::cout << "  " << Color::RED         << "Claw"        << Color::RESET << " (uncommon)- direct attack\n";
                break;
            default: break;
        }
    } else {
        switch (enemy.getType()) {
            case EnemyType::MELEE:
                std::cout << "  " << Color::RED     << "Attack"  << Color::RESET << " (likely)   - deals ~" << std::max(0, atk - def) << " dmg (reduced by your armor)\n";
                std::cout << "  " << Color::ARMOR_CLR << "Defend" << Color::RESET << " (rarely)   - gains " << def << " armor\n";
                break;
            case EnemyType::RANGED:
                std::cout << "  " << Color::RED      << "Pierce attack"   << Color::RESET << " (likely)     - deals ~" << std::max(0, atk - def) << " dmg, bypasses half your armor\n";
                std::cout << "  " << Color::ARMOR_CLR << "Defend"         << Color::RESET << " (sometimes)  - gains " << std::max(1, def - 1) << " armor\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Crippling shot" << Color::RESET << " (rarely)     - weakens you (-2 dmg for 2 turns)\n";
                break;
            case EnemyType::TANK:
                std::cout << "  " << Color::ARMOR_CLR << "Defend" << Color::RESET << " (likely)   - gains " << def << " armor\n";
                std::cout << "  " << Color::RED       << "Attack" << Color::RESET << " (sometimes)- deals ~" << std::max(0, std::max(1, atk - 2) - def) << " dmg (reduced by your armor)\n";
                break;
            case EnemyType::CASTER:
                std::cout << "  " << Color::CARD_SPECIAL << "Poison Bolt" << Color::RESET << " (sometimes) - poisons you (3 stacks)\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Fireball"    << Color::RESET << " (sometimes) - burns you (2 turns)\n";
                std::cout << "  " << Color::RED         << "Attack"      << Color::RESET << " (sometimes) - deals ~" << std::max(0, atk + 1 - def) << " dmg\n";
                if (enemy.getHealth() < enemy.getMaxHealth() / 3)
                    std::cout << "  " << Color::HEAL    << "Heal"         << Color::RESET << " (likely, low HP!) - recovers ~" << (8 + def / 2) << " HP\n";
                else
                    std::cout << "  " << Color::DIM     << "[May cast Heal if HP drops below 33%]" << Color::RESET << "\n";
                break;
            case EnemyType::BEAST:
                std::cout << "  " << Color::RED       << "Attack"         << Color::RESET << " (likely)   - deals ~" << std::max(0, atk - def) << " dmg (reduced by your armor)\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Venomous bite"  << Color::RESET << " (sometimes)- poisons you (3 stacks)\n";
                std::cout << "  " << Color::ARMOR_CLR  << "Defend"         << Color::RESET << " (rarely)   - gains " << std::max(1, def - 1) << " armor\n";
                break;
            case EnemyType::UNDEAD:
                std::cout << "  " << Color::RED      << "Attack"         << Color::RESET << " (likely)   - deals ~" << std::max(0, atk - def) << " dmg (reduced by your armor)\n";
                std::cout << "  " << Color::CARD_SPECIAL << "Chilling touch" << Color::RESET << " (sometimes)- weakens you (-2 dmg for 2 turns)\n";
                break;
        }
    }
    std::cout << "\n";
}

int Game::calculateDamage(int attackValue, int defenseValue) const {
    int damage = attackValue - defenseValue;
    return (damage < 0) ? 0 : damage;
}

bool Game::spendEnergy(int cost) {
    if (playerEnergy < cost) {
        std::cout << Color::DAMAGE << "Not enough plays! Need " << cost
                  << " but only have " << playerEnergy << " remaining." << Color::RESET << "\n";
        return false;
    }
    playerEnergy -= cost;
    return true;
}

void Game::resetEnergy() {
    playerEnergy = maxEnergy;
}

void Game::applyCardEffect(const Card& card) {
    int val = card.getValue();
    switch (card.getEffect()) {
        case CardEffect::POISON:
            enemy.applyStatus(StatusType::POISON, val);
            std::cout << "  " << Color::POISON_CLR << "Applied " << val << " Poison to enemy! (" << val << " dmg/turn for 3 turns)" << Color::RESET << "\n";
            break;
        case CardEffect::BURN:
            enemy.applyStatus(StatusType::BURN, val);
            std::cout << "  " << Color::BURN_CLR << "Applied " << val << " Burn to enemy! (" << val << " dmg/turn for 3 turns)" << Color::RESET << "\n";
            break;
        case CardEffect::STUN:
            if (enemy.tryApplyStun())
                std::cout << "  " << Color::STUN_CLR << "Enemy is STUNNED - they'll lose their next turn!" << Color::RESET << "\n";
            else
                std::cout << "  " << Color::DIM << "The boss resists the stun!" << Color::RESET << "\n";
            break;
        case CardEffect::WEAK: {
            // Multiplier scales with rarity instead of value (duration is fixed at
            // 3 turns regardless): Uncommon 1.5x, Rare 1.75x, Super Rare 2x.
            double weakMult = card.isSuperRare() ? 2.0 : card.isRare() ? 1.75 : 1.5;
            enemy.applyStatus(StatusType::WEAK, 3, weakMult);
            std::cout << "  " << Color::WEAK_CLR << "Applied Weak to enemy for 3 turns! (deals " << weakMult << "x less damage)" << Color::RESET << "\n";
            break;
        }
        case CardEffect::COUNTER:
            counterAttackActive = true;
            counterBonusValue = val;
            std::cout << "  " << Color::CYAN << "You brace for a counterattack. If they strike, you hit back for double"
                      << (val > 0 ? (" +" + std::to_string(val)) : std::string()) << "." << Color::RESET << "\n";
            break;
        case CardEffect::PARRY:
            parryActive = true;
            parryBonusValue = val;
            std::cout << "  " << Color::CYAN << "You raise your guard. If they attack, you'll parry"
                      << (val > 0 ? (" (+" + std::to_string(val) + " riposte)") : std::string()) << " and stun them." << Color::RESET << "\n";
            break;
        case CardEffect::HEAL: {
            int before = playerHealth;
            playerHealth = std::min(maxPlayerHealth, playerHealth + val);
            std::cout << "  " << Color::HEAL << "Recovered " << (playerHealth - before) << " HP!" << Color::RESET
                      << " (" << playerHealth << "/" << maxPlayerHealth << ")\n";
            break;
        }
        case CardEffect::STRENGTH: {
            // Same rarity scaling as Bloodlust's inline version (Uncommon 1.2x, Rare 2x,
            // Super Rare 3x) - this path is for pure-buff SPECIAL cards like Strengthen.
            double buff = card.isSuperRare() ? 3.0 : card.isRare() ? 2.0 : 1.2;
            playerStatus.apply(StatusType::STRENGTH, 2, 1.5, buff);
            std::cout << "  " << Color::STRENGTH_CLR << "Strength surges - x" << buff << " damage for 2 turns!" << Color::RESET << "\n";
            break;
        }
        default:
            break;
    }
}

// Ailments the enemy inflicts on the player go through here instead of straight to
// playerStatus.apply() so Dodge/Status Guard can intercept. STRENGTH is the
// player's own self-buff, never routed through here, so it's not interceptable.
void Game::applyPlayerStatus(StatusType type, int amount, double weakMultiplier) {
    // Dodge is the ultimate stance: it reverses ANY player-targeted effect, not
    // just direct attacks (that part is handled in doAttack()). The reversed
    // effect hits the enemy at double strength, plus Dodge's own bonus on top -
    // e.g. a 4-dmg Poison becomes an 8-dmg-plus-bonus Poison on the enemy instead.
    if (counterAttackActive) {
        counterAttackActive = false;
        int reflectedAmount = amount * 2 + counterBonusValue;
        enemy.applyStatus(type, reflectedAmount, weakMultiplier);
        Audio::playSFX("special");
        std::cout << "  " << Color::GREEN << "Dodge! You reverse the effect back onto the enemy - doubled, +"
                  << counterBonusValue << "!" << Color::RESET << "\n";
        return;
    }
    if (statusWardActive) {
        statusWardActive = false;
        std::cout << "  " << Color::CYAN << "Status Guard blocks the ailment!" << Color::RESET << "\n";
        return;
    }
    playerStatus.apply(type, amount, weakMultiplier);
}

void Game::playCardFromHand(int index) {
    try {
        const Card& card = playerDeck.getCardFromHand(index - 1);

        if (playerDeck.isCardUsed(index - 1)) {
            std::cout << Color::DIM << "Card " << index << " already played this turn." << Color::RESET << "\n";
            return;
        }
        if (!spendEnergy(card.getCost())) return;

        Card playedCard = playerDeck.playCard(index - 1);

        lastActionWasCardPlay = true;
        lastPlayedCardType = playedCard.getType();
        lastPlayedPhysType = playedCard.getPhysType();
        lastPlayedPhysType2 = playedCard.getPhysType2();

        std::cout << "Played: [" << playedCard.getName() << "] (Cost: " << playedCard.getCost() << ")\n";

        if (playedCard.getType() == CardType::ATTACK) {
            bool pierce         = (playedCard.getEffect() == CardEffect::PIERCE);
            bool doubleHit      = (playedCard.getEffect() == CardEffect::DOUBLE_HIT);
            int  hits           = doubleHit ? 2 : 1;
            double weakMult     = playerStatus.getWeakMultiplier();
            double strengthMult = playerStatus.getStrengthMultiplier();

            DamageType weakness = enemy.getWeakness();
            bool hitsWeakness = weakness != DamageType::NONE &&
                                (playedCard.getPhysType() == weakness || playedCard.getPhysType2() == weakness
                                 || playedCard.getElemType() == weakness);
            DamageType resistance = enemy.getResistance();
            bool hitsResistance = resistance != DamageType::NONE &&
                                  (playedCard.getPhysType() == resistance || playedCard.getPhysType2() == resistance
                                   || playedCard.getElemType() == resistance);

            for (int hitNum = 1; hitNum <= hits && enemy.isAlive(); hitNum++) {
                int bonusDamage  = std::max(0, playedCard.getValue() + upgrades.getDamageBonus() + equipDamageBonus);
                bonusDamage = (int)(bonusDamage * weakMult * strengthMult);
                if (hitsWeakness) bonusDamage = (int)(bonusDamage * 1.5);
                if (hitsResistance) bonusDamage = (int)(bonusDamage * 0.5);
                int defenseValue = pierce ? 0 : enemy.getBaseDefense();
                int damageDealt  = calculateDamage(bonusDamage, defenseValue);
                int hpBefore = enemy.getHealth();
                enemy.takeDamage(damageDealt);
                int hpLost = hpBefore - enemy.getHealth();
                int armorBlocked = damageDealt - hpLost;
                Audio::playSFX(!enemy.isAlive() ? "dead" : "attack");
                std::cout << "  " << Color::PLAYER_ATTACK
                          << (hits > 1 ? ("Hit " + std::to_string(hitNum) + ": dealt ") : "Dealt ")
                          << hpLost << " damage to enemy!"
                          << Color::RESET << " (Enemy HP: "
                          << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")";
                if (hitsWeakness)
                    std::cout << " " << Color::YELLOW << "[Weakness! x1.5]" << Color::RESET;
                if (hitsResistance)
                    std::cout << " " << Color::DIM << "[Resisted x0.5]" << Color::RESET;
                if (weakMult < 1.0)
                    std::cout << " " << Color::WEAK_CLR << "[Weakened]" << Color::RESET;
                if (strengthMult > 1.0)
                    std::cout << " " << Color::STRENGTH_CLR << "[Strength x" << strengthMult << "]" << Color::RESET;
                if (pierce)
                    std::cout << " " << Color::MAGENTA << "[Armor-Piercing]" << Color::RESET;
                if (armorBlocked > 0)
                    std::cout << " " << Color::ARMOR_CLR << "[" << armorBlocked << " blocked by armor]" << Color::RESET;
                std::cout << "\n";
                // let each hit's sound finish before the next one cuts in
                if (doubleHit && hitNum < hits) UIHelper::pause(300);
            }

            if (playedCard.getEffect() == CardEffect::STRENGTH) {
                // Scales with rarity, same idea as Weaken/Shatter/Sunder: Uncommon 1.2x,
                // Rare 2x, Super Rare 3x (only Strengthen and Bloodlust use this today).
                double strengthBuff = playedCard.isSuperRare() ? 3.0 : playedCard.isRare() ? 2.0 : 1.2;
                playerStatus.apply(StatusType::STRENGTH, 2, 1.5, strengthBuff);
                std::cout << "  " << Color::STRENGTH_CLR << "Strength surges - x" << strengthBuff << " damage for 2 turns!" << Color::RESET << "\n";
            }
        } else if (playedCard.getType() == CardType::DEFEND) {
            int bonusArmor = playedCard.getValue() + upgrades.getArmorBonus() + equipArmorBonus;
            playerArmor += bonusArmor;
            Audio::playSFX("defend");
            std::cout << "  " << Color::ARMOR_CLR << "Gained " << bonusArmor << " armor!"
                      << Color::RESET << " (Total: " << Color::ARMOR_CLR << playerArmor << Color::RESET << ")\n";

            if (playedCard.getEffect() == CardEffect::FORTIFY) {
                playerArmorPersistTurns = 3;
                std::cout << "  " << Color::CYAN << "Fortified! This armor won't fade for 3 turns (until broken)." << Color::RESET << "\n";
            }
            if (playedCard.getEffect() == CardEffect::IMPAIR) {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> coinFlip(0, 1);
                if (coinFlip(gen) == 0) {
                    enemy.applyStatus(StatusType::WEAK, 2);
                    std::cout << "  " << Color::WEAK_CLR << "The impact staggers the enemy - Weakened for 2 turns!" << Color::RESET << "\n";
                } else {
                    std::cout << "  " << Color::DIM << "(No impair this time.)" << Color::RESET << "\n";
                }
            }
            if (playedCard.getEffect() == CardEffect::CHIP) {
                int chipDmg = 3;
                int hpBefore = enemy.getHealth();
                enemy.takeDamageRaw(chipDmg);
                int hpLost = hpBefore - enemy.getHealth();
                Audio::playSFX(!enemy.isAlive() ? "dead" : "hit");
                std::cout << "  " << Color::PLAYER_ATTACK << "The shield's edge bites - dealt " << hpLost << " damage!"
                          << Color::RESET << " (Enemy HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
            }
            if (playedCard.getEffect() == CardEffect::WARD) {
                statusWardActive = true;
                std::cout << "  " << Color::CYAN << "Warded! The next ailment the enemy inflicts on you will be blocked." << Color::RESET << "\n";
            }
        } else if (playedCard.getType() == CardType::SPECIAL) {
            Audio::playSFX(effectSoundName(playedCard.getEffect()));
            applyCardEffect(playedCard);
        }
        if (playerEnergy > 0)
            std::cout << Color::DIM << "  (" << playerEnergy << " play" << (playerEnergy != 1 ? "s" : "") << " left)" << Color::RESET << "\n";
    } catch (const std::out_of_range&) {
        std::cout << "Invalid card index!\n";
    }
}

void Game::enemyTurn() {
    if (!enemy.isAlive()) return;

    // Wipe armor from 2 turns ago (i.e. left over from the enemy's own last turn) - not
    // from this one. A Defend action below can grant fresh armor that then survives
    // through the player's entire next turn, which is the whole point of Defending.
    enemy.resetArmor();

    // Tick enemy status effects at the start of their turn. Poison/Burn are
    // elemental (Poison/Fire respectively), so they get the same weakness (+50%)
    // and resistance (-50%) treatment as a matching-tagged attack card would.
    int poisonDmg = enemy.processPoison();
    if (poisonDmg > 0) {
        if (enemy.getWeakness()   == DamageType::POISON) poisonDmg = (int)(poisonDmg * 1.5);
        if (enemy.getResistance() == DamageType::POISON) poisonDmg = (int)(poisonDmg * 0.5);
        enemy.takeDamageRaw(poisonDmg);
        std::cout << Color::POISON_CLR << "Poison:" << Color::RESET
                  << " enemy takes " << Color::PLAYER_ATTACK << poisonDmg << Color::RESET << " damage! ("
                  << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << " HP)\n";
        UIHelper::pause(250);
        if (!enemy.isAlive()) { Audio::playSFX("dead"); return; }
    }
    int burnDmg = enemy.processBurn();
    if (burnDmg > 0) {
        if (enemy.getWeakness()   == DamageType::FIRE) burnDmg = (int)(burnDmg * 1.5);
        if (enemy.getResistance() == DamageType::FIRE) burnDmg = (int)(burnDmg * 0.5);
        enemy.takeDamageRaw(burnDmg);
        std::cout << Color::BURN_CLR << "Burn:" << Color::RESET
                  << " enemy takes " << Color::PLAYER_ATTACK << burnDmg << Color::RESET << " damage! ("
                  << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                  << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << " HP)\n";
        UIHelper::pause(250);
        if (!enemy.isAlive()) { Audio::playSFX("dead"); return; }
    }
    if (enemy.processStun()) {
        UIHelper::typeWrite(std::string(Color::STUN_CLR) + "Enemy is STUNNED and loses their turn!" + Color::RESET + "\n");
        UIHelper::pause(400);
        return;
    }

    // Bosses have their own AI
    if (enemy.isBoss()) {
        bossAction();
        return;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> rollDist(0, 99);
    int roll = rollDist(gen);

    // Apply WEAK penalty to attack, then tick it
    double weakMult = enemy.getWeakMultiplier();
    enemy.processWeak();

    auto doAttack = [&](int atk, bool pierceHalfArmor) {
        atk = (int)(atk * weakMult);
        // Dodge fires before Parry when both are active (uncapped, higher priority)
        if (counterAttackActive) {
            counterAttackActive = false;
            int counterDmg = atk * 2 + counterBonusValue;
            int hpBefore = enemy.getHealth();
            enemy.takeDamage(counterDmg);
            int hpLost = hpBefore - enemy.getHealth();
            Audio::playSFX(!enemy.isAlive() ? "dead" : "attack");
            std::cout << Color::GREEN << "Dodge! You sidestep the attack and counter for " << hpLost << " damage!" << Color::RESET
                      << " (Enemy HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
            UIHelper::pause(200);
            return;
        }
        if (parryActive) {
            int parryCap = playerArmor + parryBonusValue * 3; // current armor + Parry's own bonus - stack armor first to parry bigger hits
            parryActive = false;
            if (atk <= parryCap) {
                // Regular Ranged enemies attack from a distance - you can still block
                // the hit, but you're too far away to riposte or stun them for it.
                // Bosses skip this even if typed RANGED (e.g. the Dragon's claws are melee-range).
                bool tooFarToRiposte = !enemy.isBoss() && enemy.getType() == EnemyType::RANGED;
                if (tooFarToRiposte) {
                    Audio::playSFX("special");
                    std::cout << Color::CYAN << "Parry! You block the shot - no damage taken, but they're too far away to riposte."
                              << Color::RESET << "\n";
                    UIHelper::pause(300);
                    return;
                }
                int riposteDmg = (int)(atk * 1.5) + parryBonusValue;
                int hpBefore = enemy.getHealth();
                enemy.takeDamage(riposteDmg); // ignores defense - takeDamage only accounts for armor
                int hpLost = hpBefore - enemy.getHealth();
                bool stunned = enemy.tryApplyStun();
                Audio::playSFX(!enemy.isAlive() ? "dead" : "special");
                std::cout << Color::CYAN << "Parry! You deflect the blow - no damage taken. Riposte for " << hpLost
                          << " damage!" << (stunned ? " Enemy is stunned!" : " Boss resists the stun!") << Color::RESET
                          << " (Enemy HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                UIHelper::pause(300);
                return;
            } else {
                std::cout << Color::BOLD << Color::RED << "The blow is too powerful to parry! Your guard breaks!" << Color::RESET << "\n";
                UIHelper::pause(250);
            }
        }
        int effectiveArmor = pierceHalfArmor ? (playerArmor / 2) : playerArmor;
        int actualDamage = atk - effectiveArmor;
        if (actualDamage < 0) actualDamage = 0;
        playerArmor -= (pierceHalfArmor ? atk / 2 : atk);
        if (playerArmor < 0) playerArmor = 0;
        playerHealth -= actualDamage;
        if (playerHealth < 0) playerHealth = 0;
        Audio::playSFX("hit");
        std::cout << Color::DAMAGE << "Enemy attacks for " << actualDamage << " damage!" << Color::RESET;
        if (weakMult < 1.0)
            std::cout << " " << Color::WEAK_CLR << "[Weakened]" << Color::RESET;
        std::cout << "  HP: " << hpColor(playerHealth, maxPlayerHealth)
                  << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
        UIHelper::pause(200);
    };

    auto doDefend = [&](int amt) {
        enemy.gainArmor(amt);
        bool fizzled = counterAttackActive || parryActive;
        counterAttackActive = false;
        parryActive = false;
        if (fizzled)
            std::cout << Color::ARMOR_CLR << "Enemy braces - +" << amt << " armor." << Color::RESET
                      << " " << Color::DIM << "(No attack - your stance fizzles.)" << Color::RESET << "\n";
        else
            std::cout << Color::ARMOR_CLR << "Enemy braces - +" << amt << " armor (absorbs incoming damage)." << Color::RESET << "\n";
        UIHelper::pause(150);
    };

    EnemyType t = enemy.getType();
    int atk = enemy.getBaseAttack();
    int def = enemy.getBaseDefense();

    switch (t) {
        case EnemyType::MELEE:
            if (roll < 70) doAttack(atk, false);
            else doDefend(def);
            break;
        case EnemyType::RANGED:
            if (roll < 60) {
                doAttack(atk, true); // pierce half armor
            } else if (roll < 80) {
                doDefend(std::max(1, def - 1));
            } else {
                applyPlayerStatus(StatusType::WEAK, 2);
                Audio::playSFX("special");
                std::cout << Color::WEAK_CLR << "Enemy fires a crippling shot! You are Weakened for 2 turns." << Color::RESET << "\n";
            }
            break;
        case EnemyType::TANK:
            if (roll < 65) doDefend(def); // was def+3, too much in early encounters
            else doAttack(std::max(1, atk - 2), false);
            break;
        case EnemyType::CASTER:
            if (enemy.getHealth() < enemy.getMaxHealth() / 3 && roll < 60) {
                int healAmt = 8 + (def / 2);
                enemy.heal(healAmt);
                std::cout << "Enemy casts heal and recovers " << healAmt << " HP! ("
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << ")\n";
            } else if (roll < 40) {
                applyPlayerStatus(StatusType::POISON, 3);
                Audio::playSFX("poison");
                std::cout << Color::POISON_CLR << "Enemy casts Poison Bolt! You are poisoned for 3 stacks." << Color::RESET << "\n";
            } else if (roll < 60) {
                applyPlayerStatus(StatusType::BURN, 2);
                Audio::playSFX("fire");
                std::cout << Color::BURN_CLR << "Enemy casts Fireball! You are burning for 2 turns." << Color::RESET << "\n";
            } else {
                doAttack(atk + 1, false);
            }
            break;
        case EnemyType::BEAST:
            if (roll < 60) {
                doAttack(atk, false);
            } else if (roll < 85) {
                applyPlayerStatus(StatusType::POISON, 3);
                Audio::playSFX("poison");
                std::cout << Color::POISON_CLR << "Enemy sinks its fangs in - venomous bite! You are poisoned for 3 stacks." << Color::RESET << "\n";
            } else {
                doDefend(std::max(1, def - 1));
            }
            break;
        case EnemyType::UNDEAD:
            if (roll < 75) {
                doAttack(atk, false);
            } else {
                applyPlayerStatus(StatusType::WEAK, 2);
                Audio::playSFX("special");
                std::cout << Color::WEAK_CLR << "Enemy's chilling touch saps your strength! Weakened for 2 turns." << Color::RESET << "\n";
            }
            break;
        default:
            doAttack(atk, false);
    }
}

void Game::displayTurnInfo() const {
    std::cout << "\n" << Color::BOLD << "-- Turn " << turnNumber << " --" << Color::RESET << "\n";
    if (playerTurnActive) {
        std::cout << Color::ENERGY_CLR << playerEnergy << "/" << maxEnergy << Color::RESET << " plays remaining\n";
    }
}

void Game::resetArmor() {
    // Fortified armor lasts until its timer runs out or it's broken; the rest wipes each turn.
    // Enemy armor is handled separately (see enemyTurn()) - it needs to survive one turn
    // longer than this, or a Defend action would never actually block anything.
    if (playerArmorPersistTurns > 0) {
        playerArmorPersistTurns--;
    } else {
        playerArmor = 0;
    }
}

void Game::endPlayerTurn() {
    playerTurnActive = false;

    // Loops instead of running once: if the player's new turn opens stunned, that
    // turn is skipped entirely (no hand shown, straight to another enemy turn) -
    // same as how a stunned enemy loses its turn in enemyTurn().
    bool playerStunned;
    do {
        UIHelper::typeWrite(std::string("\n") + Color::BOLD + "--- Enemy's Turn ---" + Color::RESET + "\n");
        UIHelper::pause(350);
        enemyTurn();
        counterAttackActive = false;
        parryActive = false;
        resetArmor();
        turnNumber++;
        resetEnergy();

        // Tick player status effects (start of player's new turn)
        int playerPoisonDmg = playerStatus.processPoison();
        if (playerPoisonDmg > 0) {
            playerHealth = std::max(0, playerHealth - playerPoisonDmg);
            std::cout << Color::POISON_CLR << "Poison:" << Color::RESET
                      << " you take " << Color::DAMAGE << playerPoisonDmg << Color::RESET
                      << " damage! (HP: " << hpColor(playerHealth, maxPlayerHealth)
                      << playerHealth << Color::RESET << ")\n";
            UIHelper::pause(250);
        }
        int playerBurnDmg = playerStatus.processBurn();
        if (playerBurnDmg > 0) {
            playerHealth = std::max(0, playerHealth - playerBurnDmg);
            std::cout << Color::BURN_CLR << "Burn:" << Color::RESET
                      << " you take " << Color::DAMAGE << playerBurnDmg << Color::RESET
                      << " damage! (HP: " << hpColor(playerHealth, maxPlayerHealth)
                      << playerHealth << Color::RESET << ")\n";
            UIHelper::pause(250);
        }
        // WEAK/STRENGTH tick at end of player turn (after all attacks are resolved)
        playerStatus.processWeak();
        playerStatus.processStrength();

        // Discard remaining hand cards and draw a fresh hand for next turn
        playerDeck.resetDeck();
        int drawCount = 5 + upgrades.getDrawBonus();
        for (int i = 0; i < drawCount; ++i) {
            try { playerDeck.drawCard(); } catch (...) { break; }
        }

        if (playerHealth <= 0 || !enemy.isAlive()) { playerTurnActive = true; return; }

        playerStunned = playerStatus.processStun();
        if (playerStunned) {
            UIHelper::typeWrite(std::string(Color::STUN_CLR) + "You are STUNNED and lose your turn!" + Color::RESET + "\n");
            UIHelper::pause(400);
        }
    } while (playerStunned);

    playerTurnActive = true;
    UIHelper::pause(800);
    // handleInput() will clear and redraw the full state for the new turn
}

bool Game::checkGameOver() {
    if (playerHealth <= 0) {
        return true;
    }
    if (!enemy.isAlive()) {
        return true;
    }
    return false;
}

void Game::displayGameOver() {
    UIHelper::clearScreen();
    if (playerHealth <= 0) {
        Audio::playSFX("lose");
        UIHelper::pause(300);
        UIHelper::printGameOverScreen(false, currentRun.getEncountersWon(), runStats.getTotalCardsCollected());
        std::cout << "You were defeated! Better luck next time.\n";
    } else if (!enemy.isAlive()) {
        UIHelper::printGameOverScreen(true, currentRun.getEncountersWon(), runStats.getTotalCardsCollected());
        std::cout << "Enemy defeated! Onward to the next encounter!\n";
    }
}

bool Game::handleGameOverInput() {
    UIHelper::clearScreen();
    std::cout << "\n";
    int choice = UIHelper::menuSelect({"Play again", "Quit"});
    return (choice == 0);
}

void Game::handleInput() {
    if (!playerTurnActive) return;

    lastActionWasCardPlay = false;

    UIHelper::clearScreen();

    // Compact 4-line header so the full turn fits on one screen
    std::string encLabel = currentRun.isBossEncounter()
        ? std::string(Color::BOLD) + Color::MAGENTA + "BOSS" + Color::RESET
        : "Encounter " + std::to_string(currentRun.getCurrentEncounter()) + " " + currentRun.getEncounterDifficulty();
    std::cout << Color::BOLD << "Turn " << turnNumber << Color::RESET
              << "  |  " << Color::ENERGY_CLR << playerEnergy << "/" << maxEnergy << " plays" << Color::RESET
              << "  |  " << encLabel << "\n";

    int totalDmg = upgrades.getDamageBonus() + equipDamageBonus;
    int totalArm = upgrades.getArmorBonus()  + equipArmorBonus;
    std::cout << Color::BOLD << Color::WHITE << "YOU" << Color::RESET
              << "   HP:" << hpColor(playerHealth, maxPlayerHealth) << playerHealth << "/" << maxPlayerHealth << Color::RESET
              << "  " << UIHelper::createHealthBar(playerHealth, maxPlayerHealth, 16);
    if (playerArmor > 0) std::cout << "  ARM:" << Color::ARMOR_CLR << playerArmor << Color::RESET;
    if (playerArmorPersistTurns > 0)
        std::cout << " " << Color::CYAN << "[Fortified " << playerArmorPersistTurns << "]" << Color::RESET;
    if (totalDmg > 0) std::cout << "  +" << Color::CARD_ATTACK << totalDmg << "dmg" << Color::RESET;
    if (totalArm > 0) std::cout << "  +" << Color::ARMOR_CLR   << totalArm << "arm" << Color::RESET;
    std::cout << playerStatus.summary() << "\n";

    std::cout << Color::BOLD << Color::RED << "ENEMY" << Color::RESET
              << "  " << enemy.getName()
              << "  HP:" << hpColor(enemy.getHealth(), enemy.getMaxHealth())
              << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET
              << "  " << UIHelper::createHealthBar(enemy.getHealth(), enemy.getMaxHealth(), 16);
    std::cout << "  ATK:" << Color::RED << enemy.getBaseAttack() << Color::RESET
              << "  DEF:" << Color::BLUE << (enemy.getBaseDefense() + enemy.getArmor()) << Color::RESET
              << enemy.statusSummary() << "\n";

    std::cout << Color::DIM;
    for (int i = 0; i < 68; i++) std::cout << '-';
    std::cout << Color::RESET << "\n";

    int handCount = playerDeck.handSize();
    int dmgBonus  = upgrades.getDamageBonus() + equipDamageBonus;
    int armBonus  = upgrades.getArmorBonus()  + equipArmorBonus;
    double weakMult = playerStatus.getWeakMultiplier();
    double strengthMult = playerStatus.getStrengthMultiplier();

    // Build hand lines + option index mapping for the side-by-side display
    std::vector<std::string> leftLines;
    std::vector<int>         optionIndices;
    std::vector<std::string> options;
    std::vector<bool>        disabled;

    for (int i = 0; i < handCount; i++) {
        bool used       = playerDeck.isCardUsed(i);
        bool cantAfford = !used && (playerDeck.getCardFromHand(i).getCost() > playerEnergy);

        std::string optLabel = "Select Card " + std::to_string(i + 1);
        if (used) optLabel += " (used)";
        else if (cantAfford) optLabel += " (no energy)";
        options.push_back(optLabel);
        disabled.push_back(used || cantAfford);
        int optIdx = (int)options.size() - 1;

        if (used) {
            leftLines.push_back(std::string("  ") + Color::DIM + std::to_string(i + 1) + ". [USED]" + Color::RESET);
            optionIndices.push_back(optIdx);
            leftLines.push_back(""); optionIndices.push_back(-1);
        } else {
            const Card& c = playerDeck.getCardFromHand(i);
            int dispVal = c.getValue();
            if      (c.getType() == CardType::ATTACK) dispVal = (int)(std::max(0, dispVal + dmgBonus) * weakMult * strengthMult);
            else if (c.getType() == CardType::DEFEND) dispVal = dispVal + armBonus;

            const char* typeColor = (c.getTypeString() == "ATTACK") ? Color::CARD_ATTACK
                                  : (c.getTypeString() == "DEFEND") ? Color::CARD_DEFEND
                                  : Color::CARD_SPECIAL;
            std::string valLabel = (c.getTypeString() == "ATTACK") ? "DMG"
                                 : (c.getTypeString() == "DEFEND") ? "ARM" : "STK";

            std::string typePad = c.getTypeString();
            while ((int)typePad.size() < 6) typePad += ' ';
            std::string namePad = c.getName();
            while ((int)namePad.size() < 18) namePad += ' ';

            std::string mainLine =
                std::string("  ") + Color::DIM + std::to_string(i + 1) + "." + Color::RESET
                + " [" + typeColor + typePad + Color::RESET + "] "
                + Color::BOLD + rarityTint(c) + namePad + Color::RESET
                + " cost:" + Color::ENERGY_CLR + std::to_string(c.getCost()) + Color::RESET
                + "  " + valLabel + ":" + Color::GREEN + std::to_string(dispVal) + Color::RESET
                + (c.getTypeTag().empty() ? "" : ("  " + std::string(Color::YELLOW) + c.getTypeTag() + Color::RESET));

            std::string descLine = "     " + std::string(Color::DIM) + c.getDescription() + Color::RESET;

            leftLines.push_back(mainLine);    optionIndices.push_back(optIdx);
            leftLines.push_back(descLine);    optionIndices.push_back(-1);
            leftLines.push_back("");          optionIndices.push_back(-1);
        }
    }

    options.push_back("End Turn");    disabled.push_back(false);
    options.push_back("View Enemy");  disabled.push_back(false);
    options.push_back("Status");      disabled.push_back(false);

    std::cout << "\n";
    int choice = UIHelper::menuSelectRight(leftLines, optionIndices, options, 50, 0, disabled);
    if (choice < 0) return;

    if (choice < handCount) {
        playCardFromHand(choice + 1);
        if (checkGameOver()) return;
        UIHelper::pause(600);  // let the card result stay visible before redraw
        if (playerEnergy <= 0 && playerTurnActive) {
            std::cout << "\n" << Color::DIM << "[No plays left - ending your turn automatically]" << Color::RESET << "\n";
            UIHelper::pause(400);
            endPlayerTurn();
        }
    } else if (choice == handCount) {
        endPlayerTurn();
        checkGameOver();
    } else if (choice == handCount + 1) {
        UIHelper::clearScreen();
        displayEnemyInfo();
        UIHelper::waitForKey();
    } else {
        UIHelper::clearScreen();
        displayStatus();
        UIHelper::waitForKey();
    }
}

Enemy Game::generateBossEnemy() {
    int enc = currentRun.getCurrentEncounter();
    int bossHealth  = currentRun.getEnemyHealth() * 2;
    int bossAttack  = currentRun.getEnemyAttack() + 4;
    int bossDefense = currentRun.getEnemyDefense();

    std::string name;
    EnemyType   etype;
    BossType    btype;

    switch (currentRun.getBossIndex()) {
        case 1:
            name  = "Vile Witch";
            etype = EnemyType::CASTER;
            btype = BossType::VILE_WITCH;
            break;
        case 2:
            name  = "Thunder Beast";
            etype = EnemyType::MELEE;
            btype = BossType::WARLORD;
            break;
        case 3:
            name  = "Hydra";
            etype = EnemyType::BEAST;
            btype = BossType::HYDRA;
            break;
        case 4:
            name  = "Undead Dragon";
            etype = EnemyType::RANGED; // flight/wind fits better than reusing Warlord's Melee slot
            btype = BossType::DRAGON;
            break;
        default: // 0, and fallback
            name  = "Stone Colossus";
            etype = EnemyType::TANK;
            btype = BossType::STONE_COLOSSUS;
            break;
    }

    // Add tier prefix on repeat cycles through the full boss rotation
    int occurrence = (enc / 8) - 1; // 0-indexed: which boss appearance this is
    int cycle = occurrence / 5;
    if (cycle == 1) name = "Ancient " + name;
    else if (cycle >= 2) name = "Eternal " + name;

    Enemy boss(name, bossHealth, bossAttack, bossDefense, etype);
    boss.setBossType(btype);
    if (btype == BossType::STONE_COLOSSUS) boss.gainArmor(10);
    return boss;
}

void Game::bossAction() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> rollDist(0, 99);
    int roll = rollDist(gen);

    double weakMult = enemy.getWeakMultiplier();
    enemy.processWeak();
    int atk = (int)(std::max(0, enemy.getBaseAttack() + enemy.getBonusAttack()) * weakMult);

    auto doAttack = [&](int damage, bool raw) {
        // Dodge fires before Parry when both are active (uncapped, higher priority)
        if (counterAttackActive) {
            counterAttackActive = false;
            int counterDmg = damage * 2 + counterBonusValue;
            int hpBefore = enemy.getHealth();
            enemy.takeDamage(counterDmg);
            int hpLost = hpBefore - enemy.getHealth();
            Audio::playSFX(!enemy.isAlive() ? "dead" : "attack");
            std::cout << Color::GREEN << "Dodge! You sidestep the boss's attack and counter for " << hpLost << " damage!" << Color::RESET
                      << " (Boss HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                      << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
            UIHelper::pause(200);
            return;
        }
        if (parryActive) {
            int parryCap = playerArmor + parryBonusValue * 3; // current armor + Parry's own bonus - stack armor first to parry bigger hits
            parryActive = false;
            if (damage <= parryCap) {
                int riposteDmg = (int)(damage * 1.5) + parryBonusValue;
                int hpBefore = enemy.getHealth();
                enemy.takeDamage(riposteDmg); // ignores defense - takeDamage only accounts for armor
                int hpLost = hpBefore - enemy.getHealth();
                bool stunned = enemy.tryApplyStun();
                Audio::playSFX(!enemy.isAlive() ? "dead" : "special");
                std::cout << Color::CYAN << "Parry! You deflect the blow - no damage taken. Riposte for " << hpLost
                          << " damage!" << (stunned ? " Boss is stunned!" : " Boss resists the stun!") << Color::RESET
                          << " (Boss HP: " << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                UIHelper::pause(300);
                return;
            } else {
                std::cout << Color::BOLD << Color::RED << "The blow is too powerful to parry! Your guard breaks!" << Color::RESET << "\n";
                UIHelper::pause(250);
            }
        }
        if (raw) {
            playerHealth = std::max(0, playerHealth - damage);
            Audio::playSFX("hit");
            std::cout << Color::BOLD << Color::DAMAGE << "  BOSS slams for " << damage
                      << " (ignores armor)!" << Color::RESET
                      << " HP: " << hpColor(playerHealth, maxPlayerHealth)
                      << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
        } else {
            int actual = std::max(0, damage - playerArmor);
            playerArmor = std::max(0, playerArmor - damage);
            playerHealth = std::max(0, playerHealth - actual);
            Audio::playSFX("hit");
            std::cout << Color::BOLD << Color::DAMAGE << "  BOSS strikes for " << actual
                      << " damage!" << Color::RESET
                      << " HP: " << hpColor(playerHealth, maxPlayerHealth)
                      << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n";
        }
        if (weakMult < 1.0)
            std::cout << "  " << Color::WEAK_CLR << "[Weakened]" << Color::RESET << "\n";
        UIHelper::pause(350);
    };

    switch (enemy.getBossType()) {
        case BossType::STONE_COLOSSUS:
            if (roll < 15) {
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Stone Colossus uses EARTHQUAKE SLAM!" + Color::RESET + "\n");
                UIHelper::pause(300);
                doAttack(15, true);
            } else if (roll < 45) {
                enemy.gainArmor(8);
                std::cout << Color::MAGENTA << "Stone Colossus hardens!" << Color::RESET
                          << " +" << Color::ARMOR_CLR << 8 << Color::RESET
                          << " armor (" << enemy.getArmor() << " total)\n";
                UIHelper::pause(200);
            } else {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Stone Colossus strikes!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk + 4, false);
            }
            break;

        case BossType::VILE_WITCH:
            if (roll < 30) {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Vile Witch attacks!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
            } else if (roll < 70) {
                applyPlayerStatus(StatusType::POISON, 4);
                applyPlayerStatus(StatusType::BURN, 2);
                Audio::playSFX("poison");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Vile Witch casts PLAGUE!" + Color::RESET
                    + " You gain " + Color::POISON_CLR + "Poison 4" + Color::RESET
                    + " and " + Color::BURN_CLR + "Burn 2" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 85) {
                int healAmt = 20;
                enemy.heal(healAmt);
                std::cout << Color::MAGENTA << "Vile Witch siphons life, healing " << Color::HEAL
                          << healAmt << " HP!" << Color::RESET << " ("
                          << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                UIHelper::pause(250);
            } else {
                applyPlayerStatus(StatusType::POISON, 6);
                Audio::playSFX("poison");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Vile Witch casts TOXIC ERUPTION!" + Color::RESET
                    + " You gain " + Color::POISON_CLR + "Poison 6" + Color::RESET + "!\n");
                UIHelper::pause(350);
            }
            break;

        case BossType::WARLORD:
            if (roll < 12) {
                applyPlayerStatus(StatusType::STUN, 1);
                Audio::playSFX("volt");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Thunder Beast unleashes a THUNDERSTRIKE!" + Color::RESET
                    + " You are " + Color::STUN_CLR + "STUNNED" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 27) {
                applyPlayerStatus(StatusType::WEAK, 3);
                Audio::playSFX("special");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Thunder Beast roars a BATTLECRY!" + Color::RESET
                    + " You are " + Color::WEAK_CLR + "Weakened 3" + Color::RESET + "!\n");
                UIHelper::pause(350);
            }
            UIHelper::typeWrite(std::string(Color::MAGENTA) + "Thunder Beast attacks!" + Color::RESET + "\n");
            UIHelper::pause(200);
            doAttack(atk, false);
            if (enemy.getBonusAttack() < 6) {
                enemy.addBonusAttack(1);
                std::cout << Color::MAGENTA << "Thunder Beast grows stronger!" << Color::RESET
                          << " (total bonus +" << Color::RED << enemy.getBonusAttack() << Color::RESET << " attack)\n";
                UIHelper::pause(200);
            }
            break;

        case BossType::HYDRA:
            if (roll < 20) {
                int healAmt = 18;
                enemy.heal(healAmt);
                std::cout << Color::MAGENTA << "Hydra regrows a severed head, healing " << Color::HEAL
                          << healAmt << " HP!" << Color::RESET << " ("
                          << hpColor(enemy.getHealth(), enemy.getMaxHealth())
                          << enemy.getHealth() << "/" << enemy.getMaxHealth() << Color::RESET << ")\n";
                UIHelper::pause(250);
            } else if (roll < 50) {
                applyPlayerStatus(StatusType::POISON, 5);
                Audio::playSFX("poison");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Hydra sinks its fangs in - VENOMOUS BITE!" + Color::RESET
                    + " You gain " + Color::POISON_CLR + "Poison 5" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 75) {
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Hydra lashes out with TWIN STRIKE!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
                if (enemy.isAlive()) doAttack(atk, false);
            } else {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Hydra bites!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
            }
            break;

        case BossType::DRAGON:
            if (roll < 20) {
                applyPlayerStatus(StatusType::BURN, 4);
                Audio::playSFX("fire");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Dragon unleashes FIRE BREATH!" + Color::RESET
                    + " You gain " + Color::BURN_CLR + "Burn 4" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 45) {
                applyPlayerStatus(StatusType::WEAK, 3);
                Audio::playSFX("special");
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Dragon's WING BUFFET knocks you off balance!" + Color::RESET
                    + " You are " + Color::WEAK_CLR + "Weakened 3" + Color::RESET + "!\n");
                UIHelper::pause(350);
            } else if (roll < 70) {
                UIHelper::typeWrite(std::string(Color::BOLD) + Color::MAGENTA + "Dragon rakes with CLAW RAKE!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk + 5, true);
            } else {
                UIHelper::typeWrite(std::string(Color::MAGENTA) + "Dragon claws at you!" + Color::RESET + "\n");
                UIHelper::pause(200);
                doAttack(atk, false);
            }
            break;

        default:
            doAttack(atk, false);
    }
}

void Game::offerBossReward() {
    UIHelper::clearScreen();
    std::vector<Card> rewards = rewardPool.generateRareRewards(3, maxEnergy, playerDeck.getAllCardNames());

    std::cout << "\n" << Color::BOLD << Color::YELLOW << "Boss reward" << Color::RESET << " - choose one:\n\n";

    std::vector<std::string> leftLines;
    std::vector<int>         optionIndices;
    std::vector<std::string> options;

    for (size_t i = 0; i < rewards.size(); i++) {
        const Card& c = rewards[i];
        const char* typeColor = (c.getTypeString() == "ATTACK") ? Color::CARD_ATTACK
                              : (c.getTypeString() == "DEFEND") ? Color::CARD_DEFEND
                              : Color::CARD_SPECIAL;
        const char* valLabel = (c.getTypeString() == "ATTACK") ? "DMG"
                             : (c.getTypeString() == "DEFEND") ? "ARM" : "STK";

        leftLines.push_back(std::string("  ") + Color::BOLD + std::to_string(i + 1) + "." + Color::RESET
            + " [" + typeColor + c.getTypeString() + Color::RESET + "] "
            + Color::BOLD + rarityTint(c) + c.getName() + Color::RESET);
        optionIndices.push_back((int)i);

        leftLines.push_back(std::string("     Cost:") + Color::ENERGY_CLR + std::to_string(c.getCost()) + Color::RESET
            + "  " + valLabel + ":" + Color::GREEN + std::to_string(c.getValue()) + Color::RESET
            + (c.getTypeTag().empty() ? "" : ("  " + std::string(Color::YELLOW) + c.getTypeTag() + Color::RESET)));
        optionIndices.push_back(-1);

        leftLines.push_back(std::string("     ") + Color::DIM + c.getDescription() + Color::RESET);
        optionIndices.push_back(-1);

        leftLines.push_back("");
        optionIndices.push_back(-1);

        options.push_back("Select Card " + std::to_string(i + 1));
    }
    options.push_back("Skip");

    int choice = UIHelper::menuSelectRight(leftLines, optionIndices, options, 45);

    if (choice < 0 || choice >= (int)rewards.size()) {
        std::cout << "Skipping reward.\n";
        UIHelper::waitForKey();
        return;
    }

    playerDeck.addCard(rewards[choice]);
    runStats.addCardToRun();
    std::cout << "\n" << Color::YELLOW << "Added " << rewards[choice].getName() << " to your deck!" << Color::RESET << "\n";
    UIHelper::waitForKey();
}

void Game::offerExtraPlay() {
    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << Color::YELLOW << "A hard-won boss kill leaves you invigorated." << Color::RESET << "\n\n";

    std::vector<std::string> leftLines = {
        std::string("  ") + Color::ENERGY_CLR + "Extra Play" + Color::RESET
            + " - +1 max energy per turn (" + std::to_string(maxEnergy) + " -> " + std::to_string(maxEnergy + 1) + ")",
        std::string("  ") + Color::DIM + "Skip" + Color::RESET
    };
    std::vector<int> optionIndices = {0, 1};
    int choice = UIHelper::menuSelectRight(leftLines, optionIndices, {"Take Extra Play", "Skip"}, 55);

    if (choice == 0) {
        maxEnergy++;
        playerEnergy = maxEnergy;
        std::cout << "\n" << Color::ENERGY_CLR << "You feel invigorated! Max plays per turn increased to " << maxEnergy << "." << Color::RESET << "\n";
        Audio::playSFX("upgrade");
    } else {
        std::cout << Color::DIM << "\nYou let it pass.\n" << Color::RESET;
    }
    UIHelper::waitForKey();
}

void Game::startEncounter() {
    UIHelper::clearScreen();
    playerDeck.resetDeck();
    int drawCount = 5 + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    if (currentRun.isBossEncounter()) {
        enemy = generateBossEnemy();
    } else {
        int health  = currentRun.getEnemyHealth();
        int attack  = currentRun.getEnemyAttack();
        int defense = currentRun.getEnemyDefense();

        int enc = currentRun.getCurrentEncounter();
        int typeIndex = (enc - 1) % 6;
        EnemyType etype;
        switch (typeIndex) {
            case 0: etype = EnemyType::MELEE;  break;
            case 1: etype = EnemyType::RANGED; break;
            case 2: etype = EnemyType::TANK;   break;
            case 3: etype = EnemyType::CASTER; break;
            case 4: etype = EnemyType::BEAST;  break;
            case 5: etype = EnemyType::UNDEAD; break;
            default: etype = EnemyType::MELEE; break;
        }
        std::string name = Enemy::generateName(etype, enc);
        std::string typeLabel = (etype == EnemyType::MELEE) ? "Melee"
                              : (etype == EnemyType::RANGED) ? "Ranged"
                              : (etype == EnemyType::TANK)   ? "Tank"
                              : (etype == EnemyType::CASTER) ? "Caster"
                              : (etype == EnemyType::BEAST)  ? "Beast" : "Undead";
        name += " (" + typeLabel + ")";
        enemy = Enemy(name, health, attack, defense, etype);
    }

    inEncounter = true;
    turnNumber = 1;
    playerTurnActive = true;
    playerArmor = 0;
    playerEnergy = maxEnergy;
    playerStatus.reset();

    currentRun.displayRunStats();

    if (currentRun.isBossEncounter()) {
        Audio::playSFX("boss");
        UIHelper::printBossHeader(currentRun.getCurrentEncounter(), enemy.getName());
    } else {
        UIHelper::printEncounterHeader(currentRun.getCurrentEncounter(),
                                       currentRun.getEncounterDifficulty(),
                                       currentRun.getEncounterTier());
    }

    displayStatus();
    displayTurnInfo();
    UIHelper::pause(600);
    // handleInput() will render the hand + menu side-by-side on first input
}

void Game::nextEncounter() {
    currentRun.nextEncounter();
    startEncounter();
}

void Game::restSite() {
    // Rest/Forge commit and end the visit; Return loops back to this menu.
    while (true) {
    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << Color::CYAN << "Rest site" << Color::RESET << "\n\n";

    std::vector<std::string> leftLines = {
        std::string("  ") + Color::HEAL + "Rest" + Color::RESET
            + "  - heal to full (" + std::to_string(playerHealth) + "/" + std::to_string(maxPlayerHealth) + " HP)",
        std::string("  ") + Color::YELLOW + "Forge" + Color::RESET
            + " - upgrade a card (+3 value, -1 cost)",
        std::string("  ") + Color::CYAN + "View Deck" + Color::RESET
            + " - browse your cards, discard ones you dislike",
        std::string("  ") + Color::DIM + "Skip" + Color::RESET
            + "  - press on without resting"
    };
    std::vector<int> optionIndices = {0, 1, 2, 3};
    std::vector<std::string> options = {"Rest", "Forge", "View Deck", "Skip"};

    int siteChoice = UIHelper::menuSelectRight(leftLines, optionIndices, options, 52);

    if (siteChoice == 0) {
        playerHealth = maxPlayerHealth;
        Audio::playSFX("heal");
        std::cout << Color::HEAL << "\nYou rest and fully recover to " << maxPlayerHealth << " HP." << Color::RESET << "\n";
        UIHelper::waitForKey();
        break; // committed - progress as normal
    } else if (siteChoice == 1) {
        if (playerDeck.totalCards() == 0) {
            std::cout << "Your deck is empty - nothing to upgrade.\n";
            UIHelper::waitForKey();
            continue; // nothing happened - back to the rest site menu
        }

        std::vector<Card> allCards = playerDeck.getAllCardsOrdered();

        // Group identical cards together - picking a group upgrades every copy at once.
        std::vector<const Card*> groupCard;
        std::vector<int>         groupCount;
        for (size_t i = 0; i < allCards.size(); i++) {
            const Card& c = allCards[i];
            bool merged = false;
            for (size_t g = 0; g < groupCard.size(); g++) {
                if (groupCard[g]->getName() == c.getName()) {
                    groupCount[g]++;
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                groupCard.push_back(&allCards[i]);
                groupCount.push_back(1);
            }
        }

        std::vector<std::string> cardLines;
        std::vector<int>         cardOptIdx;
        std::vector<std::string> cardOptions;
        std::vector<bool>        cardDisabled;

        for (size_t g = 0; g < groupCard.size(); g++) {
            const Card& c = *groupCard[g];
            int upgradesLeft = c.getMaxUpgrades() - c.getUpgradeCount();
            bool maxed = upgradesLeft <= 0;

            const char* typeColor = (c.getTypeString() == "ATTACK") ? Color::CARD_ATTACK
                                  : (c.getTypeString() == "DEFEND") ? Color::CARD_DEFEND
                                  : Color::CARD_SPECIAL;
            const char* valLabel = (c.getTypeString() == "ATTACK") ? "DMG"
                                 : (c.getTypeString() == "DEFEND") ? "ARM" : "STK";
            std::string typePad = c.getTypeString();
            while ((int)typePad.size() < 6) typePad += ' ';
            std::string namePad = c.getName();
            if (groupCount[g] > 1) namePad += " (x" + std::to_string(groupCount[g]) + ")";
            while ((int)namePad.size() < 22) namePad += ' ';

            cardLines.push_back(std::string("  ") + Color::DIM + std::to_string(g + 1) + "." + Color::RESET
                + " [" + typeColor + typePad + Color::RESET + "] "
                + Color::BOLD + rarityTint(c) + namePad + Color::RESET
                + " cost:" + Color::ENERGY_CLR + std::to_string(c.getCost()) + Color::RESET
                + "  " + valLabel + ":" + Color::GREEN + std::to_string(c.getValue()) + Color::RESET
                + (c.getTypeTag().empty() ? "" : ("  " + std::string(Color::YELLOW) + c.getTypeTag() + Color::RESET))
                + "  " + (maxed ? (std::string(Color::DIM) + "[MAXED]" + Color::RESET)
                                : (std::string(Color::CYAN) + "[" + std::to_string(upgradesLeft) + " upgrade" + (upgradesLeft != 1 ? "s" : "") + " left]" + Color::RESET)));
            cardOptIdx.push_back((int)g);

            cardLines.push_back(std::string("     ") + Color::DIM + c.getDescription() + Color::RESET);
            cardOptIdx.push_back(-1);

            cardLines.push_back("");
            cardOptIdx.push_back(-1);

            cardOptions.push_back("Select Card " + std::to_string(g + 1));
            cardDisabled.push_back(maxed);
        }
        cardOptions.push_back("Return");
        cardDisabled.push_back(false);

        UIHelper::clearScreen();
        std::cout << "\n" << Color::BOLD << Color::YELLOW << "Forge" << Color::RESET
                   << " - pick a card to upgrade (all copies upgrade together):\n\n";
        int groupChoice = UIHelper::menuSelectRight(cardLines, cardOptIdx, cardOptions, 54, 0, cardDisabled);

        if (groupChoice < 0 || groupChoice >= (int)groupCard.size()) {
            continue; // backed out - back to the rest site menu, no commitment made
        } else {
            std::string beforeName = groupCard[groupChoice]->getName();
            int upgradedCount = playerDeck.upgradeCardGroup(beforeName);
            if (upgradedCount > 0) {
                Audio::playSFX("upgrade");
                std::string afterName = beforeName + "+";
                std::vector<Card> updated = playerDeck.getAllCardsOrdered();
                auto it = std::find_if(updated.begin(), updated.end(),
                                        [&](const Card& c) { return c.getName() == afterName; });
                std::cout << Color::GREEN << "\n"
                          << (upgradedCount > 1 ? ("All " + std::to_string(upgradedCount) + " ") : std::string())
                          << beforeName << (upgradedCount > 1 ? " cards" : "") << " upgraded to " << afterName << "!"
                          << Color::RESET;
                if (it != updated.end())
                    std::cout << " (cost:" << it->getCost() << " val:" << it->getValue() << ")";
                std::cout << "\n";
                UIHelper::waitForKey();
            }
            break; // committed - progress as normal
        }
    } else if (siteChoice == 2) {
        viewDeckManage();
        continue; // browsing/discarding never costs your rest site visit - back to the rest site menu
    } else {
        std::cout << Color::DIM << "\nYou press on without resting." << Color::RESET << "\n";
        UIHelper::waitForKey();
        break;
    }
    } // while(true)
}

void Game::viewDeckManage() {
    while (true) {
        UIHelper::clearScreen();
        if (playerDeck.totalCards() == 0) {
            std::cout << "\nYour deck is empty.\n";
            UIHelper::waitForKey();
            return;
        }

        std::vector<Card> allCards = playerDeck.getAllCardsOrdered();

        // Group identical cards (dupes are just how the deck deals out cards).
        std::vector<const Card*> groupCard;
        std::vector<int>         groupCount;
        for (size_t i = 0; i < allCards.size(); i++) {
            const Card& c = allCards[i];
            bool merged = false;
            for (size_t g = 0; g < groupCard.size(); g++) {
                if (groupCard[g]->getName() == c.getName()) { groupCount[g]++; merged = true; break; }
            }
            if (!merged) { groupCard.push_back(&allCards[i]); groupCount.push_back(1); }
        }

        std::cout << "\n" << Color::BOLD << Color::CYAN << "Your Deck" << Color::RESET
                   << " - " << allCards.size() << " cards, " << groupCard.size() << " unique\n\n";

        auto buildCell = [&](const Card& c, int count, int idx) -> std::string {
            const char* typeColor = (c.getTypeString() == "ATTACK") ? Color::CARD_ATTACK
                                  : (c.getTypeString() == "DEFEND") ? Color::CARD_DEFEND
                                  : Color::CARD_SPECIAL;
            const char* valLabel = (c.getTypeString() == "ATTACK") ? "DMG"
                                 : (c.getTypeString() == "DEFEND") ? "ARM" : "STK";
            std::string name = c.getName();
            if (count > 1) name += " x" + std::to_string(count);
            return std::string(Color::DIM) + std::to_string(idx + 1) + "." + Color::RESET
                + " [" + typeColor + c.getTypeString() + Color::RESET + "] "
                + Color::BOLD + rarityTint(c) + name + Color::RESET
                + " " + valLabel + ":" + Color::GREEN + std::to_string(c.getValue()) + Color::RESET
                + " c:" + Color::ENERGY_CLR + std::to_string(c.getCost()) + Color::RESET
                + (c.getTypeTag().empty() ? "" : (" " + std::string(Color::YELLOW) + c.getTypeTag() + Color::RESET));
        };

        // Grid: three cards per row.
        const int cellWidth = 32;
        for (size_t g = 0; g < groupCard.size(); g += 3) {
            for (size_t col = 0; col < 3 && g + col < groupCard.size(); col++) {
                std::string cell = buildCell(*groupCard[g + col], groupCount[g + col], (int)(g + col));
                if (col + 1 < 3 && g + col + 1 < groupCard.size()) {
                    int pad = cellWidth - UIHelper::visibleLen(cell);
                    std::cout << "  " << cell << (pad > 0 ? std::string(pad, ' ') : "");
                } else {
                    std::cout << "  " << cell;
                }
            }
            std::cout << "\n";
        }
        std::cout << "\n";

        std::vector<std::string> options;
        for (size_t g = 0; g < groupCard.size(); g++)
            options.push_back("Discard 1x " + groupCard[g]->getName());
        options.push_back("Return");

        int choice = UIHelper::menuSelect(options);
        if (choice < 0 || choice >= (int)groupCard.size()) return; // Return or ESC

        if (playerDeck.totalCards() <= 1) {
            std::cout << "\n" << Color::DIM << "You must keep at least one card." << Color::RESET << "\n";
            UIHelper::waitForKey();
            continue;
        }

        std::string name = groupCard[choice]->getName();
        if (playerDeck.removeCardByName(name)) {
            std::cout << "\n" << Color::RED << "Discarded one " << name << " from your deck." << Color::RESET << "\n";
            UIHelper::waitForKey();
            // stay on this screen - browsing/discarding never costs the rest site visit
        }
    }
}

void Game::handleEncounterWin() {
    currentRun.winEncounter();
    Audio::playSFX("win");
    UIHelper::pause(300);
    UIHelper::clearScreen();
    UIHelper::typeWrite(std::string("\n") + Color::BOLD + Color::GREEN + "Victory!" + Color::RESET + "\n");
    UIHelper::pause(300);
    std::cout << "Enemies Defeated: " << Color::GREEN << currentRun.getEncountersWon() << Color::RESET << "\n";

    if (enemy.isBoss()) {
        // First-ever Dragon kill (occurrence 4, cycle 0) closes out the boss rotation -
        // everything past this point is the same 5 bosses again, scaled up indefinitely.
        int occurrence = (currentRun.getCurrentEncounter() / 8) - 1;
        if (enemy.getBossType() == BossType::DRAGON && occurrence / 5 == 0) {
            UIHelper::waitForKey();
            UIHelper::clearScreen();
            std::cout << "\n" << Color::BOLD << Color::MAGENTA << "THE MAIN GAME IS COMPLETE" << Color::RESET << "\n\n";
            std::cout << "  You've beaten all 5 bosses. From here on it's endless mode - the\n";
            std::cout << "  same bosses keep coming back, stronger each time (\"Ancient\", then\n";
            std::cout << "  \"Eternal\"), along with tougher regular encounters. See how far you\n";
            std::cout << "  can push it.\n\n";
            UIHelper::waitForKey();
        }

        offerBossReward();
        // Every 2nd boss defeated (occurrence 2, 4, 6...) also grants a shot at +1 max energy.
        int bossOccurrence = currentRun.getCurrentEncounter() / 8;
        if (bossOccurrence % 2 == 0) offerExtraPlay();
    } else {
        offerCardReward();
    }

    if (currentRun.getCurrentEncounter() % 3 == 0)
        offerEquipmentDrop();

    restSite();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << Color::GREEN << "Round complete!" << Color::RESET << "\n\n";
    std::cout << "  Encounters cleared: " << Color::GREEN << currentRun.getEncountersWon() << Color::RESET << "\n";
    std::cout << "  HP: " << hpColor(playerHealth, maxPlayerHealth)
              << playerHealth << "/" << maxPlayerHealth << Color::RESET << "\n\n";

    std::vector<std::string> contLines = {
        std::string("  ") + Color::GREEN + "Continue" + Color::RESET + " - enter the next encounter",
        std::string("  ") + Color::DIM   + "End run" + Color::RESET  + "  - finish here and bank your progress"
    };
    std::vector<int> contIdx = {0, 1};
    int choice = UIHelper::menuSelectRight(contLines, contIdx, {"Continue", "End run"}, 50);

    if (choice == 0) {
        nextEncounter();
    } else {
        inEncounter = false;
        currentRun.loseRun();
    }
}

void Game::offerCardReward() {
    UIHelper::clearScreen();
    bool rarityBoost = upgrades.isActive(6);
    std::vector<Card> rewards = rewardPool.generateWeightedRewards(3, rarityBoost, maxEnergy, playerDeck.getAllCardNames());

    if (rewards.empty()) {
        std::cout << "\n" << Color::DIM << "No new cards left to offer - you already own everything available at this cost."
                   << Color::RESET << "\n";
        UIHelper::waitForKey();
        return;
    }

    std::cout << "\n" << Color::BOLD << Color::YELLOW << "Card reward" << Color::RESET << " - pick one to add to your deck:\n\n";

    std::vector<std::string> leftLines;
    std::vector<int>         optionIndices;
    std::vector<std::string> options;

    for (size_t i = 0; i < rewards.size(); i++) {
        const Card& c = rewards[i];
        const char* typeColor = (c.getTypeString() == "ATTACK") ? Color::CARD_ATTACK
                              : (c.getTypeString() == "DEFEND") ? Color::CARD_DEFEND
                              : Color::CARD_SPECIAL;
        const char* valLabel = (c.getTypeString() == "ATTACK") ? "DMG"
                             : (c.getTypeString() == "DEFEND") ? "ARM" : "STK";

        leftLines.push_back(std::string("  ") + Color::BOLD + std::to_string(i + 1) + "." + Color::RESET
            + " [" + typeColor + c.getTypeString() + Color::RESET + "] "
            + Color::BOLD + rarityTint(c) + c.getName() + Color::RESET);
        optionIndices.push_back((int)i);

        leftLines.push_back(std::string("     Cost:") + Color::ENERGY_CLR + std::to_string(c.getCost()) + Color::RESET
            + "  " + valLabel + ":" + Color::GREEN + std::to_string(c.getValue()) + Color::RESET
            + (c.getTypeTag().empty() ? "" : ("  " + std::string(Color::YELLOW) + c.getTypeTag() + Color::RESET)));
        optionIndices.push_back(-1);

        leftLines.push_back(std::string("     ") + Color::DIM + c.getDescription() + Color::RESET);
        optionIndices.push_back(-1);

        leftLines.push_back("");
        optionIndices.push_back(-1);

        options.push_back("Select Card " + std::to_string(i + 1));
    }
    options.push_back("Skip");

    int choice = UIHelper::menuSelectRight(leftLines, optionIndices, options, 40);
    if (choice < 0 || choice >= (int)rewards.size()) {
        std::cout << "Skipping reward.\n";
        UIHelper::waitForKey();
        return;
    }

    playerDeck.addCard(rewards[choice]);
    runStats.addCardToRun();
    std::cout << "\n" << Color::GREEN << "Added " << rewards[choice].getName() << " to your deck!" << Color::RESET << "\n";
    UIHelper::waitForKey();
}

void Game::applyUpgrades() {
    if (upgrades.isActive(1)) {
        playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
        playerDeck.addCard(Card("Strike", "Deal 5 damage", CardType::ATTACK, 1, 5));
    }
    maxPlayerHealth += upgrades.getHealthBonus();
    playerHealth = maxPlayerHealth;
    maxEnergy += upgrades.getEnergyBonus();
    playerEnergy = maxEnergy;
}

void Game::offerEquipmentDrop() {
    UIHelper::clearScreen();
    std::cout << "\n" << Color::YELLOW << "Equipment" << Color::RESET << " - you spot some gear on the ground:\n\n";

    EquipTier weapon = weaponTierAt(weaponTier);
    EquipTier armor  = armorTierAt(armorTier);
    int hpBoost = 15;

    std::vector<std::string> leftLines = {
        std::string("  ") + Color::RED + "[WEAPON]" + Color::RESET + " " + weapon.name,
        std::string("     ") + Color::DIM + "+" + std::to_string(weapon.bonus) + " permanent damage on all attacks" + Color::RESET,
        "",
        std::string("  ") + Color::ARMOR_CLR + "[ARMOR]" + Color::RESET + "  " + armor.name,
        std::string("     ") + Color::DIM + "+" + std::to_string(armor.bonus) + " permanent armor per defend" + Color::RESET,
        "",
        std::string("  ") + Color::HEAL + "[VIGOR]" + Color::RESET + "  Health Pouch",
        std::string("     ") + Color::DIM + "+" + std::to_string(hpBoost) + " max HP, permanently (" + std::to_string(maxPlayerHealth) + " -> " + std::to_string(maxPlayerHealth + hpBoost) + ")" + Color::RESET,
        "",
        std::string("  ") + Color::DIM + "Leave it behind" + Color::RESET,
        ""
    };
    std::vector<int> optionIndices = {0, -1, -1, 1, -1, -1, 2, -1, -1, 3, -1};
    std::vector<std::string> options = {"Take " + weapon.name, "Take " + armor.name, "Take Health Pouch", "Leave it"};

    int choice = UIHelper::menuSelectRight(leftLines, optionIndices, options, 44);

    if (choice == 0) {
        equipDamageBonus += weapon.bonus;
        weaponTier++;
        std::cout << Color::RED << "\nYou equip the " << weapon.name << ". +" << weapon.bonus << " damage!" << Color::RESET << "\n";
        Audio::playSFX("upgrade");
        UIHelper::waitForKey();
    } else if (choice == 1) {
        equipArmorBonus += armor.bonus;
        armorTier++;
        std::cout << Color::CYAN << "\nYou equip the " << armor.name << ". +" << armor.bonus << " armor per defend!" << Color::RESET << "\n";
        Audio::playSFX("upgrade");
        UIHelper::waitForKey();
    } else if (choice == 2) {
        maxPlayerHealth += hpBoost;
        playerHealth += hpBoost;
        Audio::playSFX("heal");
        std::cout << Color::HEAL << "\nYou consume the Health Pouch! Max HP permanently increased by " << hpBoost
                   << "! (" << playerHealth << "/" << maxPlayerHealth << ")" << Color::RESET << "\n";
        UIHelper::waitForKey();
    } else {
        std::cout << Color::DIM << "\nYou leave it behind." << Color::RESET << "\n";
        UIHelper::waitForKey();
    }
}

void Game::selectUpgrades() {
    upgrades.checkAndUnlockUpgrades(runStats.getTotalEncountersWon(), runStats.getTotalCardsCollected());
    upgrades.selectActiveUpgrades();
}

void Game::displayRunStats() const {
    currentRun.displayRunStats();
}

bool Game::showMainMenu() {
    while (true) {
        std::cout << "\n";
        int choice = UIHelper::menuSelect({"Start Game", "How to Play", "Quit"});
        if (choice == 0) return true;
        if (choice == 1) {
            showHowToPlay();
            UIHelper::clearScreen();
            UIHelper::printTitle();
            continue;
        }
        return false; // Quit or ESC
    }
}

void Game::showHowToPlay() {
    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << Color::CYAN << "HOW TO PLAY" << Color::RESET << "\n\n";

    std::cout << Color::BOLD << "GOAL" << Color::RESET << "\n";
    std::cout << "  Survive as many encounters as you can. Build your deck, upgrade your\n";
    std::cout << "  best cards, and defeat the bosses that appear every 8th fight.\n\n";

    std::cout << Color::BOLD << "YOUR TURN" << Color::RESET << "\n";
    std::cout << "  Each turn you draw a hand and get a number of \"plays\" (energy).\n";
    std::cout << "  Playing a card costs plays. When you're done, End Turn to let the\n";
    std::cout << "  enemy act, then a new turn begins.\n\n";

    std::cout << Color::BOLD << "CARD TYPES" << Color::RESET << "\n";
    std::cout << "  " << Color::CARD_ATTACK  << "ATTACK " << Color::RESET << " deals damage to the enemy\n";
    std::cout << "  " << Color::CARD_DEFEND  << "DEFEND " << Color::RESET << " grants you armor, which absorbs incoming damage\n";
    std::cout << "  " << Color::CARD_SPECIAL << "SPECIAL" << Color::RESET << " buffs, debuffs, and unique effects\n\n";

    std::cout << Color::BOLD << "DAMAGE TYPES & WEAKNESSES" << Color::RESET << "\n";
    std::cout << "  Some cards carry a damage-type tag: " << Color::YELLOW << "Smash, Pierce" << Color::RESET
              << " (physical) or\n";
    std::cout << "  " << Color::YELLOW << "Fire, Poison, Wind" << Color::RESET << " (elemental). Every enemy is WEAK to one type\n";
    std::cout << "  (+50% damage) and some RESIST another (-50% damage). Check \"View\n";
    std::cout << "  Enemy\" in combat to plan your attacks.\n\n";

    std::cout << Color::BOLD << "STATUS EFFECTS" << Color::RESET << "\n";
    std::cout << "  " << Color::POISON_CLR << "Poison" << Color::RESET << " / " << Color::BURN_CLR << "Burn" << Color::RESET
              << "     damage over time, ticks down each turn\n";
    std::cout << "  " << Color::STUN_CLR << "Stun" << Color::RESET << "          skip the target's next turn entirely\n";
    std::cout << "  " << Color::WEAK_CLR << "Weak" << Color::RESET << "          -2 attack for a number of turns\n";
    std::cout << "  " << Color::STRENGTH_CLR << "Strength" << Color::RESET << "      damage dealt is multiplied for a number of turns (buff)\n\n";

    std::cout << Color::BOLD << "CARD RARITY" << Color::RESET << "\n";
    std::cout << "  Common (starter) < " << Color::COMMON_TINT << "Uncommon" << Color::RESET << " < "
              << Color::RARE_TINT << "Rare" << Color::RESET << " < " << Color::SUPER_RARE_TINT << "Super Rare" << Color::RESET
              << " < " << Color::LEGENDARY_TINT << "Legendary" << Color::RESET << "\n";
    std::cout << "  Higher rarity cards hit harder and can be upgraded more times at the\n";
    std::cout << "  Forge. Legendary cards only ever drop from boss rewards.\n\n";

    std::cout << Color::BOLD << "REST SITES" << Color::RESET << "\n";
    std::cout << "  After winning a fight: Rest (heal to full), Forge (upgrade a card -\n";
    std::cout << "  all copies upgrade together), View Deck (browse and discard cards you\n";
    std::cout << "  don't want), or Skip. Rest, Forge, and Skip all move you on to the\n";
    std::cout << "  next fight; View Deck lets you keep browsing until you hit Return.\n\n";

    std::cout << Color::BOLD << "REWARDS" << Color::RESET << "\n";
    std::cout << "  Win a fight -> pick a new card. Every 3rd encounter -> equipment (a\n";
    std::cout << "  permanent weapon, armor, or a Health Pouch for +max HP). Beat a boss\n";
    std::cout << "  -> pick from 3 rare-or-better cards (a rare shot at the Legendary\n";
    std::cout << "  Dodge). Every 2nd boss also offers +1 max energy.\n\n";

    std::cout << Color::BOLD << "BETWEEN RUNS" << Color::RESET << "\n";
    std::cout << "  Winning encounters and collecting cards unlocks permanent upgrades\n";
    std::cout << "  (more HP, more damage, more energy...) you can toggle on for your next run.\n\n";

    UIHelper::waitForKey("  (press any key to continue)");

    // Clear before this menu specifically - the wall of text above reliably sits at
    // (or past) the terminal's scroll boundary, which breaks the save/restore-cursor
    // redraw consistently (unlike shorter menus, where it's only an occasional glitch).
    UIHelper::clearScreen();
    std::cout << "\n";
    int choice = UIHelper::menuSelect({"Tutorial", "Return"});
    if (choice == 0) showTutorial();
}

void Game::showTutorial() {
    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << Color::CYAN << "TUTORIAL" << Color::RESET << "\n\n";
    std::cout << "  Fight the Training Dummy and learn the mechanics.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << "YOUR HAND" << Color::RESET << "\n\n";
    std::cout << "  Each turn you draw a hand of cards and get a number of \"plays\"\n";
    std::cout << "  (energy), shown at the top. Every card costs some of that energy.\n";
    std::cout << "  Arrow keys highlight a card, Enter plays it.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << "ATTACK, DEFEND, SPECIAL" << Color::RESET << "\n\n";
    std::cout << "  " << Color::CARD_ATTACK << "ATTACK" << Color::RESET << " cards deal damage. " << Color::CARD_DEFEND << "DEFEND" << Color::RESET
              << " cards grant armor that\n";
    std::cout << "  absorbs incoming damage. " << Color::CARD_SPECIAL << "SPECIAL" << Color::RESET << " cards do everything else -\n";
    std::cout << "  status effects, counters, heals, and more.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << "DAMAGE TYPES" << Color::RESET << "\n\n";
    std::cout << "  Some attacks carry a damage-type tag, like " << Color::BOLD << "Bash" << Color::RESET << " (" << Color::YELLOW << "Smash" << Color::RESET
              << ") and " << Color::BOLD << "Lunge" << Color::RESET << " (" << Color::YELLOW << "Pierce" << Color::RESET << ")\n";
    std::cout << "  in your hand. Every enemy is weak to one type for +50% damage -\n";
    std::cout << "  check View Enemy to see which.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << "ENDING YOUR TURN" << Color::RESET << "\n\n";
    std::cout << "  Done playing cards? Pick \"End Turn\" and the enemy acts. \"View\n";
    std::cout << "  Enemy\" shows their weakness/resistance; \"Status\" shows your own\n";
    std::cout << "  HP, armor, and active effects in detail.\n\n";
    std::cout << "  Your turn.\n\n";
    UIHelper::waitForKey("  (press any key to begin)");

    // Snapshot everything the tutorial touches, so a real run starts clean afterward
    // regardless of how this practice fight goes.
    int savedHealth        = playerHealth;
    int savedArmor         = playerArmor;
    int savedArmorPersist  = playerArmorPersistTurns;
    int savedEnergy        = playerEnergy;
    int savedTurn          = turnNumber;
    bool savedTurnActive   = playerTurnActive;
    bool savedInEncounter  = inEncounter;

    enemy = Enemy("Training Dummy", 35, 4, 2, EnemyType::MELEE);
    playerHealth = maxPlayerHealth;
    playerArmor = 0;
    playerArmorPersistTurns = 0;
    playerStatus.reset();
    counterAttackActive = false;
    parryActive = false;
    turnNumber = 1;
    playerTurnActive = true;
    inEncounter = true;
    resetEnergy();
    playerDeck.resetDeck();
    int drawCount = 5 + upgrades.getDrawBonus();
    for (int i = 0; i < drawCount; ++i) {
        try { playerDeck.drawCard(); } catch (...) { break; }
    }

    UIHelper::clearScreen();
    std::cout << "\n" << Color::DIM << "Tip: " << Color::RESET << Color::ENERGY_CLR << playerEnergy << "/" << maxEnergy
              << " plays" << Color::RESET << " up top is your energy for the turn - every card's cost comes\n";
    std::cout << "  out of that pool, so you can't play more than you can afford.\n\n";
    UIHelper::waitForKey("  (press any key to start turn 1)");

    // Capped by turnNumber (bumped only when a turn actually ends via End Turn),
    // not by call count - freely checking View Enemy/Status shouldn't burn a "turn".
    const int startTurn = turnNumber;
    const int maxTutorialTurns = 6;
    bool dummyDefeated = false;
    bool attackTipShown = false, defendTipShown = false, specialTipShown = false, damageTypeTipShown = false;
    while (playerHealth > 0 && turnNumber - startTurn < maxTutorialTurns) {
        handleInput();

        // Read back what was actually played, via state playCardFromHand() sets -
        // this survives even if the same call also auto-ended the turn and reset
        // the hand (which would otherwise wipe any "used" flags we'd diffed against).
        if (lastActionWasCardPlay) {
            if (!attackTipShown && lastPlayedCardType == CardType::ATTACK) {
                attackTipShown = true;
                std::cout << "\n" << Color::DIM << "Tip: " << Color::RESET << "the dummy's " << Color::BLUE << "DEF" << Color::RESET
                          << " (defense) soaks up part of your attack's DMG\n";
                std::cout << "  value - that's why the damage dealt came in lower than the card's number.\n\n";
                UIHelper::waitForKey();
            } else if (!defendTipShown && lastPlayedCardType == CardType::DEFEND) {
                defendTipShown = true;
                std::cout << "\n" << Color::DIM << "Tip: " << Color::RESET << "when the enemy hits you, their attack's damage\n";
                std::cout << "  is subtracted by your armor first - that's what DEFEND cards are for.\n\n";
                UIHelper::waitForKey();
            } else if (!specialTipShown && lastPlayedCardType == CardType::SPECIAL) {
                specialTipShown = true;
                std::cout << "\n" << Color::DIM << "Tip: " << Color::RESET << "SPECIAL cards often only do something under a\n";
                std::cout << "  condition. Parry, for example, only blocks and ripostes if the enemy actually\n";
                std::cout << "  attacks this turn - if they don't, it does nothing.\n\n";
                UIHelper::waitForKey();
            }

            if (!damageTypeTipShown && lastPlayedCardType == CardType::ATTACK
                && (lastPlayedPhysType != DamageType::NONE || lastPlayedPhysType2 != DamageType::NONE)) {
                damageTypeTipShown = true;
                bool matched = lastPlayedPhysType == enemy.getWeakness() || lastPlayedPhysType2 == enemy.getWeakness();
                if (matched) {
                    std::cout << "\n" << Color::DIM << "Tip: " << Color::RESET << "that card's damage-type tag matched the dummy's\n";
                    std::cout << "  weakness, so it hit for +50% bonus damage.\n\n";
                } else {
                    std::cout << "\n" << Color::DIM << "Tip: " << Color::RESET << "that card's damage-type tag didn't match the\n";
                    std::cout << "  dummy's weakness this time, so no bonus. Check View Enemy to see what an\n";
                    std::cout << "  enemy IS weak to before picking your attacks.\n\n";
                }
                UIHelper::waitForKey();
            }
        }

        if (!enemy.isAlive()) { dummyDefeated = true; break; }
    }

    UIHelper::clearScreen();
    if (dummyDefeated) {
        std::cout << "\n" << Color::BOLD << Color::GREEN << "Training Dummy defeated!" << Color::RESET << "\n\n";
    } else {
        std::cout << "\n" << Color::BOLD << Color::CYAN << "Tutorial complete." << Color::RESET << "\n\n";
    }
    std::cout << "  That's the core loop. Good luck out there.\n\n";
    UIHelper::waitForKey();

    UIHelper::clearScreen();
    std::cout << "\n" << Color::BOLD << Color::CYAN << "AFTER A REAL WIN" << Color::RESET << "\n\n";
    std::cout << "  Winning a real fight takes you to a rest site with:\n\n";
    std::cout << "  " << Color::HEAL << "Rest" << Color::RESET << "       heal to full HP\n";
    std::cout << "  " << Color::YELLOW << "Forge" << Color::RESET << "      upgrade a card - all copies of it upgrade together\n";
    std::cout << "  " << Color::CYAN << "View Deck" << Color::RESET << "  browse your deck, discard cards you don't want\n";
    std::cout << "  " << Color::DIM << "Skip" << Color::RESET << "       move on without doing any of the above\n\n";
    std::cout << "  Rest, Forge, and Skip all move you to the next fight; View Deck\n";
    std::cout << "  lets you keep browsing until you choose Return.\n\n";
    UIHelper::waitForKey("  (press any key to return to the menu)");

    // Restore pre-tutorial state so the real run starts clean
    playerHealth = savedHealth;
    playerArmor = savedArmor;
    playerArmorPersistTurns = savedArmorPersist;
    playerStatus.reset();
    counterAttackActive = false;
    parryActive = false;
    turnNumber = savedTurn;
    playerTurnActive = savedTurnActive;
    inEncounter = savedInEncounter;
    playerEnergy = savedEnergy;
    playerDeck.resetDeck();
}

void Game::run() {
    init();

    UIHelper::printTitle();

    if (!showMainMenu()) {
        return;
    }

    upgrades.checkAndUnlockUpgrades(0, 0);
    upgrades.displayUpgradeInfo();
    
    currentRun.startRun();
    startEncounter();
    
    while (running) {
        handleInput();
        
        if (checkGameOver()) {
            if (enemy.isAlive()) {
                displayGameOver();
                currentRun.displayRunStats();
                int encounters = currentRun.getEncountersWon();
                runStats.completeRun(encounters);
                runStats.displayRunSummary(encounters);
                
                currentRun.loseRun();
                inEncounter = false;
            } else {
                handleEncounterWin();
                if (inEncounter) {
                    continue;
                }
                int encounters = currentRun.getEncountersWon();
                runStats.completeRun(encounters);
                runStats.displayRunSummary(encounters);
            }
            
            if (handleGameOverInput()) {
                selectUpgrades();
                maxPlayerHealth = 100;
                playerHealth = maxPlayerHealth;
                playerArmor = 0;
                playerEnergy = 3;
                maxEnergy = 3;
                equipDamageBonus = 0;
                equipArmorBonus  = 0;
                turnNumber = 1;
                playerTurnActive = true;
                playerDeck = Deck();
                init();
                
                runStats.resetRunStats();
                currentRun = Run();
                currentRun.startRun();
                
                upgrades.displayUpgradeInfo();
                startEncounter();
            } else {
                running = false;
                runStats.displayCumulativeStats();
            }
        }
    }

    std::cout << "Thanks for playing!\n";
}
