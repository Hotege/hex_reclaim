/**
 * @file test_sim.c
 * @brief Tests of the simulation layer: board, position, actions and playouts.
 *
 * test_hexreclaim.c checks the rules against an independent model through the
 * ::hr_game interface, which is built on the same position code. The tests here
 * check the simulation functions against that interface and against each other.
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
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);\
            exit(1);                                                         \
        }                                                                    \
    } while (0)

/**
 * @brief Compares two positions byte for byte.
 *
 * @param a One position.
 * @param b The other.
 * @return Non-zero if they are equal.
 */
static int same(const hr_pos *a, const hr_pos *b)
{
    return memcmp(a, b, sizeof *a) == 0;
}

/** @brief Tests the geometry held by a board. */
static void test_board(void)
{
    hr_board b, before;
    memset(&b, 0xAA, sizeof b);
    memcpy(&before, &b, sizeof b);
    CHECK(hr_board_init(NULL, 2) == HR_ERR_ARG);
    CHECK(hr_board_init(&b, 0) == HR_ERR_ARG);
    CHECK(hr_board_init(&b, HR_MAX_RADIUS + 1) == HR_ERR_ARG);
    CHECK(memcmp(&b, &before, sizeof b) == 0);

    for (int n = 1; n <= HR_MAX_RADIUS; n++) {
        int cells = 3 * n * (n + 1) + 1;
        CHECK(hr_board_init(&b, n) == HR_OK);
        CHECK(b.radius == n && b.cells == cells);
        CHECK(b.words == (cells + 63) / 64);

        for (int c = 0; c < cells; c++) {
            hr_coord at = {b.q[c], b.r[c]};
            CHECK(hr_board_index(&b, at) == c);
            if (c > 0) /* ordered by r, then q */
                CHECK(b.r[c] > b.r[c - 1] ||
                      (b.r[c] == b.r[c - 1] && b.q[c] > b.q[c - 1]));
            for (int d = 0; d < HR_DIR_COUNT; d++) {
                int i = hr_board_index(&b, hr_step(at, (hr_dir)d));
                CHECK(b.neighbor[c][d] == (i < 0 ? HR_NO_CELL : i));
            }
        }

        int on = 0;
        for (int r = -n - 2; r <= n + 2; r++)
            for (int q = -n - 2; q <= n + 2; q++) {
                hr_coord at = {q, r};
                on += hr_board_index(&b, at) >= 0;
            }
        CHECK(on == cells);
        hr_coord far = {INT_MAX, INT_MIN};
        CHECK(hr_board_index(&b, far) == -1);
    }
}

/** @brief Tests the action encoding and its conversion to and from moves. */
static void test_actions_encoding(void)
{
    for (int c = 0; c < HR_MAX_CELLS; c++) {
        hr_action p = hr_action_place(c);
        CHECK(p < HR_ACTION_LIMIT);
        CHECK(hr_action_cell(p) == c && hr_action_type(p) == HR_PLACE);
        CHECK(hr_action_dir(p) == HR_E);
        for (int d = 0; d < HR_DIR_COUNT; d++) {
            hr_action s = hr_action_sweep(c, (hr_dir)d);
            CHECK(s < HR_ACTION_LIMIT);
            CHECK(hr_action_cell(s) == c && hr_action_type(s) == HR_SWEEP);
            CHECK(hr_action_dir(s) == (hr_dir)d);
        }
    }

    hr_board b;
    CHECK(hr_board_init(&b, 3) == HR_OK);
    for (int c = 0; c < b.cells; c++) {
        for (int k = 0; k < HR_ACTIONS_PER_CELL; k++) {
            hr_action a = (hr_action)(c * HR_ACTIONS_PER_CELL + k), back;
            CHECK(hr_action_encode(&b, hr_action_decode(&b, a), &back) ==
                  HR_OK);
            CHECK(back == a);
        }
    }

    hr_action out = 4321;
    hr_move bad = hr_place(0, 0);
    bad.type = (hr_move_type)2;
    CHECK(hr_action_encode(NULL, hr_place(0, 0), &out) == HR_ERR_ARG);
    CHECK(hr_action_encode(&b, hr_place(0, 0), NULL) == HR_ERR_ARG);
    CHECK(hr_action_encode(&b, bad, &out) == HR_ERR_BAD_MOVE);
    CHECK(hr_action_encode(&b, hr_sweep(0, 0, HR_DIR_COUNT), &out) ==
          HR_ERR_BAD_MOVE);
    CHECK(hr_action_encode(&b, hr_place(4, 0), &out) == HR_ERR_OFF_BOARD);
    CHECK(out == 4321);
}

