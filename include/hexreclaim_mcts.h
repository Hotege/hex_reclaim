/**
 * @file hexreclaim_mcts.h
 * @brief Monte Carlo tree search for hex_reclaim.
 *
 * UCT search with uniformly random playouts. A position whose winner is certain
 * is marked as proven, and proofs are propagated towards the root
 * (MCTS-Solver): a position is won if one move is proven to win, and lost if
 * all moves are proven to lose. Given enough iterations and nodes, small boards
 * and endgames are therefore solved exactly.
 *
 * Like the rest of the library this module performs no dynamic memory
 * allocation, has no mutable global state and does no I/O: the caller provides
 * the memory of the search tree. Unlike the core it needs the C math library
 * (`-lm`).
 *
 * A search is an ::hr_mcts object plus an array of ::hr_mcts_node and,
 * optionally, a transposition table, see hr_mcts_set_table(). Searches are
 * independent of each other, so every thread can run its own with its own seed,
 * and the visit counts of the root moves can then be added up.
 *
 * To search again after a move, initialize a new search from the new position.
 *
 * @todo Multithreading: the library starts no threads. Parallel playouts and
 * root parallelization of the search are still to be implemented.
 *
 * @todo Reuse the part of the tree below the new position after a move.
 *
 * @todo Try playout policies stronger than uniformly random moves.
 */
#ifndef HEXRECLAIM_MCTS_H
#define HEXRECLAIM_MCTS_H

#include "hexreclaim.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Default exploration constant of a search.
 *
 * Chosen by playing searches with different constants against each other on
 * boards of radius 2 to 5.
 */
#define HR_MCTS_EXPLORATION 0.35

/** @brief A node array of at least this size is accepted for every position. */
#define HR_MCTS_MIN_NODES (HR_MAX_MOVES + 1)

/**
 * @brief One node of a search tree.
 *
 * Public only so that callers can allocate nodes; the fields are for internal
 * use by the library.
 */
typedef struct {
    uint32_t visits;      /**< Iterations that passed through the node. */
    uint32_t wins;        /**< Those won by the player who moved into it. */
    uint32_t first_child; /**< Index of the first child. */
    unsigned action : 11;   /**< Action leading to the node. */
    unsigned children : 11; /**< Number of children; 0 if not expanded. */
    unsigned proven : 2;    /**< 0, or 1 + the player proven to win. */
} hr_mcts_node;

/**
 * @brief One slot of a transposition table.
 *
 * Public only so that callers can allocate tables; the fields are for internal
 * use by the library.
 */
typedef struct {
    uint64_t key;         /**< Hash of an expanded position. */
    uint32_t first_child; /**< Index of its first child; 0 for a free slot. */
    uint32_t children;    /**< Number of its children. */
} hr_mcts_entry;

/**
 * @brief State of one search.
 *
 * Initialize it with hr_mcts_init(). Apart from @ref exploration, the fields
 * are for internal use only.
 */
typedef struct {
    hr_board board;      /**< Copy of the board. */
    hr_pos root;         /**< Position being searched. */
    hr_mcts_node *nodes; /**< Node array of the caller; node 0 is the root. */
    uint32_t capacity;   /**< Size of the node array. */
    uint32_t used;       /**< Number of nodes in use. */
    hr_mcts_entry *table; /**< Transposition table of the caller, or NULL. */
    uint32_t table_size;  /**< Size of the table; 0 without one. */
    hr_rng rng;          /**< Generator for expansion and playouts. */
    /**
     * Weight of the exploration term of UCB1, at least 0. Larger values
     * spread the iterations over more moves. May be changed between runs.
     */
    double exploration;
} hr_mcts;

/** @brief What a search knows about one move of the root position. */
typedef struct {
    hr_action action; /**< The move. */
    uint32_t visits;  /**< Iterations that started with it. */
    uint32_t wins;    /**< Those won by the player to move at the root. */
    hr_player proven; /**< Proven winner after the move, or ::HR_NOBODY. */
} hr_mcts_move;

