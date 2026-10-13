// Stuff you run into on the road between fights: the Anvil (weld two cards
// into one), the Offering (three cards of a rarity for one, maybe a rarity
// up), View Deck's trade, and the camps you find after beating a Moon Shade.
#include "Game.h"
#include "Achievements.h"
#include "Audio.h"
#include "CardBar.h"
#include "Colors.h"
#include "Console.h"
#include "EnemyArt.h"
#include "Hud.h"
#include "Platform.h"
#include "UIHelper.h"
#include <algorithm>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <vector>
#include "GameShared.h"

namespace {
// When each stop turns up. The Anvil once you've picked a few cards, the
// Offering after both rarity unlocks so any three can go up.
const int ANVIL_AFTER = 15, OFFERING_AFTER = 25;
// There's a second of each too: the Unchosen's anvil after it falls (48) and
// the Paladin's shrine after him (46). At encounter 15 most decks have nothing
// worth welding yet, so this gives it another go. They only show up after the
// first one, so a random road can't hit them out of order.
// Chance to go up a rarity before Fortune, by the rarity you give.
const int UP_CHANCE[4] = { 75, 50, 25, 10 };   // bumped up after playtesting
const char* const RARITY[5] = { "starter", "uncommon", "rare", "super rare", "legendary" };

std::string aRarity(int rank) {
    const char* r = RARITY[std::max(0, std::min(4, rank))];
    return std::string(r[0] == 'u' ? "an " : "a ") + r;
}

std::string listNames(const std::vector<Card>& cards) {
    std::string s;
    for (size_t i = 0; i < cards.size(); ++i)
        s += (i == 0 ? "" : i + 1 == cards.size() ? " and " : ", ") + cards[i].getName();
    return s;
}
} // namespace

void Game::roadStops() {
    if (currentRun.inChurch()) return;
    const int at = currentRun.getRoadPosition();
    if (!(stopsDone & 1) && at >= ANVIL_AFTER)    { stopsDone |= 1; anvilStop(false); }
    if (!(stopsDone & 2) && at >= OFFERING_AFTER) { stopsDone |= 2; offeringStop(false); }
    const std::string& who = enemy.getName();
    if (!(stopsDone & 4) && at > ANVIL_AFTER && who.find("Unchosen") != std::string::npos) {
        stopsDone |= 4;
        anvilStop(true);
    }
    if (!(stopsDone & 8) && at > OFFERING_AFTER && who.find("Paladin") != std::string::npos) {
        stopsDone |= 8;
        offeringStop(true);
    }
}

// Pick one card from the deck, 8 a page like the forge, with a count on
// duplicates. Returns the index into `cards`, or -1 for Back.
int Game::pickCardPaged(const std::string& title, const std::vector<Card>& cards, const std::vector<int>& counts) {
    const int PER = 8;
    const int pages = std::max(1, ((int)cards.size() + PER - 1) / PER);
    int page = 0;
    while (true) {
        const int first = page * PER, n = std::min(PER, (int)cards.size() - first);
        std::vector<CardBar::Card> widgets;
        for (int i = 0; i < n; ++i) {
            CardBar::Card w = cardWidget(cards[(size_t)(first + i)]);
            if (counts[(size_t)(first + i)] > 1) w.name += " x" + std::to_string(counts[(size_t)(first + i)]);
            widgets.push_back(w);
        }
        std::vector<CardBar::Action> acts;
        if (pages > 1) {
            acts.push_back(CardBar::Action{ "Previous page", page == 0 });
            acts.push_back(CardBar::Action{ "Next page", page >= pages - 1 });
        }
        acts.push_back(CardBar::Action{ "Back", false });
        UIHelper::clearScreen();
        const int choice = CardBar::pick(pages > 1 ? title + "   page " + std::to_string(page + 1) + "/"
                                                     + std::to_string(pages)
                                                   : title,
                                         widgets, acts, 4);
        if (choice <= -2) {   // "+" opens the full card text
            const int ci = -2 - choice;
            if (ci >= 0 && ci < n) {
                const Card& c = cards[(size_t)(first + ci)];
                CardBar::showDetail(widgets[(size_t)ci], c.getDescription(), widgets[(size_t)ci].typeLabel,
                                    rarityName(c), c.getUpgradeCount());
            }
            continue;
        }
        if (choice < 0) return -1;
        if (choice < n) return first + choice;
        const int act = choice - n;
        if (pages > 1 && act == 0) { page--; continue; }
        if (pages > 1 && act == 1) { page++; continue; }
        return -1;
    }
}

