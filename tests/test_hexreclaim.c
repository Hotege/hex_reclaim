/**
 * @file test_hexreclaim.c
 * @brief Tests of the hex_reclaim game-state library.
 *
 * Contains a case for each rule, plus randomized differential testing against a
 * naive reference model. The model implements the rules separately: it uses no
 * sentinels and no incremental counters, and rescans the whole board each time.
 */
#include "hexreclaim.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief Number of checks performed. */
static long checks = 0;

/**
 * @brief Counts a check and exits with a message if it fails.
 *
 * @param cond Condition that must hold.
 */
#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            fprintf(stderr, "失败 %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

/**
 * @brief Builds a coordinate pair.
 *
 * @param q Column coordinate.
 * @param r Row coordinate.
 * @return The coordinates.
 */
static hr_coord at(int q, int r)
{
    hr_coord c;
    c.q = q;
    c.r = r;
    return c;
}

/* ============================================================ Rule cases */

/** @brief Tests hr_init() and the state of a new game. */
static void test_init(void)
{
    hr_game g, before;
    memset(&g, 0xAA, sizeof g);
    memcpy(&before, &g, sizeof g);

    CHECK(hr_init(NULL, 2, 3) == HR_ERR_ARG);
    CHECK(hr_init(&g, 0, 3) == HR_ERR_ARG);
    CHECK(hr_init(&g, HR_MAX_RADIUS + 1, 3) == HR_ERR_ARG);
    CHECK(hr_init(&g, 2, 0) == HR_ERR_ARG);
    CHECK(hr_init(&g, 2, HR_MAX_PIECES + 1) == HR_ERR_ARG);
    CHECK(memcmp(&g, &before, sizeof g) == 0);

    const int sizes[] = {7, 19, 37, 61, 91, 127, 169, 217};
    for (int n = 1; n <= HR_MAX_RADIUS; n++) {
        CHECK(hr_init(&g, n, 3) == HR_OK);
        CHECK(hr_radius(&g) == n);
        CHECK(hr_pieces_per_player(&g) == 3);
        CHECK(hr_cell_count(&g) == sizes[n - 1]);
        CHECK(hr_empty_count(&g) == sizes[n - 1]);
        CHECK(hr_to_move(&g) == HR_P0);
        CHECK(!hr_is_over(&g));
        CHECK(hr_winner(&g) == HR_NOBODY);
        CHECK(hr_ply(&g) == 0);
        for (int p = HR_P0; p <= HR_P1; p++) {
            CHECK(hr_in_hand(&g, (hr_player)p) == 3);
            CHECK(hr_pieces_on_board(&g, (hr_player)p) == 0);
            CHECK(hr_claimed(&g, (hr_player)p) == 0);
            CHECK(hr_score(&g, (hr_player)p) == 0);
        }
        CHECK(hr_in_hand(&g, HR_NOBODY) == -1);
        CHECK(hr_score(&g, (hr_player)2) == -1);

        int count = 0;
        for (int r = -n - 2; r <= n + 2; r++)
            for (int q = -n - 2; q <= n + 2; q++)
                if (hr_on_board(&g, at(q, r))) {
                    count++;
                    CHECK(hr_cell_at(&g, at(q, r)) == HR_EMPTY);
                } else {
                    CHECK(hr_cell_at(&g, at(q, r)) == HR_OFF_BOARD);
                }
        CHECK(count == sizes[n - 1]);
    }
}

/** @brief Tests the geometry of the six directions. */
static void test_directions(void)
{
    for (int d = 0; d < HR_DIR_COUNT; d++) {
        hr_coord a = hr_step(at(0, 0), (hr_dir)d);
        int dist = (abs(a.q) + abs(a.r) + abs(a.q + a.r)) / 2;
        CHECK(dist == 1);

        hr_coord back = hr_step(a, (hr_dir)((d + 3) % HR_DIR_COUNT));
        CHECK(back.q == 0 && back.r == 0);

        for (int e = 0; e < d; e++) {
            hr_coord b = hr_step(at(0, 0), (hr_dir)e);
            CHECK(a.q != b.q || a.r != b.r);
        }
    }
    hr_coord same = hr_step(at(4, -2), HR_DIR_COUNT);
    CHECK(same.q == 4 && same.r == -2);
}

