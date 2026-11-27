/**
 * @file hexreclaim.c
 * @brief Implementation of the hex_reclaim game-state library.
 *
 * A position stores the empty cells, pieces and claims as bit sets over densely
 * numbered cells, so a board of radius 4 or less fits in the first 64-bit word
 * of each set. The board holds a table of neighbor indices. Off-board neighbors
 * are ::HR_NO_CELL, a bit that is never set, so walking along a ray needs no
 * bounds checks.
 */
#include "hexreclaim.h"

#include <string.h>

/* Fails to compile if HR_NO_CELL could collide with a cell or leave a set. */
typedef char hr_no_cell_is_valid[(HR_MAX_CELLS <= HR_NO_CELL &&
                                  HR_NO_CELL < 64 * HR_SET_WORDS) ? 1 : -1];

/** @brief Change in `q` for one step, indexed by ::hr_dir. */
static const int DQ[HR_DIR_COUNT] = {1, 1, 0, -1, -1, 0};
/** @brief Change in `r` for one step, indexed by ::hr_dir. */
static const int DR[HR_DIR_COUNT] = {0, -1, -1, 0, 1, 1};

/* ================================================================ Bit sets */

/**
 * @brief Counts the set bits of a word.
 *
 * Uses the compiler builtin only where it is known to become an instruction.
 *
 * @param x The word.
 * @return The number of set bits.
 */
static inline unsigned popcount64(uint64_t x)
{
#if defined(__GNUC__) && !defined(HR_NO_BUILTINS) && \
    (defined(__POPCNT__) || defined(__aarch64__))
    return (unsigned)__builtin_popcountll(x);
#else
    x -= (x >> 1) & 0x5555555555555555ULL;
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (unsigned)((x * 0x0101010101010101ULL) >> 56);
#endif
}

/**
 * @brief Gets the position of the lowest set bit.
 *
 * @param x A non-zero word.
 * @return The bit position, 0 to 63.
 */
static inline int lowest_bit(uint64_t x)
{
#if defined(__GNUC__) && !defined(HR_NO_BUILTINS)
    return __builtin_ctzll(x);
#else
    return (int)popcount64((x & (0 - x)) - 1);
#endif
}

/**
 * @brief Gets the position of a set bit by its rank.
 *
 * @param x The word.
 * @param k Rank counted from the lowest set bit, starting at 0; must be less
 *          than the number of set bits.
 * @return The bit position, 0 to 63.
 */
static inline int select_bit(uint64_t x, unsigned k)
{
    const uint64_t ones = 0x0101010101010101ULL;
    const uint64_t highs = 0x8080808080808080ULL;

    /* Byte i of sum: number of set bits in bytes 0..i. */
    uint64_t sum = x - ((x >> 1) & 0x5555555555555555ULL);
    sum = (sum & 0x3333333333333333ULL) +
          ((sum >> 2) & 0x3333333333333333ULL);
    sum = ((sum + (sum >> 4)) & 0x0F0F0F0F0F0F0F0FULL) * ones;

    /* The wanted byte follows all bytes whose running count is <= k. */
    uint64_t not_past = (((k * ones) | highs) - sum) & highs;
    int shift = (int)(((not_past >> 7) * ones) >> 56) * 8;
    if (shift != 0)
        k -= (unsigned)(sum >> (shift - 8)) & 0xFFu;

    unsigned byte = (unsigned)(x >> shift) & 0xFFu;
    while (k-- > 0)
        byte &= byte - 1;
    return shift + lowest_bit(byte);
}

/**
 * @brief Adds a cell to a set.
 *
 * @param set  The set.
 * @param cell Cell index.
 */
static inline void set_add(uint64_t *set, int cell)
{
    set[cell >> 6] |= (uint64_t)1 << (cell & 63);
}

/**
 * @brief Removes a cell from a set.
 *
 * @param set  The set.
 * @param cell Cell index.
 */
static inline void set_remove(uint64_t *set, int cell)
{
    set[cell >> 6] &= ~((uint64_t)1 << (cell & 63));
}

/* ================================================================== Helpers */

/**
 * @brief Tests whether a value is one of the six directions.
 *
 * @param dir Value to test.
 * @return `true` if @p dir is valid.
 */
