/**
 * @file mc.c
 * @brief Example: flat Monte Carlo evaluation of the opening position.
 *
 * Usage: `mc [n] [m] [playouts] [seed]`. Every distinct first move is followed
 * by the given number of random playouts; the best moves for P0 and the playout
 * speed are printed. The defaults are 2, 3, 20000 and 1.
 */
#include "hexreclaim.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/** @brief Number of moves printed. */
#define SHOWN 5

/** @brief Direction names, indexed by ::hr_dir. */
static const char *const DIR_NAMES[HR_DIR_COUNT] = {"e",  "ne", "nw",
                                                    "w",  "sw", "se"};

/** @brief Result of the playouts that followed one action. */
typedef struct {
    hr_action action; /**< The action evaluated. */
    long wins;        /**< Playouts won by the player who made it. */
} result;

/**
 * @brief Orders results by decreasing number of wins, for qsort().
 *
 * @param a One result.
 * @param b The other.
 * @return Negative, zero or positive as qsort() expects.
 */
static int by_wins(const void *a, const void *b)
{
    const result *x = a, *y = b;
    if (x->wins != y->wins)
        return x->wins > y->wins ? -1 : 1;
    return (int)x->action - (int)y->action;
}

/**
 * @brief Evaluates the opening moves and prints the best ones.
 *
 * @param argc Number of command-line arguments.
 * @param argv Command-line arguments, all optional.
 * @return 0 on success, 1 on invalid arguments.
 */
int main(int argc, char **argv)
{
    static hr_board board;
    static hr_action actions[HR_MAX_MOVES];
    static result results[HR_MAX_MOVES];
    int n = argc > 1 ? atoi(argv[1]) : 2;
    int m = argc > 2 ? atoi(argv[2]) : 3;
    long playouts = argc > 3 ? atol(argv[3]) : 20000;
    uint64_t seed = argc > 4 ? strtoull(argv[4], NULL, 10) : 1;
    hr_pos root;
    hr_rng rng;

    if (playouts < 1 || hr_board_init(&board, n) != HR_OK ||
        hr_pos_init(&root, &board, m) != HR_OK) {
        fprintf(stderr,
                "Usage: %s [radius 1..%d] [pieces 1..%d] [playouts] [seed]\n",
                argv[0], HR_MAX_RADIUS, HR_MAX_PIECES);
        return 1;
    }
    hr_rng_seed(&rng, seed);

    hr_player mover = hr_pos_to_move(&root);
    size_t count = hr_pos_actions(&board, &root, actions, HR_MAX_MOVES);
    clock_t start = clock();

    for (size_t i = 0; i < count; i++) {
        hr_pos child = root;
        hr_pos_apply(&board, &child, actions[i]);
        results[i].action = actions[i];
        results[i].wins = 0;
        for (long k = 0; k < playouts; k++) {
            hr_pos p = child; /* a plain copy starts each simulation */
            if (hr_pos_playout(&board, &p, &rng, HR_PLAY_TO_DECIDED) == mover)
                results[i].wins++;
        }
    }

    double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
    qsort(results, count, sizeof results[0], by_wins);

    for (size_t i = 0; i < count && i < SHOWN; i++) {
        hr_move move = hr_action_decode(&board, results[i].action);
        if (move.type == HR_PLACE)
            printf("p %2d %2d    ", move.at.q, move.at.r);
        else
            printf("s %2d %2d %-2s ", move.at.q, move.at.r,
                   DIR_NAMES[move.dir]);
        printf("win rate %.2f%%\n",
               100.0 * (double)results[i].wins / (double)playouts);
    }

    double total = (double)count * (double)playouts;
    printf("%.0f playouts", total);
    if (seconds > 0)
        printf(" in %.2f s (%.0f per second)", seconds, total / seconds);
    putchar('\n');
    return 0;
}