/** @brief Tests placements and rejected moves. */
static void test_place_errors(void)
{
    hr_game g, before;
    CHECK(hr_init(&g, 2, 1) == HR_OK);
    CHECK(hr_apply(&g, hr_place(0, 0)) == HR_OK);
    CHECK(hr_cell_at(&g, at(0, 0)) == HR_PIECE_P0);
    CHECK(hr_in_hand(&g, HR_P0) == 0);
    CHECK(hr_claimed(&g, HR_P0) == 0); /* placing does not claim yet */
    CHECK(hr_score(&g, HR_P0) == 1);   /* but the cell is locked in */
    CHECK(hr_to_move(&g) == HR_P1);

    memcpy(&before, &g, sizeof g);
    CHECK(hr_apply(&g, hr_place(0, 0)) == HR_ERR_NOT_EMPTY);
    CHECK(hr_apply(&g, hr_place(3, 0)) == HR_ERR_OFF_BOARD);
    CHECK(hr_apply(&g, hr_place(2, 1)) == HR_ERR_OFF_BOARD);
    CHECK(hr_apply(&g, hr_place(INT_MAX, INT_MAX)) == HR_ERR_OFF_BOARD);
    CHECK(hr_apply(&g, hr_place(INT_MIN, INT_MIN)) == HR_ERR_OFF_BOARD);
    CHECK(hr_apply(&g, hr_sweep(0, 0, HR_E)) == HR_ERR_NOT_OWN_PIECE);
    CHECK(hr_apply(&g, hr_sweep(1, 0, HR_E)) == HR_ERR_NOT_OWN_PIECE);
    CHECK(hr_apply(&g, hr_sweep(0, 0, HR_DIR_COUNT)) == HR_ERR_BAD_MOVE);
    CHECK(hr_apply(&g, hr_sweep(0, 0, (hr_dir)-1)) == HR_ERR_BAD_MOVE);
    hr_move bad = hr_place(1, 0);
    bad.type = (hr_move_type)2;
    CHECK(hr_apply(&g, bad) == HR_ERR_BAD_MOVE);
    CHECK(hr_apply(NULL, bad) == HR_ERR_ARG);
    CHECK(memcmp(&g, &before, sizeof g) == 0); /* failures change nothing */

    CHECK(hr_apply(&g, hr_place(1, 0)) == HR_OK);
    CHECK(hr_apply(&g, hr_place(-1, 0)) == HR_ERR_NO_PIECE_IN_HAND);
}

/** @brief Tests how sweeps stop and what they claim. */
static void test_sweep(void)
{
    hr_game g;
    CHECK(hr_init(&g, 2, 3) == HR_OK);
    CHECK(hr_apply(&g, hr_place(-2, 0)) == HR_OK);
    CHECK(hr_apply(&g, hr_place(1, 0)) == HR_OK);

    /* Blocked by an opposing piece: stop one cell short of it. */
    CHECK(hr_ray_length(&g, at(-2, 0), HR_E) == 2);
    CHECK(hr_apply(&g, hr_sweep(-2, 0, HR_E)) == HR_OK);
    CHECK(hr_cell_at(&g, at(-2, 0)) == HR_CLAIMED_P0);
    CHECK(hr_cell_at(&g, at(-1, 0)) == HR_CLAIMED_P0);
    CHECK(hr_cell_at(&g, at(0, 0)) == HR_CLAIMED_P0);
    CHECK(hr_cell_at(&g, at(1, 0)) == HR_PIECE_P1);
    CHECK(hr_cell_at(&g, at(2, 0)) == HR_EMPTY);
    CHECK(hr_claimed(&g, HR_P0) == 3);
    CHECK(hr_in_hand(&g, HR_P0) == 3);
    CHECK(hr_empty_count(&g) == 19 - 4);

    /* The first cell is claimed: only the origin is claimed. */
    CHECK(hr_ray_length(&g, at(1, 0), HR_W) == 0);
    CHECK(hr_apply(&g, hr_sweep(1, 0, HR_W)) == HR_OK);
    CHECK(hr_cell_at(&g, at(1, 0)) == HR_CLAIMED_P1);
    CHECK(hr_cell_at(&g, at(0, 0)) == HR_CLAIMED_P0); /* claims never flip */
    CHECK(hr_claimed(&g, HR_P1) == 1);
    CHECK(hr_empty_count(&g) == 19 - 4);

    /* Sweeping to the boundary claims 2n+1 cells. */
    CHECK(hr_init(&g, 3, 3) == HR_OK);
    CHECK(hr_apply(&g, hr_place(-3, 0)) == HR_OK);
    CHECK(hr_apply(&g, hr_place(0, 3)) == HR_OK);
    CHECK(hr_apply(&g, hr_sweep(-3, 0, HR_E)) == HR_OK);
    CHECK(hr_claimed(&g, HR_P0) == 7);

    /* The first cell is off the board: only the origin is claimed. */
    CHECK(hr_apply(&g, hr_sweep(0, 3, HR_SE)) == HR_OK);
    CHECK(hr_claimed(&g, HR_P1) == 1);

    /* The mover's own pieces block as well. */
    CHECK(hr_init(&g, 2, 3) == HR_OK);
    CHECK(hr_apply(&g, hr_place(-2, 0)) == HR_OK);
    CHECK(hr_apply(&g, hr_place(0, 2)) == HR_OK);
    CHECK(hr_apply(&g, hr_place(0, 0)) == HR_OK);
    CHECK(hr_apply(&g, hr_place(0, -2)) == HR_OK);
    CHECK(hr_apply(&g, hr_sweep(-2, 0, HR_E)) == HR_OK);
    CHECK(hr_claimed(&g, HR_P0) == 2);
    CHECK(hr_cell_at(&g, at(0, 0)) == HR_PIECE_P0);

    CHECK(hr_ray_length(&g, at(3, 0), HR_E) == -1);
    CHECK(hr_ray_length(&g, at(0, 0), HR_DIR_COUNT) == -1);
}

