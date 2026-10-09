// Everything written to disk: the save slots, the progress and
// achievements that outlive a run, and the settings.
#include "Game.h"
#include "Achievements.h"
#include "Audio.h"
#include "Colors.h"
#include "UIHelper.h"
#include "EnemyArt.h"
#include "ProjectileTable.h"
#include "Console.h"
#include "CardBar.h"
#include "Hud.h"
#include "Platform.h"
#include <SDL.h>
#include <algorithm>
#include <iostream>
#include <random>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <stdexcept>
#include "GameShared.h"

static const char* effectToStr(CardEffect e) {
    switch (e) {
        case CardEffect::POISON:     return "POISON";
        case CardEffect::BURN:       return "BURN";
        case CardEffect::REND:       return "REND";
        case CardEffect::STUN:       return "STUN";
        case CardEffect::WEAK:       return "WEAK";
        case CardEffect::COUNTER:    return "COUNTER";
        case CardEffect::PARRY:      return "PARRY";
        case CardEffect::PIERCE:     return "PIERCE";
        case CardEffect::FORTIFY:    return "FORTIFY";
        case CardEffect::STRENGTH:   return "STRENGTH";
        case CardEffect::DOUBLE_HIT: return "DOUBLE_HIT";
        case CardEffect::IMPAIR:     return "IMPAIR";
        case CardEffect::CHIP:       return "CHIP";
        case CardEffect::HEAL:       return "HEAL";
        case CardEffect::WARD:       return "WARD";
        case CardEffect::TAUNT:      return "TAUNT";
        case CardEffect::FEAR:       return "FEAR";
        case CardEffect::TRUESTRIKE: return "TRUESTRIKE";
        case CardEffect::TRUE_DOUBLE: return "TRUE_DOUBLE";
        case CardEffect::SCRAP:          return "SCRAP";
        case CardEffect::RECKLESS:       return "RECKLESS";
        case CardEffect::SELFWEAK:       return "SELFWEAK";
        case CardEffect::OVEREXTEND:     return "OVEREXTEND";
        case CardEffect::BLOODPRICE:     return "BLOODPRICE";
        case CardEffect::WILDCHARGE:     return "WILDCHARGE";
        case CardEffect::BERSERK:        return "BERSERK";
        case CardEffect::TURTLE:         return "TURTLE";
        case CardEffect::EMBERBLADE:     return "EMBERBLADE";
        case CardEffect::ADRENALINE:     return "ADRENALINE";
        case CardEffect::SHATTERPOINT:   return "SHATTERPOINT";
        case CardEffect::BLOODPACT:      return "BLOODPACT";
        case CardEffect::UNSTABLEWARD:   return "UNSTABLEWARD";
        case CardEffect::ALLIN:          return "ALLIN";
        case CardEffect::LASTSTAND:      return "LASTSTAND";
        case CardEffect::BORROWED:       return "BORROWED";
        case CardEffect::PACTRUIN:       return "PACTRUIN";
        case CardEffect::SACRIFICE:      return "SACRIFICE";
        case CardEffect::RAISE:          return "RAISE";
        case CardEffect::UNLEASH:        return "UNLEASH";
        case CardEffect::TURNABOUT:      return "TURNABOUT";
        case CardEffect::FEINT:          return "FEINT";
        default:                     return "NONE";
    }
}
static CardEffect strToEffect(const std::string& s) { return Card::effectFromString(s); }
static const char* dmgToStr(DamageType t) {
    switch (t) {
        case DamageType::SMASH:  return "SMASH";
        case DamageType::PIERCE: return "PIERCE";
        case DamageType::FIRE:   return "FIRE";
        case DamageType::POISON: return "POISON";
        case DamageType::WIND:   return "WIND";
        default:                 return "NONE";
    }
}
static DamageType strToDmg(const std::string& s) { return Card::damageTypeFromString(s); }
static const char* cardTypeToStr(CardType t) {
    switch (t) {
        case CardType::ATTACK: return "ATTACK";
        case CardType::DEFEND: return "DEFEND";
        default:                return "SPECIAL";
    }
}
static CardType strToCardType(const std::string& s) {
    if (s == "ATTACK") return CardType::ATTACK;
    if (s == "DEFEND") return CardType::DEFEND;
    return CardType::SPECIAL;
}