/** @brief Tests position setup, queries and rejected actions. */
static void test_pos_errors(void)
{
    hr_board b, other;
    hr_pos p, before;
    CHECK(hr_board_init(&b, 2) == HR_OK);
    CHECK(hr_board_init(&other, 3) == HR_OK);

    memset(&p, 0xAA, sizeof p);
    memcpy(&before, &p, sizeof p);
    CHECK(hr_pos_init(NULL, &b, 3) == HR_ERR_ARG);
    CHECK(hr_pos_init(&p, NULL, 3) == HR_ERR_ARG);
    CHECK(hr_pos_init(&p, &b, 0) == HR_ERR_ARG);
    CHECK(hr_pos_init(&p, &b, HR_MAX_PIECES + 1) == HR_ERR_ARG);
    CHECK(same(&p, &before));

    /* No padding, so positions can be compared and hashed as bytes. */
    CHECK(sizeof(hr_pos) == 5 * HR_SET_WORDS * sizeof(uint64_t) + 8);

    CHECK(hr_pos_init(&p, &b, 1) == HR_OK);
    CHECK(hr_pos_to_move(&p) == HR_P0 && !hr_pos_is_over(&p));
    CHECK(hr_pos_decided(&p) == HR_NOBODY);
    CHECK(hr_pos_score(&p, HR_NOBODY) == -1);
    CHECK(hr_pos_cell(&p, 0) == HR_EMPTY);
    CHECK(hr_pos_cell(&p, -1) == HR_OFF_BOARD);
    CHECK(hr_pos_cell(&p, b.cells) == HR_OFF_BOARD);
    CHECK(hr_pos_cell(&p, HR_MAX_CELLS) == HR_OFF_BOARD);
    CHECK(!hr_set_has(p.empty, HR_NO_CELL));
    CHECK(hr_pos_ray_length(&b, &p, -1, HR_E) == -1);
    CHECK(hr_pos_ray_length(&b, &p, b.cells, HR_E) == -1);
    CHECK(hr_pos_ray_length(&b, &p, 0, HR_DIR_COUNT) == -1);
    CHECK(hr_pos_ray_length(&b, &p, 0, HR_E) == 2);

    CHECK(hr_pos_apply(&b, &p, hr_action_place(0)) == HR_OK);
    memcpy(&before, &p, sizeof p);
    CHECK(hr_pos_apply(NULL, &p, 0) == HR_ERR_ARG);
    CHECK(hr_pos_apply(&b, NULL, 0) == HR_ERR_ARG);
    CHECK(hr_pos_apply(&other, &p, 0) == HR_ERR_ARG);
    CHECK(hr_pos_apply(&b, &p, hr_action_place(b.cells)) == HR_ERR_OFF_BOARD);
    CHECK(hr_pos_apply(&b, &p, UINT16_MAX) == HR_ERR_OFF_BOARD);
    CHECK(hr_pos_apply(&b, &p, hr_action_place(0)) == HR_ERR_NOT_EMPTY);
    CHECK(hr_pos_apply(&b, &p, hr_action_sweep(0, HR_E)) ==
          HR_ERR_NOT_OWN_PIECE);
    CHECK(same(&p, &before));
    CHECK(hr_pos_apply(&b, &p, hr_action_place(1)) == HR_OK);
    memcpy(&before, &p, sizeof p);
    CHECK(hr_pos_apply(&b, &p, hr_action_place(2)) ==
          HR_ERR_NO_PIECE_IN_HAND);
    CHECK(same(&p, &before));

    hr_rng rng;
    hr_rng_seed(&rng, 1);
    hr_pos_playout(&b, &p, &rng, HR_PLAY_TO_END);
    memcpy(&before, &p, sizeof p);
    CHECK(hr_pos_apply(&b, &p, 0) == HR_ERR_GAME_OVER);
    CHECK(same(&p, &before));
}