static bool valid_dir(hr_dir dir)
{
    return (unsigned)dir < (unsigned)HR_DIR_COUNT;
}

/**
 * @brief Tests whether a value names one of the two players.
 *
 * @param player Value to test.
 * @return `true` for ::HR_P0 and ::HR_P1.
 */
static bool valid_player(hr_player player)
{
    return player == HR_P0 || player == HR_P1;
}

/**
 * @brief Tests whether coordinates lie on a board of a given radius.
 *
 * @param n Board radius.
 * @param c Coordinates; any `int` values are allowed.
 * @return `true` if @p c is on the board.
 */
static bool in_radius(int n, hr_coord c)
{
    /* Bound q and r first so that computing q + r cannot overflow. */
    if (c.q < -n || c.q > n || c.r < -n || c.r > n)
        return false;
    return c.q + c.r >= -n && c.q + c.r <= n;
}

/**
 * @brief Gets the smallest column coordinate of a row.
 *
 * @param n Board radius.
 * @param r Row coordinate.
 * @return The smallest `q` on the board in row @p r.
 */
static int row_first_q(int n, int r)
{
    return r < 0 ? -n - r : -n;
}

/* ============================================================ Position core */

/**
 * @brief Checks a well-formed action against a position in progress.
 *
 * @param pos  The position.
 * @param cell A cell on the board.
 * @param type Kind of the action.
 * @return ::HR_OK or the reason the action is illegal.
 */
static hr_status check_cell(const hr_pos *pos, int cell, hr_move_type type)
{
    if (type == HR_PLACE) {
        if (!hr_set_has(pos->empty, cell))
            return HR_ERR_NOT_EMPTY;
        if (pos->in_hand[pos->turn] == 0)
            return HR_ERR_NO_PIECE_IN_HAND;
    } else if (!hr_set_has(pos->piece[pos->turn], cell)) {
        return HR_ERR_NOT_OWN_PIECE;
    }
    return HR_OK;
}

/**
 * @brief Makes a legal action.
 *
 * @param board  The board.
 * @param pos    The position.
 * @param action A legal action.
 * @return The number of cells a sweep covered beyond its origin; 0 for a
 *         placement.
 */
static inline int play(const hr_board *board, hr_pos *pos, hr_action action)
{
    int player = pos->turn;
    int cell = hr_action_cell(action);
    int swept = 0;

    if (hr_action_type(action) == HR_PLACE) {
        set_remove(pos->empty, cell);
        set_add(pos->piece[player], cell);
        pos->empty_count--;
        pos->in_hand[player]--;
        pos->score[player]++;
    } else {
        int dir = (int)hr_action_dir(action);

        set_remove(pos->piece[player], cell);
        set_add(pos->claimed[player], cell);
        for (int i = board->neighbor[cell][dir]; hr_set_has(pos->empty, i);
             i = board->neighbor[i][dir]) {
            set_remove(pos->empty, i);
            set_add(pos->claimed[player], i);
            swept++;
        }
        pos->empty_count = (uint8_t)(pos->empty_count - swept);
        pos->score[player] = (uint8_t)(pos->score[player] + swept);
        pos->in_hand[player]++;
    }
    pos->turn ^= 1;
    return swept;
}

/**
 * @brief Gets the directions in which a piece has distinct sweeps.
 *
 * These are all open directions plus the first blocked one, if any.
 *
 * @param board The board.
 * @param pos   The position.
 * @param cell  Cell of the piece.
 * @return A mask with bit `d` set for each such direction `d`.
 */
static inline unsigned sweep_choices(const hr_board *board, const hr_pos *pos,
                                     int cell)
{
    unsigned open = 0;
    for (int d = 0; d < HR_DIR_COUNT; d++)
        open |= (unsigned)hr_set_has(pos->empty, board->neighbor[cell][d]) << d;
    /* x | (x + 1) additionally sets the lowest clear bit of x. */
    return (open | (open + 1)) & ((1u << HR_DIR_COUNT) - 1);
}

/**
 * @brief Gets a cell of a set by its rank.
 *
 * @param set   The set.
 * @param words Number of words that can be non-zero.
 * @param k     Rank counted from the lowest cell, starting at 0; must be less
 *              than the number of cells in the set.
 * @return The cell index.
 */
