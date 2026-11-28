/**
 * @file hexreclaim.h
 * @brief Game-state library for hex_reclaim.
 *
 * Written in C99. The library performs no dynamic memory allocation, has no
 * mutable global state and does no I/O.
 *
 * @par Two layers
 * - ::hr_game is a whole game: board, current position and move history. Use it
 *   to play, record, replay and undo games. It can live on the stack and be
 *   copied with plain assignment.
 * - ::hr_board with ::hr_pos is the compact layer for Monte Carlo simulation
 *   and tree search. A board holds the geometry of one radius; it is built once
 *   and only read afterwards, so threads can share it. A position is a small
 *   value without history or pointers.
 *
 * ::hr_game is built on the same board and position types, so the rules are
 * implemented only once.
 *
 * @par Coordinates
 * Cells are addressed by axial coordinates `(q, r)`, with the center at
 * `(0, 0)`. A board of radius `n` consists of all cells satisfying `|q| <= n`,
 * `|r| <= n` and `|q + r| <= n`, which makes `3n(n+1)+1` cells.
 *
 * @par Cell indices
 * The simulation layer numbers the cells from 0 to `3n(n+1)`, ordered by
 * increasing `r` and then increasing `q`.
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
 * Except for the init functions, every @p game, @p board and @p pos pointer
 * passed to the library must point to an object that the corresponding init
 * function has successfully initialized, and a position must only be used with
 * a board of the radius it was initialized with. Query and simulation functions
 * do not check these conditions.
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
 * A suitable buffer size for hr_legal_moves() and hr_pos_actions().
 */
#define HR_MAX_MOVES (6 * HR_MAX_CELLS)

/** @brief Number of 64-bit words in a set of cells. */
#define HR_SET_WORDS ((HR_MAX_CELLS + 63) / 64)

/** @brief Number of action codes per cell: a placement and six sweeps. */
#define HR_ACTIONS_PER_CELL 7

/**
 * @brief Every ::hr_action is smaller than this value.
 *
 * A suitable size for arrays holding one entry per action.
 */
#define HR_ACTION_LIMIT (HR_ACTIONS_PER_CELL * HR_MAX_CELLS)

/**
 * @brief Cell index meaning "off the board".
 *
 * It is a valid argument of hr_set_has(), which returns `false` for it in every
 * cell set of a position.
 */
#define HR_NO_CELL 255

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

/** @brief A single move, addressed by coordinates. */
typedef struct {
    hr_move_type type; /**< Kind of the move. */
    hr_coord at;       /**< Target of a placement, or the piece that sweeps. */
    hr_dir dir;        /**< Sweep direction; used by ::HR_SWEEP only. */
} hr_move;

/**
 * @brief A single move in compact form, addressed by cell index.
 *
 * The value is `cell * 7 + k`, where `k` is 0 for a placement and `1 + dir` for
 * a sweep. Its meaning depends on the board radius.
 */