// The full road's own: its clears and long hauls, and everything that only
// happens on it (the vigils, the Moonstruck, the true form). The quick road
// cannot earn these.
static bool fullRoadOnly(int id) {
    switch (id) {
        case Achievements::CLEAR: case Achievements::HEAD_START: case Achievements::NO_REST:
        case Achievements::NO_DISCARD: case Achievements::NO_BOONS: case Achievements::NO_RELICS:
        case Achievements::MAGICIAN: case Achievements::ONE_TRICK: case Achievements::NO_LEGENDS:
        case Achievements::TRUE_FORM: case Achievements::OWN_BLOW:
        case Achievements::VIGIL: case Achievements::EVERY_VIGIL:
        case Achievements::MOONSTRUCK: case Achievements::MEET_MOON: case Achievements::ALL_FORMS:
        case Achievements::HARD: case Achievements::RANDOM_HARD:
        case Achievements::FALSE_MOON:
            return true;
        default:
            return false;
    }
}

std::string Game::progressPath() const { return Audio::saveDir() + "progress.dat"; }

std::string Game::settingsPath() const { return Audio::saveDir() + "settings.cfg"; }

void Game::loadSettings() {
    std::ifstream in(settingsPath());
    if (!in.is_open()) return;
    std::string tag;
    while (in >> tag) {
        if      (tag == "TEXT")  in >> optTextSpeed;
        else if (tag == "PACE")  in >> optPace;
        else if (tag == "MUSIC") in >> optMusic;
        else if (tag == "SFX")   in >> optSfx;
        else if (tag == "CONFIRM") in >> optConfirm;
        else if (tag == "MENUSFX") in >> optMenuSounds;
    }
    optConfirm = std::max(0, std::min(2, optConfirm));
    optMenuSounds = optMenuSounds ? 1 : 0;
    applySettings();
}

void Game::saveSettings() const {
    std::ofstream out(settingsPath(), std::ios::trunc);
    if (!out.is_open()) return;
    out << "MOONSTRUCK_SETTINGS_V1\n"
        << "TEXT "  << optTextSpeed << "\n"
        << "PACE "  << optPace      << "\n"
        << "MUSIC " << optMusic     << "\n"
        << "SFX "   << optSfx       << "\n"
        << "CONFIRM " << optConfirm << "\n"
        << "MENUSFX " << optMenuSounds << "\n";
}

// The values reach different systems, so this is the one place that knows
// all of them and the only thing the screen has to call.
void Game::applySettings() const {
    UIHelper::setTextSpeed(optTextSpeed);
    Platform::setPacePercent(optPace);
    Audio::setMusicVolume(optMusic);
    Audio::setSfxVolume(optSfx);
    Audio::setMenuSounds(optMenuSounds != 0);
}

