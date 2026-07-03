#include "Deck.h"
#include "UIHelper.h"
#include "Colors.h"
#include <algorithm>
#include <random>
#include <iostream>

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

void Deck::discardCard(const Card& card) {
    auto it = std::find_if(hand.begin(), hand.end(),
                          [&card](const Card& c) { return c.getName() == card.getName(); });
    if (it != hand.end()) {
        discard.push_back(*it);
        hand.erase(it);
    }
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

int Deck::deckSize() const {
    return cards.size();
}

int Deck::discardSize() const {
    return discard.size();
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
    handUsed[index] = true; // mark slot — card stays in hand until resetDeck()
    return card;
}

bool Deck::isCardUsed(int index) const {
    if (index < 0 || index >= static_cast<int>(handUsed.size())) return true;
    return handUsed[index];
}

void Deck::displayHand(int weakPenalty, int damageBonus, int armorBonus) const {
    UIHelper::printCardHeader(hand.size());
    for (size_t i = 0; i < hand.size(); ++i) {
        if (i < handUsed.size() && handUsed[i]) {
            std::cout << "  " << Color::DIM << (i + 1) << ". [USED]" << Color::RESET << "\n\n";
            continue;
        }
        const Card& c = hand[i];
        int dispVal = c.getValue();
        if (c.getType() == CardType::ATTACK)
            dispVal = std::max(0, dispVal + damageBonus - weakPenalty);
        else if (c.getType() == CardType::DEFEND)
            dispVal = dispVal + armorBonus;
        UIHelper::printCardRow(static_cast<int>(i) + 1, c.getTypeString(), c.getName(),
                               c.getCost(), dispVal, c.getDescription());
    }
    UIHelper::printBoxEnd();
}

void Deck::displayDeck() const {
    UIHelper::printLine(60, '=');
    std::cout << "DECK STATUS\n";
    std::cout << "  Cards: " << cards.size() << " | Hand: " << hand.size()
              << " | Discard: " << discard.size() << "\n";
    UIHelper::printLine(60, '=');
}

void Deck::displayAllCards() const {
    UIHelper::printCardHeader(totalCards());
    int idx = 1;
    for (const auto& c : cards)
        UIHelper::printCardRow(idx++, c.getTypeString(), c.getName(), c.getCost(), c.getValue(), c.getDescription());
    for (const auto& c : hand)
        UIHelper::printCardRow(idx++, c.getTypeString(), c.getName(), c.getCost(), c.getValue(), c.getDescription());
    for (const auto& c : discard)
        UIHelper::printCardRow(idx++, c.getTypeString(), c.getName(), c.getCost(), c.getValue(), c.getDescription());
    UIHelper::printBoxEnd();
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

bool Deck::upgradeCardAt(int index) {
    if (index < 0) return false;
    auto tryUpgrade = [](Card& c) -> bool {
        if (c.getUpgradeCount() >= c.getMaxUpgrades()) return false;
        c.upgrade();
        return true;
    };
    if (index < (int)cards.size()) {
        return tryUpgrade(cards[index]);
    }
    index -= (int)cards.size();
    if (index < (int)hand.size()) {
        return tryUpgrade(hand[index]);
    }
    index -= (int)hand.size();
    if (index < (int)discard.size()) {
        return tryUpgrade(discard[index]);
    }
    return false;
}
