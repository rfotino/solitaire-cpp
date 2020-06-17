#include <iostream>
#include <sstream>

#include "Solver.h"

DEFINE_uint64(state_cache_size, 1000000, "Max entries for solver state cache");
DEFINE_uint64(move_cache_size, 100000,
	      "Max entries for tableau move cache");

namespace solitaire {
  // Helpers for making human-readable cache keys
  const static std::vector<char> SUIT_CHARS = {'S', 'H', 'D', 'C'};
  const static std::vector<char> RANK_CHARS =
    {'A', '2', '3', '4', '5', '6', '7', '8', '9', 'T', 'J', 'Q', 'K'};

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

  std::vector<Move> Solver::_getValidMoves(const Solitaire& game) {
    std::vector<Move> moves;
    _addAceMoves(game, moves);
    _addToFoundationMoves(game, moves);
    _addCardRevealingMoves(game, moves);
    _addWasteToTableauMoves(game, moves);
    _addDrawMove(game, moves);
    _addTableauToTableauMoves(game, moves);
    return moves;
  }

  void Solver::_addAceMoves(const Solitaire& game, std::vector<Move>& moves) {
    if (!game.waste().empty() && game.waste().back().rank() == 0) {
      moves.push_back(Move(MoveType::WASTE_TO_FOUNDATION, {}));
    }
    for (uint8_t srcColIdx = 0; srcColIdx < game.tableau().size(); srcColIdx++) {
      const auto& column = game.tableau().at(srcColIdx);
      if (!column.faceUp.empty() && column.faceUp.back().rank() == 0) {
	moves.push_back(Move(MoveType::TABLEAU_TO_FOUNDATION, {srcColIdx}));
      }
    }
  }

  void Solver::_addToFoundationMoves(const Solitaire& game,
				     std::vector<Move>& moves) {
    if (!game.waste().empty() && game.waste().back().rank() != 0) {
      const Move move(MoveType::WASTE_TO_FOUNDATION, {});
      if (game.isValid(move)) {
	moves.push_back(std::move(move));
      }
    }
    for (uint8_t srcColIdx = 0; srcColIdx < game.tableau().size();
	 srcColIdx++) {
      const auto& column = game.tableau().at(srcColIdx);
      if (!column.faceUp.empty() && column.faceUp.back().rank() != 0) {
	const Move move(MoveType::TABLEAU_TO_FOUNDATION, {srcColIdx});
	if (game.isValid(move)) {
	  moves.push_back(std::move(move));
	}
      }
    }
  }

