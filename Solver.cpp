#include <iostream>
#include <sstream>
#include <folly/Hash.h>

#include "Solver.h"

DEFINE_uint64(state_cache_size, 1000000, "Max entries for solver state cache");
DEFINE_uint64(move_cache_size, 100000,
	      "Max entries for tableau move cache");

namespace solitaire {
  // Helpers for making human-readable cache keys
  const static std::array<char, NUM_SUITS> SUIT_CHARS = {'S', 'H', 'D', 'C'};
  const static std::array<char, NUM_RANKS> RANK_CHARS =
    {'A', '2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K'};
  const static auto START_TIME = std::chrono::steady_clock::now();
  static size_t totalMoves = 0;

  // Main entry point for solving, this starts the timer and starts solving
  // and returns the winning moves (if any) and some diagnostic info like
  // time elapsed.
  SolverResult Solver::solve() {
    SolverResult result;
    _startTime = std::chrono::steady_clock::now();
    std::set<std::vector<Card>> seenCardStacks;
    const auto winningMoves = _solveImpl(_game, seenCardStacks, false, 0);
    auto endTime = std::chrono::steady_clock::now();
    result.elapsed =
      std::chrono::duration_cast<std::chrono::seconds>(endTime - _startTime);
    if (winningMoves) {
      result.status = SolverStatus::SOLVED;
      result.moves = *winningMoves;
    } else if (endTime - _startTime >= _timeout) {
      result.status = SolverStatus::TIMEOUT;
    } else {
      result.status = SolverStatus::NO_SOLUTION;
    }
    return result;
  }

  void Solver::_getValidMoves(const Solitaire& game,
			      std::array<Move, MAX_VALID_MOVES>& moves,
			      size_t& numMoves) {
    _addAceMoves(game, moves, numMoves);
    _addToFoundationMoves(game, moves, numMoves);
    _addCardRevealingMoves(game, moves, numMoves);
    _addWasteToTableauMoves(game, moves, numMoves);
    _addDrawMove(game, moves, numMoves);
    _addTableauToTableauMoves(game, moves, numMoves);
  }

  void Solver::_addAceMoves(const Solitaire& game,
			    std::array<Move, MAX_VALID_MOVES>& moves,
			    size_t& numMoves) {
    if (game.wasteSize() > 0 &&
	game.hand()[game.handSize() - game.wasteSize()].rank == 0) {
      moves[numMoves++] = Move(MoveType::WASTE_TO_FOUNDATION, {-1, -1, -1});
    }
    for (int8_t srcColIdx = 0; srcColIdx < game.tableau().size(); srcColIdx++) {
      const auto& column = game.tableau()[srcColIdx];
      if (column.faceUpSize > 0 &&
	  column.faceUp[column.faceUpSize - 1].rank == 0) {
	moves[numMoves++] =
	  Move(MoveType::TABLEAU_TO_FOUNDATION, {srcColIdx, -1, -1});
      }
    }
  }

  void Solver::_addToFoundationMoves(const Solitaire& game,
				     std::array<Move, MAX_VALID_MOVES>& moves,
				     size_t& numMoves) {
    if (game.wasteSize() > 0 &&
	game.hand()[game.handSize() - game.wasteSize()].rank != 0) {
      const Move move(MoveType::WASTE_TO_FOUNDATION, {-1, -1, -1});
      if (game.isValid(move)) {
	moves[numMoves++] = move;
      }
    }
    for (int8_t srcColIdx = 0; srcColIdx < game.tableau().size();
	 srcColIdx++) {
      const auto& column = game.tableau()[srcColIdx];
      if (column.faceUpSize > 0 &&
	  column.faceUp[column.faceUpSize - 1].rank != 0) {
	const Move move(MoveType::TABLEAU_TO_FOUNDATION, {srcColIdx, -1, -1});
	if (game.isValid(move)) {
	  moves[numMoves++] = move;
	}
      }
    }
  }