// The deck grouped by name, rarest first (same as the forge and View Deck),
// filtered by `keep`, minus whatever's already been picked.
static void groupCards(const std::vector<Card>& all, const std::vector<Card>& picked,
                       const std::function<bool(const Card&)>& keep, std::vector<Card>& cards,
                       std::vector<int>& counts) {
    cards.clear();
    counts.clear();
    for (const Card& c : all) {
        if (!keep(c)) continue;
        bool merged = false;
        for (size_t g = 0; g < cards.size(); ++g)
            if (cards[g].getName() == c.getName()) { counts[g]++; merged = true; break; }
        if (!merged) { cards.push_back(c); counts.push_back(1); }
    }
    for (const Card& p : picked)
        for (size_t g = 0; g < cards.size(); ++g)
            if (cards[g].getName() == p.getName()) { counts[g]--; break; }
    for (size_t g = cards.size(); g-- > 0; )
        if (counts[g] <= 0) { cards.erase(cards.begin() + (long)g); counts.erase(counts.begin() + (long)g); }
    // Rarest first, as everywhere else.
    std::vector<size_t> order(cards.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&](size_t a, size_t b) { return Game::rarityOf(cards[a]) > Game::rarityOf(cards[b]); });
    std::vector<Card> c2;
    std::vector<int> n2;
    for (size_t i : order) { c2.push_back(cards[i]); n2.push_back(counts[i]); }
    cards.swap(c2);
    counts.swap(n2);
}

// --- the Anvil ---------------------------------------------------------------
// Two cards become one: costs both, plays the first then the second, and
// only takes one slot in your hand.
void Game::anvilStop(bool unchosens) {
    bool ask = true;   // Pick again goes straight back to the cards
    while (true) {
        const std::vector<Card> all = playerDeck.getAllCardsOrdered();
        int weldable = 0;
        for (const Card& c : all) weldable += Card::canWeld(c) ? 1 : 0;
        const bool can = weldable >= 2;
        UIHelper::clearScreen();
        Hud::setActive(false);
        std::vector<CardBar::Action> acts{
            CardBar::Action{ "Weld two cards", can ? "hammer two of your cards into one card that plays both"
                                                   : "you need two cards it can take", !can },
            CardBar::Action{ "Walk past", "leave it and press on", false } };
        const char* scene = unchosens
            ? "The Unchosen was forging a sword like yours here, and never got to finish it"
            : "The witch's guards left an anvil here. It is still warm";
        if (ask && CardBar::pick(scene, {}, acts, 0) != 0) return;
        ask = true;

        // The first card, then the second; Back from the second picks the first again.
        std::vector<Card> picked;
        while (picked.size() < 2) {
            std::vector<Card> cards;
            std::vector<int> counts;
            groupCards(all, picked, [](const Card& c) { return Card::canWeld(c); }, cards, counts);
            const std::string title = picked.empty()
                ? std::string("The Anvil   pick the card that plays first")
                : "The Anvil   pick the card that plays after " + picked[0].getName();
            const int i = pickCardPaged(title, cards, counts);
            if (i < 0) {
                if (picked.empty()) break;
                picked.pop_back();
                continue;
            }
            picked.push_back(cards[(size_t)i]);
        }
        if (picked.size() < 2) continue;

        // What they would make, before anything is hammered.
        const Card welded = Card::weld(picked[0], picked[1]);
        bool weldThem = false, again = false;
        while (!weldThem && !again) {
            std::vector<CardBar::Action> sure{
                CardBar::Action{ "Weld them", "they become this one card for the rest of the run", false },
                CardBar::Action{ "Pick again", "choose two other cards", false },
                CardBar::Action{ "Walk past", "leave the anvil and press on", false } };
            const std::vector<CardBar::Card> face{ cardWidget(welded) };
            UIHelper::clearScreen();
            CardBar::setNextHighlight(1);   // on "Weld them"
            const int c = CardBar::pick("Welded, they make this card", face, sure, 1);
            if (c <= -2 || c == 0) {
                CardBar::showDetail(face[0], welded.getDescription(), face[0].typeLabel, rarityName(welded),
                                    welded.getUpgradeCount());
                continue;
            }
            if (c == 1) weldThem = true;
            else if (c == 2) again = true;
            else return;
        }
        if (!weldThem) { ask = false; continue; }

        playerDeck.removeCardByName(picked[0].getName());
        playerDeck.removeCardByName(picked[1].getName());
        playerDeck.addCard(welded);
        Audio::playSFXPitched("upgrade", 0.75f);
        Platform::shake(160, 3.0f);
        notice("You hammer " + picked[0].getName() + " and " + picked[1].getName() + " into one card: "
               + welded.getName() + ".");
        return;
    }
}

