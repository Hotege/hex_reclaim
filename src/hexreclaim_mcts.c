/**
 * @file hexreclaim_mcts.c
 * @brief Implementation of the Monte Carlo tree search.
 *
 * Nodes hold no positions: each iteration replays the moves from a copy of the
 * root position. The children of a node are allocated together and shuffled, so
 * visiting unvisited children in order tries them in random order.
 */
#include "hexreclaim_mcts.h"

#include <math.h>

/* Fails to compile if the bit fields of a node became too narrow. */
typedef char hr_mcts_fields_fit[(HR_ACTION_LIMIT <= 2048 &&
                                 HR_MAX_MOVES < 2048) ? 1 : -1];

/**
 * @brief Encodes a proven winner for hr_mcts_node::proven.
 *
 * @param winner ::HR_P0 or ::HR_P1.
 * @return The field value.
 */
static unsigned proof_of(int winner)
{
    return 1u + (unsigned)winner;
}

/**
 * @brief Decodes hr_mcts_node::proven.
 *
 * @param proven The field value.
 * @return The proven winner, or ::HR_NOBODY.
 */
static hr_player proven_winner(unsigned proven)
{
    return proven == 0 ? HR_NOBODY : (hr_player)(proven - 1);
}

/**
 * @brief Gives a node its children, if the node array has room for them.
 *
 * @param search The search.
 * @param node   A node without children.
 * @param pos    The position of the node, with a game in progress.
 * @return `true` if the node was expanded.
 */
static bool expand(hr_mcts *search, hr_mcts_node *node, const hr_pos *pos)
{
    hr_action actions[HR_MAX_MOVES];
    size_t count =
        hr_pos_actions(&search->board, pos, actions, HR_MAX_MOVES);

    if (count > search->capacity - search->used)
        return false;

    node->first_child = search->used;
    node->children = (unsigned)count;
    /* Take the actions in random order. */
    for (size_t left = count; left > 0; left--) {
        size_t i = hr_rng_below(&search->rng, (uint32_t)left);
        hr_mcts_node *child = &search->nodes[search->used++];

        child->visits = 0;
        child->wins = 0;
        child->first_child = 0;
        child->action = actions[i];
        child->children = 0;
        child->proven = 0;
        actions[i] = actions[left - 1];
    }
    return true;
}

/**
 * @brief Chooses the child to descend into, by UCB1.
 *
 * An unvisited child comes first. Children proven to lose are skipped.
 *
 * @param search The search.
 * @param node   An expanded node that is not proven.
 * @param mover  The player to move at the node.
 * @return The chosen child.
 */
static hr_mcts_node *select_child(const hr_mcts *search,
                                  const hr_mcts_node *node, int mover)
{
    hr_mcts_node *child = &search->nodes[node->first_child];
    hr_mcts_node *best = child;
    unsigned loss = proof_of(mover ^ 1);
    double log_visits = node->visits > 1 ? log((double)node->visits) : 0.0;
    double best_value = -1.0;

    for (unsigned i = 0; i < node->children; i++) {
        if (child[i].visits == 0)
            return &child[i];
        if (child[i].proven == loss)
            continue;

        double inverse = 1.0 / (double)child[i].visits;
        double value = (double)child[i].wins * inverse +
                       search->exploration * sqrt(log_visits * inverse);
        if (value > best_value) {
            best_value = value;
            best = &child[i];
        }
    }
    return best;
}

/**
 * @brief Proves a node, if possible, after one of its children was proven.
 *
 * @param search The search.
 * @param node   An expanded node that is not proven.
 * @param mover  The player to move at the node.
 * @param proof  The proof of the child just proven.
 */
static void prove_from_child(const hr_mcts *search, hr_mcts_node *node,
                             int mover, unsigned proof)
{
    if (proof == proof_of(mover)) { /* one winning move is enough */
        node->proven = proof;
        return;
    }

    const hr_mcts_node *child = &search->nodes[node->first_child];
    for (unsigned i = 0; i < node->children; i++) {
        if (child[i].proven != proof)
            return; /* some move is not yet known to lose */
    }
    node->proven = proof;
}

/**
 * @brief Runs one iteration: selection, expansion, playout, backpropagation.
 *
 * @param search A search whose root is not proven.
 */
