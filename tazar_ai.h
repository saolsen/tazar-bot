#ifndef TAZAR_AI_H
#define TAZAR_AI_H

#include "tazar_game.h"

Command ai_select_command_heuristic(Game *game, Command *commands, size_t num_commands);

typedef struct {
    double *scores;
    int *passes;
    int i;
} MCState;

MCState ai_mc_state_init(Game *game, Command *commands, int num_commands);

void ai_mc_state_cleanup(MCState *state);

void ai_mc_think(MCState *state, Game *game, Command *commands, int num_commands, int iterations);

Command ai_mc_select_command(MCState *state, Game *game, Command *commands, int num_commands);

typedef enum {
    NODE_NONE,
    NODE_DECISION,
    NODE_CHANCE,
    NODE_OVER,
} NodeKind;

typedef struct {
    NodeKind kind;
    Game game;
    Command command;
    uint32_t parent_i;
    uint32_t first_child_i;
    uint32_t num_children;
    uint32_t num_children_to_expand;
    uint32_t visits;
    double total_reward;
    double probability;
} Node;

typedef struct {
    uint32_t root;
    Node *nodes;
    uintptr_t nodes_len;
    uintptr_t nodes_cap;
} MCTSState;

MCTSState ai_mcts_state_init(Game *game, Command *commands, int num_commands);

void ai_mcts_state_cleanup(MCTSState *state);

void ai_mcts_think(MCTSState *state, Game *game, Command *commands, int num_commands,
                   int iterations);

Command ai_mcts_select_command(MCTSState *state, Game *game, Command *commands, int num_commands);

int ai_test(void);
int ui_main(void);

#endif // TAZAR_AI_H