// --- the Offering and the trade --------------------------------------------
// Three cards of one rarity, one at a time. The first pick sets the rarity,
// Back undoes the last pick. Welds and legendaries can't go in. Returns false
// if you back all the way out.
bool Game::pickThreeOfARarity(const std::string& what, std::vector<Card>& out) {
    out.clear();
    const std::vector<Card> all = playerDeck.getAllCardsOrdered();
    int have[5] = { 0, 0, 0, 0, 0 };
    for (const Card& c : all)
        if (!c.isWeld()) have[rarityOf(c)]++;
    while (out.size() < 3) {
        const int rank = out.empty() ? -1 : rarityOf(out[0]);
        std::vector<Card> cards;
        std::vector<int> counts;
        groupCards(all, out, [&](const Card& c) {
            if (c.isWeld()) return false;
            const int r = rarityOf(c);
            return r < 4 && (rank < 0 ? have[r] >= 3 : r == rank);
        }, cards, counts);
        const size_t left = 3 - out.size();
        const std::string title = out.empty()
            ? what + "   pick three cards of the same rarity"
            : what + "   " + std::to_string(left) + " more " + RARITY[rank] + (left == 1 ? " card" : " cards")
              + "   given so far: " + listNames(out);
        const int i = pickCardPaged(title, cards, counts);
        if (i < 0) {
            if (out.empty()) return false;
            out.pop_back();
            continue;
        }
        out.push_back(cards[(size_t)i]);
    }
    return true;
}

// Can the deck give three of one rarity at all.
bool Game::hasThreeOfARarity() const {
    int have[5] = { 0, 0, 0, 0, 0 };
    for (const Card& c : playerDeck.getAllCardsOrdered())
        if (!c.isWeld()) have[rarityOf(c)]++;
    return have[0] >= 3 || have[1] >= 3 || have[2] >= 3 || have[3] >= 3;
}

// What the stone or the trade hands back: the right rarity, never one of the
// cards you gave, and something you don't already own if there is one. A card
// you discarded comes back with its forge upgrades.
bool Game::cardOfRarity(int rank, const std::vector<Card>& given, Card& out) {
    const std::vector<Card> pool = rank == 0 ? starterCards() : rewardPool.cardsOfRarity(rank);
    std::set<std::string> gave;
    for (const Card& c : given) gave.insert(c.getBaseName());
    const std::vector<std::string> owned = playerDeck.getAllCardNames();
    std::vector<Card> fresh, any;
    for (const Card& c : pool) {
        if (gave.count(c.getBaseName())) continue;
        any.push_back(c);
        if (std::find(owned.begin(), owned.end(), c.getBaseName()) == owned.end()) fresh.push_back(c);
    }
    // A legendary is never a second copy.
    const std::vector<Card>& from = (fresh.empty() && rank < 4) ? any : fresh;
    if (from.empty()) return false;
    static thread_local std::mt19937 gen(std::random_device{}());
    out = takeBackDiscard(from[std::uniform_int_distribution<size_t>(0, from.size() - 1)(gen)]);
    return true;
}

// The three you gave go like a discard: out of the deck but remembered with
// their upgrades, in case a reward brings one back.
void Game::giveUp(const std::vector<Card>& cards) {
    for (const Card& c : cards) {
        if (playerDeck.removeCardByName(c.getName())) rememberDiscard(c);
    }
    discardedThisRun = true;
}

// Shows what came back, until it is taken.
void Game::showGotCard(const std::string& title, const Card& got) {
    const std::vector<CardBar::Card> face{ cardWidget(got) };
    while (true) {
        UIHelper::clearScreen();
        CardBar::setNextHighlight(1);   // on "Take it"
        const int c = CardBar::pick(title, face, { CardBar::Action{ "Take it", false } }, 1);
        if (c <= -2 || c == 0) {
            CardBar::showDetail(face[0], got.getDescription(), face[0].typeLabel, rarityName(got),
                                got.getUpgradeCount());
            continue;
        }
        return;
    }
}