/** @brief Tests a game that ends with a placement. */
static void test_end_by_place(void)
{
    static const int cells[7][2] = {
        {0, 0}, {1, 0}, {1, -1}, {0, -1}, {-1, 0}, {-1, 1}, {0, 1}};
    hr_game g;
    CHECK(hr_init(&g, 1, 7) == HR_OK);
    for (int i = 0; i < 7; i++) {
        CHECK(!hr_is_over(&g));
        CHECK(hr_apply(&g, hr_place(cells[i][0], cells[i][1])) == HR_OK);
    }
    CHECK(hr_is_over(&g));
    CHECK(hr_to_move(&g) == HR_NOBODY);
    CHECK(hr_claimed(&g, HR_P0) == 0 && hr_claimed(&g, HR_P1) == 0);
    CHECK(hr_score(&g, HR_P0) == 4 && hr_score(&g, HR_P1) == 3);
    CHECK(hr_winner(&g) == HR_P0);
    CHECK(hr_legal_moves(&g, NULL, 0) == 0);
    CHECK(hr_apply(&g, hr_sweep(0, 0, HR_E)) == HR_ERR_GAME_OVER);

    CHECK(hr_undo(&g) == HR_OK); /* undo the final move */
    CHECK(!hr_is_over(&g));
    CHECK(hr_to_move(&g) == HR_P0);
    CHECK(hr_winner(&g) == HR_NOBODY);
    CHECK(hr_cell_at(&g, at(0, 1)) == HR_EMPTY);
}

/** @brief Tests a game that ends with a sweep. */
static void test_end_by_sweep(void)
{
    hr_game g;
    CHECK(hr_init(&g, 1, 3) == HR_OK);
    CHECK(hr_apply(&g, hr_place(-1, 0)) == HR_OK);
    CHECK(hr_apply(&g, hr_place(1, -1)) == HR_OK);
    CHECK(hr_apply(&g, hr_place(0, -1)) == HR_OK);
    CHECK(hr_apply(&g, hr_place(-1, 1)) == HR_OK);
    CHECK(hr_apply(&g, hr_place(0, 1)) == HR_OK);
    CHECK(hr_apply(&g, hr_sweep(1, -1, HR_NE)) == HR_OK); /* waiting move */
    CHECK(hr_empty_count(&g) == 2);
    CHECK(hr_apply(&g, hr_sweep(-1, 0, HR_E)) == HR_OK);
    CHECK(hr_is_over(&g));
    CHECK(hr_score(&g, HR_P0) == 5 && hr_score(&g, HR_P1) == 2);
    CHECK(hr_winner(&g) == HR_P0);
}

