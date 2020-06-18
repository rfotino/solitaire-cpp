#pragma once

#include <array>
#include <chrono>
#include <set>
#include <vector>

#include <folly/Optional.h>
#include <folly/container/EvictingCacheMap.h>
#include <folly/container/F14Set.h>
#include <gflags/gflags.h>

#include "Solitaire.h"

DECLARE_uint64(state_cache_size);
DECLARE_uint64(move_cache_size);

namespace solitaire {
  // Helpers for making human-readable cache keys
  const static std::array<char, NUM_SUITS> SUIT_CHARS = {'S', 'H', 'D', 'C'};
  const static std::array<char, NUM_RANKS> RANK_CHARS =
    {'A', '2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K'};

  enum class SolverStatus { SOLVED, TIMEOUT, NO_SOLUTION };
  struct SolverResult {
    SolverStatus status;
    std::chrono::seconds elapsed;
    std::vector<Move> moves;
  };

  class Solver {
   public:
    Solver(const Solitaire& game, std::chrono::seconds timeout)
      : _game(game), _timeout(timeout), _stateCache(FLAGS_state_cache_size),
	_tableauMoveCache(FLAGS_move_cache_size), _numCalls(0) {}
    SolverResult solve();
    size_t getNumCalls() const { return _numCalls; }

  private:
    const static size_t MAX_VALID_MOVES = 25;
    const static size_t MAX_VALID_TABLEAU_MOVES = 14;
    void _getValidMoves(const Solitaire& game,
			std::array<Move, MAX_VALID_MOVES>& moves,
			size_t& numMoves);
    void _addAceMoves(const Solitaire& game,
		      std::array<Move, MAX_VALID_MOVES>& moves,
		      size_t& numMoves);
    void _addToFoundationMoves(const Solitaire& game,
			       std::array<Move, MAX_VALID_MOVES>& moves,
			       size_t& numMoves);
    void _addCardRevealingMoves(const Solitaire& game,
				std::array<Move, MAX_VALID_MOVES>& moves,
				size_t& numMoves);
    void _addWasteToTableauMoves(const Solitaire& game,
				 std::array<Move, MAX_VALID_MOVES>& moves,
				 size_t& numMoves);
    void _addDrawMove(const Solitaire& game,
		      std::array<Move, MAX_VALID_MOVES>& moves,
		      size_t& numMoves);
    void _addTableauToTableauMoves(const Solitaire& game,
				   std::array<Move, MAX_VALID_MOVES>& moves,
				   size_t& numMoves);
    uint64_t _getGameCacheStr(const Solitaire& game, bool canFlipDeck) const;
    folly::Optional<std::vector<Move>>
      _maybeApplyMove(const Move& move, const Solitaire& Game,
		      std::set<std::vector<Card>>& seenCardStacks,
		      bool canFlipDeck, size_t depth);
    folly::Optional<std::vector<Move>>
      _solveImpl(const Solitaire& game,
		 std::set<std::vector<Card>>& seenCardStacks,
		 bool canFlipDeck,
		 size_t depth);

    Solitaire _game;
    std::chrono::steady_clock::time_point _startTime;
    std::chrono::seconds _timeout;
    folly::EvictingCacheMap<uint64_t, bool> _stateCache;
    folly::EvictingCacheMap<
      uint64_t, std::pair<std::array<Move, MAX_VALID_TABLEAU_MOVES>, size_t>>
    _tableauMoveCache;
    size_t _numCalls;
  };
}
