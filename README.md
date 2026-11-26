# hexreclaim

A C99 library that maintains the state of one hex_reclaim game. It depends only on the standard library, performs no dynamic memory allocation, has no mutable global state and does no I/O.

## Building

```sh
make            # build/libhexreclaim.a, the example program and the test program
make test       # run the tests
./build/play 2 3   # command-line game: radius 2, 3 pieces per player
```

You can also drop `include/hexreclaim.h` and `src/hexreclaim.c` straight into your own project. The source files are UTF-8 (they contain Chinese strings); with MSVC, add `/utf-8`.

## Usage

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
| Errors | `hr_status_str` |

See the comments in `include/hexreclaim.h` for the detailed contracts.

## Implementation conventions

The rules follow the design document (2026-11-26 version). The following are choices made at the library level:

- `n` and `m` have not been decided yet, so both are runtime parameters: `1 <= n <= 8`, `1 <= m <= 255`.
- `HR_P0` always moves first. The caller decides who plays as P0.
- When the game ends, cells holding a piece still read as `HR_PIECE_*`; they count toward the piece owner's score.
- `hr_score` = claimed cells + pieces on the board. During the game it is a lower bound on that player's final score; once the game is over it is the final score.
- Even if one player has locked in more than half of the cells, the game continues until no empty cell remains.
- When one piece is blocked in several directions, each of those directions is a legal move, and `hr_legal_moves` lists them separately. They all have the same result, so a search may deduplicate sweeps with `hr_ray_length == 0` by their origin.

## For search programs

- `hr_game` is a plain struct (about 3 KB); `hr_game child = parent;` makes a complete copy.
- You can also search in place with `hr_apply` / `hr_undo`.
- `HR_MAX_MOVES` can be used as the size of a move buffer.
- The history is not part of the position. For a transposition table, derive the key from the contents of each cell and the player to move (the number of pieces in hand follows from the board and `m`).
- Saving a game record: store `n`, `m` and the move sequence read with `hr_history_move`; replaying it restores the game.

## Thread safety

Separate `hr_game` objects are independent of each other. If multiple threads modify the same object, you must do your own locking.

## Tests

`tests/test_hexreclaim.c` contains test cases for each rule, plus differential testing over 4152 random games with radius 1–8: every move is compared against a separately written naive model, and after each game ends, replay and step-by-step undo are checked.