void Game::showSettings() {
    // Six settings and a way out. Left and right move the row under the
    // cursor, and the value is applied as it moves, so a volume is heard
    // while it is being set rather than after the screen closes.
    auto speedWord = [](int v) {
        return std::to_string(v) + "%   " + (v == 0 ? "instant" : v < 80 ? "slow"
                                           : v <= 140 ? "normal" : "fast");
    };
    // Stored as how long each beat holds, so a bigger number is a slower
    // game. Shown the other way round: on a speed control, right is faster.
    auto paceWord = [](int v) {
        return std::to_string(300 - v) + "%   " + (v >= 190 ? "slow" : v >= 130 ? "normal"
                                                 : v >= 95 ? "quick" : "snappy");
    };
    auto volWord = [](int v) {
        return std::to_string(v) + "%   " + (v == 0 ? "off" : v < 35 ? "quiet"
                                           : v < 75 ? "medium" : "full");
    };
    auto confirmWord = [](int v) {
        return std::string(v == 0 ? "ask every time" : v == 1 ? "skip the battle one" : "skip all of them");
    };
    auto onOffWord = [](int v) { return std::string(v ? "on" : "off"); };
    auto applied = [this]() { applySettings(); };
    // Turned on, it answers with its own sound; turned off, it cannot.
    auto tapped  = [this]() { applySettings(); Audio::menuSelect(); };
    auto heard   = [this]() { applySettings(); if (optSfx > 0) Audio::playSFX("special"); };

    // The pace row keeps its range the right way up and flips the bar
    // instead, so the value stays clampable and right still means faster.
    CardBar::Action pace{ "Combat pace", &optPace, 60, 240, 10, paceWord, applied };
    pace.invert = true;
    // The press-a-key before each of your turns, or that and every yes/no and
    // result screen. Story pauses and overwriting a save always ask. Three
    // named settings, so a picker rather than a bar.
    CardBar::Action confirms{ "Confirmations", &optConfirm, 0, 2, 1, confirmWord, applied };
    confirms.picker = true;
    CardBar::Action menuSounds{ "Menu sounds", &optMenuSounds, 0, 1, 1, onOffWord, tapped };
    menuSounds.picker = true;
    std::vector<CardBar::Action> acts{
        CardBar::Action{ "Text speed",  &optTextSpeed,   0, 300, 20, speedWord, applied },
        pace,
        CardBar::Action{ "Music",       &optMusic,       0, 100,  5, volWord,   applied },
        CardBar::Action{ "Sound",       &optSfx,         0, 100,  5, volWord,   heard   },
        menuSounds,
        confirms,
        CardBar::Action{ "Back", false },
    };
    CardBar::pick("Settings        left and right to set", {}, acts, 0);
    saveSettings();
}

std::string Game::winSavePath() const { return Audio::saveDir() + "winrun.dat"; }

// What has been cleared. Its own file beside the three slots, because dying
// clears a slot and this is the one thing a death must never take away.
void Game::loadProgress() {
    std::ifstream in(progressPath());
    if (!in.is_open()) return;
    std::string tag;
    unsigned long long ach = 0, ach2 = 0, sits = 0;
    int forms = 0, beaten = 0, moonOut = 0;
    while (in >> tag) {
        if (tag == "CLEARED") in >> clearedMask;
        else if (tag == "SETS") in >> unlockedSets;
        else if (tag == "ACH") in >> ach;
        else if (tag == "ACH2") in >> ach2;
        else if (tag == "FORMS") in >> forms;
        else if (tag == "BEATEN") in >> beaten;
        else if (tag == "SITS") in >> sits;
        else if (tag == "MOON") in >> moonOut;
    }
    // Clears from before there were achievements still count, quietly.
    if (clearedMask & 1) ach |= 1ULL << Achievements::CLEAR;
    if (clearedMask & 6) ach |= 1ULL << Achievements::HARD;
    Achievements::setMask(ach);
    Achievements::setMask(ach2, 1);
    Achievements::setFormsSeen(forms);
    Achievements::setFormsBeaten(beaten);
    Achievements::setSitsHeard(sits);
    // Saves from before the record still put it out if they hold the crimson one.
    Achievements::setFalseMoonBeaten(moonOut != 0 || Achievements::has(Achievements::FALSE_MOON));
}

void Game::saveProgress() const {
    std::ofstream out(progressPath(), std::ios::trunc);
    if (!out.is_open()) return;
    out << "MOONSTRUCK_PROGRESS_V1\n";
    out << "CLEARED " << clearedMask << "\n";
    out << "SETS " << unlockedSets << "\n";
    out << "ACH " << Achievements::mask() << "\n";
    out << "ACH2 " << Achievements::mask(1) << "\n";
    out << "FORMS " << Achievements::formsSeen() << "\n";
    out << "BEATEN " << Achievements::formsBeaten() << "\n";
    out << "SITS " << Achievements::sitsHeard() << "\n";
    out << "MOON " << (Achievements::falseMoonBeaten() ? 1 : 0) << "\n";
}

void Game::earn(int achievement) {
    if (onQuick() && fullRoadOnly(achievement)) return;
    if (Achievements::earn(achievement)) {
        // The last of the rest, with the False Moon already put out, brings
        // the crimson one with it, whichever road it fell on.
        if (Achievements::crimsonDue()) Achievements::earn(Achievements::FALSE_MOON);
        saveProgress();
    }
}

