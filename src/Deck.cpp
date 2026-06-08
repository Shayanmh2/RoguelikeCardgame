#include "Deck.h"
#include "UIHelper.h"
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
    hand.erase(hand.begin() + index);
    discard.push_back(card);
    return card;
}

void Deck::displayHand() const {
    UIHelper::printCardHeader(hand.size());
    for (size_t i = 0; i < hand.size(); ++i) {
        UIHelper::printCardRow(i + 1, hand[i].getTypeString(), hand[i].getName(), 
                               hand[i].getCost(), hand[i].getValue(), hand[i].getDescription());
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

bool Deck::upgradeCardAt(int index) {
    if (index < 0) return false;
    if (index < (int)cards.size()) {
        cards[index].upgrade();
        return true;
    }
    index -= (int)cards.size();
    if (index < (int)hand.size()) {
        hand[index].upgrade();
        return true;
    }
    index -= (int)hand.size();
    if (index < (int)discard.size()) {
        discard[index].upgrade();
        return true;
    }
    return false;
}