/** @brief Tests that the move order leaves no trace in a position. */
static void test_transposition(void)
{
    hr_board b;
    hr_pos x, y;
    CHECK(hr_board_init(&b, 4) == HR_OK);
    CHECK(hr_pos_init(&x, &b, 4) == HR_OK);
    y = x;
    const int order_x[] = {0, 10, 20, 30}, order_y[] = {20, 30, 0, 10};
    for (int i = 0; i < 4; i++) {
        CHECK(hr_pos_apply(&b, &x, hr_action_place(order_x[i])) == HR_OK);
        CHECK(hr_pos_apply(&b, &y, hr_action_place(order_y[i])) == HR_OK);
    }
    CHECK(same(&x, &y));
}

/**
 * @brief Checks that the cell sets and counters of a position agree.
 *
 * @param b The board.
 * @param p The position.
 */
static void check_invariants(const hr_board *b, const hr_pos *p)
{
    int empty = 0, pieces[2] = {0, 0}, claimed[2] = {0, 0};
    for (int c = 0; c < 64 * HR_SET_WORDS; c++) {
        int in = hr_set_has(p->empty, c);
        empty += hr_set_has(p->empty, c);
        for (int s = 0; s < 2; s++) {
            in += hr_set_has(p->piece[s], c) + hr_set_has(p->claimed[s], c);
            pieces[s] += hr_set_has(p->piece[s], c);
            claimed[s] += hr_set_has(p->claimed[s], c);
        }
        CHECK(in == (c < b->cells)); /* the sets partition the board */
    }
    CHECK(p->empty_count == empty);
    for (int s = 0; s < 2; s++) {
        CHECK(p->in_hand[s] == p->pieces - pieces[s]);
        CHECK(p->score[s] == pieces[s] + claimed[s]);
    }
}

/**
 * @brief Checks hr_pos_actions() against the legal moves of the game.
 *
 * The actions must lead to pairwise different positions, and to exactly the set
 * of positions the legal moves lead to.
 *
 * @param g A game in progress.
 */
static void check_action_list(const hr_game *g)
{
    static hr_move moves[HR_MAX_MOVES];
    static hr_pos after[HR_MAX_MOVES];
    static hr_action actions[HR_MAX_MOVES];
    static char matched[HR_MAX_MOVES];
    const hr_board *b = hr_game_board(g);
    const hr_pos *p = hr_game_position(g);

    size_t total = hr_legal_moves(g, moves, HR_MAX_MOVES), distinct = 0;
    for (size_t i = 0; i < total; i++) {
        hr_action a;
        hr_pos next = *p;
        CHECK(hr_action_encode(b, moves[i], &a) == HR_OK);
        CHECK(hr_pos_apply(b, &next, a) == HR_OK);
        size_t j = 0;
        while (j < distinct && !same(&after[j], &next))
            j++;
        if (j == distinct)
            after[distinct++] = next;
    }

    size_t count = hr_pos_actions(b, p, actions, HR_MAX_MOVES);
    CHECK(count == distinct);
    CHECK(hr_pos_actions(b, p, NULL, 0) == count);
    memset(matched, 0, distinct);
    for (size_t i = 0; i < count; i++) {
        hr_pos next = *p;
        if (i > 0)
            CHECK(actions[i] > actions[i - 1]);
        CHECK(hr_pos_apply(b, &next, actions[i]) == HR_OK);
        size_t j = 0;
        while (j < distinct && !same(&after[j], &next))
            j++;
        CHECK(j < distinct && !matched[j]);
        matched[j] = 1;
    }

    hr_action buf[2] = {0, UINT16_MAX};
    CHECK(hr_pos_actions(b, p, buf, 1) == count);
    CHECK(buf[0] == actions[0] && buf[1] == UINT16_MAX);
}

