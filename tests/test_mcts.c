/**
 * @file test_mcts.c
 * @brief Tests of the Monte Carlo tree search.
 *
 * Proven results are compared with an exhaustive solver that shares only the
 * rules with the search: it tries every action code with hr_pos_apply() and
 * plays each line to the very end of the game.
 */
#include "hexreclaim_mcts.h"

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

/** @brief Size of the node array shared by the tests. */
#define ARENA (1u << 21)

/** @brief Node array shared by the tests. */
static hr_mcts_node arena[ARENA];

/* ============================================================ Exact solver */

/** @brief Base-2 logarithm of the size of the solver's hash table. */
#define MEMO_BITS 22
/** @brief Size of the solver's hash table. */
#define MEMO_SIZE (1u << MEMO_BITS)

/** @brief Keys of solved positions; 0 marks a free slot. */
static uint64_t memo_key[MEMO_SIZE];
/** @brief Winners of solved positions. */
static uint8_t memo_winner[MEMO_SIZE];
/** @brief Number of solved positions stored. */
static size_t memo_used;

/** @brief Forgets all results; needed when radius or piece count changes. */
static void solver_clear(void)
{
    memset(memo_key, 0, sizeof memo_key);
    memo_used = 0;
}

/**
 * @brief Encodes a position of a board of radius 2 or less.
 *
 * @param b The board.
 * @param p The position.
 * @return A non-zero number identifying cell contents and player to move.
 */
static uint64_t position_key(const hr_board *b, const hr_pos *p)
{
    uint64_t key = 0;
    for (int c = 0; c < b->cells; c++)
        key = key * 5 + (uint64_t)(hr_pos_cell(p, c) - HR_EMPTY);
    return key * 2 + p->turn + 1;
}

/**
 * @brief Finds the slot of a key in the hash table.
 *
 * @param key The key.
 * @return Index of the slot holding the key, or of the free slot for it.
 */
static size_t memo_slot(uint64_t key)
{
    size_t i = (size_t)((key * 0x9E3779B97F4A7C15ULL) >> (64 - MEMO_BITS));
    while (memo_key[i] != 0 && memo_key[i] != key)
        i = (i + 1) % MEMO_SIZE;
    return i;
}

/**
 * @brief Determines the winner with best play by trying everything.
 *
 * @param b A board of radius 2 or less.
 * @param p The position.
 * @return The winner, 0 or 1.
 */
static int solve(const hr_board *b, const hr_pos *p)
{
    if (hr_pos_is_over(p))
        return p->score[0] > p->score[1] ? 0 : 1;

    uint64_t key = position_key(b, p);
    size_t i = memo_slot(key);
    if (memo_key[i] == key)
        return memo_winner[i];

    int mover = p->turn, winner = 1 - mover;
    for (int a = 0; a < b->cells * HR_ACTIONS_PER_CELL; a++) {
        hr_pos next = *p;
        if (hr_pos_apply(b, &next, (hr_action)a) == HR_OK &&
            solve(b, &next) == mover) {
            winner = mover;
            break;
        }
    }

    CHECK(memo_used < MEMO_SIZE / 4 * 3);
    i = memo_slot(key); /* the recursion filled further slots */
    memo_key[i] = key;
    memo_winner[i] = (uint8_t)winner;
    memo_used++;
    return winner;
}

/* ============================================================ Tree checks */

/**
 * @brief Checks a subtree for consistency.
 *
 * Verifies the children, the visit and win counts, which both must add up from
 * the children, and the proofs.
 *
 * @param s The search.
 * @param index Index of the subtree's root node.
 * @param p Position of that node.
 * @param full Non-zero if the node array may have run out.
 * @param exact Non-zero to compare every proof with the solver.
 */
