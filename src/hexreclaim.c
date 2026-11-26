/**
 * @file hexreclaim.c
 * @brief Implementation of the hex_reclaim game-state library.
 *
 * The board is stored as a two-dimensional array laid out by axial coordinates
 * (row = `r`, column = `q`). Entries outside the board are always
 * ::HR_OFF_BOARD. Stepping one cell in a direction adds a constant to the index,
 * so sweeping needs no bounds checks.
 */
#include "hexreclaim.h"

#include <string.h>

/** @brief Row and column of coordinate (0, 0) in the internal array. */
#define CENTER (HR_MAX_RADIUS + 1)

/** @brief Change in `q` for one step, indexed by ::hr_dir. */
static const int DQ[HR_DIR_COUNT] = {1, 1, 0, -1, -1, 0};
/** @brief Change in `r` for one step, indexed by ::hr_dir. */
static const int DR[HR_DIR_COUNT] = {0, -1, -1, 0, 1, 1};

/**
 * @brief Converts axial coordinates to an index into the internal board array.
 *
 * @param q Column coordinate.
 * @param r Row coordinate.
 * @return The array index.
 */
static int cell_index(int q, int r)
{
    return (r + CENTER) * HR_GRID_SPAN + (q + CENTER);
}

/**
 * @brief Converts an index into the internal board array to axial coordinates.
 *
 * @param index The array index.
 * @return The coordinates.
 */
static hr_coord index_coord(int index)
{
    hr_coord c;
    c.q = index % HR_GRID_SPAN - CENTER;
    c.r = index / HR_GRID_SPAN - CENTER;
    return c;
}

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
 * @brief Gets the index difference of one step in a direction.
 *
 * @param dir A valid direction.
 * @return The offset to add to a board array index.
 */