/** @brief Tests the history, move buffer limits and status strings. */
static void test_history_and_buffers(void)
{
    hr_game g;
    hr_move m, buf[3];
    CHECK(hr_init(&g, 2, 2) == HR_OK);
    CHECK(hr_undo(&g) == HR_ERR_NO_HISTORY);
    CHECK(hr_undo(NULL) == HR_ERR_ARG);
    CHECK(hr_history_move(&g, 0, &m) == HR_ERR_ARG);

    CHECK(hr_legal_moves(&g, NULL, 0) == 19);
    buf[2] = hr_sweep(99, 99, HR_SE);
    CHECK(hr_legal_moves(&g, buf, 2) == 19);
    CHECK(buf[0].type == HR_PLACE && buf[1].type == HR_PLACE);
    CHECK(buf[2].at.q == 99); /* nothing past cap */

    CHECK(hr_apply(&g, hr_place(1, -2)) == HR_OK);
    CHECK(hr_history_move(&g, 0, &m) == HR_OK);
    CHECK(m.type == HR_PLACE && m.at.q == 1 && m.at.r == -2);
    CHECK(hr_history_move(&g, 1, &m) == HR_ERR_ARG);
    CHECK(hr_history_move(&g, -1, &m) == HR_ERR_ARG);
    CHECK(hr_history_move(&g, 0, NULL) == HR_ERR_ARG);

    for (int s = HR_OK; s <= HR_ERR_NO_HISTORY; s++)
        CHECK(hr_status_str((hr_status)s)[0] != '\0');
    CHECK(hr_status_str((hr_status)100) != NULL);
}

/* ============================================================ Reference model */

/** @brief Side length of the model's board array. */
#define SPAN (2 * HR_MAX_RADIUS + 1)

/** @brief State of the reference model. */
typedef struct {
    int n,    /**< Board radius. */
        m,    /**< Pieces per player. */
        turn; /**< Player to move, 0 or 1. */
    /** Cells: 0 empty; 1 and 2 pieces; 3 and 4 claimed (P0 and P1). */
    int s[SPAN][SPAN];
} model;

/** @brief Change in `q` for one step in each direction. */
static const int MQ[6] = {1, 1, 0, -1, -1, 0};
/** @brief Change in `r` for one step in each direction. */
static const int MR[6] = {0, -1, -1, 0, 1, 1};

/**
 * @brief Tests whether coordinates lie on the model's board.
 *
 * @param x The model.
 * @param q Column coordinate.
 * @param r Row coordinate.
 * @return Non-zero if the cell is on the board.
 */
static int m_on(const model *x, int q, int r)
{
    return abs(q) <= x->n && abs(r) <= x->n && abs(-q - r) <= x->n;
}

/**
 * @brief Accesses a cell of the model.
 *
 * @param x The model.
 * @param q Column coordinate.
 * @param r Row coordinate.
 * @return Pointer to the cell value.
 */
static int *m_cell(model *x, int q, int r)
{
    return &x->s[q + HR_MAX_RADIUS][r + HR_MAX_RADIUS];
}

/**
 * @brief Counts the cells of the model that hold a value.
 *
 * @param x The model.
 * @param value Cell value to count.
 * @return The number of cells.
 */
static int m_count(model *x, int value)
{
    int count = 0;
    for (int q = -x->n; q <= x->n; q++)
        for (int r = -x->n; r <= x->n; r++)
            if (m_on(x, q, r) && *m_cell(x, q, r) == value)
                count++;
    return count;
}

/**
 * @brief Counts the consecutive empty cells after a cell in one direction.
 *
 * @param x The model.
 * @param q Column of the starting cell.
 * @param r Row of the starting cell.
 * @param d Direction, 0 to 5.
 * @return The number of empty cells.
 */
static int m_ray(model *x, int q, int r, int d)
{
    int len = 0;
    for (;;) {
        q += MQ[d];
        r += MR[d];
        if (!m_on(x, q, r) || *m_cell(x, q, r) != 0)
            return len;
        len++;
    }
}

/**
 * @brief Makes a move in the model.
 *
 * @param x The model.
 * @param mv A legal move.
 */