static inline int select_cell(const uint64_t *set, int words, unsigned k)
{
    int w = 0;
    while (w < words - 1) {
        unsigned n = popcount64(set[w]);
        if (k < n)
            break;
        k -= n;
        w++;
    }
    return 64 * w + select_bit(set[w], k);
}

/**
 * @brief Picks a legal move uniformly at random.
 *
 * The moves are those of hr_legal_moves(): one placement per empty cell if a
 * piece is in hand, and six sweeps per piece on the board. Their number follows
 * from the counters, so only the chosen one has to be located.
 *
 * @param board The board.
 * @param pos   A position in progress.
 * @param rng   Random generator.
 * @return The move as an action; a blocked sweep keeps the direction drawn.
 */
static inline hr_action sample(const hr_board *board, const hr_pos *pos,
                               hr_rng *rng)
{
    int player = pos->turn;
    unsigned places = pos->in_hand[player] > 0 ? pos->empty_count : 0;
    unsigned on_board = (unsigned)(pos->pieces - pos->in_hand[player]);

    /* A position in progress always has a move, so the bound is >= 1. */
    unsigned k = hr_rng_below(rng, places + HR_DIR_COUNT * on_board);

    if (k < places)
        return hr_action_place(select_cell(pos->empty, board->words, k));

    k -= places;
    int cell = select_cell(pos->piece[player], board->words,
                           k / HR_DIR_COUNT);
    return hr_action_sweep(cell, (hr_dir)(k % HR_DIR_COUNT));
}

/* ======================================================= Board and position */

int hr_board_index(const hr_board *board, hr_coord at)
{
    int n = board->radius;
    if (!in_radius(n, at))
        return -1;
    return board->row_start[at.r + n] + at.q - row_first_q(n, at.r);
}

hr_status hr_board_init(hr_board *board, int radius)
{
    if (board == NULL || radius < 1 || radius > HR_MAX_RADIUS)
        return HR_ERR_ARG;

    memset(board, 0, sizeof *board);
    memset(board->neighbor, HR_NO_CELL, sizeof board->neighbor);
    board->radius = (uint8_t)radius;

    int cells = 0;
    for (int r = -radius; r <= radius; r++) {
        int last = r < 0 ? radius : radius - r;
        board->row_start[r + radius] = (uint8_t)cells;
        for (int q = row_first_q(radius, r); q <= last; q++) {
            board->q[cells] = (int8_t)q;
            board->r[cells] = (int8_t)r;
            cells++;
        }
    }
    board->cells = (uint8_t)cells;
    board->words = (uint8_t)((cells + 63) / 64);

    for (int c = 0; c < cells; c++) {
        for (int d = 0; d < HR_DIR_COUNT; d++) {
            hr_coord next = {board->q[c] + DQ[d], board->r[c] + DR[d]};
            int i = hr_board_index(board, next);
            if (i >= 0)
                board->neighbor[c][d] = (uint8_t)i;
        }
    }
    return HR_OK;
}

hr_status hr_action_encode(const hr_board *board, hr_move move, hr_action *out)
{
    if (board == NULL || out == NULL)
        return HR_ERR_ARG;
    if (move.type != HR_PLACE && move.type != HR_SWEEP)
        return HR_ERR_BAD_MOVE;
    if (move.type == HR_SWEEP && !valid_dir(move.dir))
        return HR_ERR_BAD_MOVE;

    int cell = hr_board_index(board, move.at);
    if (cell < 0)
        return HR_ERR_OFF_BOARD;
    *out = move.type == HR_PLACE ? hr_action_place(cell)
                                 : hr_action_sweep(cell, move.dir);
    return HR_OK;
}

hr_move hr_action_decode(const hr_board *board, hr_action action)
{
    int cell = hr_action_cell(action);
    int q = board->q[cell], r = board->r[cell];

    return hr_action_type(action) == HR_PLACE
               ? hr_place(q, r)
               : hr_sweep(q, r, hr_action_dir(action));
}