static void check_node(const hr_mcts *s, uint32_t index, const hr_pos *p,
                       int full, int exact)
{
    CHECK(index < s->used);
    const hr_mcts_node *node = &s->nodes[index];
    int mover = p->turn;
    hr_player decided = hr_pos_decided(p);

    CHECK(node->wins <= node->visits);
    CHECK(node->proven <= 2);
    if (exact && node->proven != 0)
        CHECK(solve(&s->board, p) == (int)node->proven - 1);

    if (node->children == 0) {
        if (node->proven != 0)
            CHECK(decided == (hr_player)(node->proven - 1));
        else if (node->visits > 0)
            CHECK(decided == HR_NOBODY);
        if (node->proven == 0 && !full)
            CHECK(node->visits <= 1); /* the second visit expands */
        return;
    }

    hr_action listed[HR_MAX_MOVES];
    char seen[HR_ACTION_LIMIT] = {0};
    size_t count = hr_pos_actions(&s->board, p, listed, HR_MAX_MOVES);
    CHECK(node->children == count);
    for (size_t i = 0; i < count; i++)
        seen[listed[i]] = 1;

    long visits = 0, wins = 0;
    int any_win = 0, all_lost = 1;
    for (size_t i = 0; i < count; i++) {
        uint32_t c = node->first_child + (uint32_t)i;
        const hr_mcts_node *child = &s->nodes[c];
        hr_pos next = *p;

        CHECK(seen[child->action] == 1); /* each action exactly once */
        seen[child->action] = 2;
        CHECK(hr_pos_apply(&s->board, &next, (hr_action)child->action) ==
              HR_OK);
        visits += child->visits;
        wins += (long)child->visits - (long)child->wins;
        any_win |= child->proven == 1u + (unsigned)mover;
        all_lost &= child->proven == 2u - (unsigned)mover;
        check_node(s, c, &next, full, exact);
    }

    /* Iterations that ended at this node, before it was expanded. */
    long own = (long)node->visits - visits;
    if (index == 0)
        CHECK(own == 0);
    else if (full)
        CHECK(own >= 1);
    else
        CHECK(own == 1);
    CHECK((long)node->wins - wins >= 0 && (long)node->wins - wins <= own);

    if (decided != HR_NOBODY) {
        /* Only the root is expanded although its winner is certain. */
        CHECK(index == 0 && node->proven == 1u + (unsigned)decided);
    } else if (node->proven == 1u + (unsigned)mover) {
        CHECK(any_win);
    } else if (node->proven != 0) {
        CHECK(all_lost);
    } else {
        CHECK(!any_win && !all_lost);
    }
}

/**
 * @brief Plays random moves from the start.
 *
 * @param b The board.
 * @param p Receives the position reached.
 * @param pieces Pieces per player.
 * @param plies Maximum number of moves; fewer if the winner becomes certain.
 * @param rng Random generator.
 */
static void random_position(const hr_board *b, hr_pos *p, int pieces,
                            int plies, hr_rng *rng)
{
    CHECK(hr_pos_init(p, b, pieces) == HR_OK);
    for (int i = 0; i < plies; i++) {
        hr_pos next = *p;
        hr_action a;
        CHECK(hr_pos_random_action(b, &next, rng, &a));
        CHECK(hr_pos_apply(b, &next, a) == HR_OK);
        if (hr_pos_decided(&next) != HR_NOBODY)
            break;
        *p = next;
    }
}

/* ============================================================ Test cases */

/** @brief Tests rejected arguments and the smallest node arrays. */
static void test_init(void)
{
    hr_board b, other;
    hr_pos p, over;
    hr_mcts s, before;
    hr_rng rng;
    CHECK(hr_board_init(&b, 2) == HR_OK);
    CHECK(hr_board_init(&other, 3) == HR_OK);
    CHECK(hr_pos_init(&p, &b, 3) == HR_OK);
    over = p;
    hr_rng_seed(&rng, 1);
    hr_pos_playout(&b, &over, &rng, HR_PLAY_TO_END);

    memset(&s, 0xAA, sizeof s);
    memcpy(&before, &s, sizeof s);
    CHECK(hr_mcts_init(NULL, &b, &p, arena, ARENA, 1) == HR_ERR_ARG);
    CHECK(hr_mcts_init(&s, NULL, &p, arena, ARENA, 1) == HR_ERR_ARG);
    CHECK(hr_mcts_init(&s, &b, NULL, arena, ARENA, 1) == HR_ERR_ARG);
    CHECK(hr_mcts_init(&s, &b, &p, NULL, ARENA, 1) == HR_ERR_ARG);
    CHECK(hr_mcts_init(&s, &other, &p, arena, ARENA, 1) == HR_ERR_ARG);
    CHECK(hr_mcts_init(&s, &b, &p, arena, 19, 1) == HR_ERR_ARG);
    CHECK(hr_mcts_init(&s, &b, &p, arena, 0, 1) == HR_ERR_ARG);
    CHECK(hr_mcts_init(&s, &b, &over, arena, ARENA, 1) == HR_ERR_GAME_OVER);
    CHECK(memcmp(&s, &before, sizeof s) == 0);

    /* Just enough room for the root and its children. */
    CHECK(hr_mcts_init(&s, &b, &p, arena, 20, 1) == HR_OK);
    CHECK(hr_mcts_iterations(&s) == 0);
    CHECK(hr_mcts_proven(&s) == HR_NOBODY);
    CHECK(hr_mcts_moves(&s, NULL, 0) == 19);
    CHECK(hr_mcts_best(&s).visits == 0);
    CHECK(hr_mcts_run(&s, 0) == 0);
    CHECK(hr_mcts_run(&s, 500) == 500);
    CHECK(s.used == 20 && hr_mcts_iterations(&s) == 500);
    check_node(&s, 0, &p, 1, 0);

    hr_mcts_move moves[3];
    moves[2].action = 999;
    CHECK(hr_mcts_moves(&s, moves, 2) == 19);
    CHECK(moves[2].action == 999);
}

