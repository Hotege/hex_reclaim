/**
 * @file hexreclaim.h
 * @brief Game-state library for hex_reclaim.
 *
 * Written in C99. The library performs no dynamic memory allocation, has no
 * global state and does no I/O. A whole game is a single ::hr_game value: it can
 * live on the stack and be copied with plain assignment, which is convenient for
 * tree search.
 *
 * @par Coordinates
 * Cells are addressed by axial coordinates `(q, r)`, with the center at
 * `(0, 0)`. A board of radius `n` consists of all cells satisfying `|q| <= n`,
 * `|r| <= n` and `|q + r| <= n`, which makes `3n(n+1)+1` cells.
 *
 * @par Directions
 * Pointy-top hexagons, with `r` increasing downwards:
 * @verbatim
              NW (0,-1)    NE (+1,-1)
           W (-1,0)    (q,r)    E (+1,0)
              SW (-1,+1)   SE (0,+1)
   @endverbatim
 *
 * @par Preconditions
 * Except for hr_init(), every @p game pointer passed to the library must point
 * to an object that hr_init() has successfully initialized. Query functions do
 * not check for null pointers.
 */
#ifndef HEXRECLAIM_H
#define HEXRECLAIM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Capacity limits
 * @{
 */

/** @brief Largest supported board radius. */
#define HR_MAX_RADIUS 8

/** @brief Largest supported number of pieces per player (`m`). */
#define HR_MAX_PIECES 255

/** @brief Number of cells on a board of radius ::HR_MAX_RADIUS. */
#define HR_MAX_CELLS (3 * HR_MAX_RADIUS * (HR_MAX_RADIUS + 1) + 1)

/**
 * @brief Upper bound on the number of moves in one game.
 *
 * Every move advances at least one cell along `empty -> piece -> claimed`, so a
 * game lasts at most `2 * cells - 1` moves.
 */
#define HR_MAX_PLIES (2 * HR_MAX_CELLS - 1)

/**
 * @brief Upper bound on the number of legal moves in any position.
 *
 * A suitable buffer size for hr_legal_moves().
 */
#define HR_MAX_MOVES (6 * HR_MAX_CELLS)

/**
 * @brief Side length of the internal board array, including a one-cell border
 *        of sentinels.
 */
#define HR_GRID_SPAN (2 * HR_MAX_RADIUS + 3)

/** @} */

/**
 * @name Basic types
 * @{
 */

/** @brief Identifies a player. */
typedef enum {
    HR_NOBODY = -1, /**< No player. */
    HR_P0 = 0,      /**< The player who moves first. */
    HR_P1 = 1       /**< The player who moves second. */
} hr_player;

/** @brief Contents of a cell. */
typedef enum {
    HR_OFF_BOARD = 0, /**< Outside the board. */
    HR_EMPTY,         /**< Empty: neither claimed nor holding a piece. */
    HR_PIECE_P0,      /**< Holds a piece of P0 that has not swept yet. */
    HR_PIECE_P1,      /**< Holds a piece of P1 that has not swept yet. */
    HR_CLAIMED_P0,    /**< Claimed by P0. */
    HR_CLAIMED_P1     /**< Claimed by P1. */
} hr_cell;

/** @brief The six sweep directions, each crossing one edge of a cell. */
typedef enum {
    HR_E = 0,    /**< East: `(+1, 0)`. */
    HR_NE,       /**< North-east: `(+1, -1)`. */
    HR_NW,       /**< North-west: `(0, -1)`. */
    HR_W,        /**< West: `(-1, 0)`. */
    HR_SW,       /**< South-west: `(-1, +1)`. */
    HR_SE,       /**< South-east: `(0, +1)`. */
    HR_DIR_COUNT /**< Number of directions; not a valid direction. */
} hr_dir;

/** @brief Axial coordinates of a cell. */
typedef struct {
    int q; /**< Column axis. */
    int r; /**< Row axis, increasing downwards. */
} hr_coord;

/** @brief Kind of a move. */
typedef enum {
    HR_PLACE = 0, /**< Place a piece from hand onto an empty cell. */
    HR_SWEEP = 1  /**< Sweep with one of the mover's pieces on the board. */
} hr_move_type;

/** @brief A single move. */
typedef struct {
    hr_move_type type; /**< Kind of the move. */
    hr_coord at;       /**< Target of a placement, or the piece that sweeps. */
    hr_dir dir;        /**< Sweep direction; used by ::HR_SWEEP only. */
} hr_move;

/** @brief Result code of a library operation. */
typedef enum {
    HR_OK = 0,               /**< Success. */
    HR_ERR_ARG,              /**< Null pointer or argument out of range. */
    HR_ERR_GAME_OVER,        /**< The game has already ended. */
    HR_ERR_BAD_MOVE,         /**< Invalid move type or direction. */
    HR_ERR_OFF_BOARD,        /**< The coordinates are outside the board. */
    HR_ERR_NOT_EMPTY,        /**< The target of a placement is not empty. */
    HR_ERR_NO_PIECE_IN_HAND, /**< The mover has no piece in hand. */
    HR_ERR_NOT_OWN_PIECE,    /**< No piece of the mover at the sweep origin. */
    HR_ERR_NO_HISTORY        /**< There is no move to undo. */
} hr_status;

/** @} */

/**
 * @name Game object
 * @{
 */

/** @brief One history entry. For internal use by the library. */
typedef struct {
    uint16_t at;   /**< Internal index of the cell the move was made at. */
    uint8_t type;  /**< Kind of the move, an ::hr_move_type value. */
    uint8_t dir;   /**< Sweep direction, an ::hr_dir value; 0 for placements. */
    uint8_t swept; /**< Cells a sweep covered, not counting its origin. */
} hr_record;

/**
 * @brief Complete state of one game.
 *
 * The fields are for internal use only; access them through the library
 * functions.
 */
typedef struct {
    uint8_t radius;      /**< Board radius `n`. */
    uint8_t pieces;      /**< Number of pieces per player `m`. */
    uint8_t turn;        /**< Player whose turn it is. */
    uint8_t in_hand[2];  /**< Pieces in hand, per player. */
    uint16_t empty;      /**< Number of empty cells. */
    uint16_t claimed[2]; /**< Number of claimed cells, per player. */
    uint16_t ply;        /**< Number of entries in @ref history. */
    uint8_t cells[HR_GRID_SPAN * HR_GRID_SPAN]; /**< ::hr_cell values. */
    hr_record history[HR_MAX_PLIES]; /**< Moves made, oldest first. */
} hr_game;

/** @} */

/**
 * @name Move constructors
 * @{
 */

/**
 * @brief Builds a placement move.
 *
 * @param q Column coordinate of the target cell.
 * @param r Row coordinate of the target cell.
 * @return The move. It is not checked for legality.
 */
static inline hr_move hr_place(int q, int r)
{
    hr_move m;
    m.type = HR_PLACE;
    m.at.q = q;
    m.at.r = r;
    m.dir = HR_E;
    return m;
}

/**
 * @brief Builds a sweep move.
 *
 * @param q   Column coordinate of the piece that sweeps.
 * @param r   Row coordinate of the piece that sweeps.
 * @param dir Sweep direction.
 * @return The move. It is not checked for legality.
 */
static inline hr_move hr_sweep(int q, int r, hr_dir dir)
{
    hr_move m;
    m.type = HR_SWEEP;
    m.at.q = q;
    m.at.r = r;
    m.dir = dir;
    return m;
}

/** @} */

/**
 * @name Initialization
 * @{
 */

/**
 * @brief Starts a new game.
 *
 * The board is empty, each player holds @p pieces pieces in hand, and P0 moves
 * first.
 *
 * @param[out] game   Object to initialize.
 * @param[in]  radius Board radius, from 1 to ::HR_MAX_RADIUS.
 * @param[in]  pieces Pieces per player, from 1 to ::HR_MAX_PIECES.
 * @retval HR_OK      Success.
 * @retval HR_ERR_ARG @p game is NULL or an argument is out of range; @p *game
 *                    is left unmodified.
 */
hr_status hr_init(hr_game *game, int radius, int pieces);

/** @} */

/**
 * @name Queries
 * @{
 */

/**
 * @brief Gets the board radius.
 *
 * @param game The game.
 * @return The radius `n`.
 */
int hr_radius(const hr_game *game);

/**
 * @brief Gets the number of pieces each player owns.
 *
 * @param game The game.
 * @return The number of pieces per player `m`.
 */
int hr_pieces_per_player(const hr_game *game);

/**
 * @brief Gets the total number of cells on the board.
 *
 * @param game The game.
 * @return `3n(n+1)+1` for a board of radius `n`.
 */
int hr_cell_count(const hr_game *game);

/**
 * @brief Tests whether coordinates lie on the board.
 *
 * @param game The game.
 * @param at   Coordinates to test.
 * @return `true` if @p at is on the board.
 */
bool hr_on_board(const hr_game *game, hr_coord at);

/**
 * @brief Gets the contents of a cell.
 *
 * @param game The game.
 * @param at   Coordinates of the cell.
 * @return The contents, or ::HR_OFF_BOARD if @p at is outside the board.
 */
hr_cell hr_cell_at(const hr_game *game, hr_coord at);

/**
 * @brief Gets the player who owns the piece or claim in a cell.
 *
 * @param cell Cell contents.
 * @return The owner, or ::HR_NOBODY for an empty or off-board cell.
 */
hr_player hr_cell_owner(hr_cell cell);

/**
 * @brief Gets the player to move.
 *
 * @param game The game.
 * @return The player to move, or ::HR_NOBODY once the game is over.
 */
hr_player hr_to_move(const hr_game *game);

/**
 * @brief Tests whether the game is over.
 *
 * The game ends as soon as the board has no empty cell.
 *
 * @param game The game.
 * @return `true` if the game is over.
 */
bool hr_is_over(const hr_game *game);

/**
 * @brief Gets the winner.
 *
 * The total number of cells is odd, so a finished game always has a winner.
 *
 * @param game The game.
 * @return The winner, or ::HR_NOBODY while the game is in progress.
 */
hr_player hr_winner(const hr_game *game);

/**
 * @brief Gets the number of empty cells.
 *
 * @param game The game.
 * @return The number of empty cells.
 */
int hr_empty_count(const hr_game *game);

/**
 * @brief Gets the number of pieces a player has in hand.
 *
 * @param game   The game.
 * @param player ::HR_P0 or ::HR_P1.
 * @return The number of pieces, or -1 if @p player is invalid.
 */
int hr_in_hand(const hr_game *game, hr_player player);

/**
 * @brief Gets the number of pieces a player has on the board.
 *
 * @param game   The game.
 * @param player ::HR_P0 or ::HR_P1.
 * @return The number of pieces, or -1 if @p player is invalid.
 */
int hr_pieces_on_board(const hr_game *game, hr_player player);

/**
 * @brief Gets the number of cells a player has claimed.
 *
 * @param game   The game.
 * @param player ::HR_P0 or ::HR_P1.
 * @return The number of claimed cells, or -1 if @p player is invalid.
 */
int hr_claimed(const hr_game *game, hr_player player);

/**
 * @brief Gets the number of cells a player has locked in.
 *
 * This is the number of claimed cells plus the number of pieces on the board.
 * While the game is in progress it is a lower bound on the player's final score;
 * once the game is over it is the final score.
 *
 * @param game   The game.
 * @param player ::HR_P0 or ::HR_P1.
 * @return The number of locked-in cells, or -1 if @p player is invalid.
 */
int hr_score(const hr_game *game, hr_player player);

/** @} */

/**
 * @name Moves
 * @{
 */

/**
 * @brief Checks whether the player to move may make a move.
 *
 * The game state is not changed.
 *
 * @param game The game.
 * @param move The move to check.
 * @retval HR_OK                   The move is legal.
 * @retval HR_ERR_ARG              @p game is NULL.
 * @retval HR_ERR_GAME_OVER        The game has already ended.
 * @retval HR_ERR_BAD_MOVE         Invalid move type or sweep direction.
 * @retval HR_ERR_OFF_BOARD        The coordinates are outside the board.
 * @retval HR_ERR_NOT_EMPTY        The target of a placement is not empty.
 * @retval HR_ERR_NO_PIECE_IN_HAND A placement without a piece in hand.
 * @retval HR_ERR_NOT_OWN_PIECE    A sweep from a cell that does not hold a
 *                                 piece of the player to move.
 */
hr_status hr_check(const hr_game *game, hr_move move);

/**
 * @brief Measures how far a sweep can travel.
 *
 * Counts the consecutive empty cells starting next to @p from (which is not
 * counted) in direction @p dir. A sweep made there claims one cell more than the
 * returned value. The result does not depend on whose turn it is, so this can
 * also be used to inspect the opponent's rays.
 *
 * @param game The game.
 * @param from Starting cell.
 * @param dir  Direction.
 * @return The number of empty cells, or -1 if @p from is outside the board or
 *         @p dir is invalid.
 */
int hr_ray_length(const hr_game *game, hr_coord from, hr_dir dir);

/**
 * @brief Makes a move.
 *
 * On failure the state is not changed.
 *
 * @param game The game.
 * @param move The move to make.
 * @return ::HR_OK on success, otherwise one of the errors of hr_check().
 */
hr_status hr_apply(hr_game *game, hr_move move);

/**
 * @brief Undoes the last move, which may be the one that ended the game.
 *
 * @param game The game.
 * @retval HR_OK             Success.
 * @retval HR_ERR_ARG        @p game is NULL.
 * @retval HR_ERR_NO_HISTORY There is no move to undo.
 */
hr_status hr_undo(hr_game *game);

/**
 * @brief Enumerates all legal moves of the player to move.
 *
 * The order of the moves is deterministic.
 *
 * @note Several directions of one piece may be blocked at the very first cell.
 *       Each is listed separately, although they all lead to the same result.
 *
 * @param[in]  game The game.
 * @param[out] out  Buffer for the moves; may be NULL to only count them.
 * @param[in]  cap  Maximum number of moves to write to @p out.
 * @return The total number of legal moves, which may exceed @p cap; 0 if the
 *         game is over.
 */
size_t hr_legal_moves(const hr_game *game, hr_move *out, size_t cap);

/** @} */

/**
 * @name History
 * @{
 */

/**
 * @brief Gets the number of moves made so far.
 *
 * @param game The game.
 * @return The number of moves that have been made and not undone.
 */
int hr_ply(const hr_game *game);

/**
 * @brief Gets a move from the history.
 *
 * @param[in]  game  The game.
 * @param[in]  index Index of the move, starting at 0 for the first one.
 * @param[out] out   Receives the move.
 * @retval HR_OK      Success.
 * @retval HR_ERR_ARG A pointer is NULL or @p index is out of range.
 */
hr_status hr_history_move(const hr_game *game, int index, hr_move *out);

/** @} */

/**
 * @name Helpers
 * @{
 */

/**
 * @brief Gets the neighbor of a cell.
 *
 * @param at  Starting coordinates.
 * @param dir Direction of the step.
 * @return The adjacent coordinates, which may be outside the board; @p at
 *         unchanged if @p dir is invalid.
 */
hr_coord hr_step(hr_coord at, hr_dir dir);

/**
 * @brief Describes a status code.
 *
 * @param status Status code.
 * @return A UTF-8 description in Chinese; never NULL.
 */
const char *hr_status_str(hr_status status);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* HEXRECLAIM_H */