/**
 * @brief Starts a search of a position.
 *
 * The board and the position are copied; the node array is used by reference
 * and must stay valid as long as the search is. Searching the same position
 * with the same seed, capacity, exploration constant and number of iterations
 * gives the same result.
 *
 * Expanding a node adds one node for each distinct action of its position, so
 * on average the tree grows by a few to some tens of nodes per iteration. When
 * the array is full, the search goes on without growing the tree further.
 *
 * @param[out] search   Object to initialize.
 * @param[in]  board    The board.
 * @param[in]  pos      Position to search, with a game in progress.
 * @param[in]  nodes    Node array; its contents are overwritten.
 * @param[in]  capacity Number of nodes in the array; at least one more than the
 *                      number of distinct actions of @p pos, which
 *                      ::HR_MCTS_MIN_NODES always satisfies.
 * @param[in]  seed     Seed of the random generator.
 * @retval HR_OK            Success.
 * @retval HR_ERR_ARG       A pointer is NULL, board and position have different
 *                          radii, or the array is too small; @p *search is left
 *                          unmodified.
 * @retval HR_ERR_GAME_OVER The game has already ended; @p *search is left
 *                          unmodified.
 */
hr_status hr_mcts_init(hr_mcts *search, const hr_board *board,
                       const hr_pos *pos, hr_mcts_node *nodes, size_t capacity,
                       uint64_t seed);

/**
 * @brief Gives a search a transposition table.
 *
 * Different move orders often lead to the same position. Without a table, the
 * search treats each occurrence separately. With one, it recognizes the
 * position by hr_pos_hash() and lets all occurrences share their children: what
 * is learned about the moves from a position, including proofs, serves every
 * way of reaching it, and the subtree is stored only once. The statistics of
 * the moves that lead to the position remain separate.
 *
 * Call this after hr_mcts_init() and before the first iteration. The table is
 * cleared and then used by reference, like the node array. Each expanded
 * position takes one entry, so one entry for every four nodes is plenty. A
 * position that finds no free slot is searched without being shared.
 *
 * Two positions with the same hash would be taken for one, which is very
 * unlikely, see hr_pos_hash(). It could distort the results but cannot make the
 * search access memory out of bounds.
 *
 * @param search   The search.
 * @param table    Table of @p capacity entries; its contents are overwritten.
 * @param capacity Number of entries, at least 1. Any number works.
 * @retval HR_OK      Success.
 * @retval HR_ERR_ARG A pointer is NULL, @p capacity is 0 or iterations have
 *                    already been run; the search and the table are left
 *                    unmodified.
 */
hr_status hr_mcts_set_table(hr_mcts *search, hr_mcts_entry *table,
                            size_t capacity);

/**
 * @brief Runs iterations of a search.
 *
 * Stops early once the root is proven, because nothing more can be learned. Can
 * be called repeatedly, for example in small batches while watching a clock.
 *
 * @param search     The search.
 * @param iterations Number of iterations to run.
 * @return The number of iterations run.
 */
uint32_t hr_mcts_run(hr_mcts *search, uint32_t iterations);

/**
 * @brief Gets the move the search recommends.
 *
 * A proven win if there is one; otherwise the most visited move among those not
 * proven to lose. If every move is proven to lose, the most visited one.
 *
 * If the winner of the root position is already certain (see hr_pos_decided()),
 * no iterations are run and an arbitrary legal move is returned. The same holds
 * before the first iteration.
 *
 * @param search The search.
 * @return The recommended move.
 */
hr_mcts_move hr_mcts_best(const hr_mcts *search);

/**
 * @brief Gets the statistics of all moves of the root position.
 *
 * These are the distinct actions of hr_pos_actions(), in no particular order.
 *
 * @param[in]  search The search.
 * @param[out] out    Buffer for the moves; may be NULL to only count them.
 * @param[in]  cap    Maximum number of moves to write to @p out.
 * @return The total number of moves, which may exceed @p cap.
 */
size_t hr_mcts_moves(const hr_mcts *search, hr_mcts_move *out, size_t cap);

/**
 * @brief Gets the number of iterations run so far.
 *
 * @param search The search.
 * @return The number of iterations.
 */
uint32_t hr_mcts_iterations(const hr_mcts *search);

/**
 * @brief Gets the proven winner of the root position.
 *
 * @param search The search.
 * @return The player who wins with best play, or ::HR_NOBODY if not proven.
 */
hr_player hr_mcts_proven(const hr_mcts *search);

#ifdef __cplusplus
}
#endif

#endif /* HEXRECLAIM_MCTS_H */