/** @brief Checks trees grown on boards of various sizes. */
static void test_trees(void)
{
    static const int sizes[][2] = {{1, 2}, {2, 3}, {3, 6}, {4, 6}, {5, 12},
                                   {8, 255}};
    hr_rng rng;
    hr_rng_seed(&rng, 2);

    for (size_t k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        hr_board b;
        CHECK(hr_board_init(&b, sizes[k][0]) == HR_OK);
        for (int stage = 0; stage < 4; stage++) {
            hr_pos p;
            hr_mcts s;
            random_position(&b, &p, sizes[k][1], stage * b.cells / 3, &rng);
            size_t moves = hr_pos_actions(&b, &p, NULL, 0);

            CHECK(hr_mcts_init(&s, &b, &p, arena, ARENA, k + 10) == HR_OK);
            uint32_t done = hr_mcts_run(&s, 3000);
            CHECK(done == hr_mcts_iterations(&s));
            CHECK(done == 3000 || hr_mcts_proven(&s) != HR_NOBODY);
            CHECK(s.used < ARENA - HR_MAX_MOVES);
            check_node(&s, 0, &p, 0, 0);

            /* A nearly full array stops the growth, not the search. */
            size_t small = 3 * moves + 1;
            CHECK(hr_mcts_init(&s, &b, &p, arena, small, k) == HR_OK);
            done = hr_mcts_run(&s, 3000);
            CHECK(done == 3000 || hr_mcts_proven(&s) != HR_NOBODY);
            CHECK(s.used <= small);
            check_node(&s, 0, &p, 1, 0);
        }
    }
}

/** @brief Tests positions whose winner is certain before the game is over. */
static void test_decided_root(void)
{
    hr_board b;
    hr_rng rng;
    int found = 0;
    hr_rng_seed(&rng, 6);
    CHECK(hr_board_init(&b, 2) == HR_OK);

    for (int g = 0; g < 500 && found < 50; g++) {
        hr_pos p;
        hr_mcts s;
        CHECK(hr_pos_init(&p, &b, 3) == HR_OK);
        hr_player winner = hr_pos_playout(&b, &p, &rng, HR_PLAY_TO_DECIDED);
        if (hr_pos_is_over(&p))
            continue;
        found++;

        CHECK(hr_mcts_init(&s, &b, &p, arena, ARENA, (uint64_t)g) == HR_OK);
        CHECK(hr_mcts_proven(&s) == winner);
        CHECK(hr_mcts_run(&s, 100) == 0);
        CHECK(hr_mcts_iterations(&s) == 0);
        check_node(&s, 0, &p, 0, 0);

        hr_pos next = p;
        CHECK(hr_pos_apply(&b, &next, hr_mcts_best(&s).action) == HR_OK);
    }
    CHECK(found == 50);
}

/**
 * @brief Compares the statistics of two searches.
 *
 * @param x One search.
 * @param y The other.
 * @return Non-zero if all root moves have equal statistics.
 */
static int same_results(const hr_mcts *x, const hr_mcts *y)
{
    hr_mcts_move a[HR_MAX_MOVES], b[HR_MAX_MOVES];
    size_t count = hr_mcts_moves(x, a, HR_MAX_MOVES);
    if (hr_mcts_moves(y, b, HR_MAX_MOVES) != count)
        return 0;
    for (size_t i = 0; i < count; i++)
        if (a[i].action != b[i].action || a[i].visits != b[i].visits ||
            a[i].wins != b[i].wins || a[i].proven != b[i].proven)
            return 0;
    return 1;
}

/** @brief Tests that a search depends only on its inputs. */
static void test_reproducible(void)
{
    static hr_mcts_node second[1u << 18];
    hr_board b;
    hr_pos p;
    hr_mcts x, y;
    CHECK(hr_board_init(&b, 3) == HR_OK);
    CHECK(hr_pos_init(&p, &b, 4) == HR_OK);

    CHECK(hr_mcts_init(&x, &b, &p, arena, 1u << 18, 7) == HR_OK);
    CHECK(hr_mcts_init(&y, &b, &p, second, 1u << 18, 7) == HR_OK);
    hr_mcts_run(&x, 4000);
    for (int i = 0; i < 40; i++) /* batches make no difference */
        hr_mcts_run(&y, 100);
    CHECK(same_results(&x, &y));

    CHECK(hr_mcts_init(&y, &b, &p, second, 1u << 18, 8) == HR_OK);
    hr_mcts_run(&y, 4000);
    CHECK(!same_results(&x, &y));
}