static void iterate(hr_mcts *search)
{
    uint32_t path[HR_MAX_PLIES + 1];
    int depth = 0;
    hr_pos pos = search->root;
    hr_mcts_node *node = &search->nodes[0];
    path[0] = 0;

    while (node->proven == 0 && node->children != 0) {
        node = select_child(search, node, pos.turn);
        hr_pos_apply(&search->board, &pos, (hr_action)node->action);
        path[++depth] = (uint32_t)(node - search->nodes);
    }

    /* A leaf is expanded when it is reached for the second time. */
    if (node->proven == 0 && node->visits > 0 && expand(search, node, &pos)) {
        node = select_child(search, node, pos.turn);
        hr_pos_apply(&search->board, &pos, (hr_action)node->action);
        path[++depth] = (uint32_t)(node - search->nodes);
    }

    if (node->proven == 0) {
        hr_player decided = hr_pos_decided(&pos);
        if (decided != HR_NOBODY)
            node->proven = proof_of(decided);
    }
    int winner = node->proven != 0
                     ? (int)proven_winner(node->proven)
                     : (int)hr_pos_playout(&search->board, &pos, &search->rng,
                                           HR_PLAY_TO_DECIDED);

    for (int d = depth; d >= 0; d--) {
        hr_mcts_node *n = &search->nodes[path[d]];
        /* Turns alternate, so the depth tells who moves at the node. */
        int mover = search->root.turn ^ (d & 1);

        n->visits++;
        n->wins += winner != mover; /* won by who moved into the node */
        if (d < depth && n->proven == 0) {
            unsigned proof = search->nodes[path[d + 1]].proven;
            if (proof != 0)
                prove_from_child(search, n, mover, proof);
        }
    }
}

hr_status hr_mcts_init(hr_mcts *search, const hr_board *board,
                       const hr_pos *pos, hr_mcts_node *nodes, size_t capacity,
                       uint64_t seed)
{
    if (search == NULL || board == NULL || pos == NULL || nodes == NULL ||
        board->radius != pos->radius)
        return HR_ERR_ARG;
    if (hr_pos_is_over(pos))
        return HR_ERR_GAME_OVER;
    if (capacity <= hr_pos_actions(board, pos, NULL, 0))
        return HR_ERR_ARG;

    search->board = *board;
    search->root = *pos;
    search->nodes = nodes;
    search->capacity =
        capacity > UINT32_MAX ? UINT32_MAX : (uint32_t)capacity;
    search->used = 1;
    hr_rng_seed(&search->rng, seed);
    search->exploration = HR_MCTS_EXPLORATION;

    nodes[0].visits = 0;
    nodes[0].wins = 0;
    nodes[0].first_child = 0;
    nodes[0].action = 0;
    nodes[0].children = 0;
    nodes[0].proven = 0;
    /* Cannot fail: the capacity was checked above. */
    expand(search, &nodes[0], &search->root);

    hr_player decided = hr_pos_decided(&search->root);
    if (decided != HR_NOBODY)
        nodes[0].proven = proof_of(decided);
    return HR_OK;
}

uint32_t hr_mcts_run(hr_mcts *search, uint32_t iterations)
{
    const hr_mcts_node *root = &search->nodes[0];
    uint32_t done = 0;

    while (done < iterations && root->proven == 0 &&
           root->visits < UINT32_MAX) {
        iterate(search);
        done++;
    }
    return done;
}

/**
 * @brief Gets the statistics of a child of the root.
 *
 * @param node The child.
 * @return The statistics.
 */
static hr_mcts_move move_of(const hr_mcts_node *node)
{
    hr_mcts_move move;
    move.action = (hr_action)node->action;
    move.visits = node->visits;
    move.wins = node->wins;
    move.proven = proven_winner(node->proven);
    return move;
}

hr_mcts_move hr_mcts_best(const hr_mcts *search)
{
    const hr_mcts_node *root = &search->nodes[0];
    const hr_mcts_node *child = &search->nodes[root->first_child];
    const hr_mcts_node *best = child;
    int best_rank = -1;

    for (unsigned i = 0; i < root->children; i++) {
        /* 2: proven win, 1: open, 0: proven loss. */
        int rank = child[i].proven == 0 ? 1
                   : child[i].proven == proof_of(search->root.turn) ? 2
                                                                    : 0;
        if (rank > best_rank ||
            (rank == best_rank &&
             (child[i].visits > best->visits ||
              (child[i].visits == best->visits &&
               child[i].wins > best->wins)))) {
            best_rank = rank;
            best = &child[i];
        }
    }
    return move_of(best);
}

size_t hr_mcts_moves(const hr_mcts *search, hr_mcts_move *out, size_t cap)
{
    const hr_mcts_node *root = &search->nodes[0];

    for (unsigned i = 0; out != NULL && i < root->children && i < cap; i++)
        out[i] = move_of(&search->nodes[root->first_child + i]);
    return root->children;
}

uint32_t hr_mcts_iterations(const hr_mcts *search)
{
    return search->nodes[0].visits;
}

hr_player hr_mcts_proven(const hr_mcts *search)
{
    return proven_winner(search->nodes[0].proven);
}