// A heartbeat under any fight you are losing: at a fifth of your health or
// less it beats until you climb back above that or the fight is over.
void Game::updateHeartbeat() {
    const bool low = inEncounter && playerHealth > 0 && playerHealth * 5 <= maxPlayerHealth;
    if (low) Audio::startLoop("heartbeat");
    else     Audio::stopLoop();
}

void Game::checkDeckAchievements() {
    for (const Card& c : playerDeck.getAllCardsOrdered())
        if (c.isLegendary()) { earn(Achievements::LEGEND); return; }
}

// A clear: record the road walked, and keep the run that walked it so the
// harder roads can be started with that deck.
void Game::recordClear() {
    clearedMask |= (runMode == Mode::HARD) ? 2 : (runMode == Mode::RANDOM_HARD) ? 4 : 1;
    // What you take off the thing you just put down. Finishing the fifty is
    // the Shadow Knight's own plate and blade; the moon's comes off the hard
    // road, which is the only place it was ever going to come from.
    if (runMode == Mode::HARD || runMode == Mode::RANDOM_HARD) unlockedSets |= 2;
    else                                                       unlockedSets |= 1;
    if (runMode == Mode::HARD || runMode == Mode::RANDOM_HARD) earn(Achievements::HARD);
    if (runMode == Mode::RANDOM_HARD) earn(Achievements::RANDOM_HARD);
    saveProgress();
    writeWinSave();
}

void Game::writeWinSave() const {
    std::ofstream out(winSavePath(), std::ios::trunc);
    if (out.is_open()) writeSaveTo(out);
}

bool Game::loadWinSave() {
    std::ifstream in(winSavePath());
    return in.is_open() && loadSaveFrom(in);
}

bool Game::hasWinSave() const {
    std::error_code ec;
    return std::filesystem::exists(winSavePath(), ec);
}

std::string Game::savePath(int slot) const {
    return Audio::saveDir() + "save" + std::to_string(slot) + ".dat";
}

bool Game::saveExists(int slot) const {
    return std::filesystem::exists(savePath(slot));
}

bool Game::anySaveExists() const {
    for (int i = 1; i <= SAVE_SLOTS; i++)
        if (saveExists(i)) return true;
    return false;
}

// The single save.dat from before save slots. Moved into slot 1 rather than
// abandoned, so an in-progress run survives the update.
void Game::migrateLegacySave() const {
    const std::string legacy = Audio::saveDir() + "save.dat";
    if (!std::filesystem::exists(legacy) || saveExists(1)) return;
    std::error_code ec;
    std::filesystem::rename(legacy, savePath(1), ec);
}

// Enough of the file to tell the slots apart, read without disturbing the run.
std::string Game::saveSummary(int slot) const {
    std::ifstream in(savePath(slot));
    if (!in.is_open()) return "";
    std::string line;
    if (!std::getline(in, line) || line != "SAVE_V1") return "damaged save";
    int encounter = 0, won = 0;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        if (tag == "ENCOUNTER") iss >> encounter;
        else if (tag == "WON")  iss >> won;
        else if (line.rfind("CARD|", 0) == 0) break;   // the header is all above the deck
    }
    if (encounter <= 0) return "damaged save";
    // Past 50 is the church, which counts down to the False Moon at 0.
    return "Encounter " + std::to_string(encounter > 50 ? encounter - 60 : encounter) + ", "
         + std::to_string(won) + (won == 1 ? " win" : " wins");
}

// Shared by saving and loading. Saving offers every slot (an occupied one is
// overwritten, after a confirmation); loading offers only the filled ones.
int Game::chooseSaveSlot(const std::string& title, bool forSaving) {
    while (true) {
        // Rows, not card widgets: a slot is a line of text.
        std::vector<CardBar::Action> acts;
        std::vector<int> slotOf;
        for (int i = 1; i <= SAVE_SLOTS; i++) {
            const std::string summary = saveSummary(i);
            const bool empty = summary.empty();
            acts.push_back(CardBar::Action{ "Slot " + std::to_string(i),
                                            empty ? "empty" : summary,
                                            /*disabled*/!forSaving && empty });
            slotOf.push_back(i);
        }
        acts.push_back(CardBar::Action{ forSaving ? "Don't save" : "Back",
                                        forSaving ? "carry on without saving" : "back to the menu", false });
        // Said on the screen where it matters. A death clears the slot the
        // run was saved into, and finding that out afterwards is no good.
        const std::string heading = forSaving
            ? title + "        dying deletes the save"
            : title;
        int choice = CardBar::pick(heading, {}, acts, 0);
        if (choice < 0 || choice >= (int)slotOf.size()) return 0;
        const int slot = slotOf[choice];
        if (!forSaving && saveSummary(slot).empty()) continue;   // an empty slot has nothing to load
        if (forSaving && !saveSummary(slot).empty()
            && !confirm("Overwrite slot " + std::to_string(slot) + "?", /*always*/true)) continue;
        return slot;
    }
}

