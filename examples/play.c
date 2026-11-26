/**
 * @file play.c
 * @brief Example: a two-player game on the command line.
 *
 * Usage: `play [n] [m]`, where `n` is the board radius and `m` the number of
 * pieces per player. The defaults are 2 and 3.
 */
#include "hexreclaim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
        printf("P%d(%c/%c)  手中 %d  已占 %d  锁定 %d\n", p, "ab"[p], "AB"[p],
               hr_in_hand(g, who), hr_claimed(g, who), hr_score(g, who));
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
            printf(" (占%d格)",
                   1 + hr_ray_length(g, moves[i].at, moves[i].dir));
        putchar('\n');
    }
    printf("共 %zu 个合法操作\n", total);
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

    if (hr_init(&game, n, m) != HR_OK) {
        fprintf(stderr, "用法：%s [半径 1..%d] [棋子数 1..%d]\n", argv[0],
                HR_MAX_RADIUS, HR_MAX_PIECES);
        return 1;
    }
    puts("指令：p q r（落子） s q r 方向（扫略：e ne nw w sw se）"
         " u（撤销） l（合法操作） x（退出）");

    for (;;) {
        print_state(&game);
        if (hr_is_over(&game))
            printf("对局结束，P%d 获胜。（u 撤销，x 退出）\n> ",
                   (int)hr_winner(&game));
        else
            printf("P%d 行动 > ", (int)hr_to_move(&game));
        fflush(stdout);

        if (fgets(line, sizeof line, stdin) == NULL)
            break;

        char cmd = '\0';
        hr_move move;
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
        } else {
            puts("无法识别的指令");
        }
        if (status != HR_OK)
            printf("错误：%s\n", hr_status_str(status));
    }
    return 0;
}
