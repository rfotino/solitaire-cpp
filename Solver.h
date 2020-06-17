#pragma once

#include <chrono>
#include <set>

#include <folly/Optional.h>
#include <folly/container/EvictingCacheMap.h>
#include <folly/container/F14Set.h>
#include <gflags/gflags.h>

#include "Solitaire.h"

DECLARE_uint64(state_cache_size);
DECLARE_uint64(move_cache_size);

namespace solitaire {
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

  private:
    std::vector<Move> _getValidMoves(const Solitaire& game);
    void _addAceMoves(const Solitaire& game, std::vector<Move>& moves);
    void _addToFoundationMoves(const Solitaire& game, std::vector<Move>& moves);
    void _addCardRevealingMoves(const Solitaire& game,
				std::vector<Move>& moves);
    void _addWasteToTableauMoves(const Solitaire& game,
				 std::vector<Move>& moves);
    void _addDrawMove(const Solitaire& game, std::vector<Move>& moves);
    void _addTableauToTableauMoves(const Solitaire& game,
				   std::vector<Move>& moves);
    std::string _getGameCacheStr(const Solitaire& game, bool canFlipDeck) const;
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
    folly::EvictingCacheMap<std::string, bool> _stateCache;
    folly::EvictingCacheMap<std::string, std::vector<Move>> _tableauMoveCache;
    uint64_t _numCalls;
  };
}
