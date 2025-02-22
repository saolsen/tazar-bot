#ifndef TAZAR_AI_MCTS_H
#define TAZAR_AI_MCTS_H

#include "tazar_game.h"

typedef enum {
    NODE_NONE,
    NODE_DECISION,
    NODE_CHANCE,
    NODE_OVER,
} NodeKind;

typedef struct {
    NodeKind kind;
    uint32_t parent_i;
    uint32_t first_child_i;
    uint32_t num_children;
    uint32_t num_children_to_expand;
    uint32_t visits;
    Command command;
    VolleyResult volley_result;
    Player scored_player;
    double total_reward;
    double probability;
} Node;

typedef struct {
    uint32_t root;
    Node *nodes;
    uintptr_t nodes_len;
    uintptr_t nodes_cap;
} MCTSState;

MCTSState ai_mcts_state_init(MCTSState *prev_state, Game *game, Command *commands, int num_commands);
void ai_mcts_state_cleanup(MCTSState *state);
void ai_mcts_think(MCTSState *state, Game *game, Command *commands, int num_commands,
                   int iterations);
Command ai_mcts_select_command(MCTSState *state, Game *game, Command *commands, int num_commands);

#endif
