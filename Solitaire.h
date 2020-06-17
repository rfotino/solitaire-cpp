#pragma once

#include <array>
#include <folly/Conv.h>
#include <folly/Random.h>

namespace solitaire {
  typedef int8_t Suit;
  typedef int8_t Rank;
  const static Suit SPADES = 0;
  const static Suit HEARTS = 1;
  const static Suit DIAMONDS = 2;
  const static Suit CLUBS = 3;
  const static size_t NUM_SUITS = 4;
  const static size_t NUM_RANKS = 13;
  const static size_t NUM_CARDS = NUM_RANKS * NUM_SUITS;

  class Card {
   public:
    Card() = default;
    Card(Suit _suit, Rank _rank) : suit(_suit), rank(_rank) {}
    std::string toUnicode() const;
    bool operator<(const Card& other) const {
      return suit == other.suit ? rank < other.rank : suit < other.suit;
    }
    inline friend std::ostream&
    operator<<(std::ostream& os, const Card& c) {
      return os << c.toUnicode();
    }
    Suit suit;
    Rank rank;
  };

  std::array<Card, NUM_CARDS> getShuffledDeck();

  enum class MoveType {
    DRAW,
    WASTE_TO_FOUNDATION,
    WASTE_TO_TABLEAU,
    TABLEAU_TO_FOUNDATION,
    TABLEAU_TO_TABLEAU,
  };

  const static size_t NUM_MOVE_EXTRAS = 3;

  class Move {
   public:
    Move() = default;
    Move(MoveType type, std::array<int8_t, NUM_MOVE_EXTRAS> extras) :
      _type(type), _extras(extras) {}
    MoveType type() const { return _type; }
    const std::array<int8_t, NUM_MOVE_EXTRAS>& extras() const { return _extras; }
    inline friend std::ostream&
    operator<<(std::ostream& os, const Move& m) {
      os << folly::to<std::string>
	(static_cast<std::underlying_type<MoveType>::type>(m._type));
      for (const auto e : m._extras) {
	os << std::string(" ") << folly::to<std::string>(e);
      }
      return os;
    }
   private:
    MoveType _type;
    std::array<int8_t, NUM_MOVE_EXTRAS> _extras;
  };

  const static size_t TABLEAU_SIZE = 7;
  const static size_t MAX_HAND_SIZE = 24;
  struct TableauColumn {
    std::array<Card, TABLEAU_SIZE - 1> faceDown;
    std::array<Card, NUM_RANKS> faceUp;
    size_t faceDownSize = 0;
    size_t faceUpSize = 0;
  };

  class Solitaire {
   public:
    Solitaire() : Solitaire(getShuffledDeck(), 3) {}
    Solitaire(size_t drawSize) : Solitaire(getShuffledDeck(), drawSize) {}
    Solitaire(const std::array<Card, NUM_CARDS>& deck) : Solitaire(deck, 3) {}
    Solitaire(const std::array<Card, NUM_CARDS>& deck, size_t drawSize);

    const std::array<Rank, NUM_SUITS>& foundation() const { return _foundation; }
    const std::array<Card, MAX_HAND_SIZE>& hand() const { return _hand; }
    const std::array<TableauColumn, TABLEAU_SIZE>&
      tableau() const { return _tableau; }
    const size_t drawSize() const { return _drawSize; }
    const size_t handSize() const { return _handSize; }
    const size_t wasteSize() const { return _wasteSize; }

    bool isValid(const Move& move) const;
    void apply(const Move& move);
    bool isWon() const;
    std::string toConsoleString() const;
    inline friend std::ostream& operator<<(std::ostream& os, const Solitaire& s) {
      return os << s.toConsoleString();
    }

  private:
    size_t _drawSize;
    std::array<Rank, NUM_SUITS> _foundation;
    std::array<Card, MAX_HAND_SIZE> _hand;
    std::array<TableauColumn, TABLEAU_SIZE> _tableau;
    size_t _handSize;
    size_t _wasteSize;
  };
}