hr_status hr_pos_init(hr_pos *pos, const hr_board *board, int pieces)
{
    if (pos == NULL || board == NULL || pieces < 1 || pieces > HR_MAX_PIECES)
        return HR_ERR_ARG;

    memset(pos, 0, sizeof *pos);
    pos->radius = board->radius;
    pos->pieces = (uint8_t)pieces;
    pos->turn = HR_P0;
    pos->empty_count = board->cells;
    pos->in_hand[HR_P0] = (uint8_t)pieces;
    pos->in_hand[HR_P1] = (uint8_t)pieces;
    for (int c = 0; c < board->cells; c++)
        set_add(pos->empty, c);
    return HR_OK;
}

hr_cell hr_pos_cell(const hr_pos *pos, int cell)
{
    if (cell < 0 || cell >= HR_MAX_CELLS)
        return HR_OFF_BOARD;
    if (hr_set_has(pos->empty, cell))
        return HR_EMPTY;
    for (int p = HR_P0; p <= HR_P1; p++) {
        if (hr_set_has(pos->piece[p], cell))
            return (hr_cell)(HR_PIECE_P0 + p);
        if (hr_set_has(pos->claimed[p], cell))
            return (hr_cell)(HR_CLAIMED_P0 + p);
    }
    return HR_OFF_BOARD;
}

int hr_pos_ray_length(const hr_board *board, const hr_pos *pos, int cell,
                      hr_dir dir)
{
    if (cell < 0 || cell >= board->cells || !valid_dir(dir))
        return -1;

    int n = 0;
    for (int i = board->neighbor[cell][dir]; hr_set_has(pos->empty, i);
         i = board->neighbor[i][dir])
        n++;
    return n;
}

hr_status hr_pos_apply(const hr_board *board, hr_pos *pos, hr_action action)
{
    if (board == NULL || pos == NULL || board->radius != pos->radius)
        return HR_ERR_ARG;
    if (hr_pos_is_over(pos))
        return HR_ERR_GAME_OVER;

    int cell = hr_action_cell(action);
    if (cell >= board->cells)
        return HR_ERR_OFF_BOARD;
    hr_status status = check_cell(pos, cell, hr_action_type(action));
    if (status == HR_OK)
        play(board, pos, action);
    return status;
}

/* =============================================================== Simulation */

size_t hr_pos_actions(const hr_board *board, const hr_pos *pos, hr_action *out,
                      size_t cap)
{
    if (hr_pos_is_over(pos))
        return 0;

    size_t total = 0;
    int player = pos->turn;
    bool can_place = pos->in_hand[player] > 0;

    for (int w = 0; w < board->words; w++) {
        uint64_t bits = pos->piece[player][w];
        if (can_place)
            bits |= pos->empty[w];
        for (; bits != 0; bits &= bits - 1) {
            int c = 64 * w + lowest_bit(bits);
            if (hr_set_has(pos->empty, c)) {
                if (out != NULL && total < cap)
                    out[total] = hr_action_place(c);
                total++;
                continue;
            }
            unsigned m = sweep_choices(board, pos, c);
            for (; m != 0; m &= m - 1) {
                if (out != NULL && total < cap)
                    out[total] = hr_action_sweep(c, (hr_dir)lowest_bit(m));
                total++;
            }
        }
    }
    return total;
}

bool hr_pos_random_action(const hr_board *board, const hr_pos *pos,
                          hr_rng *rng, hr_action *out)
{
    if (hr_pos_is_over(pos))
        return false;

    hr_action action = sample(board, pos, rng);
    if (hr_action_type(action) == HR_SWEEP) {
        int cell = hr_action_cell(action);
        const uint8_t *next = board->neighbor[cell];
        if (!hr_set_has(pos->empty, next[hr_action_dir(action)])) {
            /* Report the blocked direction that hr_pos_actions() lists. */
            int d = 0;
            while (hr_set_has(pos->empty, next[d]))
                d++;
            action = hr_action_sweep(cell, (hr_dir)d);
        }
    }
    *out = action;
    return true;
}

hr_player hr_pos_playout(const hr_board *board, hr_pos *pos, hr_rng *rng,
                         hr_playout_stop stop)
{
    /* The cell count is odd: more than half means score > half. */
    int half = board->cells / 2;

    while (!hr_pos_is_over(pos)) {
        if (stop == HR_PLAY_TO_DECIDED &&
            (pos->score[HR_P0] > half || pos->score[HR_P1] > half))
            break;
        play(board, pos, sample(board, pos, rng));
    }
    return hr_pos_decided(pos);
}

