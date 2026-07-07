#ifndef DECK_H
#define DECK_H

#include "Card.h"
#include <vector>

class Deck {
private:
    std::vector<Card> cards;
    std::vector<Card> hand;
    std::vector<Card> discard;
    std::vector<bool> handUsed; // true = card played this turn; slot stays visible as [USED]

public:
    Deck();

    void addCard(const Card& card);
    void shuffle();
    Card drawCard();
    void resetDeck();

    int handSize() const;

    const Card& getCardFromHand(int index) const;
    Card playCard(int index);       // marks slot [USED]; card moves to discard at resetDeck()
    bool isCardUsed(int index) const;

    int totalCards() const;
    int upgradeCardGroup(const std::string& exactName); // upgrades every copy of the named card; returns count upgraded
    bool removeCardByName(const std::string& exactName); // removes exactly one copy; true if found
    std::vector<std::string> getAllCardNames() const; // base names (trailing '+' stripped)
    std::vector<Card> getAllCardsOrdered() const;     // cards + hand + discard
};

#endif
