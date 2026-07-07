#include "Deck.h"
#include <algorithm>
#include <random>

Deck::Deck() {}

void Deck::addCard(const Card& card) {
    cards.push_back(card);
}

void Deck::shuffle() {
    auto rng = std::default_random_engine(std::random_device{}());
    std::shuffle(cards.begin(), cards.end(), rng);
}

Card Deck::drawCard() {
    if (cards.empty()) {
        if (discard.empty()) {
            throw std::runtime_error("No cards to draw!");
        }
        cards = discard;
        discard.clear();
        shuffle();
    }

    Card drawnCard = cards.back();
    cards.pop_back();
    hand.push_back(drawnCard);
    handUsed.push_back(false);
    return drawnCard;
}

void Deck::resetDeck() {
    for (const auto& card : hand) {
        discard.push_back(card);
    }
    hand.clear();
    handUsed.clear();
}

int Deck::handSize() const {
    return hand.size();
}

const Card& Deck::getCardFromHand(int index) const {
    if (index < 0 || index >= static_cast<int>(hand.size())) {
        throw std::out_of_range("Invalid card index");
    }
    return hand[index];
}

Card Deck::playCard(int index) {
    if (index < 0 || index >= static_cast<int>(hand.size())) {
        throw std::out_of_range("Invalid card index");
    }
    Card card = hand[index];
    handUsed[index] = true; // mark slot - card stays in hand until resetDeck()
    return card;
}

bool Deck::isCardUsed(int index) const {
    if (index < 0 || index >= static_cast<int>(handUsed.size())) return true;
    return handUsed[index];
}

int Deck::totalCards() const {
    return (int)(cards.size() + hand.size() + discard.size());
}

std::vector<std::string> Deck::getAllCardNames() const {
    std::vector<std::string> names;
    auto addBase = [&](const std::string& n) {
        std::string base = n;
        while (!base.empty() && base.back() == '+') base.pop_back();
        names.push_back(base);
    };
    for (const auto& c : cards)   addBase(c.getName());
    for (const auto& c : hand)    addBase(c.getName());
    for (const auto& c : discard) addBase(c.getName());
    return names;
}

std::vector<Card> Deck::getAllCardsOrdered() const {
    std::vector<Card> all;
    all.insert(all.end(), cards.begin(), cards.end());
    all.insert(all.end(), hand.begin(), hand.end());
    all.insert(all.end(), discard.begin(), discard.end());
    return all;
}

int Deck::upgradeCardGroup(const std::string& exactName) {
    // All copies share the same upgrade count (they're always upgraded together),
    // so checking any one instance's cap tells us whether the whole group can upgrade.
    bool capReached = false;
    bool found = false;
    auto checkCap = [&](const Card& c) {
        if (c.getName() != exactName) return;
        found = true;
        if (c.getUpgradeCount() >= c.getMaxUpgrades()) capReached = true;
    };
    for (const auto& c : cards)   checkCap(c);
    for (const auto& c : hand)    checkCap(c);
    for (const auto& c : discard) checkCap(c);
    if (!found || capReached) return 0;

    int upgraded = 0;
    auto upgradeMatching = [&](Card& c) {
        if (c.getName() == exactName) { c.upgrade(); upgraded++; }
    };
    for (auto& c : cards)   upgradeMatching(c);
    for (auto& c : hand)    upgradeMatching(c);
    for (auto& c : discard) upgradeMatching(c);
    return upgraded;
}

bool Deck::removeCardByName(const std::string& exactName) {
    for (size_t i = 0; i < cards.size(); i++) {
        if (cards[i].getName() == exactName) { cards.erase(cards.begin() + i); return true; }
    }
    for (size_t i = 0; i < hand.size(); i++) {
        if (hand[i].getName() == exactName) {
            hand.erase(hand.begin() + i);
            if (i < handUsed.size()) handUsed.erase(handUsed.begin() + i);
            return true;
        }
    }
    for (size_t i = 0; i < discard.size(); i++) {
        if (discard[i].getName() == exactName) { discard.erase(discard.begin() + i); return true; }
    }
    return false;
}