static void m_apply(model *x, hr_move mv)
{
    int q = mv.at.q, r = mv.at.r;
    if (mv.type == HR_PLACE) {
        *m_cell(x, q, r) = 1 + x->turn;
    } else {
        int len = m_ray(x, q, r, mv.dir);
        for (int i = 0; i <= len; i++)
            *m_cell(x, q + i * MQ[mv.dir], r + i * MR[mv.dir]) = 3 + x->turn;
    }
    x->turn = 1 - x->turn;
}

/**
 * @brief Compares the library with the model in every respect.
 *
 * This includes the verdict on every candidate move, illegal ones too.
 *
 * @param g Game held by the library.
 * @param x The model.
 */
static void compare(const hr_game *g, model *x)
{
    static hr_move moves[HR_MAX_MOVES];
    int empty = m_count(x, 0);
    int over = empty == 0;
    int hand = x->m - m_count(x, 1 + x->turn);
    int cells = 0;

    CHECK(hr_empty_count(g) == empty);
    CHECK(hr_is_over(g) == over);
    CHECK(hr_to_move(g) == (over ? HR_NOBODY : (hr_player)x->turn));
    for (int p = 0; p < 2; p++) {
        int pieces = m_count(x, 1 + p), claimed = m_count(x, 3 + p);
        CHECK(hr_pieces_on_board(g, (hr_player)p) == pieces);
        CHECK(hr_in_hand(g, (hr_player)p) == x->m - pieces);
        CHECK(hr_claimed(g, (hr_player)p) == claimed);
        CHECK(hr_score(g, (hr_player)p) == pieces + claimed);
    }

    size_t expected = 0;
    for (int r = -x->n - 1; r <= x->n + 1; r++) {
        for (int q = -x->n - 1; q <= x->n + 1; q++) {
            int on = m_on(x, q, r);
            int v = on ? *m_cell(x, q, r) : -1;
            cells += on;
            hr_cell want = on ? (hr_cell)(HR_EMPTY + v) : HR_OFF_BOARD;
            CHECK(hr_cell_at(g, at(q, r)) == want);

            int can_place = !over && on && v == 0 && hand > 0;
            CHECK((hr_check(g, hr_place(q, r)) == HR_OK) == can_place);
            expected += (size_t)can_place;

            for (int d = 0; d < 6; d++) {
                int can_sweep = !over && on && v == 1 + x->turn;
                CHECK((hr_check(g, hr_sweep(q, r, (hr_dir)d)) == HR_OK) ==
                      can_sweep);
                expected += (size_t)can_sweep;
                CHECK(hr_ray_length(g, at(q, r), (hr_dir)d) ==
                      (on ? m_ray(x, q, r, d) : -1));
            }
        }
    }

    size_t total = hr_legal_moves(g, moves, HR_MAX_MOVES);
    CHECK(total == expected);
    CHECK(total <= HR_MAX_MOVES);
    CHECK(over || total > 0); /* a move always exists in play */
    for (size_t i = 0; i < total; i++) {
        CHECK(hr_check(g, moves[i]) == HR_OK);
        /*
         * The deterministic order (increasing r, q, dir) also rules out
         * duplicates.
         */
        if (i > 0) {
            hr_move a = moves[i - 1], b = moves[i];
            CHECK(a.at.r < b.at.r ||
                  (a.at.r == b.at.r && a.at.q < b.at.q) ||
                  (a.at.r == b.at.r && a.at.q == b.at.q && a.dir < b.dir));
        }
    }

    if (over) {
        int s0 = hr_score(g, HR_P0), s1 = hr_score(g, HR_P1);
        CHECK(s0 + s1 == cells);
        CHECK(s0 != s1);
        CHECK(hr_winner(g) == (s0 > s1 ? HR_P0 : HR_P1));
    } else {
        CHECK(hr_winner(g) == HR_NOBODY);
    }
}

/* ============================================================ Randomized differential tests */

/** @brief State of the pseudo-random generator. */
static uint32_t rng_state = 2463534242u;

/**
 * @brief Generates a pseudo-random number (xorshift32).
 *
 * @return The next number.
 */
