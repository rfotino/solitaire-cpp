#pragma once

#include <vector>
#include <folly/Conv.h>
#include <folly/Random.h>

namespace solitaire {
  typedef int8_t Suit;
  typedef int8_t Rank;
  const Suit SPADES = 0;
  const Suit HEARTS = 1;
  const Suit DIAMONDS = 2;
  const Suit CLUBS = 3;
  const size_t NUM_SUITS = 4;
  const size_t NUM_RANKS = 13;

  class Card {
   public:
    Card(Suit suit, Rank rank) : _suit(suit), _rank(rank) {}
    Suit suit() const { return _suit; }
    Rank rank() const { return _rank; }
    std::string toUnicode() const;
    bool operator<(const Card& other) const {
      return _suit == other._suit ? _rank < other._rank : _suit < other._suit;
    }
    inline friend std::ostream&
    operator<<(std::ostream& os, const Card& c) {
      return os << c.toUnicode();
    }

   private:
    Suit _suit;
    Rank _rank;
  };

  std::vector<Card> getShuffledDeck();

  enum class MoveType {
    DRAW,
    WASTE_TO_FOUNDATION,
    WASTE_TO_TABLEAU,
    TABLEAU_TO_FOUNDATION,
    TABLEAU_TO_TABLEAU,
  };

  class Move {
   public:
    Move(MoveType type, std::vector<uint8_t> extras) :
      _type(type), _extras(extras) {}
    MoveType type() const { return _type; }
    const std::vector<uint8_t>& extras() const { return _extras; }
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
    std::vector<uint8_t> _extras;
  };

  struct TableauColumn {
    std::vector<Card> faceDown;
    std::vector<Card> faceUp;
  };

  class Solitaire {
   public:
    Solitaire() : Solitaire(getShuffledDeck(), 3) {}
    Solitaire(size_t drawSize) : Solitaire(getShuffledDeck(), drawSize) {}
    Solitaire(const std::vector<Card>& deck) : Solitaire(deck, 3) {}
    Solitaire(const std::vector<Card>& deck, size_t drawSize);

    const std::vector<Rank>& foundation() const { return _foundation; }
    const std::vector<Card>& hand() const { return _hand; }
    const std::vector<Card>& waste() const { return _waste; }
    const std::vector<TableauColumn>&
      tableau() const { return _tableau; }
    const size_t drawSize() const { return _drawSize; }

    bool isValid(const Move& move) const;
    void apply(const Move& move);
    bool isWon() const;
    std::string toConsoleString() const;
    inline friend std::ostream& operator<<(std::ostream& os, const Solitaire& s) {
      return os << s.toConsoleString();
    }

  private:
    size_t _drawSize;
    std::vector<Rank> _foundation;
    std::vector<Card> _hand;
    std::vector<Card> _waste;
    std::vector<TableauColumn> _tableau;
  };
}