  // Tableau-to-tableau moves that reveal a card. Tableau-foundation moves
  // that reveal a card are covered in _addToFoundationMoves()
  void Solver::_addCardRevealingMoves(const Solitaire& game,
				      std::array<Move, MAX_VALID_MOVES>& moves,
				      size_t& numMoves) {
    // Store new moves in a vector to be added later, since we
    // are going to priority order them first
    std::array<Move, MAX_VALID_TABLEAU_MOVES> newMoves;
    size_t numNewMoves = 0;
    // If there is no empty space, prioritize creating an empty
    // space when choosing between cards to reveal
    bool needsKingSpace = true;
    for (int8_t srcColIdx = 0; srcColIdx < game.tableau().size();
	 srcColIdx++) {
      const auto& srcCol = game.tableau()[srcColIdx];
      if (srcCol.faceUpSize == 0) {
	needsKingSpace = false;
      } else if (srcCol.faceDownSize > 0) {
	for (int8_t dstColIdx = 0; dstColIdx < game.tableau().size();
	     dstColIdx++) {
	  if (srcColIdx == dstColIdx) {
	    continue;
	  }
	  const Move move(MoveType::TABLEAU_TO_TABLEAU,
			  {srcColIdx, 0, dstColIdx});
	  if (game.isValid(move)) {
	    newMoves[numNewMoves] = move;
	    numNewMoves++;
	  }
	}
      }
    }
    // Sort new moves by number of face down cards in the source
    // column. If we need a king space, prioritize columns with fewer
    // face down cards. Otherwise prioritize columns with the most
    // face down cards.
    const auto sortFunc =
      [&game, needsKingSpace](const Move& lhs, const Move& rhs) {
	const auto lhsColIdx = lhs.extras()[0];
	const auto rhsColIdx = rhs.extras()[0];
	const auto lhsCount = game.tableau()[lhsColIdx].faceDownSize;
	const auto rhsCount = game.tableau()[rhsColIdx].faceDownSize;
	if (lhsCount == rhsCount) {
	  return lhsColIdx < rhsColIdx;
	} else if (needsKingSpace) {
	  return lhsCount < rhsCount;
	} else {
	  return rhsCount < lhsCount;
	}
      };
    std::sort(newMoves.begin(), newMoves.begin() + numNewMoves, sortFunc);
    for (auto i = 0; i < numNewMoves; i++) {
      moves[numMoves++] = newMoves[i];
    }
  }

  void Solver::_addWasteToTableauMoves(const Solitaire& game,
				       std::array<Move, MAX_VALID_MOVES>& moves,
				       size_t& numMoves) {
    for (int8_t dstColIdx = 0; dstColIdx < game.tableau().size();
	 dstColIdx++) {
      const Move move(MoveType::WASTE_TO_TABLEAU, {dstColIdx, -1, -1});
      if (game.isValid(move)) {
	moves[numMoves++] = move;
      }
    }
  }

  void Solver::_addDrawMove(const Solitaire& game,
			    std::array<Move, MAX_VALID_MOVES>& moves,
			    size_t& numMoves) {
    const Move move(MoveType::DRAW, {-1, -1, -1});
    if (game.isValid(move)) {
      moves[numMoves++] = move;
    }
  }

  // Tableau-to-tableau moves that don't reveal a card. Some room for
  // optimization here, because there are often a lot of face up cards
  // but very few or no valid moves. Could cache which moves are valid
  // for a given tableau
  void Solver::_addTableauToTableauMoves(const Solitaire& game,
					 std::array<Move, MAX_VALID_MOVES>& moves,
					 size_t& numMoves) {
    // Get cache key
    const static size_t MAX_CACHE_KEY_SIZE = ((NUM_RANKS * 2) + 1) * TABLEAU_SIZE;
    char cacheKey[MAX_CACHE_KEY_SIZE];
    size_t cacheKeySize = 0;
    for (auto i = 0; i < game.tableau().size(); i++) {
      const auto& column = game.tableau()[i];
      cacheKey[cacheKeySize++] = '0' + i;
      cacheKey[cacheKeySize++] = '0' + column.faceDownSize;
      for (auto i = 0; i < column.faceUpSize; i++) {
	const auto c = column.faceUp[i];
	cacheKey[cacheKeySize++] = RANK_CHARS[c.rank];
	cacheKey[cacheKeySize++] = SUIT_CHARS[c.suit];
      }
      cacheKey[cacheKeySize++] = '|';
    }
    const auto cacheKeyHash = folly::hash::fnv64_buf(cacheKey, cacheKeySize);
    if (_tableauMoveCache.exists(cacheKeyHash)) {
      const auto& cached = _tableauMoveCache.get(cacheKeyHash);
      for (auto i = 0; i < cached.second; i++) {
	moves[numMoves++] = cached.first[i];
      }
      return;
    }

    std::array<Move, MAX_VALID_TABLEAU_MOVES> newMoves;
    size_t numNewMoves = 0;

    for (int8_t srcColIdx = 0; srcColIdx < game.tableau().size();
	 srcColIdx++) {
      const auto& srcCol = game.tableau()[srcColIdx];
      // Start at 1 to skip over card-revealing moves and moves that
      // just shuffle the king to another empty space
      for (int8_t srcRowIdx = 1; srcRowIdx < srcCol.faceUpSize;
	   srcRowIdx++) {
	for (int8_t dstColIdx = 0; dstColIdx < game.tableau().size();
	     dstColIdx++) {
	  if (srcColIdx == dstColIdx) {
	    continue;
	  }
	  const Move move(MoveType::TABLEAU_TO_TABLEAU,
			  {srcColIdx, srcRowIdx, dstColIdx});
	  if (game.isValid(move)) {
	    newMoves[numNewMoves++] = move;
	    moves[numMoves++] = move;
	  }
	}
      }
    }

    _tableauMoveCache.set(cacheKeyHash, std::make_pair(newMoves, numNewMoves));
  }

