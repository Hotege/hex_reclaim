/**
 * @file play.c
 * @brief Example: a game on the command line, against a person or the computer.
 *
 * Usage: `play [n] [m]`, where `n` is the board radius and `m` the number of
 * pieces per player. The defaults are 2 and 3.
 *
 * Both players enter their moves, or ask the computer to move for them with the
 * command `a`, which runs a Monte Carlo tree search.
 */
#include "hexreclaim_mcts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/** @brief Nodes of the computer's search tree (64 MiB). */
#define SEARCH_NODES (1u << 22)

/** @brief Iterations of a computer move if the command names no number. */
#define DEFAULT_ITERATIONS 100000

/** @brief Largest number of iterations accepted on input. */
#define MAX_ITERATIONS 1000000000L

/** @brief Direction names accepted on input, indexed by ::hr_dir. */
static const char *const DIR_NAMES[HR_DIR_COUNT] = {"e",  "ne", "nw",
                                                    "w",  "sw", "se"};

/**
 * @brief Gets the character that displays a cell.
 *
 * @param cell Cell contents.
 * @return The character.
 */
static char symbol(hr_cell cell)
{
    switch (cell) {
    case HR_EMPTY:      return '.';
    case HR_PIECE_P0:   return 'a';
    case HR_PIECE_P1:   return 'b';
    case HR_CLAIMED_P0: return 'A';
    case HR_CLAIMED_P1: return 'B';
    default:            return ' ';
    }
}

/**
 * @brief Prints the board and the figures of both players.
 *
 * @param g The game.
 */
static void print_state(const hr_game *g)
{
    int n = hr_radius(g);

    putchar('\n');
    for (int r = -n; r <= n; r++) {
        int first = r < 0 ? -n - r : -n;
        int last = r < 0 ? n : n - r;
        printf("r=%2d  %*s", r, abs(r), "");
        for (int q = first; q <= last; q++) {
            hr_coord c = {q, r};
            printf("%c ", symbol(hr_cell_at(g, c)));
        }
        printf("%*s  q: %d..%d\n", abs(r), "", first, last);
    }
    for (int p = HR_P0; p <= HR_P1; p++) {
        hr_player who = (hr_player)p;
        printf("P%d(%c/%c)  in hand %d  claimed %d  locked %d\n", p,
               "ab"[p], "AB"[p], hr_in_hand(g, who), hr_claimed(g, who),
               hr_score(g, who));
    }
}

/**
 * @brief Prints a move in the input syntax.
 *
 * @param m The move.
 */
static void print_move(hr_move m)
{
    if (m.type == HR_PLACE)
        printf("p %d %d", m.at.q, m.at.r);
    else
        printf("s %d %d %s", m.at.q, m.at.r, DIR_NAMES[m.dir]);
}

/**
 * @brief Prints all legal moves and their number.
 *
 * @param g The game.
 */
static void print_legal(const hr_game *g)
{
    static hr_move moves[HR_MAX_MOVES];
    size_t total = hr_legal_moves(g, moves, HR_MAX_MOVES);

    for (size_t i = 0; i < total; i++) {
        print_move(moves[i]);
        if (moves[i].type == HR_SWEEP)
            printf(" (claims %d)",
                   1 + hr_ray_length(g, moves[i].at, moves[i].dir));
        putchar('\n');
    }
    printf("legal moves: %zu\n", total);
}

/**
 * @brief Parses a line holding a placement or sweep command.
 *
 * @param line The input line.
 * @param[out] out Receives the move.
 * @return 1 on success, 0 if the line is malformed.
 */
static int parse_move(const char *line, hr_move *out)
{
    int q, r;
    char name[3], extra;

    if (sscanf(line, " p %d %d %c", &q, &r, &extra) == 2) {
        *out = hr_place(q, r);
        return 1;
    }
    if (sscanf(line, " s %d %d %2s %c", &q, &r, name, &extra) == 3) {
        for (int d = 0; d < HR_DIR_COUNT; d++) {
            if (strcmp(name, DIR_NAMES[d]) == 0) {
                *out = hr_sweep(q, r, (hr_dir)d);
                return 1;
            }
        }
    }
    return 0;
}

/**
 * @brief Parses a line holding a computer-move command.
 *
 * @param line The input line.
 * @param[out] iterations Receives the number of iterations to search.
 * @return 1 on success, 0 if the line is malformed.
 */