/* ===================================================================== Game */

hr_status hr_init(hr_game *game, int radius, int pieces)
{
    if (game == NULL || radius < 1 || radius > HR_MAX_RADIUS ||
        pieces < 1 || pieces > HR_MAX_PIECES)
        return HR_ERR_ARG;

    memset(game, 0, sizeof *game);
    /* Cannot fail: the arguments were checked above. */
    hr_board_init(&game->board, radius);
    hr_pos_init(&game->pos, &game->board, pieces);
    return HR_OK;
}

const hr_board *hr_game_board(const hr_game *game)
{
    return &game->board;
}

const hr_pos *hr_game_position(const hr_game *game)
{
    return &game->pos;
}

int hr_radius(const hr_game *game)
{
    return game->board.radius;
}

int hr_pieces_per_player(const hr_game *game)
{
    return game->pos.pieces;
}

int hr_cell_count(const hr_game *game)
{
    return game->board.cells;
}

bool hr_on_board(const hr_game *game, hr_coord at)
{
    return in_radius(game->board.radius, at);
}

hr_cell hr_cell_at(const hr_game *game, hr_coord at)
{
    int cell = hr_board_index(&game->board, at);
    return cell < 0 ? HR_OFF_BOARD : hr_pos_cell(&game->pos, cell);
}

hr_player hr_cell_owner(hr_cell cell)
{
    switch (cell) {
    case HR_PIECE_P0:
    case HR_CLAIMED_P0:
        return HR_P0;
    case HR_PIECE_P1:
    case HR_CLAIMED_P1:
        return HR_P1;
    default:
        return HR_NOBODY;
    }
}

bool hr_is_over(const hr_game *game)
{
    return hr_pos_is_over(&game->pos);
}

hr_player hr_to_move(const hr_game *game)
{
    return hr_pos_to_move(&game->pos);
}

hr_player hr_winner(const hr_game *game)
{
    return hr_is_over(game) ? hr_pos_decided(&game->pos) : HR_NOBODY;
}

int hr_empty_count(const hr_game *game)
{
    return game->pos.empty_count;
}

int hr_in_hand(const hr_game *game, hr_player player)
{
    return valid_player(player) ? game->pos.in_hand[player] : -1;
}

int hr_pieces_on_board(const hr_game *game, hr_player player)
{
    return valid_player(player)
               ? game->pos.pieces - game->pos.in_hand[player]
               : -1;
}

int hr_claimed(const hr_game *game, hr_player player)
{
    return valid_player(player)
               ? hr_score(game, player) - hr_pieces_on_board(game, player)
               : -1;
}

int hr_score(const hr_game *game, hr_player player)
{
    return hr_pos_score(&game->pos, player);
}

/**
 * @brief Checks a move of a game and converts it to an action.
 *
 * @param game The game, may be NULL.
 * @param move The move to check.
 * @param[out] out Receives the action if the move is legal.
 * @return The result documented for hr_check().
 */
static hr_status legal_action(const hr_game *game, hr_move move,
                              hr_action *out)
{
    if (game == NULL)
        return HR_ERR_ARG;
    if (hr_is_over(game))
        return HR_ERR_GAME_OVER;

    hr_status status = hr_action_encode(&game->board, move, out);
    if (status != HR_OK)
        return status;
    return check_cell(&game->pos, hr_action_cell(*out), move.type);
}

hr_status hr_check(const hr_game *game, hr_move move)
{
    hr_action action;
    return legal_action(game, move, &action);
}

int hr_ray_length(const hr_game *game, hr_coord from, hr_dir dir)
{
    int cell = hr_board_index(&game->board, from);
    return hr_pos_ray_length(&game->board, &game->pos, cell, dir);
}

hr_status hr_apply(hr_game *game, hr_move move)
{
    hr_action action;
    hr_status status = legal_action(game, move, &action);
    if (status != HR_OK)
        return status;

    /*
     * While the game is in progress, ply <= 2 * cells - 2 (see HR_MAX_PLIES),
     * so this stays in bounds.
     */
    game->played[game->ply] = action;
    game->swept[game->ply] =
        (uint8_t)play(&game->board, &game->pos, action);
    game->ply++;
    return HR_OK;
}