/** @brief Plays random games, mirroring each in a bare position. */
static void test_mirrored_games(void)
{
    static const int pieces[] = {1, 2, 3, 5, 12, HR_MAX_PIECES};
    static hr_game g;
    static hr_action actions[HR_MAX_MOVES];
    hr_rng rng;
    hr_rng_seed(&rng, 12345);

    for (int n = 1; n <= HR_MAX_RADIUS; n++) {
        int rounds = n <= 2 ? 100 : n <= 4 ? 10 : 1;
        for (size_t k = 0; k < sizeof pieces / sizeof pieces[0]; k++) {
            for (int round = 0; round < rounds; round++) {
                hr_board b;
                hr_pos p;
                CHECK(hr_init(&g, n, pieces[k]) == HR_OK);
                CHECK(hr_board_init(&b, n) == HR_OK);
                CHECK(hr_pos_init(&p, &b, pieces[k]) == HR_OK);
                CHECK(memcmp(&b, hr_game_board(&g), sizeof b) == 0);
                CHECK(same(&p, hr_game_position(&g)));

                hr_action a;
                while (hr_pos_random_action(&b, &p, &rng, &a)) {
                    check_invariants(&b, &p);
                    check_action_list(&g);

                    size_t count =
                        hr_pos_actions(&b, &p, actions, HR_MAX_MOVES);
                    size_t i = 0;
                    while (i < count && actions[i] != a)
                        i++;
                    CHECK(i < count);

                    CHECK(hr_apply(&g, hr_action_decode(&b, a)) == HR_OK);
                    CHECK(hr_pos_apply(&b, &p, a) == HR_OK);
                    CHECK(same(&p, hr_game_position(&g)));
                    CHECK(hr_pos_to_move(&p) == hr_to_move(&g));
                    for (int c = 0; c < b.cells; c++) {
                        hr_coord at = {b.q[c], b.r[c]};
                        CHECK(hr_pos_cell(&p, c) == hr_cell_at(&g, at));
                    }
                }

                check_invariants(&b, &p);
                CHECK(hr_is_over(&g) && hr_pos_is_over(&p));
                CHECK(hr_pos_actions(&b, &p, NULL, 0) == 0);
                CHECK(hr_pos_decided(&p) == hr_winner(&g));

                hr_pos end = p;
                uint64_t state = rng.state;
                a = 999;
                CHECK(!hr_pos_random_action(&b, &p, &rng, &a));
                CHECK(hr_pos_playout(&b, &p, &rng, HR_PLAY_TO_END) ==
                      hr_winner(&g));
                CHECK(a == 999 && rng.state == state && same(&p, &end));
            }
        }
    }
}

/**
 * @brief Checks the distribution of the actions sampled from one position.
 *
 * Every legal move must be equally likely, so an action's weight is 1, except
 * that the listed blocked sweep of a piece carries the weight of all blocked
 * directions of that piece. Every action must occur, and each frequency must lie
 * within five standard deviations of the expected one. The seed is fixed.
 *
 * @param b The board.
 * @param p A position in progress.
 * @param rng Random generator.
 */
static void check_distribution(const hr_board *b, const hr_pos *p,
                               hr_rng *rng)
{
    static hr_action actions[HR_MAX_MOVES];
    static long hits[HR_ACTION_LIMIT];
    static int weight[HR_MAX_MOVES];
    size_t count = hr_pos_actions(b, p, actions, HR_MAX_MOVES);
    long moves = 0;

    for (size_t i = 0; i < count; i++) {
        int cell = hr_action_cell(actions[i]);
        weight[i] = 1;
        if (hr_action_type(actions[i]) == HR_SWEEP &&
            hr_pos_ray_length(b, p, cell, hr_action_dir(actions[i])) == 0) {
            weight[i] = 0;
            for (int d = 0; d < HR_DIR_COUNT; d++)
                weight[i] +=
                    hr_pos_ray_length(b, p, cell, (hr_dir)d) == 0;
        }
        moves += weight[i];
    }
    /* Cross-check the total with the counters. */
    int player = p->turn;
    CHECK(moves == (p->in_hand[player] > 0 ? p->empty_count : 0) +
                       6 * (p->pieces - p->in_hand[player]));

    long samples = 2000L * moves;
    memset(hits, 0, sizeof hits);
    for (long i = 0; i < samples; i++) {
        hr_action a;
        CHECK(hr_pos_random_action(b, p, rng, &a));
        CHECK(a < HR_ACTION_LIMIT);
        hits[a]++;
    }

    long listed = 0;
    for (size_t i = 0; i < count; i++) {
        double prob = (double)weight[i] / (double)moves;
        double expected = (double)samples * prob;
        double diff = (double)hits[actions[i]] - expected;
        CHECK(hits[actions[i]] > 0);
        CHECK(diff * diff <= 25.0 * expected * (1.0 - prob) + 0.5);
        listed += hits[actions[i]];
    }
    CHECK(listed == samples); /* nothing outside the list */
}

