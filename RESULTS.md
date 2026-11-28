# Results

What the tree search and the random playouts of this library have shown about hex_reclaim so far. The measurements were made on 2026-11-27 and 2026-11-28, with GCC `-O2` on one thread of a 2-core cloud Xeon.

`n` is the radius of the board and `m` the number of pieces per player. P0 moves first. There are two kinds of results:

- **Proven:** the search established who wins with best play.
- **Measured:** how often a side won games played by programs. This does not show who wins with best play.

## Summary

- P0 wins with best play on radius 1 (`m` = 1 to 7) and on radius 2 with `m` = 1 and `m` = 2.
- From the empty board, the search has proven nothing for radius 2 with 3 or more pieces, nor for any larger board.
- P0 won more than half of the games in every setting measured, but how often depends strongly on how well the sides play.
- The results do not show which `n` and `m` to choose, or what compensation P1 should get.

## Proven results

| Board | Position searched | Winner with best play |
|---|---|---|
| n=1, m=1 to 7 | empty board | P0 |
| n=2, m=1 | empty board | P0 |
| n=2, m=1 | after P0 places in the center or in a corner | P1 |
| n=2, m=1 | after P0 places next to the center or in the middle of an edge | P0 |
| n=2, m=2 | empty board | P0; placing at (1, 0), next to the center, wins |

All of these agree with a separate exhaustive search, which is not part of this repository. In addition, `tests/test_mcts.c` compares proofs with its own exhaustive solver on 2107 positions of radius 1 and 300 endgames of radius 2, and they all agree.

### Effort on radius 2

| Position | Without transposition table | With transposition table |
|---|---|---|
| m=1, empty board | 3.80 million iterations, 4 s | 1.49 million, 2 s |
| m=1, after center | 0.27 million, 0.2 s | 0.17 million, 0.2 s |
| m=1, after next to center | 9.44 million, 11 s | 3.34 million, 5 s |
| m=1, after corner | 0.20 million, 0.2 s | 0.09 million, 0.1 s |
| m=1, after edge middle | 3.68 million, 3 s | 1.50 million, 2 s |
| m=2, empty board | not proven: node array full after 118 million, 239 s | 35.6 million (67.4 million nodes), 86 s |

### Attempts without a proof

All from the empty board of radius 2:

- `m` = 3 with a table: node array of 2^28 nodes full after 131 million iterations (394 s).
- `m` = 3 and `m` = 4 without a table: not proven in 110 s with up to 2^27 nodes.
- `m` = 4 with a table, and `m` of 5 or more, were not tried.

## Win rate of the first player

Share of the games won by P0, with 95% intervals.

| Board | Uniformly random moves | Search, 2,000 iterations per move | Search, 20,000, run A | Search, 20,000, run B |
|---|---|---|---|---|
| n=2, m=3 | 60.1% | 62 ± 3% | 99 ± 1% | 98.5 ± 1% |
| n=3, m=3 | – | 77 ± 3% | 74 ± 6% | 71 ± 4% |
| n=3, m=6 | – | 75 ± 3% | 76 ± 6% | 77 ± 4% |
| n=4, m=6 | – | 72 ± 3% | 73 ± 6% | 69 ± 5% |
| Games per entry | 20 million | 1,000 | 200 | 400 |

A dash means that no figure was recorded. In run A both sides used the same search. In run B and in the games with 2,000 iterations, one side used a transposition table and the other did not; no difference in strength between the two was found (see below).

- **P0 is ahead in every setting.**
- **The rate depends on playing strength.** According to the exhaustive search, P0 wins n=2, m=3 with best play, yet random moves and 2,000 iterations give only about 60%. The rates for radius 3 and 4 therefore do not tell how large the advantage of P0 is with best play, and they may change with larger budgets.
- **No clear effect of `m`.** On radius 3 the differences between `m` = 3 and `m` = 6 are small and do not point the same way in all runs.

## Open questions

- **Choice of `n` and `m`.** No conclusion yet.
- **Compensation for P1.** No data: the search only learns who wins, not by how many cells. The exhaustive search reports that on radius 2 with `m` of 2 or more, P0 wins after every first move, by exactly one cell with best play. There, a compensation of one cell would make the game a draw and two cells would make P1 win. These computations say nothing about this for larger boards.

## Strength of the search

Flat Monte Carlo, used for comparison, divides the same number of random playouts evenly among the moves of the current position and picks the move with the highest win rate. The table gives the win rate of the side named first.

| Match | Games | n=2, m=3 | n=3, m=6 | n=4, m=6 |
|---|---|---|---|---|
| Search 2,000 against flat Monte Carlo 2,000 | 400 | 86% | 89% | 82% |
| Search 20,000 against flat Monte Carlo 20,000 | 200 | 99.5% | 99% | 100% |
| Search 20,000 against search 2,000 | 200 | 86% | 91.5% | 97.5% |

- Against uniformly random moves, 2,000 iterations per move won every game: 200 games on n=3, m=6 and 100 on n=5, m=12.
- Strength still rises clearly from 2,000 to 20,000 iterations.
- **Exploration constant.** The default 0.35 was chosen from matches between values from 0.05 to 2.0. At 2,000 iterations, 0.2 was about as good (48 to 57% against 0.35) and the other values were weaker. At 20,000 iterations, 0.35 beat 0.1, 0.2, 0.6 and 1.0, except on n=2, m=3, where 0.2 and 0.6 were about level with it. The range from 0.2 to 0.6 was not examined more finely, and boards of radius 6 or more and larger budgets were not tested.
- **Transposition table.** At equal numbers of iterations (2,000 and 20,000) a search with a table scored 47 to 51% against one without, and its iterations mostly ran 1 to 19% slower. It does make proofs cheaper: over sets of radius-2 positions, the total iterations fell by factors of up to 3.7 and the total time by factors of up to 2.9.

## Method

- **Matches.** The sides take turns moving first. Every move starts a new search from the current position, with an array of 2^22 nodes and, for a side using a table, 2^20 entries. The move played is the one `hr_mcts_best` returns. A game ends once a side has locked in more than half of the cells. The exploration constant is 0.35 unless stated otherwise.
- **Intervals** are binomial, treating the games as independent.
- **Proofs on radius 2.** Node arrays of 2^27 nodes for `m` = 1 and 2^28 for the two `m` = 2 runs; tables of 2^25 entries; seed 99 for `m` = 1 and 12345 otherwise. With the same sizes and seed, the iteration counts repeat exactly.
- **Repeated.** All the successful proofs reported above, and the 20 million random games, were run again on 2026-11-28 with the current source.
- The programs that played the matches and timed the proofs are not part of this repository.