hr_status hr_undo(hr_game *game)
{
    if (game == NULL)
        return HR_ERR_ARG;
    if (game->ply == 0)
        return HR_ERR_NO_HISTORY;

    game->ply--;
    hr_pos *pos = &game->pos;
    pos->turn ^= 1;

    int player = pos->turn;
    hr_action action = game->played[game->ply];
    int cell = hr_action_cell(action);

    if (hr_action_type(action) == HR_PLACE) {
        set_remove(pos->piece[player], cell);
        set_add(pos->empty, cell);
        pos->empty_count++;
        pos->in_hand[player]++;
        pos->score[player]--;
    } else {
        int dir = (int)hr_action_dir(action);
        int swept = game->swept[game->ply];

        /*
         * The swept cells were necessarily empty before, and the origin
         * necessarily held the mover's own piece.
         */
        set_remove(pos->claimed[player], cell);
        set_add(pos->piece[player], cell);
        for (int k = 0, i = cell; k < swept; k++) {
            i = game->board.neighbor[i][dir];
            set_remove(pos->claimed[player], i);
            set_add(pos->empty, i);
        }
        pos->empty_count = (uint8_t)(pos->empty_count + swept);
        pos->score[player] = (uint8_t)(pos->score[player] - swept);
        pos->in_hand[player]--;
    }

    game->played[game->ply] = 0;
    game->swept[game->ply] = 0;
    return HR_OK;
}

/**
 * @brief Counts a move and stores it if the buffer still has room.
 *
 * @param out Move buffer, may be NULL.
 * @param cap Capacity of @p out.
 * @param count Running number of moves, incremented by one.
 * @param move The move to add.
 */
static void push_move(hr_move *out, size_t cap, size_t *count, hr_move move)
{
    if (out != NULL && *count < cap)
        out[*count] = move;
    (*count)++;
}

size_t hr_legal_moves(const hr_game *game, hr_move *out, size_t cap)
{
    if (hr_is_over(game))
        return 0;

    const hr_board *board = &game->board;
    const hr_pos *pos = &game->pos;
    size_t count = 0;
    int player = pos->turn;
    bool can_place = pos->in_hand[player] > 0;

    for (int w = 0; w < board->words; w++) {
        uint64_t bits = pos->piece[player][w];
        if (can_place)
            bits |= pos->empty[w];
        for (; bits != 0; bits &= bits - 1) {
            int c = 64 * w + lowest_bit(bits);
            int q = board->q[c], r = board->r[c];
            if (hr_set_has(pos->empty, c)) {
                push_move(out, cap, &count, hr_place(q, r));
                continue;
            }
            for (int d = 0; d < HR_DIR_COUNT; d++)
                push_move(out, cap, &count, hr_sweep(q, r, (hr_dir)d));
        }
    }
    return count;
}

int hr_ply(const hr_game *game)
{
    return game->ply;
}

hr_status hr_history_move(const hr_game *game, int index, hr_move *out)
{
    if (game == NULL || out == NULL || index < 0 || index >= game->ply)
        return HR_ERR_ARG;

    *out = hr_action_decode(&game->board, game->played[index]);
    return HR_OK;
}

hr_coord hr_step(hr_coord at, hr_dir dir)
{
    if (valid_dir(dir)) {
        at.q += DQ[dir];
        at.r += DR[dir];
    }
    return at;
}

const char *hr_status_str(hr_status status)
{
    switch (status) {
    case HR_OK:                   return "success";
    case HR_ERR_ARG:              return "invalid argument";
    case HR_ERR_GAME_OVER:        return "the game is already over";
    case HR_ERR_BAD_MOVE:         return "invalid move type or direction";
    case HR_ERR_OFF_BOARD:        return "coordinates are off the board";
    case HR_ERR_NOT_EMPTY:        return "target cell is not empty";
    case HR_ERR_NO_PIECE_IN_HAND: return "no piece in hand";
    case HR_ERR_NOT_OWN_PIECE:    return "no piece of the mover on that cell";
    case HR_ERR_NO_HISTORY:       return "no move to undo";
    }
    return "unknown error";
}