static int parse_search(const char *line, uint32_t *iterations)
{
    long count;
    char extra;
    int end = 0;

    sscanf(line, " a %n", &end);
    if (end > 0 && line[end] == '\0') {
        *iterations = DEFAULT_ITERATIONS;
        return 1;
    }
    if (sscanf(line, " a %ld %c", &count, &extra) == 1 && count >= 1 &&
        count <= MAX_ITERATIONS) {
        *iterations = (uint32_t)count;
        return 1;
    }
    return 0;
}

/**
 * @brief Searches for a move and plays it for the player to move.
 *
 * @param g          The game.
 * @param iterations Number of search iterations.
 * @param rng        Source of the seeds of the searches.
 * @return ::HR_OK, or ::HR_ERR_GAME_OVER if the game has ended.
 */
static hr_status computer_move(hr_game *g, uint32_t iterations, hr_rng *rng)
{
    static hr_mcts_node nodes[SEARCH_NODES];
    static hr_mcts search;
    const hr_board *board = hr_game_board(g);
    const hr_pos *pos = hr_game_position(g);
    int mover = (int)hr_to_move(g);
    hr_player decided = hr_pos_decided(pos);

    hr_status status = hr_mcts_init(&search, board, pos, nodes, SEARCH_NODES,
                                    hr_rng_next(rng));
    if (status != HR_OK)
        return status;
    hr_mcts_run(&search, iterations);
    hr_mcts_move best = hr_mcts_best(&search);

    printf("P%d plays: ", mover);
    print_move(hr_action_decode(board, best.action));
    if (decided != HR_NOBODY)
        printf("  (P%d already holds more than half of the cells)\n",
               (int)decided);
    else if (hr_mcts_proven(&search) != HR_NOBODY)
        printf("  (proven: P%d wins with best play; %u iterations)\n",
               (int)hr_mcts_proven(&search),
               (unsigned)hr_mcts_iterations(&search));
    else
        printf("  (win rate of P%d %.1f%%, from %u of %u iterations)\n",
               mover, 100.0 * best.wins / best.visits, (unsigned)best.visits,
               (unsigned)hr_mcts_iterations(&search));
    return hr_apply(g, hr_action_decode(board, best.action));
}

/**
 * @brief Runs the interactive game loop.
 *
 * @param argc Number of command-line arguments.
 * @param argv Command-line arguments: radius and pieces, both optional.
 * @return 0 on normal exit, 1 on invalid arguments.
 */
int main(int argc, char **argv)
{
    static hr_game game;
    int n = argc > 1 ? atoi(argv[1]) : 2;
    int m = argc > 2 ? atoi(argv[2]) : 3;
    char line[128];
    hr_rng rng;

    if (hr_init(&game, n, m) != HR_OK) {
        fprintf(stderr, "Usage: %s [radius 1..%d] [pieces 1..%d]\n", argv[0],
                HR_MAX_RADIUS, HR_MAX_PIECES);
        return 1;
    }
    hr_rng_seed(&rng, (uint64_t)time(NULL));
    puts("Commands: p q r (place), s q r dir (sweep; dir: e ne nw w sw se),\n"
         "          a [iterations] (computer moves), u (undo),\n"
         "          l (legal moves), x (quit)");

    for (;;) {
        print_state(&game);
        if (hr_is_over(&game))
            printf("Game over, P%d wins. (u: undo, x: quit)\n> ",
                   (int)hr_winner(&game));
        else
            printf("P%d to move > ", (int)hr_to_move(&game));
        fflush(stdout);

        if (fgets(line, sizeof line, stdin) == NULL)
            break;

        char cmd = '\0';
        hr_move move;
        uint32_t iterations;
        hr_status status = HR_OK;
        sscanf(line, " %c", &cmd);

        if (cmd == 'x') {
            break;
        } else if (cmd == 'u') {
            status = hr_undo(&game);
        } else if (cmd == 'l') {
            print_legal(&game);
        } else if (parse_move(line, &move)) {
            status = hr_apply(&game, move);
        } else if (parse_search(line, &iterations)) {
            status = computer_move(&game, iterations, &rng);
        } else {
            puts("Unrecognized command");
        }
        if (status != HR_OK)
            printf("Error: %s\n", hr_status_str(status));
    }
    return 0;
}
