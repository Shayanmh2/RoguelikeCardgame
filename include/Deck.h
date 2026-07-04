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
    void discardCard(const Card& card);
    void resetDeck();

    int handSize() const;
    int deckSize() const;
    int discardSize() const;

    const Card& getCardFromHand(int index) const;
    Card playCard(int index);       // marks slot [USED]; card moves to discard at resetDeck()
    bool isCardUsed(int index) const;

    // weakPenalty / damageBonus / armorBonus modify the displayed value on attack/defend cards.
    void displayHand(int weakPenalty = 0, int damageBonus = 0, int armorBonus = 0) const;
    void displayDeck() const;
    void displayAllCards() const;

    int totalCards() const;
    // Upgrades every copy of the named card at once (duplicates are just how the
    // starter deck/RNG deals cards — the player thinks of them as one card, not N
    // separate upgrade slots). Returns how many copies were upgraded (0 if maxed/not found).
    int upgradeCardGroup(const std::string& exactName);
    bool removeCardByName(const std::string& exactName); // removes exactly one copy; true if found
    std::vector<std::string> getAllCardNames() const; // base names (trailing '+' stripped)
    std::vector<Card> getAllCardsOrdered() const;     // cards + hand + discard
};

#endif
