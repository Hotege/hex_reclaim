# hexreclaim

A C99 library that maintains the state of hex_reclaim games, simulates them and searches for moves. It depends only on the standard library (the tree search also needs the math library, `-lm`), performs no dynamic memory allocation, has no mutable global state and does no I/O.

## Building

```sh
make            # build/libhexreclaim.a, the example programs and the test programs
make test       # run the tests
./build/play 2 3         # command-line game: radius 2, 3 pieces per player; "a" = computer moves
./build/mc 2 3 20000     # Monte Carlo ranking of the first moves, with playout speed
```

You can also drop `include/hexreclaim.h` and `src/hexreclaim.c` straight into your own project. For the tree search, add `include/hexreclaim_mcts.h` and `src/hexreclaim_mcts.c`, and link with `-lm`.

## Two layers

| Type | What it is | Use it for |
|---|---|---|
| `hr_game` | Board, position and move history (about 3 KB) | Playing, recording, replaying, undoing |
| `hr_board` + `hr_pos` | Fixed geometry of one radius, and a 168-byte position without history | Simulation and tree search |

`hr_game` is built on the other two, so the rules are implemented once.

## Playing a game

```c
#include <stdio.h>
#include "hexreclaim.h"

int main(void)
{
    hr_game g;
    if (hr_init(&g, 2, 3) != HR_OK)        /* radius n=2, m=3 pieces per player */
        return 1;

    hr_apply(&g, hr_place(-2, 0));         /* P0 places a piece */
    hr_apply(&g, hr_place(1, 0));          /* P1 places a piece */

    hr_status s = hr_apply(&g, hr_sweep(-2, 0, HR_E));  /* P0 sweeps east */
    if (s != HR_OK)
        printf("%s\n", hr_status_str(s));

    printf("P0 has claimed %d cells\n", hr_claimed(&g, HR_P0));   /* 3 */
    hr_undo(&g);                            /* undo the sweep */
    return 0;
}
```

## Simulation

```c
#include <stdio.h>
#include "hexreclaim.h"

int main(void)
{
    hr_game g;
    if (hr_init(&g, 3, 3) != HR_OK)
        return 1;

    const hr_board *board = hr_game_board(&g); /* only read: shareable */
    hr_pos root = *hr_game_position(&g);       /* a plain value */
    hr_rng rng;
    hr_rng_seed(&rng, 1);                      /* one generator per thread */

    int wins = 0;
    for (int i = 0; i < 10000; i++) {
        hr_pos p = root;                       /* copying starts a new playout */
        wins += hr_pos_playout(board, &p, &rng, HR_PLAY_TO_DECIDED) == HR_P0;
    }
    printf("P0 wins %d of 10000 random games\n", wins);

    /* Expand a search node, then play one of the actions in the real game. */
    hr_action actions[HR_MAX_MOVES];
    size_t count = hr_pos_actions(board, &root, actions, HR_MAX_MOVES);
    if (count > 0)
        hr_apply(&g, hr_action_decode(board, actions[0]));
    return 0;
}
```

Without a game, set up the two objects directly with `hr_board_init` and `hr_pos_init`.

- **Cells and actions.** Cells are numbered by increasing `r`, then `q`. An `hr_action` is a `uint16_t` holding `cell * 7 + k` (`k` = 0 places, `1 + dir` sweeps); every action is below `HR_ACTION_LIMIT`. `hr_action_encode` and `hr_action_decode` convert to and from `hr_move`.
- **Playout policy.** `hr_pos_random_action` and `hr_pos_playout` make every legal move equally likely.
- **Waiting moves.** Sweeps of one piece that are blocked at the first cell have the same result. `hr_pos_actions` lists one of them, so a search tree gets no duplicate children, and `hr_pos_random_action` reports that same one.
- **Stopping early.** `HR_PLAY_TO_DECIDED` stops once a player has locked in more than half of the cells. Use `HR_PLAY_TO_END` if the final scores matter.
- **Transpositions.** A position holds no history and no padding, so equal positions are equal byte for byte and can be compared or hashed as bytes.
- **Custom policies.** Callers may read the fields of `hr_board` and `hr_pos` (`neighbor`, the cell sets, the counters) together with `hr_set_has` and `hr_pos_ray_length`.
- **No undo.** Keep a copy of the position instead.

## Tree search

`hexreclaim_mcts.h` provides a Monte Carlo tree search: UCT with uniformly random playouts. A position whose winner is certain is marked as proven, and proofs are propagated towards the root (MCTS-Solver), so small boards and endgames can be solved exactly.

```c
#include <stdio.h>
#include "hexreclaim_mcts.h"

#define NODES (1u << 20)                       /* 16 MiB */

int main(void)
{
    static hr_mcts_node nodes[NODES];          /* the tree, owned by the caller */
    static hr_mcts search;
    hr_game g;
    if (hr_init(&g, 3, 3) != HR_OK)
        return 1;

    const hr_board *board = hr_game_board(&g);
    if (hr_mcts_init(&search, board, hr_game_position(&g), nodes, NODES,
                     1 /* seed */) != HR_OK)
        return 1;
    hr_mcts_run(&search, 20000);               /* iterations */

    hr_mcts_move best = hr_mcts_best(&search);
    printf("win rate %.2f over %u visits\n",
           (double)best.wins / best.visits, (unsigned)best.visits);
    hr_apply(&g, hr_action_decode(board, best.action));
    return 0;
}
```

