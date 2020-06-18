#include <iostream>
#include <map>
#include <string>

#include <folly/dynamic.h>
#include <folly/json.h>
#include <glog/logging.h>
#include <gflags/gflags.h>

#include "Solitaire.h"
#include "Solver.h"

DEFINE_uint64(timeout, 30, "Solver timeout in seconds.");

using namespace solitaire;

// Input lines use the printable characters from the JS implementation
// rather than the integer-based internal representation in C++. These
// maps are used to transform the input into something this solver
// understands.
const static std::map<char, Suit> CHAR_SUITS =
  {{'S', SPADES}, {'H', HEARTS}, {'C', CLUBS}, {'D', DIAMONDS}};
const static std::map<char, Rank> CHAR_RANKS =
  {{'A', 0}, {'2', 1}, {'3', 2}, {'4', 3}, {'5', 4}, {'6', 5}, {'7', 6},
   {'8', 7}, {'9', 8}, {'T', 9}, {'J', 10}, {'Q', 11}, {'K', 12}};

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  for (std::string line; std::getline(std::cin, line); ) {
    // Parse the line into a deck of cards, do some basic checking
    // like there are at least 52 cards in the input and the suits/ranks
    // are valid. No testing is done to make sure every card is represented
    // in the deck without duplicates.
    std::array<Card, NUM_CARDS> deck;
    if (line.size() < deck.size() * 2) {
      std::cerr << "Line not large enough, exiting" << std::endl;
      exit(1);
    }
    for (auto i = 0; i < deck.size(); i++) {
      const char rankChar = line[i * 2];
      const char suitChar = line[(i * 2) + 1];
      if (CHAR_RANKS.find(rankChar) == CHAR_RANKS.end() ||
	  CHAR_SUITS.find(suitChar) == CHAR_SUITS.end()) {
	std::cerr << "Found invalid card " << rankChar << suitChar << std::endl;
	exit(1);
      }
      deck[i] = Card(CHAR_SUITS.at(suitChar), CHAR_RANKS.at(rankChar));
    }

    // Attempt to solve the game from this deck, with timeout
    Solitaire game(deck);
    Solver solver(game, std::chrono::seconds(FLAGS_timeout));
    std::cerr << game << std::endl;
    auto result = solver.solve();

    // Dump diagnostic info to stderr
    switch (result.status) {
    case SolverStatus::SOLVED:
      std::cerr << "Found solution in " << result.moves.size() << " moves."
		<< std::endl;
      break;
    case SolverStatus::TIMEOUT:
      std::cerr << "Solver timed out, unknown if solution exists." << std::endl;
      break;
    case SolverStatus::NO_SOLUTION:
      std::cerr << "No solution exists." << std::endl;
      break;
    }
    std::cerr << "Time elapsed: " << result.elapsed.count()
	      << " seconds" << std::endl;

    // Gather output data for this game to be printed as JSON
    folly::dynamic output = folly::dynamic::object;
    output["status"] = result.status == SolverStatus::SOLVED ? "win" :
      (result.status == SolverStatus::TIMEOUT ? "timeout" : "lose");
    output["deck"] = folly::dynamic::array;
    for (const auto card : deck) {
      output["deck"].push_back(std::string() +
			       RANK_CHARS[card.rank] + SUIT_CHARS[card.suit]);
    }
    if (result.status == SolverStatus::SOLVED) {
      output["winningMoves"] = folly::dynamic::array;
      for (const auto& move : result.moves) {
	folly::dynamic extras = folly::dynamic::array;
	for (const auto extra : move.extras()) {
	  extras.push_back(extra);
	}
	const auto moveType =
	  static_cast<typename std::underlying_type<MoveType>::type>(move.type());
	output["winningMoves"].push_back(
	  folly::dynamic::object("type", moveType)("extras", extras));
      }
    } else {
      output["winningMoves"] = nullptr;
    }
    output["movesConsidered"] = solver.getNumCalls();
    output["elapsedSeconds"] = result.elapsed.count();
    output["timeoutSeconds"] = FLAGS_timeout;
    output["version"] = "cpp";

    // Write output to stdout as JSON
    std::cout << folly::toJson(output) << std::endl;
  }

  return 0;
}
