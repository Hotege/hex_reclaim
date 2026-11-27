# hexreclaim

A C99 library that maintains the state of hex_reclaim games and simulates them. It depends only on the standard library, performs no dynamic memory allocation, has no mutable global state and does no I/O.

## Building

```sh
make            # build/libhexreclaim.a, the example programs and the test programs
make test       # run the tests
./build/play 2 3         # command-line game: radius 2, 3 pieces per player
./build/mc 2 3 20000     # Monte Carlo ranking of the first moves, with playout speed
```

You can also drop `include/hexreclaim.h` and `src/hexreclaim.c` straight into your own project.

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
| Errors | `hr_status_str` |

See the comments in `include/hexreclaim.h` for the detailed contracts.

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

Separate `hr_game` and `hr_pos` objects are independent of each other, and any number of threads may share one `hr_board`. Give each thread its own positions and its own `hr_rng` with its own seed. If multiple threads modify the same object, you must do your own locking.

## Tests

- `tests/test_hexreclaim.c` contains test cases for each rule, plus differential testing over 4152 random games with radius 1–8: every move is compared against a separately written naive model, and after each game ends, replay and step-by-step undo are checked.
- `tests/test_sim.c` tests the simulation layer: the cell sets stay consistent, actions lead to exactly the positions the legal moves lead to, sampled moves follow the documented distribution, and playouts agree with move-by-move play in both stopping modes.