  /**
   * Turn the game state into a cache string that can be used for branch
   * pruning when we come across an equivalent state during search.
   * Some game states will produce the same cache string even though
   * the game states are not identical - but they would have to be
   * equivalent in the sense that if one state is solvable, the other
   * is solvable and vice versa. For example, identical stacks in the
   * tableau can be rearranged or the hand/talon can be at a different
   * state but with the same accessible cards.
   */
  uint64_t Solver::_getGameCacheStr(const Solitaire& game,
				    bool canFlipDeck) const {
    // canFlip | wasteIdx | hand | foundation | tableau
    const static size_t MAX_CACHE_STR_SIZE = 128;
    const static char SEPARATOR = '|';
    std::array<char, MAX_CACHE_STR_SIZE> cacheStr;
    size_t cacheStrSize = 0;

    cacheStr[cacheStrSize++] = canFlipDeck ? '1' : '0';

    cacheStr[cacheStrSize++] = 'a' + game.wasteSize();
    for (auto i = 0; i < game.handSize(); i++) {
      const auto card = game.hand()[i];
      cacheStr[cacheStrSize++] = RANK_CHARS[card.rank];
      cacheStr[cacheStrSize++] = SUIT_CHARS[card.suit];
    }
    cacheStr[cacheStrSize++] = SEPARATOR;

    for (const auto f : game.foundation()) {
      cacheStr[cacheStrSize++] = f >= 0 ? RANK_CHARS[f] : '0';
    }
    cacheStr[cacheStrSize++] = SEPARATOR;

    // Tableau column strings are of the form:
    // concat(colIdx, faceDownSize, faceUpCards) if they have face down
    // cards, otherwise the string is just faceUpCards. These are sorted
    // so if colIdx, faceDownSize are present, those are used first, then
    // the value of the first face up card. From left to right the sorted
    // columns become (hasFaceDownCards, onlyFaceUpCards, emptySpace).
    std::array<size_t, TABLEAU_SIZE> sortedTableauIndices;
    std::iota(sortedTableauIndices.begin(), sortedTableauIndices.end(), 0);
    const auto sortFunc =
      [&game](size_t i1, size_t i2) {
	const auto& lhs = game.tableau()[i1];
	const auto& rhs = game.tableau()[i2];
	if (lhs.faceDownSize > 0 && rhs.faceDownSize > 0) {
	  return i1 < i2;
	} else if (lhs.faceDownSize > 0 && rhs.faceDownSize == 0) {
	  return true;
	} else if (lhs.faceDownSize == 0 && rhs.faceDownSize > 0) {
	  return false;
	} else {  // neither has face down cards
	  if (lhs.faceUpSize > 0 && rhs.faceUpSize > 0) {
	    return lhs.faceUp[0] < rhs.faceUp[0];
	  } else if (lhs.faceUpSize > 0 && rhs.faceUpSize == 0) {
	    return true;
	  } else if (lhs.faceUpSize == 0 && rhs.faceUpSize > 0) {
	    return false;
	  } else {  // neither has face up cards
	    return i1 < i2;
	  }
	}
      };
    std::sort(sortedTableauIndices.begin(),
	      sortedTableauIndices.end(), sortFunc);
    for (const auto i : sortedTableauIndices) {
      const auto& column = game.tableau()[i];
      if (column.faceDownSize > 0) {
	cacheStr[cacheStrSize++] = '0' + i;
	cacheStr[cacheStrSize++] = '0' + column.faceDownSize;
      }
      for (auto j = 0; j < column.faceUpSize; j++) {
	cacheStr[cacheStrSize++] = RANK_CHARS[column.faceUp[j].rank];
	cacheStr[cacheStrSize++] = SUIT_CHARS[column.faceUp[j].suit];
      }
      cacheStr[cacheStrSize++] = SEPARATOR;
    }
    return folly::hash::fnv64_buf(cacheStr.data(), cacheStrSize);
  }