void Game::saveGame(int slot) {
    currentSaveSlot = slot;
    std::ofstream out(savePath(slot), std::ios::trunc);
    if (!out.is_open()) {
        std::cout << Color::YELLOW << "Warning: couldn't write the save file." << Color::RESET << "\n";
        return;
    }
    writeSaveTo(out);
}

// The save file itself. Split out so the permanent win snapshot is written by
// the same code that writes a slot, and can never drift from it.
void Game::writeSaveTo(std::ostream& out) const {
    out << "SAVE_V1\n";
    out << "MODE " << (int)runMode << "\n";
    out << "RANDSEED " << randomSeed << "\n";
    out << "ROADBONUS " << roadBonus << " " << roadGearPct << "\n";
    out << "ENCOUNTER " << currentRun.getCurrentEncounter() << "\n";
    out << "WON " << currentRun.getEncountersWon() << "\n";
    out << "MAXHP " << maxPlayerHealth << "\n";
    out << "HP " << playerHealth << "\n";
    out << "MAXENERGY " << maxEnergy << "\n";
    // Written for readability only - both are rebuilt from the tiers on load.
    out << "EQUIPDMG " << equipDamagePercent << "\n";
    out << "EQUIPARM " << equipArmorPercent << "\n";
    out << "LUCK " << runLuck << "\n";
    out << "HANDBONUS " << handSizeBonus << "\n";
    out << "REWARDBONUS " << rewardChoiceBonus << "\n";
    out << "ATTUNE " << attunementBoons << "\n";
    out << "GEARINT " << gearInterval << "\n";
    out << "SEALS " << sealsBroken << "\n";
    out << "RELICS " << relicsOwned << " " << (redThreadUsed ? 1 : 0) << "\n";
    out << "MOONSEEN " << moonZonesSeen << "\n";
    out << "MOONMET " << moonstruckMet << "\n";
    out << "MOONBEATEN " << moonZonesBeaten << "\n";
    out << "SAT " << satCount << "\n";
    out << "RESTED " << (restedThisRun ? 1 : 0) << "\n";
    out << "DISCARDED " << (discardedThisRun ? 1 : 0) << "\n";
    out << "NONDOT " << (playedNonDot ? 1 : 0) << "\n";
    out << "LASTDEAD " << (lastUndead.empty() ? std::string("-") : lastUndead) << "\n";
    out << "WORN " << wornWeapon << " " << wornArmor << "\n";
    out << "WEAPONTIER " << weaponTier << "\n";
    out << "ARMORTIER " << armorTier << "\n";
    for (int i = 0; i < 5; i++)
        out << "UPGRADE " << i << " " << (upgrades.isUnlocked(i) ? 1 : 0) << " " << (upgrades.isActive(i) ? 1 : 0) << "\n";

    for (const Card& c : playerDeck.getAllCardsOrdered()) {
        out << "CARD|" << c.getName() << "|" << c.getDescription() << "|" << cardTypeToStr(c.getType())
            << "|" << c.getCost() << "|" << c.getValue() << "|" << effectToStr(c.getEffect())
            << "|" << (c.isRare() ? 1 : 0) << "|" << (c.isSuperRare() ? 1 : 0) << "|" << (c.isLegendary() ? 1 : 0)
            << "|" << dmgToStr(c.getPhysType()) << "|" << dmgToStr(c.getPhysType2()) << "|" << dmgToStr(c.getElemType())
            << "|" << c.getUpgradeCount() << "\n";
    }
}