static int dir_offset(hr_dir dir)
{
    return DR[dir] * HR_GRID_SPAN + DQ[dir];
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
 * @brief Gets the cell value for a piece of a player.
 *
 * @param player 0 or 1.
 * @return ::HR_PIECE_P0 or ::HR_PIECE_P1.
 */
static uint8_t piece_of(int player)
{
    return (uint8_t)(HR_PIECE_P0 + player);
}

/**
 * @brief Gets the cell value for a cell claimed by a player.
 *
 * @param player 0 or 1.
 * @return ::HR_CLAIMED_P0 or ::HR_CLAIMED_P1.
 */
static uint8_t claim_of(int player)
{
    return (uint8_t)(HR_CLAIMED_P0 + player);
}

/**
 * @brief Counts the consecutive empty cells after a cell in one direction.
 *
 * The sentinel cells guarantee that the loop terminates.
 *
 * @param game   The game.
 * @param from   Array index of the starting cell, which is not counted.
 * @param offset Index difference of one step, as returned by dir_offset().
 * @return The number of empty cells.
 */
static int count_empty(const hr_game *game, int from, int offset)
{
    int n = 0;
    for (int i = from + offset; game->cells[i] == HR_EMPTY; i += offset)
        n++;
    return n;
}

hr_status hr_init(hr_game *game, int radius, int pieces)
{
    if (game == NULL || radius < 1 || radius > HR_MAX_RADIUS ||
        pieces < 1 || pieces > HR_MAX_PIECES)
        return HR_ERR_ARG;

    memset(game, 0, sizeof *game); /* HR_OFF_BOARD == 0 */
    game->radius = (uint8_t)radius;
    game->pieces = (uint8_t)pieces;
    game->turn = HR_P0;
    game->in_hand[HR_P0] = (uint8_t)pieces;
    game->in_hand[HR_P1] = (uint8_t)pieces;

    for (int r = -radius; r <= radius; r++) {
        for (int q = -radius; q <= radius; q++) {
            hr_coord c = {q, r};
            if (in_radius(radius, c)) {
                game->cells[cell_index(q, r)] = HR_EMPTY;
                game->empty++;
            }
        }
    }
    return HR_OK;
}

int hr_radius(const hr_game *game)
{
    return game->radius;
}

int hr_pieces_per_player(const hr_game *game)
{
    return game->pieces;
}

int hr_cell_count(const hr_game *game)
{
    return 3 * game->radius * (game->radius + 1) + 1;
}

bool hr_on_board(const hr_game *game, hr_coord at)
{
    return in_radius(game->radius, at);
}

hr_cell hr_cell_at(const hr_game *game, hr_coord at)
{
    if (!in_radius(game->radius, at))
        return HR_OFF_BOARD;
    return (hr_cell)game->cells[cell_index(at.q, at.r)];
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
    return game->empty == 0;
}

hr_player hr_to_move(const hr_game *game)
{
    return hr_is_over(game) ? HR_NOBODY : (hr_player)game->turn;
}

hr_player hr_winner(const hr_game *game)
{
    if (!hr_is_over(game))
        return HR_NOBODY;
    return hr_score(game, HR_P0) > hr_score(game, HR_P1) ? HR_P0 : HR_P1;
}

int hr_empty_count(const hr_game *game)
{
    return game->empty;
}

int hr_in_hand(const hr_game *game, hr_player player)
{
    return valid_player(player) ? game->in_hand[player] : -1;
}

int hr_pieces_on_board(const hr_game *game, hr_player player)
{
    return valid_player(player) ? game->pieces - game->in_hand[player] : -1;
}

int hr_claimed(const hr_game *game, hr_player player)
{
    return valid_player(player) ? game->claimed[player] : -1;
}

int hr_score(const hr_game *game, hr_player player)
{
    if (!valid_player(player))
        return -1;
    return hr_claimed(game, player) + hr_pieces_on_board(game, player);
}

hr_status hr_check(const hr_game *game, hr_move move)
{
    if (game == NULL)
        return HR_ERR_ARG;
    if (hr_is_over(game))
        return HR_ERR_GAME_OVER;
    if (move.type != HR_PLACE && move.type != HR_SWEEP)
        return HR_ERR_BAD_MOVE;
    if (move.type == HR_SWEEP && !valid_dir(move.dir))
        return HR_ERR_BAD_MOVE;
    if (!in_radius(game->radius, move.at))
        return HR_ERR_OFF_BOARD;

    uint8_t cell = game->cells[cell_index(move.at.q, move.at.r)];
    if (move.type == HR_PLACE) {
        if (cell != HR_EMPTY)
            return HR_ERR_NOT_EMPTY;
        if (game->in_hand[game->turn] == 0)
            return HR_ERR_NO_PIECE_IN_HAND;
    } else if (cell != piece_of(game->turn)) {
        return HR_ERR_NOT_OWN_PIECE;
    }
    return HR_OK;
}

int hr_ray_length(const hr_game *game, hr_coord from, hr_dir dir)
{
    if (!in_radius(game->radius, from) || !valid_dir(dir))
        return -1;
    return count_empty(game, cell_index(from.q, from.r), dir_offset(dir));
}

hr_status hr_apply(hr_game *game, hr_move move)
{
    hr_status status = hr_check(game, move);
    if (status != HR_OK)
        return status;

    int player = game->turn;
    int at = cell_index(move.at.q, move.at.r);
    /*
     * While the game is in progress, ply <= 2 * cells - 2 (see HR_MAX_PLIES),
     * so this stays in bounds.
     */
    hr_record *rec = &game->history[game->ply];
    rec->at = (uint16_t)at;
    rec->type = (uint8_t)move.type;
    rec->dir = 0;
    rec->swept = 0;

    if (move.type == HR_PLACE) {
        game->cells[at] = piece_of(player);
        game->in_hand[player]--;
        game->empty--;
    } else {
        int offset = dir_offset(move.dir);
        int swept = count_empty(game, at, offset);

        for (int i = 0; i <= swept; i++) /* i == 0 is the origin */
            game->cells[at + i * offset] = claim_of(player);
        game->claimed[player] = (uint16_t)(game->claimed[player] + swept + 1);
        game->empty = (uint16_t)(game->empty - swept);
        game->in_hand[player]++;

        rec->dir = (uint8_t)move.dir;
        rec->swept = (uint8_t)swept;
    }

    game->ply++;
    game->turn ^= 1;
    return HR_OK;
}

hr_status hr_undo(hr_game *game)
{
    if (game == NULL)
        return HR_ERR_ARG;
    if (game->ply == 0)
        return HR_ERR_NO_HISTORY;

    game->ply--;
    game->turn ^= 1;

    hr_record *rec = &game->history[game->ply];
    int player = game->turn;
    int at = rec->at;

    if (rec->type == HR_PLACE) {
        game->cells[at] = HR_EMPTY;
        game->in_hand[player]++;
        game->empty++;
    } else {
        int offset = dir_offset((hr_dir)rec->dir);
        int swept = rec->swept;

        /*
         * The swept cells were necessarily empty before, and the origin
         * necessarily held the mover's own piece.
         */
        game->cells[at] = piece_of(player);
        for (int i = 1; i <= swept; i++)
            game->cells[at + i * offset] = HR_EMPTY;
        game->claimed[player] = (uint16_t)(game->claimed[player] - swept - 1);
        game->empty = (uint16_t)(game->empty + swept);
        game->in_hand[player]--;
    }

    memset(rec, 0, sizeof *rec);
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

    size_t count = 0;
    int n = game->radius;
    int player = game->turn;
    bool can_place = game->in_hand[player] > 0;

    for (int r = -n; r <= n; r++) {
        for (int q = -n; q <= n; q++) {
            uint8_t cell = game->cells[cell_index(q, r)];
            if (cell == HR_EMPTY && can_place) {
                push_move(out, cap, &count, hr_place(q, r));
            } else if (cell == piece_of(player)) {
                for (int d = 0; d < HR_DIR_COUNT; d++)
                    push_move(out, cap, &count, hr_sweep(q, r, (hr_dir)d));
            }
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

    const hr_record *rec = &game->history[index];
    hr_coord at = index_coord(rec->at);
    *out = rec->type == HR_PLACE ? hr_place(at.q, at.r)
                                 : hr_sweep(at.q, at.r, (hr_dir)rec->dir);
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
    case HR_OK:                   return "成功";
    case HR_ERR_ARG:              return "参数无效";
    case HR_ERR_GAME_OVER:        return "对局已结束";
    case HR_ERR_BAD_MOVE:         return "操作类型或方向无效";
    case HR_ERR_OFF_BOARD:        return "坐标在棋盘外";
    case HR_ERR_NOT_EMPTY:        return "落子点不是空格";
    case HR_ERR_NO_PIECE_IN_HAND: return "手中没有棋子";
    case HR_ERR_NOT_OWN_PIECE:    return "该格没有行动方的棋子";
    case HR_ERR_NO_HISTORY:       return "没有可撤销的操作";
    }
    return "未知错误";
}
