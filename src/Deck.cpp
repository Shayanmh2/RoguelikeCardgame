#include "Deck.h"
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
    std::cout << "--- Hand (" << hand.size() << " cards) ---\n";
    for (size_t i = 0; i < hand.size(); ++i) {
        std::cout << i + 1 << ". ";
        hand[i].display();
    }
}

void Deck::displayDeck() const {
    std::cout << "--- Deck Status ---\n"
              << "Deck: " << cards.size() << " | Hand: " << hand.size()
              << " | Discard: " << discard.size() << "\n";
}