- **Memory.** The tree lives in the node array given to `hr_mcts_init`, 16 bytes per node. Expanding a node adds one child per distinct action; searching from the start of a game with radius 2 to 5 takes about 3 to 15 nodes per iteration. When the array is full, the search goes on without growing the tree. `HR_MCTS_MIN_NODES` is large enough for any position, but a useful search wants far more.
- **Time limits.** `hr_mcts_run` may be called repeatedly: run small batches and check the clock in between.
- **Proven results.** `hr_mcts_proven` returns the winner with best play once it is known, and the search then stops. The `proven` field of `hr_mcts_move` says the same about a single move.
- **All moves.** `hr_mcts_moves` reports visits and wins of every distinct move of the searched position.
- **Threads.** One search runs on one thread. To use several, give each thread its own search, node array and seed, then add up the visits of each action, preferring any move that some search has proven to win.
- **After a move.** Initialize a new search from the new position; trees are not reused.
- **Tuning.** The field `exploration` (default `HR_MCTS_EXPLORATION`, 0.35) may be changed after initialization.

## Coordinates and directions

Cells use axial coordinates `(q, r)`, with the center at `(0, 0)`. Cells on the board satisfy `|q|, |r|, |q+r| <= n`.

```
        NW (0,-1)    NE (+1,-1)
     W (-1,0)    (q,r)    E (+1,0)
        SW (-1,+1)   SE (0,+1)
```

Iterating over the whole board:

```c
int n = hr_radius(&g);
for (int r = -n; r <= n; r++)
    for (int q = -n; q <= n; q++) {
        hr_coord c = {q, r};
        if (hr_on_board(&g, c)) {
            hr_cell cell = hr_cell_at(&g, c);
            /* ... */
        }
    }
```

## API overview

| Purpose | Functions |
|---|---|
| New game | `hr_init` |
| Board | `hr_radius` `hr_cell_count` `hr_on_board` `hr_cell_at` `hr_cell_owner` `hr_empty_count` |
| Game status | `hr_to_move` `hr_is_over` `hr_winner` `hr_pieces_per_player` |
| Per-player data | `hr_in_hand` `hr_pieces_on_board` `hr_claimed` `hr_score` |
| Moves | `hr_place` / `hr_sweep` (constructors), `hr_check` `hr_apply` `hr_undo` |
| Analysis | `hr_legal_moves` `hr_ray_length` `hr_step` |
| History | `hr_ply` `hr_history_move` |
| Board and position | `hr_board_init` `hr_pos_init` `hr_game_board` `hr_game_position` `hr_board_index` |
| Position queries | `hr_pos_to_move` `hr_pos_is_over` `hr_pos_score` `hr_pos_decided` `hr_pos_cell` `hr_pos_ray_length` `hr_set_has` |
| Actions | `hr_action_place` `hr_action_sweep` `hr_action_cell` `hr_action_type` `hr_action_dir` `hr_action_encode` `hr_action_decode` |
| Simulation | `hr_pos_apply` `hr_pos_actions` `hr_pos_random_action` `hr_pos_playout` |
| Random numbers | `hr_rng_seed` `hr_rng_next` `hr_rng_below` |
| Tree search | `hr_mcts_init` `hr_mcts_run` `hr_mcts_best` `hr_mcts_moves` `hr_mcts_iterations` `hr_mcts_proven` |
| Errors | `hr_status_str` |

See the comments in the headers under `include/` for the detailed contracts.

## Implementation conventions

The rules follow the design document (2026-11-26 version). The following are choices made at the library level:

- `n` and `m` have not been decided yet, so both are runtime parameters: `1 <= n <= 8`, `1 <= m <= 255`.
- `HR_P0` always moves first. The caller decides who plays as P0.
- When the game ends, cells holding a piece still read as `HR_PIECE_*`; they count toward the piece owner's score.
- `hr_score` = claimed cells + pieces on the board. During the game it is a lower bound on that player's final score; once the game is over it is the final score.
- Even if one player has locked in more than half of the cells, the game continues until no empty cell remains. Only `HR_PLAY_TO_DECIDED` playouts stop there.
- When one piece is blocked in several directions, each of those directions is a legal move, and `hr_legal_moves` lists them separately.
- Cell sets are bit sets of four 64-bit words. Boards of radius 4 or less (up to 61 cells) use only the first word and simulate fastest.
- GCC and Clang builtins are used where available; define `HR_NO_BUILTINS` to build with portable C only.

## Thread safety

Separate `hr_game` and `hr_pos` objects are independent of each other, and any number of threads may share one `hr_board`. Give each thread its own positions and its own `hr_rng` with its own seed, and each search its own node array. If multiple threads modify the same object, you must do your own locking.

## Tests

- `tests/test_hexreclaim.c` contains test cases for each rule, plus differential testing over 4152 random games with radius 1–8: every move is compared against a separately written naive model, and after each game ends, replay and step-by-step undo are checked.
- `tests/test_sim.c` tests the simulation layer: the cell sets stay consistent, actions lead to exactly the positions the legal moves lead to, sampled moves follow the documented distribution, and playouts agree with move-by-move play in both stopping modes.
- `tests/test_mcts.c` tests the tree search: visit counts, win counts and proofs are consistent throughout the tree; proven results agree with an exhaustive solver on the radius-1 board and on radius-2 endgames; searches are reproducible; and the search beats random play.
