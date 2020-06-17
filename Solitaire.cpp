#include "Solitaire.h"
#include <folly/String.h>
#include <glog/logging.h>
#include <iostream>

namespace solitaire {
  // Helper functions
  bool isBlack(const Card c) {
    return c.suit == SPADES || c.suit == CLUBS;
  }

  bool areDifferentColors(const Card c1, const Card c2) {
    return isBlack(c1) != isBlack(c2);
  }

  // https://en.wikipedia.org/wiki/Playing_cards_in_Unicode
  std::string Card::toUnicode() const {
    std::string ret = "\U0001f0a1";
    if (suit < 2) {
      ret[3] += (0x10 * suit) + rank;
    } else {
      ret[2] = 0x83;
      ret[3] = 0x81 + (0x10 * (suit - 2)) + rank;
    }
    if (rank > 10) {
      ret[3]++;
    }
    return ret;
  }

  std::array<Card, NUM_CARDS> getShuffledDeck() {
    std::array<Card, NUM_CARDS> deck;
    for (uint8_t suit = 0; suit < NUM_SUITS; suit++) {
      for (uint8_t rank = 0; rank < NUM_RANKS; rank++) {
	deck[(suit * NUM_RANKS) + rank] = Card(suit, rank);
      }
    }
    for (auto i = deck.size() - 1; i > 0; i--) {
      auto j = folly::Random::secureRand32(i + 1);
      auto x = deck[i];
      deck[i] = deck[j];
      deck[j] = x;
    }
    return deck;
  }

  Solitaire::Solitaire(const std::array<Card, NUM_CARDS>& deck,
		       size_t drawSize) :
    _drawSize(drawSize), _handSize(MAX_HAND_SIZE), _wasteSize(0) {
    // Foundation is the four suit piles on top of the table, they
    // start empty but are filled in with ace through king. Values
    // in this map are indices in the VALUES array, or -1 if empty.
    // Game ends when foundation is all kings.
    for (auto i = 0; i < NUM_SUITS; i++) {
      _foundation[i] = -1;
    }

    // The bottom 24 cards go in the hand (higher indices are on top)
    std::copy(deck.begin(), deck.begin() + _hand.size(), _hand.begin());

    // Initialize tableau, columns of cards with zero or more face down
    // and the one on the bottom of each column facing up. Each column
    // is represented by an std::pair<> of card stacks, the first of which
    // is face down cards and the second of which is face up cards
    size_t cardsInDeck = deck.size();
    for (auto row = 0; row < _tableau.size(); row++) {
      for (auto column = row; column < _tableau.size(); column++) {
	const auto card = deck[cardsInDeck - 1];
	cardsInDeck--;
	if (row == column) {
	  _tableau[column].faceUp[_tableau[column].faceUpSize] = card;
	  _tableau[column].faceUpSize++;
	} else {
	  _tableau[column].faceDown[_tableau[column].faceDownSize] = card;
	  _tableau[column].faceDownSize++;
	}
      }
    }
  }

  bool Solitaire::isValid(const Move& move) const {
    switch (move.type()) {
    case MoveType::DRAW: {
      // If both hand and waste are empty this fails
      if (_handSize == 0) {
	return false;
      }
      break;
    }
    case MoveType::WASTE_TO_FOUNDATION: {
      // Must have at least one card in waste
      if (_wasteSize == 0) {
	return false;
      }
      // Top card in waste must be next card to add to
      // foundation for that suit
      const auto card = _hand[_handSize - _wasteSize];
      if (card.rank != _foundation[card.suit] + 1) {
	return false;
      }
      break;
    }
    case MoveType::WASTE_TO_TABLEAU: {
      const auto dstColIdx = move.extras()[0];
      // Waste must have cards and dst must be in tableau range
      if (_wasteSize == 0 || dstColIdx < 0 || dstColIdx >= _tableau.size()) {
	return false;
      }
      // If tableau column is empty, waste card must be a king
      const auto& column = _tableau.at(dstColIdx);
      const auto srcCard = _hand[_handSize - _wasteSize];
      if (column.faceUpSize == 0) {
	if (srcCard.rank != NUM_RANKS - 1) {
	  return false;
	}
      } else {
	// Src/dst cards must have opposite suits and ranks must be descending
	const auto dstCard = column.faceUp[column.faceUpSize - 1];
	if (!areDifferentColors(srcCard, dstCard) ||
	    srcCard.rank != dstCard.rank - 1) {
	  return false;
	}
      }
      break;
    }
    case MoveType::TABLEAU_TO_FOUNDATION: {
      const auto& srcColIdx = move.extras()[0];
      // Tableau src index must be in range and that column must contain at
      // least one card
      if (srcColIdx < 0 || srcColIdx >= _tableau.size() ||
	  _tableau.at(srcColIdx).faceUpSize == 0) {
	return false;
      }
      // Card must be the next one to add to that suit's foundation
      const auto& column = _tableau.at(srcColIdx);
      const auto card = column.faceUp[column.faceUpSize - 1];
      if (card.rank != _foundation[card.suit] + 1) {
	return false;
      }
      break;
    }
    case MoveType::TABLEAU_TO_TABLEAU: {
      const auto srcColIdx = move.extras()[0];
      const auto srcRowIdx = move.extras()[1];
      const auto dstColIdx = move.extras()[2];
      // Src/dst columns and srcRowIdx must be in range
      if (srcColIdx < 0 || srcColIdx >= _tableau.size() ||
	  dstColIdx < 0 || dstColIdx >= _tableau.size() ||
	  srcRowIdx < 0 || srcRowIdx >= _tableau[srcColIdx].faceUpSize) {
	return false;
      }
      const auto srcCard = _tableau[srcColIdx].faceUp[srcRowIdx];
      // If destination is empty, source card must be a king
      if (_tableau[dstColIdx].faceUpSize == 0) {
	if (srcCard.rank != NUM_RANKS - 1) {
	  return false;
	}
      } else {
	// Otherwise source card must be opposite color and one rank lower
	// than destination card
	const auto dstCard =
	  _tableau[dstColIdx].faceUp[_tableau[dstColIdx].faceUpSize - 1];
	if (!areDifferentColors(srcCard, dstCard) ||
	    srcCard.rank != dstCard.rank - 1) {
	  return false;
	}
      }
      break;
    }
    default:
      return false;
    }
    return true;
  }

