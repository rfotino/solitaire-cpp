#include "Solitaire.h"
#include <folly/String.h>
#include <glog/logging.h>
#include <iostream>

namespace solitaire {
  // Helper functions
  bool isBlack(const Card c) {
    return c.suit() == SPADES || c.suit() == CLUBS;
  }

  bool areDifferentColors(const Card c1, const Card c2) {
    return isBlack(c1) != isBlack(c2);
  }

  // https://en.wikipedia.org/wiki/Playing_cards_in_Unicode
  std::string Card::toUnicode() const {
    std::string ret = "\U0001f0a1";
    if (_suit < 2) {
      ret[3] += (0x10 * _suit) + _rank;
    } else {
      ret[2] = 0x83;
      ret[3] = 0x81 + (0x10 * (_suit - 2)) + _rank;
    }
    if (_rank > 10) {
      ret[3]++;
    }
    return ret;
  }

  std::vector<Card> getShuffledDeck() {
    std::vector<Card> deck;
    deck.reserve(NUM_SUITS * NUM_RANKS);
    for (uint8_t suit = 0; suit < NUM_SUITS; suit++) {
      for (uint8_t rank = 0; rank < NUM_RANKS; rank++) {
	deck.emplace_back(suit, rank);
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

  const size_t TABLEAU_SIZE = 7;

  Solitaire::Solitaire(const std::vector<Card>& deck, size_t drawSize) :
    _drawSize(drawSize) {
    // Foundation is the four suit piles on top of the table, they
    // start empty but are filled in with ace through king. Values
    // in this map are indices in the VALUES array, or -1 if empty.
    // Game ends when foundation is all kings.
    for (auto i = 0; i < NUM_SUITS; i++) {
      _foundation.push_back(-1);
    }

    // Initialize hand (draw pile) before dealing it out to the tableau
    _hand = deck;

    // Initialize tableau, columns of cards with zero or more face down
    // and the one on the bottom of each column facing up. Each column
    // is represented by an std::pair<> of card stacks, the first of which
    // is face down cards and the second of which is face up cards
    for (auto column = 0; column < TABLEAU_SIZE; column++) {
      _tableau.push_back(TableauColumn());
    }
    for (auto row = 0; row < _tableau.size(); row++) {
      for (auto column = row; column < _tableau.size(); column++) {
	const auto card = _hand.back();
	_hand.pop_back();
	if (row == column) {
	  _tableau[column].faceUp.push_back(card);
	} else {
	  _tableau[column].faceDown.push_back(card);
	}
      }
    }
  }

  bool Solitaire::isValid(const Move& move) const {
    switch (move.type()) {
    case MoveType::DRAW: {
      if (!move.extras().empty()) {
	return false;
      }
      // If both hand and waste are empty this fails
      if (_hand.empty() && _waste.empty()) {
	return false;
      }
      break;
    }
    case MoveType::WASTE_TO_FOUNDATION: {
      if (!move.extras().empty()) {
	return false;
      }
      // Must have at least one card in waste
      if (_waste.empty()) {
	return false;
      }
      // Top card in waste must be next card to add to
      // foundation for that suit
      const auto card = _waste.back();
      if (card.rank() != _foundation[card.suit()] + 1) {
	return false;
      }
      break;
    }
    case MoveType::WASTE_TO_TABLEAU: {
      if (move.extras().size() != 1) {
	return false;
      }
      const auto dstColIdx = move.extras().at(0);
      // Waste must have cards and dst must be in tableau range
      if (_waste.empty() || dstColIdx < 0 || dstColIdx >= _tableau.size()) {
	return false;
      }
      // If tableau column is empty, waste card must be a king
      const auto& column = _tableau.at(dstColIdx);
      const auto srcCard = _waste.back();
      if (column.faceUp.empty()) {
	if (srcCard.rank() != NUM_RANKS - 1) {
	  return false;
	}
      } else {
	// Src/dst cards must have opposite suits and ranks must be descending
	const auto dstCard = column.faceUp.back();
	if (!areDifferentColors(srcCard, dstCard) ||
	    srcCard.rank() != dstCard.rank() - 1) {
	  return false;
	}
      }
      break;
    }
    case MoveType::TABLEAU_TO_FOUNDATION: {
      if (move.extras().size() != 1) {
	return false;
      }
      const auto& srcColIdx = move.extras().at(0);
      // Tableau src index must be in range and that column must contain at
      // least one card
      if (srcColIdx < 0 || srcColIdx >= _tableau.size() ||
	  _tableau.at(srcColIdx).faceUp.empty()) {
	return false;
      }
      // Card must be the next one to add to that suit's foundation
      const auto& column = _tableau.at(srcColIdx);
      const auto card = column.faceUp.back();
      if (card.rank() != _foundation.at(card.suit()) + 1) {
	return false;
      }
      break;
    }
    case MoveType::TABLEAU_TO_TABLEAU: {
      if (move.extras().size() != 3) {
	return false;
      }
      const auto srcColIdx = move.extras().at(0);
      const auto srcRowIdx = move.extras().at(1);
      const auto dstColIdx = move.extras().at(2);
      // Src/dst columns and srcRowIdx must be in range
      if (srcColIdx < 0 || srcColIdx >= _tableau.size() ||
	  dstColIdx < 0 || dstColIdx >= _tableau.size() ||
	  srcRowIdx < 0 || srcRowIdx >= _tableau.at(srcColIdx).faceUp.size()) {
	return false;
      }
      const auto srcCard = _tableau.at(srcColIdx).faceUp.at(srcRowIdx);
      // If destination is empty, source card must be a king
      if (_tableau.at(dstColIdx).faceUp.empty()) {
	if (srcCard.rank() != NUM_RANKS - 1) {
	  return false;
	}
      } else {
	// Otherwise source card must be opposite color and one rank lower
	// than destination card
	const auto dstCard = _tableau.at(dstColIdx).faceUp.back();
	if (!areDifferentColors(srcCard, dstCard) ||
	    srcCard.rank() != dstCard.rank() - 1) {
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
      if (_hand.empty()) {
	_hand = _waste;
	_waste.clear();
	std::reverse(_hand.begin(), _hand.end());
      }
      // Draw up to drawSize cards and place in waste
      auto cardsToDraw = _drawSize;
      while (cardsToDraw > 0 && !_hand.empty()) {
	_waste.push_back(_hand.back());
	_hand.pop_back();
	cardsToDraw--;
      }
      break;
    }
    case MoveType::WASTE_TO_FOUNDATION: {
      const auto card = _waste.back();
      _foundation[card.suit()] = card.rank();
      _waste.pop_back();
      break;
    }
    case MoveType::WASTE_TO_TABLEAU: {
      const auto dstColIdx = move.extras().at(0);
      const auto card = _waste.back();
      _tableau.at(dstColIdx).faceUp.push_back(card);
      _waste.pop_back();
      break;
    }
    case MoveType::TABLEAU_TO_FOUNDATION: {
      const auto srcColIdx = move.extras().at(0);
      auto& srcCol = _tableau.at(srcColIdx);
      const auto card = srcCol.faceUp.back();
      srcCol.faceUp.pop_back();
      _foundation[card.suit()] = card.rank();
      break;
    }
    case MoveType::TABLEAU_TO_TABLEAU: {
      const auto srcColIdx = move.extras().at(0);
      const auto srcRowIdx = move.extras().at(1);
      const auto dstColIdx = move.extras().at(2);
      auto& srcCol = _tableau.at(srcColIdx);
      auto& dstCol = _tableau.at(dstColIdx);
      dstCol.faceUp.insert(dstCol.faceUp.end(),
			   srcCol.faceUp.begin() + srcRowIdx,
			   srcCol.faceUp.end());
      srcCol.faceUp.erase(srcCol.faceUp.begin() + srcRowIdx,
			  srcCol.faceUp.end());
      break;
    }
    }

    // Flip over any cards that have been exposed in the tableau
    for (auto& column : _tableau) {
      if (column.faceUp.empty() && !column.faceDown.empty()) {
	column.faceUp.push_back(column.faceDown.back());
	column.faceDown.pop_back();
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
    if (!_hand.empty() || !_waste.empty()) {
      return false;
    }
    for (const auto& col : _tableau) {
      if (!col.faceDown.empty()) {
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
    ret += !_hand.empty() ? UNICODE_FACE_DOWN  + " " : "  ";
    ret += !_waste.empty() ? _waste.back().toUnicode() + " " : "  ";
    ret += std::string(2 * (TABLEAU_SIZE - _foundation.size()), ' ');
    for (auto suit = 0; suit < _foundation.size(); suit++) {
      const auto rank = _foundation[suit];
      ret += rank >= 0 ? Card(suit, rank).toUnicode() + " " : "  ";
    }
    size_t tableauHeight = 0;
    for (const auto& column : _tableau) {
      const auto columnHeight = column.faceDown.size() + column.faceUp.size();
      if (columnHeight > tableauHeight) {
	tableauHeight = columnHeight;
      }
    }
    for (auto row = 0; row < tableauHeight; row++) {
      ret += "\n    ";
      for (const auto& column : _tableau) {
	if (row < column.faceDown.size()) {
	  ret += DOWN_COLOR + column.faceDown[row].toUnicode() + RESET + " ";
	} else if (row < column.faceDown.size() + column.faceUp.size()) {
	  ret += column.faceUp[row - column.faceDown.size()].toUnicode() + " ";
	} else {
	  ret += "  ";
	}
      }
    }
    return ret;
  }
}