void Game::offeringStop(bool paladins) {
    bool ask = true;
    while (true) {
        const bool can = hasThreeOfARarity();
        UIHelper::clearScreen();
        Hud::setActive(false);
        std::vector<CardBar::Action> acts{
            CardBar::Action{ "Make an offering", can ? "give three cards of one rarity for one card, with a chance of the next rarity up"
                                                     : "you need three cards of the same rarity", !can },
            CardBar::Action{ "Walk past", "leave it and press on", false } };
        const char* scene = paladins
            ? "The Paladin kept a shrine to the sun here, with three hollows worn into its stone"
            : "A shrine at the side of the road, with three hollows worn into its stone";
        if (ask && CardBar::pick(scene, {}, acts, 0) != 0)
            return;
        ask = true;

        std::vector<Card> three;
        if (!pickThreeOfARarity("The Offering", three)) continue;
        const int rank = rarityOf(three[0]);
        // The next rarity only once the road has opened it: rare after the
        // first boss, super rare after the second. Legendary comes with super rare.
        const int opened = std::min(2, currentRun.getRoadPosition() / 10) + 1;
        const int top = opened >= 3 ? 4 : opened;
        const int up = rank + 1 <= top ? std::min(100, UP_CHANCE[rank] + luckBonus()) : 0;
        const std::string odds = up > 0
            ? std::to_string(up) + "% chance of " + aRarity(rank + 1) + " card, or else " + aRarity(rank) + " one"
            : aRarity(rank) + " card: " + RARITY[rank + 1] + " cards have not turned up on the road yet";

        std::vector<CardBar::Card> faces;
        for (const Card& c : three) faces.push_back(cardWidget(c));
        std::vector<CardBar::Action> sure{
            CardBar::Action{ "Offer them", odds, false },
            CardBar::Action{ "Pick again", "choose three other cards", false },
            CardBar::Action{ "Walk past", "leave the shrine and press on", false } };
        int c = -1;
        while (true) {
            UIHelper::clearScreen();
            CardBar::setNextHighlight(3);   // on "Offer them"
            c = CardBar::pick("Lay these three in the hollows?", faces, sure, 3);
            if (c <= -2 || (c >= 0 && c < 3)) {
                const int ci = c <= -2 ? -2 - c : c;
                if (ci >= 0 && ci < 3)
                    CardBar::showDetail(faces[(size_t)ci], three[(size_t)ci].getDescription(), faces[(size_t)ci].typeLabel,
                                        rarityName(three[(size_t)ci]), three[(size_t)ci].getUpgradeCount());
                continue;
            }
            break;
        }
        if (c == 4) { ask = false; continue; }   // pick again
        if (c != 3) return;                       // walked past

        static thread_local std::mt19937 gen(std::random_device{}());
        const bool rose = up > 0 && std::uniform_int_distribution<>(1, 100)(gen) <= up;
        Card got("", "", CardType::ATTACK, 0, 0);
        int gotRank = rose ? rank + 1 : rank;
        if (!cardOfRarity(gotRank, three, got)) {
            gotRank = rank;   // every legendary already held: the stone gives what it can
            if (!cardOfRarity(gotRank, three, got)) { notice("The stone does not take them."); return; }
        }
        giveUp(three);
        playerDeck.addCard(got);
        if (playerDeck.totalCards() == 1) earn(Achievements::DOWN_TO_ONE);
        if (gotRank > rank) Audio::playSFXPitched("upgrade", 1.3f);
        else                Audio::playSFX("upgrade");
        showGotCard(gotRank > rank ? "The stone takes them, and leaves " + aRarity(gotRank) + " card in the hollows"
                                   : "The stone takes them, and leaves this in the hollows",
                    got);
        return;
    }
}