  void Solitaire::apply(const Move& move) {
    switch (move.type()) {
    case MoveType::DRAW: {
      // Move waste back to hand if hand is empty
      if (_wasteSize == _handSize) {
	_wasteSize = 0;
      }
      // Draw up to drawSize cards and place in waste
      _wasteSize = std::min(_wasteSize + _drawSize, _handSize);
      break;
    }
    case MoveType::WASTE_TO_FOUNDATION: {
      const auto card = _hand[_handSize - _wasteSize];
      _foundation[card.suit] = card.rank;
      for (auto i = _handSize - _wasteSize; i < _handSize - 1; i++) {
	_hand[i] = _hand[i + 1];
      }
      _handSize--;
      _wasteSize--;
      break;
    }
    case MoveType::WASTE_TO_TABLEAU: {
      const auto dstColIdx = move.extras()[0];
      // Add card to end of face up tableau column
      const auto card = _hand[_handSize - _wasteSize];
      auto& column = _tableau[dstColIdx];
      column.faceUp[column.faceUpSize] = card;
      column.faceUpSize++;
      // Remove card from hand
      for (auto i = _handSize - _wasteSize; i < _handSize - 1; i++) {
	_hand[i] = _hand[i + 1];
      }
      _handSize--;
      _wasteSize--;
      break;
    }
    case MoveType::TABLEAU_TO_FOUNDATION: {
      const auto srcColIdx = move.extras()[0];
      auto& srcCol = _tableau[srcColIdx];
      const auto card = srcCol.faceUp[srcCol.faceUpSize - 1];
      srcCol.faceUpSize--;
      _foundation[card.suit] = card.rank;
      break;
    }
    case MoveType::TABLEAU_TO_TABLEAU: {
      const auto srcColIdx = move.extras()[0];
      const auto srcRowIdx = move.extras()[1];
      const auto dstColIdx = move.extras()[2];
      auto& srcCol = _tableau[srcColIdx];
      auto& dstCol = _tableau[dstColIdx];
      for (auto i = srcRowIdx; i < srcCol.faceUpSize; i++) {
	const auto card = srcCol.faceUp[i];
	dstCol.faceUp[dstCol.faceUpSize] = card;
	dstCol.faceUpSize++;
      }
      srcCol.faceUpSize = srcRowIdx;
      break;
    }
    }

    // Flip over any cards that have been exposed in the tableau
    for (auto& column : _tableau) {
      if (column.faceUpSize == 0 && column.faceDownSize != 0) {
	column.faceUp[column.faceUpSize] = column.faceDown[column.faceDownSize - 1];
	column.faceUpSize++;
	column.faceDownSize--;
      }
    }
  }

  /**
   * Game is technically won when the foundation is all kings, but we can
   * short-circuit the solver algorithm and just call the game won when there
   * are no cards left in the hand/waste and there are no face-down cards
   * on the tableau.
   */
  bool Solitaire::isWon() const {
    if (_handSize > 0) {
      return false;
    }
    for (const auto& col : _tableau) {
      if (col.faceDownSize > 0) {
	return false;
      }
    }
    return true;
  }

  std::string Solitaire::toConsoleString() const {
    const std::string UNICODE_FACE_DOWN = "\U0001f0a0";
    const std::string DOWN_COLOR = "\u001b[31m";
    const std::string RESET = "\u001b[0m";
    std::string ret = "";
    ret += _wasteSize < _handSize ? UNICODE_FACE_DOWN  + " " : "  ";
    ret += _wasteSize > 0 ? _hand[_handSize - _wasteSize].toUnicode() + " " : "  ";
    ret += std::string(2 * (_tableau.size() - _foundation.size()), ' ');
    for (auto suit = 0; suit < _foundation.size(); suit++) {
      const auto rank = _foundation[suit];
      ret += rank >= 0 ? Card(suit, rank).toUnicode() + " " : "  ";
    }
    size_t tableauHeight = 0;
    for (const auto& column : _tableau) {
      const auto columnHeight = column.faceDownSize + column.faceUpSize;
      tableauHeight = std::max(columnHeight, tableauHeight);
    }
    for (auto row = 0; row < tableauHeight; row++) {
      ret += "\n    ";
      for (const auto& column : _tableau) {
	if (row < column.faceDownSize) {
	  ret += DOWN_COLOR + column.faceDown[row].toUnicode() + RESET + " ";
	} else if (row < column.faceDownSize + column.faceUpSize) {
	  ret += column.faceUp[row - column.faceDownSize].toUnicode() + " ";
	} else {
	  ret += "  ";
	}
      }
    }
    return ret;
  }
}
