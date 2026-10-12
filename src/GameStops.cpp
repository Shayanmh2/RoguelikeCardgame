// The stops on the road between fights (the user, 2026-10-11). Each is
// passed once a run: the Anvil in the Dark Dungeon, where two cards are
// hammered into one that plays both, and the Offering in the Wicked Forest,
// where three cards of one rarity go into the stone for one card, sometimes
// of the next rarity up. The deck's own trade, three cards for one other of
// the same rarity, is in View Deck at every rest site.
#include "Game.h"
#include "Achievements.h"
#include "Audio.h"
#include "CardBar.h"
#include "Hud.h"
#include "Platform.h"
#include "UIHelper.h"
#include <algorithm>
#include <random>
#include <set>
#include <string>
#include <vector>
#include "GameShared.h"

namespace {
// The encounter each stop is found after. The Anvil comes once a few chosen
// cards are in the deck; the Offering after both rarity unlocks (rare after
// the first boss, super rare after the second), so any three can go up.
const int ANVIL_AFTER = 15, OFFERING_AFTER = 25;
// The Offering's chance of the next rarity up, before Fortune, by the
// rarity given: three starters for an uncommon is an even bet, and the
// rarer the three, the longer the odds.
const int UP_CHANCE[4] = { 50, 35, 25, 10 };
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
    if (!(stopsDone & 1) && at >= ANVIL_AFTER)    { stopsDone |= 1; anvilStop(); }
    if (!(stopsDone & 2) && at >= OFFERING_AFTER) { stopsDone |= 2; offeringStop(); }
}

// The deck's cards to pick one from, eight to a page as the forge shows
// them, with a count on each name held more than once. Returns the index
// into `cards`, or -1 for Back.
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

// The deck in groups, one per name, rarest first, as the forge and View Deck
// list it, keeping only what `keep` allows and leaving out the copies already
// picked.
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
// Two cards become one, named for both: it costs what they cost together,
// plays the first and then the second, and takes one place in the hand.
void Game::anvilStop() {
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
        if (ask && CardBar::pick("The witch's guards left an anvil here. It is still warm", {}, acts, 0) != 0) return;
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
// Three cards of one rarity, picked one at a time: the first decides the
// rarity and the other two must match it. Back takes the last pick back.
// Welds and legendaries are never given. False when the player backs out.
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

// The card the stone or the trade gives back: of the rarity asked for, never
// one of the cards given for it, and one the deck does not hold when there is
// one. A card thrown out earlier with forge work on it comes back with it.
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

// The three given go the way a discard does: out of the deck, and remembered
// with any forge work, so a reward can bring one back as it was.
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

void Game::offeringStop() {
    bool ask = true;
    while (true) {
        const bool can = hasThreeOfARarity();
        UIHelper::clearScreen();
        Hud::setActive(false);
        std::vector<CardBar::Action> acts{
            CardBar::Action{ "Make an offering", can ? "give three cards of one rarity for one card, with a chance of the next rarity up"
                                                     : "you need three cards of the same rarity", !can },
            CardBar::Action{ "Walk past", "leave it and press on", false } };
        if (ask && CardBar::pick("A shrine at the side of the road, with three hollows worn into its stone",
                                 {}, acts, 0) != 0)
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

// View Deck's trade: three cards of one rarity for one other card of the
// same rarity. At every rest site, so it never goes up a rarity; the cards
// given can turn up again as rewards.
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