  // Tableau-to-tableau moves that reveal a card. Tableau-foundation moves
  // that reveal a card are covered in _addToFoundationMoves()
  void Solver::_addCardRevealingMoves(const Solitaire& game,
				      std::vector<Move>& moves) {
    // Store new moves in a vector to be added later, since we
    // are going to priority order them first
    std::vector<Move> newMoves;
    // If there is no empty space, prioritize creating an empty
    // space when choosing between cards to reveal
    bool needsKingSpace = true;
    for (uint8_t srcColIdx = 0; srcColIdx < game.tableau().size();
	 srcColIdx++) {
      const auto& srcCol = game.tableau().at(srcColIdx);
      if (srcCol.faceUp.empty()) {
	needsKingSpace = false;
      } else if (!srcCol.faceDown.empty()) {
	for (uint8_t dstColIdx = 0; dstColIdx < game.tableau().size();
	     dstColIdx++) {
	  if (srcColIdx == dstColIdx) {
	    continue;
	  }
	  const Move move(MoveType::TABLEAU_TO_TABLEAU,
			  {srcColIdx, 0, dstColIdx});
	  if (game.isValid(move)) {
	    newMoves.push_back(move);
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
	const auto lhsCol = lhs.extras().at(0);
	const auto rhsCol = rhs.extras().at(0);
	const auto lhsCount = game.tableau().at(lhsCol).faceDown.size();
	const auto rhsCount = game.tableau().at(rhsCol).faceDown.size();
	if (lhsCount == rhsCount) {
	  return lhsCol < rhsCol;
	} else if (needsKingSpace) {
	  return lhsCount < rhsCount;
	} else {
	  return rhsCount < lhsCount;
	}
      };
    std::sort(newMoves.begin(), newMoves.end(), sortFunc);
    moves.insert(moves.end(), newMoves.begin(), newMoves.end());
  }

  void Solver::_addWasteToTableauMoves(const Solitaire& game,
				       std::vector<Move>& moves) {
    for (uint8_t dstColIdx = 0; dstColIdx < game.tableau().size();
	 dstColIdx++) {
      const Move move(MoveType::WASTE_TO_TABLEAU, {dstColIdx});
      if (game.isValid(move)) {
	moves.push_back(std::move(move));
      }
    }
  }

  void Solver::_addDrawMove(const Solitaire& game, std::vector<Move>& moves) {
    const Move move(MoveType::DRAW, {});
    if (game.isValid(move)) {
      moves.push_back(std::move(move));
    }
  }

  // Tableau-to-tableau moves that don't reveal a card. Some room for
  // optimization here, because there are often a lot of face up cards
  // but very few or no valid moves. Could cache which moves are valid
  // for a given tableau
  void Solver::_addTableauToTableauMoves(const Solitaire& game,
					 std::vector<Move>& moves) {
    // Get cache key
    std::stringstream ss;
    for (auto i = 0; i < game.tableau().size(); i++) {
      const auto& column = game.tableau().at(i);
      ss << i;
      ss << column.faceDown.size();
      for (const auto c : column.faceUp) {
	ss << RANK_CHARS[c.rank()] << SUIT_CHARS[c.suit()];
      }
      ss << "|";
    }
    const auto cacheStr = ss.str();
    if (_tableauMoveCache.exists(cacheStr)) {
      for (const auto& m : _tableauMoveCache.get(cacheStr)) {
	moves.push_back(m);
      }
      return;
    }
    std::vector<Move> newMoves;

    for (uint8_t srcColIdx = 0; srcColIdx < game.tableau().size();
	 srcColIdx++) {
      const auto& srcCol = game.tableau().at(srcColIdx);
      // Start at 1 to skip over card-revealing moves and moves that
      // just shuffle the king to another empty space
      for (uint8_t srcRowIdx = 1; srcRowIdx < srcCol.faceUp.size();
	   srcRowIdx++) {
	for (uint8_t dstColIdx = 0; dstColIdx < game.tableau().size();
	     dstColIdx++) {
	  if (srcColIdx == dstColIdx) {
	    continue;
	  }
	  const Move move(MoveType::TABLEAU_TO_TABLEAU,
			  {srcColIdx, srcRowIdx, dstColIdx});
	  if (game.isValid(move)) {
	    newMoves.push_back(move);
	    moves.push_back(std::move(move));
	  }
	}
      }
    }

    _tableauMoveCache.set(cacheStr, std::move(newMoves));
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
  std::string Solver::_getGameCacheStr(const Solitaire& game,
				       bool canFlipDeck) const {
    const auto separator = '|';

    // Get sorted array of all cards we could possibly draw from the
    // hand/waste from this game state
    std::set<Card> accessibleDrawCardSet;
    if (!game.waste().empty()) {
      accessibleDrawCardSet.insert(game.waste().back());
    }
    std::vector<Card> newDeck = game.waste();
    std::reverse(newDeck.begin(), newDeck.end());
    newDeck.insert(newDeck.begin(), game.hand().begin(), game.hand().end());
    for (auto i = 0; i < newDeck.size(); i += game.drawSize()) {
      accessibleDrawCardSet.insert(newDeck[i]);
    }
    if (!newDeck.empty()) {
      accessibleDrawCardSet.insert(newDeck.back());
    }
    std::vector<Card> accessibleDrawCards(accessibleDrawCardSet.begin(),
					  accessibleDrawCardSet.end());
    std::sort(accessibleDrawCards.begin(), accessibleDrawCards.end());

    // Start combining all relevant parts into a string. Looks like
    // canFlip | topCard | accessibleDrawCards | foundation | tableau
    std::stringstream ss;
    ss << canFlipDeck << separator;
    if (!game.waste().empty()) {
      const auto topCard = game.waste().back();
      ss << RANK_CHARS[topCard.rank()] << SUIT_CHARS[topCard.suit()];
    }
    ss << separator;
    for (const auto card : accessibleDrawCards) {
      ss << RANK_CHARS[card.rank()] << SUIT_CHARS[card.suit()];
    }
    ss << separator;
    for (const auto f : game.foundation()) {
      ss << (f >= 0 ? RANK_CHARS[f] : '0');
    }
    ss << separator;
    std::vector<std::string> tableauStrings;
    for (auto i = 0; i < game.tableau().size(); i++) {
      const auto& column = game.tableau().at(i);
      std::stringstream ssCol;
      if (!column.faceDown.empty()) {
	ssCol << i << column.faceDown.size();
      }
      for (const auto card : column.faceUp) {
	ssCol << RANK_CHARS[card.rank()] << SUIT_CHARS[card.suit()];
      }
      tableauStrings.push_back(ssCol.str());
    }
    std::sort(tableauStrings.begin(), tableauStrings.end());
    for (const auto& tableauString : tableauStrings) {
      ss << tableauString << separator;
    }
    return ss.str();
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
      const auto newSrcStack =
	clonedGame.tableau().at(move.extras().at(0)).faceUp;
      const auto newDstStack =
	clonedGame.tableau().at(move.extras().at(2)).faceUp;
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
    if (_numCalls % 5000 == 0) {
      const auto now = std::chrono::steady_clock::now();
      const auto elapsed =
	std::chrono::duration_cast<std::chrono::seconds>(now - _startTime);
      std::cout << "calls: " << _numCalls << std::endl;
      std::cout << "depth: " << depth << std::endl;
      std::cout << "state cache size: " << _stateCache.size() << std::endl;
      std::cout << "move cache size: " << _tableauMoveCache.size() << std::endl;
      std::cout << "elapsed: " << elapsed.count() << " seconds" << std::endl;
      std::cout << game << std::endl;
    }

    const auto moves = _getValidMoves(game);
    for (const auto& move : moves) {
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