// View Deck's trade: three cards for one other card of the same rarity. It's
// at every rest site, so it never goes up a rarity.
void Game::tradeThree() {
    std::vector<Card> three;
    while (true) {
        if (!pickThreeOfARarity("Trade three", three)) return;
        const int rank = rarityOf(three[0]);
        std::vector<CardBar::Card> faces;
        for (const Card& c : three) faces.push_back(cardWidget(c));
        std::vector<CardBar::Action> sure{
            CardBar::Action{ "Trade them", "for one other " + std::string(RARITY[rank]) + " card, at random", false },
            CardBar::Action{ "Pick again", "choose three other cards", false },
            CardBar::Action{ "Back", false } };
        int c = -1;
        while (true) {
            UIHelper::clearScreen();
            CardBar::setNextHighlight(3);   // on "Trade them"
            c = CardBar::pick("Trade these three for one other " + std::string(RARITY[rank]) + " card?", faces, sure, 3);
            if (c <= -2 || (c >= 0 && c < 3)) {
                const int ci = c <= -2 ? -2 - c : c;
                if (ci >= 0 && ci < 3)
                    CardBar::showDetail(faces[(size_t)ci], three[(size_t)ci].getDescription(), faces[(size_t)ci].typeLabel,
                                        rarityName(three[(size_t)ci]), three[(size_t)ci].getUpgradeCount());
                continue;
            }
            break;
        }
        if (c == 4) continue;
        if (c != 3) return;
        Card got("", "", CardType::ATTACK, 0, 0);
        if (!cardOfRarity(rank, three, got)) { notice("There is no other card of that rarity to trade for."); return; }
        giveUp(three);
        playerDeck.addCard(got);
        earn(Achievements::TRAVEL_LIGHT);
        if (playerDeck.totalCards() == 1) earn(Achievements::DOWN_TO_ONE);
        Audio::playSFX("upgrade");
        showGotCard("You trade them for this", got);
        return;
    }
}

// --- the Lost Camps ----------------------------------------------------------
// After you beat a Moon Shade you find the camp of whoever that shape used
// to be. Read the letter and keep their keepsake, or bury them for max HP
// and never read it. Letters you read stay in the Lore Book. Zones are in
// MOON_FORMS order: Weaver, Beguiler, Werewolf, Gorgon, Templar.
//
// Keepsakes started out as cards, but having them in the deck was just
// clutter, so now they're passives: you hold them, they work on their own,
// and View Player lists them. The camp still shows one as a card so you can
// see what you're getting.
namespace {
const int BURY_MAX_HP = 5;
const int KEEPSAKE_STRIPE = 180, KEEPSAKE_NAME = 223;   // tan, and Color::KEEPSAKE_TINT's pale peach
struct Camp { const char* where; const char* title; const char* found; const char* letter; };
const Camp CAMPS[KEEPSAKE_COUNT] = {
    { "The lamplighter's last camp, deep under the dungeon", "The lamplighter's last camp",
      "You take the wick from the lamplighter's lamp. Under the lamp is a letter.",
      "Brother, the pond went dark last night, the way Grandmother always said it would under a red moon, and "
      "a light went down into the hill. I followed it with my lamp. I am three days under the ground now, and it "
      "is still ahead of me. Something keeps pace with me just past the lamplight. When I stop, it stops. If "
      "this reaches you, keep the lamps by the pond lit until I come home." },
    { "The mage's camp in the witch's halls", "The mage's camp",
      "You pick up the mage's charm. It was lying on a letter.",
      "Little sister, do not come after me. The witch does not frighten me. The singing does. It started the "
      "night the moon came up red, somewhere in the dark between the halls, and every night it knows a little "
      "more of my name. I think it wants what I know. I have hidden my book where only you would look for it, "
      "which means that if you are reading this, you came after me anyway." },
    { "A hunter's camp in the forest", "The hunter's camp",
      "You pick up the hunter's snare. A letter is tied to it.",
      "For the one with the falcon. Day nineteen on the pack's trail. The leader stopped leaving tracks three "
      "days ago, and tonight, under a red moon, I saw why. It stood up on two legs at the edge of the clearing "
      "and looked at me the way a man would. The pack scattered and has not run together since. Stay off the "
      "trail, and keep the bird close. I am going after it in the morning." },
    { "A fisher's camp across the lake", "The fisher's camp",
      "You pick up one of the fisher's nets. A letter is caught in it.",
      "The gorgon across the water has stopped blinking. She used to come down to the shore at dusk, and we "
      "would all look away until she had gone. Since the red moon she just stands there, and her stare reaches "
      "over the water now. The reeds on her side have gone to glass. I have moved my nets to this shore. I do "
      "not think it will be far enough." },
    { "The templar's own tent, below the peak", "The templar's tent",
      "You pick up the templar's candle. Beside it is a letter.",
      "To the faithful in the valley. The moon has answered us at last. Tonight it came up red over the peak "
      "and called me by my name. I am going up alone to meet it. Do not follow. Keep the candles lit until I "
      "come back down to you, whatever shape I come back in. My second-in-command will keep the church until "
      "then." },
};
// What each keepsake is, plus anything the "+" panel adds to its rule.
const char* const WHAT[KEEPSAKE_COUNT] = {
    "The wick from the lamplighter's lamp.", "The mage's charm.", "The hunter's snare.",
    "One of the fisher's nets.", "A candle from the templar's tent.",
};
const char* const MORE[KEEPSAKE_COUNT] = {
    "",
    " It waits out Sacrifice, Borrowed Time, Blood Pact, Raise Undead and the cards that only play once a fight.",
    "",
    " A blow you parry, or turn with Dodge Reversal, does not use it up.",
    "",
};

// What you pick up, then the letter in italics, until a key.
void showLetter(const Camp& camp) {
    UIHelper::clearScreen();
    Hud::setActive(false);
    const int measure = 70;
    const std::string letter = camp.letter;
    UIHelper::padToCenter(2 + 2 + (int)(letter.size() / (size_t)measure) + 1 + 2);
    UIHelper::printCenteredWrapped(std::string(Color::DIM) + camp.found + Color::RESET, measure);
    std::cout << "\n";
    UIHelper::printCenteredWrapped(std::string(Color::ITALIC) + Color::KEEPSAKE_TINT + letter + Color::RESET,
                                   measure, true);
    std::cout << "\n\n";
    UIHelper::printCentered(std::string(Color::DIM) + "(press any key)" + Color::RESET);
    UIHelper::waitForKey("");
}
} // namespace