static uint32_t rng(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/**
 * @brief Picks a random legal move.
 *
 * With @p long_game set, sweeps that claim only their origin are preferred,
 * then placements on cells that can immediately be swept that way. This
 * pushes games towards the move limit.
 *
 * @param g A game in progress.
 * @param long_game Non-zero to drag the game out.
 * @return The chosen move.
 */
static hr_move choose(const hr_game *g, int long_game)
{
    static hr_move moves[HR_MAX_MOVES], good[HR_MAX_MOVES];
    size_t total = hr_legal_moves(g, moves, HR_MAX_MOVES);
    CHECK(total > 0);

    if (long_game) {
        for (int pass = 0; pass < 2; pass++) {
            size_t count = 0;
            for (size_t i = 0; i < total; i++) {
                hr_move m = moves[i];
                int keep = 0;
                if (pass == 0 && m.type == HR_SWEEP) {
                    keep = hr_ray_length(g, m.at, m.dir) == 0;
                } else if (pass == 1 && m.type == HR_PLACE) {
                    for (int d = 0; d < 6; d++)
                        keep |= hr_ray_length(g, m.at, (hr_dir)d) == 0;
                }
                if (keep)
                    good[count++] = m;
            }
            if (count > 0)
                return good[rng() % count];
        }
    }
    return moves[rng() % total];
}

/**
 * @brief Plays one random game and verifies it, including replay and undo.
 *
 * @param n Board radius.
 * @param m Pieces per player.
 * @param long_game Passed on to choose().
 * @return The number of moves played.
 */
static int play_random_game(int n, int m, int long_game)
{
    static hr_game snapshots[HR_MAX_PLIES + 1];
    hr_game g, replay;
    model x;

    memset(&x, 0, sizeof x);
    x.n = n;
    x.m = m;
    CHECK(hr_init(&g, n, m) == HR_OK);
    compare(&g, &x);

    int plies = 0;
    while (!hr_is_over(&g)) {
        memcpy(&snapshots[plies], &g, sizeof g);
        hr_move mv = choose(&g, long_game);
        CHECK(hr_apply(&g, mv) == HR_OK);
        m_apply(&x, mv);
        compare(&g, &x);
        plies++;
        CHECK(hr_ply(&g) == plies);
        CHECK(plies <= 2 * hr_cell_count(&g) - 1);
    }

    /* Replaying the history must lead to the same state. */
    CHECK(hr_init(&replay, n, m) == HR_OK);
    for (int i = 0; i < plies; i++) {
        hr_move mv;
        CHECK(hr_history_move(&g, i, &mv) == HR_OK);
        CHECK(hr_apply(&replay, mv) == HR_OK);
    }
    CHECK(memcmp(&replay, &g, sizeof g) == 0);

    /* Undo back to the start; every step must match its snapshot. */
    for (int i = plies - 1; i >= 0; i--) {
        CHECK(hr_undo(&g) == HR_OK);
        CHECK(memcmp(&g, &snapshots[i], sizeof g) == 0);
    }
    CHECK(hr_undo(&g) == HR_ERR_NO_HISTORY);
    return plies;
}

/** @brief Runs the differential tests over a range of radii and piece counts. */
static void test_random_games(void)
{
    static const int pieces[] = {1, 2, 3, 5, 12, HR_MAX_PIECES};
    int games = 0, longest = 0;

    for (int n = 1; n <= HR_MAX_RADIUS; n++) {
        int rounds = n <= 2 ? 300 : n <= 4 ? 40 : 3;
        for (size_t k = 0; k < sizeof pieces / sizeof pieces[0]; k++) {
            for (int i = 0; i < rounds; i++) {
                int plies = play_random_game(n, pieces[k], i % 2);
                if (n == HR_MAX_RADIUS && plies > longest)
                    longest = plies;
                games++;
            }
        }
    }
    printf("随机对拍 %d 局；半径 %d 的最长对局 %d 步（上限 %d）\n", games,
           HR_MAX_RADIUS, longest, HR_MAX_PLIES);
}

/**
 * @brief Runs all tests.
 *
 * @return 0 on success; a failed check exits with status 1.
 */
int main(void)
{
    test_init();
    test_directions();
    test_place_errors();
    test_sweep();
    test_end_by_place();
    test_end_by_sweep();
    test_history_and_buffers();
    test_random_games();
    printf("全部通过（%ld 项检查），sizeof(hr_game) = %zu 字节\n", checks,
           sizeof(hr_game));
    return 0;
}
