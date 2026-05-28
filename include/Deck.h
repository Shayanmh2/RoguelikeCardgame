#ifndef DECK_H
#define DECK_H

#include "Card.h"
#include <vector>

class Deck {
private:
    std::vector<Card> cards;
    std::vector<Card> hand;
    std::vector<Card> discard;

public:
    Deck();
    
    void addCard(const Card& card);
    void shuffle();
    Card drawCard();
    void discardCard(const Card& card);
    void resetDeck();
    
    int handSize() const;
    int deckSize() const;
    int discardSize() const;
    
    const Card& getCardFromHand(int index) const;
    Card playCard(int index);
    
    void displayHand() const;
    void displayDeck() const;
};

#endif