typedef uint16_t hr_action;

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
 * @name Board, position and game objects
 * @{
 */

/**
 * @brief Geometry of a board of one radius.
 *
 * Initialize it with hr_board_init(). Callers may read the fields but must not
 * change them.
 */
typedef struct {
    uint8_t radius; /**< Board radius `n`. */
    uint8_t cells;  /**< Number of cells. */
    uint8_t words;  /**< Number of words of a cell set that can be non-zero. */
    /** Index of the adjacent cell per direction, or ::HR_NO_CELL. */
    uint8_t neighbor[HR_MAX_CELLS][HR_DIR_COUNT];
    int8_t q[HR_MAX_CELLS]; /**< Column coordinate of each cell. */
    int8_t r[HR_MAX_CELLS]; /**< Row coordinate of each cell. */
    /** Index of the first cell of each row, starting with `r = -n`. */
    uint8_t row_start[2 * HR_MAX_RADIUS + 1];
} hr_board;

/**
 * @brief A position: everything the rules need, without history.
 *
 * Cell sets are bit sets over cell indices; bit `i % 64` of word `i / 64`
 * stands for cell `i`. The sets of empty cells, pieces and claims partition the
 * board. The struct has no padding, so equal positions are equal byte for byte
 * regardless of the moves that led to them.
 *
 * Initialize it with hr_pos_init(). Callers may read the fields, for example in
 * their own playout policy, but must change them only through the library.
 */
typedef struct {
    uint64_t empty[HR_SET_WORDS];      /**< Empty cells. */
    uint64_t piece[2][HR_SET_WORDS];   /**< Cells holding a piece, per player. */
    uint64_t claimed[2][HR_SET_WORDS]; /**< Claimed cells, per player. */
    uint8_t radius;      /**< Board radius `n`. */
    uint8_t pieces;      /**< Number of pieces per player `m`. */
    uint8_t turn;        /**< Player whose turn it is. */
    uint8_t empty_count; /**< Number of empty cells. */
    uint8_t in_hand[2];  /**< Pieces in hand, per player. */
    uint8_t score[2];    /**< Locked-in cells: claims plus pieces on board. */
} hr_pos;

/**
 * @brief Complete state of one game.
 *
 * The fields are for internal use only; access them through the library
 * functions.
 */
typedef struct {
    hr_board board; /**< Geometry of the board. */
    hr_pos pos;     /**< Current position. */
    uint16_t ply;   /**< Number of moves in the history. */
    hr_action played[HR_MAX_PLIES]; /**< Moves made, oldest first. */
    uint8_t swept[HR_MAX_PLIES];    /**< Cells each sweep covered beyond its
                                         origin; 0 for placements. */
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
 * @name Actions
 * @{
 */

/**
 * @brief Builds a placement action.
 *
 * @param cell Index of the target cell.
 * @return The action. It is not checked for legality.
 */
static inline hr_action hr_action_place(int cell)
{
    return (hr_action)(cell * HR_ACTIONS_PER_CELL);
}

/**
 * @brief Builds a sweep action.
 *
 * @param cell Index of the cell of the piece that sweeps.
 * @param dir  A valid direction.
 * @return The action. It is not checked for legality.
 */
static inline hr_action hr_action_sweep(int cell, hr_dir dir)
{
    return (hr_action)(cell * HR_ACTIONS_PER_CELL + 1 + (int)dir);
}

/**
 * @brief Gets the cell index of an action.
 *
 * @param action The action.
 * @return Target of a placement, or the cell of the piece that sweeps.
 */
static inline int hr_action_cell(hr_action action)
{
    return action / HR_ACTIONS_PER_CELL;
}

/**
 * @brief Gets the kind of an action.
 *
 * @param action The action.
 * @return ::HR_PLACE or ::HR_SWEEP.
 */
static inline hr_move_type hr_action_type(hr_action action)
{
    return action % HR_ACTIONS_PER_CELL == 0 ? HR_PLACE : HR_SWEEP;
}

/**
 * @brief Gets the direction of an action.
 *
 * @param action The action.
 * @return The sweep direction, or ::HR_E for a placement.
 */
static inline hr_dir hr_action_dir(hr_action action)
{
    int k = action % HR_ACTIONS_PER_CELL;
    return k == 0 ? HR_E : (hr_dir)(k - 1);
}

/** @} */

/**
 * @name Random numbers
 *
 * A small generator (SplitMix64) whose state belongs to the caller. Give each
 * thread its own generator and its own seed. Not suitable for cryptography.
 * @{
 */

/** @brief State of a pseudo-random generator. */
typedef struct {
    uint64_t state; /**< Current state; any value is valid. */
} hr_rng;

/**
 * @brief Seeds a generator.
 *
 * @param[out] rng  The generator.
 * @param[in]  seed Any value; equal seeds give equal sequences.
 */
static inline void hr_rng_seed(hr_rng *rng, uint64_t seed)
{
    rng->state = seed;
}

/**
 * @brief Generates a pseudo-random 64-bit number.
 *
 * @param rng The generator.
 * @return The next number.
 */
static inline uint64_t hr_rng_next(hr_rng *rng)
{
    uint64_t z = (rng->state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/**
 * @brief Generates a pseudo-random number below a bound.
 *
 * Uses a multiply-and-shift reduction. Its bias is below `bound / 2^32`, which
 * is negligible for the small bounds that occur in a game.
 *
 * @param rng   The generator.
 * @param bound Exclusive upper bound, at least 1.
 * @return A number from 0 to `bound - 1`.
 */
static inline uint32_t hr_rng_below(hr_rng *rng, uint32_t bound)
{
    return (uint32_t)(((hr_rng_next(rng) >> 32) * bound) >> 32);
}

/** @} */

/**
 * @name Board and position
 *
 * The compact layer for simulation and search.
 * @{
 */

/**
 * @brief Builds the geometry of a board.
 *
 * @param[out] board  Object to initialize.
 * @param[in]  radius Board radius, from 1 to ::HR_MAX_RADIUS.
 * @retval HR_OK      Success.
 * @retval HR_ERR_ARG @p board is NULL or @p radius is out of range; @p *board
 *                    is left unmodified.
 */
hr_status hr_board_init(hr_board *board, int radius);

/**
 * @brief Gets the index of a cell.
 *
 * @param board The board.
 * @param at    Coordinates; any `int` values are allowed.
 * @return The cell index, or -1 if @p at is outside the board.
 */
int hr_board_index(const hr_board *board, hr_coord at);

/**
 * @brief Converts a move to an action.
 *
 * Only the form of the move is checked, not whether it is legal.
 *
 * @param[in]  board The board.
 * @param[in]  move  The move.
 * @param[out] out   Receives the action.
 * @retval HR_OK            Success.
 * @retval HR_ERR_ARG       A pointer is NULL.
 * @retval HR_ERR_BAD_MOVE  Invalid move type or sweep direction.
 * @retval HR_ERR_OFF_BOARD The coordinates are outside the board.
 */
hr_status hr_action_encode(const hr_board *board, hr_move move, hr_action *out);

/**
 * @brief Converts an action to a move.
 *
 * @param board  The board.
 * @param action An action whose cell is on the board.
 * @return The move.
 */
hr_move hr_action_decode(const hr_board *board, hr_action action);

/**
 * @brief Starts a position: empty board, all pieces in hand, P0 to move.
 *
 * @param[out] pos    Object to initialize.
 * @param[in]  board  The board.
 * @param[in]  pieces Pieces per player, from 1 to ::HR_MAX_PIECES.
 * @retval HR_OK      Success.
 * @retval HR_ERR_ARG A pointer is NULL or @p pieces is out of range; @p *pos is
 *                    left unmodified.
 */
hr_status hr_pos_init(hr_pos *pos, const hr_board *board, int pieces);

/**
 * @brief Tests whether a cell set contains a cell.
 *
 * @param set  A cell set of ::HR_SET_WORDS words.
 * @param cell A cell index, or ::HR_NO_CELL.
 * @return `true` if the set contains the cell.
 */
static inline bool hr_set_has(const uint64_t *set, int cell)
{
    return (set[cell >> 6] >> (cell & 63)) & 1u;
}

/**
 * @brief Tests whether the game is over.
 *
 * @param pos The position.
 * @return `true` if there is no empty cell.
 */
static inline bool hr_pos_is_over(const hr_pos *pos)
{
    return pos->empty_count == 0;
}

/**
 * @brief Gets the player to move.
 *
 * @param pos The position.
 * @return The player to move, or ::HR_NOBODY once the game is over.
 */
static inline hr_player hr_pos_to_move(const hr_pos *pos)
{
    return pos->empty_count == 0 ? HR_NOBODY : (hr_player)pos->turn;
}

/**
 * @brief Gets the number of cells a player has locked in.
 *
 * @param pos    The position.
 * @param player ::HR_P0 or ::HR_P1.
 * @return Claimed cells plus pieces on the board, or -1 if @p player is
 *         invalid.
 */
static inline int hr_pos_score(const hr_pos *pos, hr_player player)
{
    return player == HR_P0 || player == HR_P1 ? pos->score[player] : -1;
}

/**
 * @brief Gets the player who is already certain to win.
 *
 * A player with more than half of the cells locked in cannot lose any of them
 * again. Once the game is over this is the winner.
 *
 * @param pos The position.
 * @return That player, or ::HR_NOBODY if neither has more than half.
 */
static inline hr_player hr_pos_decided(const hr_pos *pos)
{
    int cells = 3 * pos->radius * (pos->radius + 1) + 1;
    if (2 * pos->score[HR_P0] > cells)
        return HR_P0;
    if (2 * pos->score[HR_P1] > cells)
        return HR_P1;
    return HR_NOBODY;
}

/**
 * @brief Gets the contents of a cell by index.
 *
 * @param pos  The position.
 * @param cell Cell index.
 * @return The contents, or ::HR_OFF_BOARD if there is no such cell.
 */
hr_cell hr_pos_cell(const hr_pos *pos, int cell);

/**
 * @brief Measures how far a sweep can travel, by cell index.
 *
 * @param board The board.
 * @param pos   The position.
 * @param cell  Starting cell, which must be on the board.
 * @param dir   Direction.
 * @return The number of consecutive empty cells after @p cell, or -1 if @p cell
 *         or @p dir is invalid.
 */
int hr_pos_ray_length(const hr_board *board, const hr_pos *pos, int cell,
                      hr_dir dir);

/**
 * @brief Makes a move in a position. There is no undo; keep a copy instead.
 *
 * On failure the position is not changed.
 *
 * @param board  The board.
 * @param pos    The position.
 * @param action The action to make.
 * @retval HR_OK                   Success.
 * @retval HR_ERR_ARG              A pointer is NULL, or board and position have
 *                                 different radii.
 * @retval HR_ERR_GAME_OVER        The game has already ended.
 * @retval HR_ERR_OFF_BOARD        The cell of the action is not on the board.
 * @retval HR_ERR_NOT_EMPTY        The target of a placement is not empty.
 * @retval HR_ERR_NO_PIECE_IN_HAND A placement without a piece in hand.
 * @retval HR_ERR_NOT_OWN_PIECE    A sweep from a cell that does not hold a
 *                                 piece of the player to move.
 */
hr_status hr_pos_apply(const hr_board *board, hr_pos *pos, hr_action action);

/**
 * @brief Computes a hash of a position, for transposition tables.
 *
 * Positions reached by different move orders are equal, so their hashes are
 * equal too. The hash covers the pieces, the claims, the player to move, the
 * radius and the number of pieces per player, which determine everything else
 * in a position. It is the same on every platform, and it costs a few
 * multiplications per word of a cell set in use.
 *
 * As with any 64-bit hash, different positions can have the same one. This is
 * very unlikely: among ten million positions, the chance that any two collide
 * is a few in a million. Compare the positions where that is not good enough.
 *
 * @param pos A position set up by the library.
 * @return The hash.
 */
uint64_t hr_pos_hash(const hr_pos *pos);

/** @} */

/**
 * @name Simulation
 * @{
 */

/** @brief Tells hr_pos_playout() when to stop. */
typedef enum {
    HR_PLAY_TO_END = 0, /**< Play until no empty cell remains, which gives the
                             exact final scores. */
    HR_PLAY_TO_DECIDED  /**< Stop as soon as the winner is certain, see
                             hr_pos_decided(). Faster; the scores at that point
                             are not final. */
} hr_playout_stop;

/**
 * @brief Enumerates the distinct actions of the player to move.
 *
 * Every action listed leads to a different position. Sweeps of one piece in
 * directions that are blocked at the very first cell all claim just the origin,
 * so only the first such direction is listed. Use this to expand nodes of a
 * search tree and to write custom playout policies. The order is by increasing
 * cell index and then direction.
 *
 * @param[in]  board The board.
 * @param[in]  pos   The position.
 * @param[out] out   Buffer for the actions; may be NULL to only count them.
 * @param[in]  cap   Maximum number of actions to write to @p out.
 * @return The total number of distinct actions, which may exceed @p cap; 0 if
 *         the game is over.
 */
size_t hr_pos_actions(const hr_board *board, const hr_pos *pos, hr_action *out,
                      size_t cap);

/**
 * @brief Picks a random move, much faster than building a list.
 *
 * Every move of hr_legal_moves() is equally likely. The result is always one of
 * the actions of hr_pos_actions(): sweeps of a piece that are blocked at the
 * very first cell are all reported as the one listed there, which is therefore
 * as likely as all of them together.
 *
 * @param[in]     board The board.
 * @param[in]     pos   The position.
 * @param[in,out] rng   Random generator.
 * @param[out]    out   Receives the action.
 * @return `false` if the game is over, in which case neither @p rng nor @p out
 *         is changed.
 */
bool hr_pos_random_action(const hr_board *board, const hr_pos *pos,
                          hr_rng *rng, hr_action *out);

/**
 * @brief Plays uniformly random moves from a position.
 *
 * Each move is chosen as by hr_pos_random_action(). The position is advanced in
 * place, so pass a copy to keep the starting position.
 *
 * @param[in]     board The board.
 * @param[in,out] pos   Starting position; receives the position reached.
 * @param[in,out] rng   Random generator.
 * @param[in]     stop  When to stop.
 * @return The winner.
 */
hr_player hr_pos_playout(const hr_board *board, hr_pos *pos, hr_rng *rng,
                         hr_playout_stop stop);

/** @} */

/**
 * @name Game: initialization
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
 * @name Game: queries
 * @{
 */

/**
 * @brief Gets the board of a game, for use with the simulation layer.
 *
 * @param game The game.
 * @return The board, valid as long as @p game is.
 */
const hr_board *hr_game_board(const hr_game *game);

/**
 * @brief Gets the current position of a game.
 *
 * Copy it to start a simulation or a search: `hr_pos p = *hr_game_position(&g);`
 *
 * @param game The game.
 * @return The position, which changes when the game does.
 */
const hr_pos *hr_game_position(const hr_game *game);

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
 * @name Game: moves
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
 *       hr_pos_actions() lists only one of them.
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
 * @name Game: history
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
 * @return A description in English; never NULL.
 */
const char *hr_status_str(hr_status status);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* HEXRECLAIM_H */