void Game::deleteSave(int slot) const {
    std::error_code ec;
    std::filesystem::remove(savePath(slot), ec);
}

// Dying only costs the run that was being played.
void Game::deleteCurrentSave() {
    if (currentSaveSlot <= 0) return;
    deleteSave(currentSaveSlot);
    currentSaveSlot = 0;
}

bool Game::loadGame(int slot) {
    std::ifstream in(savePath(slot));
    if (!in.is_open()) return false;
    currentSaveSlot = slot;
    return loadSaveFrom(in);
}

bool Game::loadSaveFrom(std::istream& in) {
    std::string line;
    if (!std::getline(in, line) || line != "SAVE_V1") return false;

    int savedEncounter = 1, savedWon = 0, savedMaxHp = 100, savedHp = 100, savedMaxEnergy = 3;
    int savedEquipDmg = 0, savedEquipArm = 0, savedWeaponTier = 0, savedArmorTier = 0;
    // Boons. Default 0, so a save written before they existed loads as a run that
    // simply never took one rather than failing to parse.
    int savedLuck = 0, savedHandBonus = 0, savedRewardBonus = 0;
    int savedAttune = 0, savedGearInt = 3;
    int savedSeals = 0, savedMoonSeen = 0, savedWornW = -1, savedWornA = -1;
    int savedRelics = 0, savedThread = 0, savedMoonMet = 0, savedSat = 0;
    int savedMoonBeaten = 0;   // a save from before it was kept has none beaten this run
    // A save from before these were written cannot say whether its run rested
    // or threw a card away, so it counts as having done both: the two clean
    // clears are for runs that can show they did neither.
    int savedRested = 1, savedDiscarded = 1, savedNonDot = 1;
    std::string savedLastDead;   // a save from before Raise Undead remembers none
    int savedMode = 0;
    unsigned savedSeed = 0;
    int savedRoadBonus = 0, savedRoadGear = 0;
    std::vector<bool> unlockedFlags(5, false), activeFlags(5, false);
    std::vector<Card> loadedCards;

    while (std::getline(in, line)) {
        if (line.empty()) continue;

        if (line.rfind("CARD|", 0) == 0) {
            std::vector<std::string> f;
            size_t pos = 5;
            while (pos <= line.size()) {
                size_t next = line.find('|', pos);
                if (next == std::string::npos) { f.push_back(line.substr(pos)); break; }
                f.push_back(line.substr(pos, next - pos));
                pos = next + 1;
            }
            if (f.size() != 13) continue; // corrupted line - skip rather than abort the whole load
            try {
                loadedCards.push_back(Card(
                    f[0], f[1], strToCardType(f[2]), std::stoi(f[3]), std::stoi(f[4]),
                    strToEffect(f[5]), f[6] == "1", strToDmg(f[9]), strToDmg(f[11]),
                    f[7] == "1", strToDmg(f[10]), f[8] == "1", std::stoi(f[12])));
            } catch (...) { continue; }
            continue;
        }

        std::istringstream iss(line);
        std::string tag;
        iss >> tag;
        if (tag == "ENCOUNTER")     iss >> savedEncounter;
        else if (tag == "WON")      iss >> savedWon;
        else if (tag == "MAXHP")    iss >> savedMaxHp;
        else if (tag == "HP")       iss >> savedHp;
        else if (tag == "MAXENERGY") iss >> savedMaxEnergy;
        else if (tag == "EQUIPDMG") iss >> savedEquipDmg;
        else if (tag == "EQUIPARM") iss >> savedEquipArm;
        else if (tag == "LUCK")        iss >> savedLuck;
        else if (tag == "HANDBONUS")   iss >> savedHandBonus;
        else if (tag == "REWARDBONUS") iss >> savedRewardBonus;
        else if (tag == "ATTUNE")      iss >> savedAttune;
        else if (tag == "GEARINT")     iss >> savedGearInt;
        else if (tag == "SEALS")       iss >> savedSeals;
        else if (tag == "RELICS")      iss >> savedRelics >> savedThread;
        else if (tag == "MOONSEEN")    iss >> savedMoonSeen;
        else if (tag == "MOONMET")     iss >> savedMoonMet;
        else if (tag == "MOONBEATEN")  iss >> savedMoonBeaten;
        else if (tag == "SAT")         iss >> savedSat;
        else if (tag == "RESTED")      iss >> savedRested;
        else if (tag == "DISCARDED")   iss >> savedDiscarded;
        else if (tag == "NONDOT")      iss >> savedNonDot;
        else if (tag == "LASTDEAD")    { iss >> savedLastDead; if (savedLastDead == "-") savedLastDead.clear(); }
        else if (tag == "MODE")        iss >> savedMode;
        else if (tag == "RANDSEED")    iss >> savedSeed;
        else if (tag == "ROADBONUS")   iss >> savedRoadBonus >> savedRoadGear;
        else if (tag == "WORN")        iss >> savedWornW >> savedWornA;
        else if (tag == "WEAPONTIER") iss >> savedWeaponTier;
        else if (tag == "ARMORTIER") iss >> savedArmorTier;
        else if (tag == "UPGRADE") {
            int idx, unl, act;
            iss >> idx >> unl >> act;
            if (idx >= 0 && idx < 5) { unlockedFlags[idx] = unl != 0; activeFlags[idx] = act != 0; }
        }
    }

    if (loadedCards.empty()) return false; // no usable deck - treat as a failed load

    playerDeck = Deck();
    for (const Card& c : loadedCards) playerDeck.addCard(c);
    playerDeck.shuffle();

    currentRun = Run();
    currentRun.loadState(savedEncounter, savedWon);

    maxPlayerHealth  = savedMaxHp;
    playerHealth     = std::min(savedHp, savedMaxHp);
    maxEnergy        = savedMaxEnergy;
    playerEnergy     = maxEnergy;
    weaponTier       = savedWeaponTier;
    armorTier        = savedArmorTier;
    runLuck           = savedLuck;
    handSizeBonus     = savedHandBonus;
    rewardChoiceBonus = savedRewardBonus;
    attunementBoons   = savedAttune;
    gearInterval      = std::max(2, savedGearInt);
    sealsBroken       = std::max(0, savedSeals);
    relicsOwned       = savedRelics;
    redThreadUsed     = savedThread != 0;
    moonZonesSeen     = savedMoonSeen;
    moonstruckMet     = std::max(0, savedMoonMet);
    moonZonesBeaten   = savedMoonBeaten & 31;
    satCount          = std::max(0, savedSat);
    restedThisRun     = savedRested != 0;
    discardedThisRun  = savedDiscarded != 0;
    playedNonDot      = savedNonDot != 0;
    lastUndead        = raisedDead(savedLastDead) ? savedLastDead : std::string();
    runMode           = (savedMode >= 0 && savedMode <= (int)Mode::QUICK) ? (Mode)savedMode : Mode::NORMAL;
    randomSeed        = savedSeed;
    roadBonus         = std::max(0, savedRoadBonus);
    roadGearPct       = std::max(0, savedRoadGear);
    currentRun.setDifficulty(difficultyFor(runMode));
    currentRun.setQuick(runMode == Mode::QUICK);
    buildRandomOrder();
    // A save from before the equipment tab wears the newest of everything.
    // Clamped to what the save found, which runs past six with a trophy set.
    const int cap = maxGearTier();
    wornWeapon = std::min(std::min(cap, savedWeaponTier), savedWornW < 0 ? cap : savedWornW);
    wornArmor  = std::min(std::min(cap, savedArmorTier),  savedWornA < 0 ? cap : savedWornA);
    // Recomputed rather than restored, so a save written before gear became a
    // percentage loads as the right percentage instead of a stale flat number.
    (void)savedEquipDmg; (void)savedEquipArm;
    equipDamagePercent = gearPercentFor(weaponTier, true);
    equipArmorPercent  = gearPercentFor(armorTier, false);
    for (int i = 0; i < 5; i++) upgrades.setUpgradeState(i, unlockedFlags[i], activeFlags[i]);

    turnNumber = 1;
    playerTurnActive = true;
    inEncounter = false; // not in combat yet - offerContinueOrEndRun() starts the next fight if Continue is chosen
    running = true;

    return true;
}