const char* Game::keepsakeName(int k) {
    static const char* const NAME[KEEPSAKE_COUNT] = { "Lamplighter's Wick", "Sister's Charm", "Hunter's Snare",
                                                      "Fisher's Net", "Templar's Candle" };
    return NAME[std::max(0, std::min((int)KEEPSAKE_COUNT - 1, k))];
}

// Rule text for the card and View Player. Everything but the Wick is once a
// fight.
std::string Game::keepsakeRule(int k) {
    switch (k) {
        case KEEP_WICK:  return "Your Fire attacks deal " + std::to_string(WICK_BONUS) + " more.";
        case KEEP_CHARM: return "The first Special card you play each fight is played twice.";
        case KEEP_SNARE: return "The first time each fight the enemy attacks you, it is stunned. Bosses often resist.";
        case KEEP_NET:   return "The enemy's first attack each fight deals half damage.";
        default:         return "Once a fight, when your turn starts below half your health: heal "
                              + std::to_string(CANDLE_HEAL) + ", even when nothing can heal you.";
    }
}

CardBar::Card Game::keepsakeCard(int k) const {
    CardBar::Card c;
    c.name = keepsakeName(k);
    c.elemTag = "[KEEPSAKE]";
    c.effect = keepsakeRule(k);
    c.item = true;   // nothing to pay and nothing to play: it works on its own
    c.tint = Console::xterm256Public(KEEPSAKE_STRIPE);
    c.nameColor = Console::xterm256Public(KEEPSAKE_NAME);
    return c;
}

void Game::lostCamp(int zone) {
    if (zone < 0 || zone >= KEEPSAKE_COUNT) return;
    const Camp& camp = CAMPS[zone];
    const std::vector<CardBar::Card> face{ keepsakeCard(zone) };
    const std::vector<CardBar::Action> acts{
        CardBar::Action{ "Read the letter", "and keep their keepsake", false },
        CardBar::Action{ "Bury them", "+" + std::to_string(BURY_MAX_HP) + " max HP, and the letter stays unread", false } };
    int c = -1;
    while (true) {
        UIHelper::clearScreen();
        Hud::setActive(false);
        CardBar::setNextHighlight(1);   // on "Read the letter"
        c = CardBar::pick(camp.where, face, acts, 1);
        if (c <= -2 || c == 0) {
            CardBar::showDetail(face[0], std::string(WHAT[zone]) + " " + keepsakeRule(zone) + MORE[zone]
                                         + " You do not play it: it works on its own.",
                                "KEEPSAKE", "", 0);
            continue;
        }
        break;
    }
    if (c == 2) {
        maxPlayerHealth += BURY_MAX_HP;
        playerHealth = std::min(maxPlayerHealth, playerHealth + BURY_MAX_HP);
        Audio::playSFX("heal");
        notice("You bury them where the camp stood, the letter with them. +" + std::to_string(BURY_MAX_HP)
               + " max HP.");
        return;
    }
    // Reading it (Esc counts too, same as the Moon Shade prize): letter first,
    // then the keepsake. The letter stays in the Lore Book even if the run dies.
    showLetter(camp);
    lettersRead |= 1 << zone;
    saveProgress();
    noteLorekeeper();
    keepsakes |= 1 << zone;
    Audio::playSFX("upgrade");
    notice("You keep the " + std::string(keepsakeName(zone)) + ". It works on its own, and View Player says how.");
}