/** @brief Tests the sampling distribution in positions of several games. */
static void test_sampling(void)
{
    static const int sizes[][2] = {{1, 2}, {2, 3}, {3, 6}, {4, 6}, {5, 12},
                                   {8, 40}};
    hr_rng rng;
    hr_rng_seed(&rng, 777);

    for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        hr_board b;
        hr_pos p;
        CHECK(hr_board_init(&b, sizes[k][0]) == HR_OK);
        CHECK(hr_pos_init(&p, &b, sizes[k][1]) == HR_OK);

        for (int ply = 0; !hr_pos_is_over(&p); ply++) {
            if (ply % (b.cells / 2 + 1) == 0 || p.empty_count == 1)
                check_distribution(&b, &p, &rng);
            hr_action a;
            CHECK(hr_pos_random_action(&b, &p, &rng, &a));
            CHECK(hr_pos_apply(&b, &p, a) == HR_OK);
        }
    }
}

/** @brief Tests both stopping modes of hr_pos_playout(). */
static void test_playout(void)
{
    static const int pieces[] = {1, 3, 6, HR_MAX_PIECES};

    for (int n = 1; n <= HR_MAX_RADIUS; n++) {
        hr_board b;
        CHECK(hr_board_init(&b, n) == HR_OK);
        for (size_t k = 0; k < sizeof pieces / sizeof pieces[0]; k++) {
            for (int seed = 0; seed < 300; seed++) {
                hr_pos start, full, step, early, walk;
                hr_rng r1, r2, r3, r4;
                hr_action a;
                CHECK(hr_pos_init(&start, &b, pieces[k]) == HR_OK);
                hr_rng_seed(&r1, (uint64_t)seed);
                r2 = r3 = r4 = r1;

                full = step = early = walk = start;
                hr_player w =
                    hr_pos_playout(&b, &full, &r1, HR_PLAY_TO_END);
                while (hr_pos_random_action(&b, &step, &r2, &a))
                    CHECK(hr_pos_apply(&b, &step, a) == HR_OK);
                CHECK(same(&full, &step) && r1.state == r2.state);
                CHECK(hr_pos_is_over(&full));
                CHECK(w != HR_NOBODY && w == hr_pos_decided(&full));
                CHECK(full.score[0] + full.score[1] == b.cells);

                /* Stops at the first position with a certain winner. */
                CHECK(hr_pos_playout(&b, &early, &r3,
                                     HR_PLAY_TO_DECIDED) == w);
                while (hr_pos_decided(&walk) == HR_NOBODY) {
                    CHECK(hr_pos_random_action(&b, &walk, &r4, &a));
                    CHECK(hr_pos_apply(&b, &walk, a) == HR_OK);
                }
                CHECK(same(&early, &walk) && r3.state == r4.state);

                /* Whatever follows, the winner stays the same. */
                hr_rng_seed(&r3, (uint64_t)seed + 1000000u);
                CHECK(hr_pos_playout(&b, &early, &r3, HR_PLAY_TO_END) == w);
            }
        }
    }
}

/**
 * @brief Runs all tests.
 *
 * @return 0 on success; a failed check exits with status 1.
 */
int main(void)
{
    test_board();
    test_actions_encoding();
    test_pos_errors();
    test_transposition();
    test_mirrored_games();
    test_sampling();
    test_playout();
    printf("All simulation tests passed (%ld checks), "
           "sizeof(hr_pos) = %zu bytes\n", checks, sizeof(hr_pos));
    return 0;
}