/** @brief Counters of the comparisons with the solver. */
typedef struct {
    int won;  /**< Positions won by the player to move. */
    int lost; /**< Positions lost by the player to move. */
} tally;

/**
 * @brief Searches a position until it is proven and compares with the solver.
 *
 * @param b A board of radius 2 or less.
 * @param p A position in progress.
 * @param seed Seed of the search.
 * @param t Counters to update.
 */
static void check_exact(const hr_board *b, const hr_pos *p, uint64_t seed,
                        tally *t)
{
    hr_mcts s;
    int truth = solve(b, p);

    CHECK(hr_mcts_init(&s, b, p, arena, ARENA, seed) == HR_OK);
    hr_mcts_run(&s, 2000000);
    CHECK(hr_mcts_proven(&s) == (hr_player)truth);
    check_node(&s, 0, p, s.used > ARENA - HR_MAX_MOVES, 1);

    hr_mcts_move best = hr_mcts_best(&s);
    hr_pos next = *p;
    CHECK(hr_pos_apply(b, &next, best.action) == HR_OK);
    if (truth == p->turn) { /* the recommended move keeps the win */
        CHECK(solve(b, &next) == truth);
        t->won++;
    } else {
        t->lost++;
    }
}

/** @brief Compares the search with the solver on the board of radius 1. */
static void test_exact_small(void)
{
    hr_board b;
    tally t = {0, 0};
    hr_rng rng;
    hr_rng_seed(&rng, 3);
    CHECK(hr_board_init(&b, 1) == HR_OK);

    printf("radius 1, winner with best play:");
    for (int m = 1; m <= 7; m++) {
        hr_pos p;
        solver_clear();
        CHECK(hr_pos_init(&p, &b, m) == HR_OK);
        printf(" m=%d P%d", m, solve(&b, &p));
        check_exact(&b, &p, (uint64_t)m, &t);
        for (int i = 0; i < 300; i++) {
            random_position(&b, &p, m, (int)hr_rng_below(&rng, 12), &rng);
            if (hr_pos_decided(&p) == HR_NOBODY)
                check_exact(&b, &p, (uint64_t)i, &t);
        }
    }
    printf("\nradius 1: %d won and %d lost positions agree\n", t.won, t.lost);
    CHECK(t.won > 100 && t.lost > 100);
}

/** @brief Compares the search with the solver on endgames of radius 2. */
static void test_exact_endgames(void)
{
    hr_board b;
    tally t = {0, 0};
    hr_rng rng;
    hr_rng_seed(&rng, 4);
    CHECK(hr_board_init(&b, 2) == HR_OK);

    for (int m = 1; m <= 3; m++) {
        solver_clear();
        for (int found = 0; found < 100;) {
            hr_pos p;
            random_position(&b, &p, m, 4 + (int)hr_rng_below(&rng, 30), &rng);
            if (hr_pos_decided(&p) != HR_NOBODY || p.empty_count > 7)
                continue;
            check_exact(&b, &p, (uint64_t)found, &t);
            found++;
        }
    }
    printf("radius 2 endgames: %d won and %d lost positions agree\n", t.won,
           t.lost);
    CHECK(t.won > 30 && t.lost > 30);
}

/** @brief Tests that the search plays far better than random moves. */
static void test_against_random(void)
{
    hr_board b;
    hr_rng rng;
    int wins = 0;
    const int games = 40;
    hr_rng_seed(&rng, 5);
    CHECK(hr_board_init(&b, 2) == HR_OK);

    for (int g = 0; g < games; g++) {
        hr_pos p;
        int searcher = g % 2;
        CHECK(hr_pos_init(&p, &b, 3) == HR_OK);
        while (!hr_pos_is_over(&p)) {
            hr_action a;
            if (p.turn == searcher) {
                hr_mcts s;
                CHECK(hr_mcts_init(&s, &b, &p, arena, ARENA,
                                   hr_rng_next(&rng)) == HR_OK);
                hr_mcts_run(&s, 2000);
                a = hr_mcts_best(&s).action;
            } else {
                CHECK(hr_pos_random_action(&b, &p, &rng, &a));
            }
            CHECK(hr_pos_apply(&b, &p, a) == HR_OK);
        }
        wins += hr_pos_decided(&p) == (hr_player)searcher;
    }
    printf("search against random moves: %d of %d games won\n", wins, games);
    CHECK(wins >= 38);
}

/**
 * @brief Runs all tests.
 *
 * @return 0 on success; a failed check exits with status 1.
 */
int main(void)
{
    test_init();
    test_trees();
    test_decided_root();
    test_reproducible();
    test_exact_small();
    test_exact_endgames();
    test_against_random();
    printf("All search tests passed (%ld checks), "
           "sizeof(hr_mcts_node) = %zu bytes\n",
           checks, sizeof(hr_mcts_node));
    return 0;
}