  /**
   * Corecursive with _solveImpl(). This applies a single move (that is
   * assumed to already be valid), considers whether to prune this branch,
   * and if the game state seems novel it recurses one level further.
   */
  folly::Optional<std::vector<Move>>
  Solver::_maybeApplyMove(const Move& move, const Solitaire& game,
			  std::set<std::vector<Card>>& seenCardStacks,
			  bool canFlipDeck, size_t depth) {
    // If you draw through the entire deck without playing from the
    // waste, you can't flip the deck and continue to draw. If the hand
    // length is zero and the move is draw we're about to flip the deck.
    // This prevents loops between moving things around on the tableau
    // and endlessly flipping through the deck.
    if (move.type() == MoveType::DRAW) {
      if (game.hand().empty()) {
	if (canFlipDeck) {
	  canFlipDeck = false;
	} else {
	  return folly::none;
	}
      }
    } else if (move.type() == MoveType::WASTE_TO_FOUNDATION ||
	       move.type() == MoveType::WASTE_TO_TABLEAU) {
      // If we're removing a card from the waste, we can flip
      // the deck again
      canFlipDeck = true;
    }

    // Clone game since we will now be applying the move
    Solitaire clonedGame(game);
    clonedGame.apply(move);

    // Check for stacks created on the tableau that we have already seen,
    // this is another reason to prune
    std::vector<std::vector<Card>> newStacks;
    if (move.type() == MoveType::TABLEAU_TO_TABLEAU) {
      const auto& srcCol = clonedGame.tableau()[move.extras()[0]];
      const auto& dstCol = clonedGame.tableau()[move.extras()[2]];
      const std::vector<Card>
	newSrcStack(srcCol.faceUp.begin(),
		    srcCol.faceUp.begin() + srcCol.faceUpSize);
      const std::vector<Card>
	newDstStack(dstCol.faceUp.begin(),
		    dstCol.faceUp.begin() + dstCol.faceUpSize);
      if (seenCardStacks.find(newSrcStack) != seenCardStacks.end() &&
	  seenCardStacks.find(newDstStack) != seenCardStacks.end()) {
	// Neither stack is new, abort
	return folly::none;
      }
      newStacks.push_back(newSrcStack);
      newStacks.push_back(newDstStack);
    }

    for (const auto& newStack : newStacks) {
      seenCardStacks.insert(newStack);
    }

    // Recurse one move further
    const auto remainingMoves =
      _solveImpl(clonedGame, seenCardStacks, canFlipDeck, depth + 1);

    // Back out changes made by applying this move before backtracking
    for (const auto& newStack : newStacks) {
      seenCardStacks.erase(newStack);
    }

    return remainingMoves;
  }

  /**
   * Corecursive with _maybeApplyMove(). Given a game state, finds all
   * valid moves and attempts to apply them one by one. Returns a list
   * of moves to win from this game, if possible, or folly::none if no
   * solution or upon timeout.
   */
  folly::Optional<std::vector<Move>>
  Solver::_solveImpl(const Solitaire& game,
		     std::set<std::vector<Card>>& seenCardStacks,
		     bool canFlipDeck, size_t depth) {
    // Short circuit if we've gone over the allotted time
    if (std::chrono::steady_clock::now() - _startTime >= _timeout) {
      return folly::none;
    }

    // Recursion base case
    if (game.isWon()) {
      return std::vector<Move>();
    }

    // Short circuit if we've seen this game state before
    const auto gameCacheStr = _getGameCacheStr(game, canFlipDeck);
    if (_stateCache.exists(gameCacheStr)) {
      _stateCache.get(gameCacheStr);  // exists() does not promote
      return folly::none;
    }
    _stateCache.set(gameCacheStr, 1 /* dummy value */);

    // Print out diagnostic info every so often
    _numCalls++;
    totalMoves++;
    if (_numCalls % 5000 == 0) {
      const auto now = std::chrono::steady_clock::now();
      const auto elapsed =
	std::chrono::duration_cast<std::chrono::seconds>(now - _startTime);
      const auto totalElapsed =
	std::chrono::duration_cast<std::chrono::seconds>(now - START_TIME).count();
      std::cout << "calls: " << _numCalls << std::endl;
      std::cout << "depth: " << depth << std::endl;
      std::cout << "state cache size: " << _stateCache.size() << std::endl;
      std::cout << "move cache size: " << _tableauMoveCache.size() << std::endl;
      std::cout << "elapsed: " << elapsed.count() << " seconds" << std::endl;
      if (totalElapsed > 0) {
	std::cout << "moves/sec: " << totalMoves / totalElapsed << std::endl;
      }
      std::cout << game << std::endl;
    }

    std::array<Move, MAX_VALID_MOVES> moves;
    size_t numMoves = 0;
    _getValidMoves(game, moves, numMoves);
    for (auto i = 0; i < numMoves; i++) {
      const auto move = moves[i];
      auto remainingMoves =
	_maybeApplyMove(move, game, seenCardStacks, canFlipDeck, depth);
      if (remainingMoves) {
	remainingMoves->insert(remainingMoves->begin(), move);
	return remainingMoves;
      }
    }
    return folly::none;
  }
}