void Game::campLore(std::vector<LoreEntry>& out) const {
    for (int z = 0; z < KEEPSAKE_COUNT; ++z)
        out.push_back(LoreEntry{ 2, CAMPS[z].title, { CAMPS[z].letter }, ((lettersRead >> z) & 1) != 0, -1 });
}

// --- the keepsakes in a fight -------------------------------------------------
int Game::wickBonus(const Card& c) const {
    return hasKeepsake(KEEP_WICK) && c.getType() == CardType::ATTACK && c.getElemType() == DamageType::FIRE
         ? WICK_BONUS : 0;
}

// ahhh the Charm was annoying to get right. Playing a Special twice is easy,
// but half of them break when you do: the once-a-fight ones have no second
// play, and Borrowed Time, Blood Pact and Raise Undead just charge you twice.
// So it skips those and waits for the next one. Welds only go twice if both
// halves would.
bool Game::charmTakes(const Card& c) const {
    if (c.getType() != CardType::SPECIAL) return false;
    if (c.isWeld()) return charmTakes(c.getParts()[0]) && charmTakes(c.getParts()[1]);
    switch (c.getEffect()) {
        case CardEffect::SACRIFICE: case CardEffect::UNLEASH:   case CardEffect::TURNABOUT:
        case CardEffect::FEINT:     case CardEffect::BORROWED:  case CardEffect::BLOODPACT:
        case CardEffect::RAISE:
            return false;
        default:
            return true;
    }
}

// The Snare stuns whatever attacks you first each fight. Same roll as the
// Stun card, so bosses can still shrug it off.
void Game::springSnare() {
    if (!keepsakeReady(KEEP_SNARE)) return;
    keepsakesSpent |= 1 << KEEP_SNARE;
    if (tryStunEnemy()) {
        EnemyArt::printBattleStatusFlash(enemy.getType(), enemy.getBossType(), EnemyArt::CastGlow::STUN, true);
        std::cout << "  " << Color::STUN_CLR << "It walks into the Hunter's Snare as it comes at you. It will lose "
                     "its next turn." << Color::RESET << "\n";
    } else {
        std::cout << "  " << Color::DIM << "It tears through the Hunter's Snare without breaking stride."
                  << Color::RESET << "\n";
    }
}

// The Net halves the first blow each fight that gets past Parry/Dodge.
int Game::castNet(int blow) {
    if (!keepsakeReady(KEEP_NET)) return blow;
    keepsakesSpent |= 1 << KEEP_NET;
    std::cout << "  " << Color::CYAN << "The Fisher's Net drags at it, and the blow lands at half." << Color::RESET << "\n";
    return blow / 2;
}

// The Candle: once a fight, at the start of your turn if you're under half HP.
// Ignores anything that blocks healing.
void Game::templarsCandle() {
    if (!keepsakeReady(KEEP_CANDLE) || playerHealth <= 0 || playerHealth * 2 >= maxPlayerHealth) return;
    keepsakesSpent |= 1 << KEEP_CANDLE;
    const int before = playerHealth;
    playerHealth = std::min(maxPlayerHealth, playerHealth + CANDLE_HEAL);
    Audio::playSFX("heal");
    EnemyArt::popNumber(playerHealth - before, false, EnemyArt::PopKind::HEAL);
    std::cout << "  " << Color::HEAL << "The Templar's Candle lights itself: +" << (playerHealth - before) << " HP."
              << Color::RESET << " (" << playerHealth << "/" << maxPlayerHealth << ")\n";
}
