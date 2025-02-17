#include "tazar_ai.h"
#include "tazar_game.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// Heuristic versions of a policy and value which guide the non RL versions of the AI.
// Returns a value in range of -46 to 46.
double heuristic_value(Game *game, Player player) {
    Player opponent = (player == PLAYER_RED ? PLAYER_BLUE : PLAYER_RED);
    int player_gold = game->gold[player];
    int player_value = 0;
    int opponent_gold = game->gold[opponent];
    int opponent_value = 0;

    int max_gold = 1 * 6 + 5 * 1 + 2 * 3 + 3 * 2;

    // Iterate over all positions on the board.
    for (u32 i = 0; i < 64; i++) {
        Piece *p = &(game->pieces[i]);
        if (p->kind == PIECE_NONE) {
            continue;
        }
        if (p->player == player) {
            player_value += piece_gold(p->kind);
        } else if (p->player == opponent) {
            opponent_value += piece_gold(p->kind);
        }
    }

    // Full value for winning dispite number of pieces left.
    if (game->status == STATUS_OVER) {
        if (game->winner == player) {
            player_gold = max_gold;
            player_value = max_gold;
            opponent_gold = 0;
            opponent_value = 0;
        } else {
            opponent_gold = max_gold;
            opponent_value = max_gold;
            player_gold = 0;
            player_value = 0;
        }
    }

    double eval = (player_gold + player_value) - (opponent_gold + opponent_value);
    return eval;
}

// probability distribution over commands that could be chosen.
void heuristic_policy(double *weights, Game *game, Command *commands, size_t num_commands) {
    double temperature = 2;
    double current_value = heuristic_value(game, game->turn.player);

    double total_weight = 0.0;

    Game *new_game = game_alloc();
    for (size_t i = 0; i < num_commands; i++) {
        game_clone(new_game, game);
        game_apply_command(new_game, new_game->turn.player, commands[i], VOLLEY_HIT);
        double new_value = heuristic_value(new_game, game->turn.player);
        double delta = new_value - current_value;
        double weight = exp(delta / temperature);
        weights[i] = weight;
        total_weight += weight;
    }
    game_free(new_game);

    for (size_t i = 0; i < num_commands; i++) {
        weights[i] /= total_weight;
    }
}

Command ai_select_command_heuristic(Game *game, Command *commands, size_t num_commands) {
    double *weights = malloc((size_t)num_commands * sizeof(*weights));
    if (weights == NULL) {
        fprintf(stderr, "Failed to allocate memory for weights\n");
        exit(1);
    }
    heuristic_policy(weights, game, commands, num_commands);

    double r = random_prob();
    Command picked_command = commands[0];
    for (size_t i = 1; i < num_commands; i++) {
        r -= weights[i];
        if (r <= 0) {
            picked_command = commands[i];
            break;
        }
    }

    free(weights);

    return picked_command;
}

double ai_rollout(Game *game, Command command, int depth) {
    Player scoring_player = game->turn.player;
    game_apply_command(game, game->turn.player, command, VOLLEY_ROLL);

    Command *commands = malloc(1024 * sizeof(*commands));
    CommandBuf command_buf = {
        .commands = commands,
        .count = 0,
        .cap = 1024,
    };

    while (game->status == STATUS_IN_PROGRESS && depth > 0) {
        game_valid_commands(&command_buf, game);
        Command next_command =
            ai_select_command_heuristic(game, command_buf.commands, command_buf.count);

        game_apply_command(game, game->turn.player, next_command, VOLLEY_ROLL);
        depth--;
    }

    double score = heuristic_value(game, scoring_player);

    free(commands);
    return score;
}

void push_num(double **buf, uintptr_t *len, uintptr_t *cap, double n) {
    // allocate more free space if needed
    if (*len >= *cap) {
        if (*cap == 0) {
            *cap = 1024;
        } else {
            *cap *= 2;
        }
        *buf = realloc(*buf, *cap * sizeof(**buf));
    }
    (*buf)[*len] = n;
    *len += 1;
}

MCState ai_mc_state_init(Game *game, Command *commands, int num_commands) {
    double *scores = malloc((size_t)num_commands * sizeof(*scores));
    int *passes = malloc((size_t)num_commands * sizeof(*passes));

    for (int i = 0; i < num_commands; i++) {
        scores[i] = 0;
        passes[i] = 0;
    }

    MCState state = {
        .scores = scores,
        .passes = passes,
        .i = 0,
    };
    return state;
}

void ai_mc_state_cleanup(MCState *state) {
    free(state->scores);
    free(state->passes);
}

void ai_mc_think(MCState *state, Game *game, Command *commands, int num_commands, int iterations) {
    double *scores = state->scores;
    int *passes = state->passes;

    Game *rollout_game = game_alloc();
    for (int iteration = 0; iteration < iterations; iteration++) {
        game_clone(rollout_game, game);
        double score = ai_rollout(rollout_game, commands[state->i], 299);
        double adjusted_score = (score + 46) / (46 * 2);
        scores[state->i] += adjusted_score;
        passes[state->i] += 1;
        state->i = (state->i + 1) % num_commands;
    }
    game_free(rollout_game);
}

Command ai_mc_select_command(MCState *state, Game *game, Command *commands, int num_commands) {
    double *scores = state->scores;
    int *passes = state->passes;

    double max_score = -INFINITY;
    int max_score_i = 0;
    for (int i = 0; i < num_commands; i++) {
        double s = scores[i] / passes[i];
        if (s > max_score) {
            max_score = s;
            max_score_i = i;
        }
    }

    Command result = commands[max_score_i];
    return result;
}

const Node zero_node;

void push_node(Node **buf, uintptr_t *len, uintptr_t *cap, Node n) {
    if (*len >= *cap) {
        if (*cap == 0) {
            *cap = 1024;
        } else {
            *cap *= 2;
        }
        *buf = realloc(*buf, *cap * sizeof(**buf));
    }
    (*buf)[*len] = n;
    *len += 1;
}

void push_command(Command **buf, uintptr_t *len, uintptr_t *cap, Command command) {
    if (*len >= *cap) {
        if (*cap == 0) {
            *cap = 1024;
        } else {
            *cap *= 2;
        }
        *buf = realloc(*buf, *cap * sizeof(**buf));
    }
    (*buf)[*len] = command;
    *len += 1;
}

MCTSState ai_mcts_state_init(Game *game, Command *commands, int num_commands) {
    Node *nodes = NULL;
    uintptr_t nodes_len = 0;
    uintptr_t nodes_cap = 0;

    push_node(&nodes, &nodes_len, &nodes_cap, zero_node);
    push_node(&nodes, &nodes_len, &nodes_cap,
              (Node){
                  .kind = NODE_DECISION,
                  .game = *game,
                  .command = (Command){0},
                  .parent_i = 0,
                  .first_child_i = 0,
                  .num_children = 0,
                  .num_children_to_expand = (uint32_t)num_commands,
                  .visits = 0,
                  .total_reward = 0,
                  .probability = 1.0,
              });
    assert(nodes_len == 2);

    uint32_t root_i = 1;

    return (MCTSState){
        .root = root_i,
        .nodes = nodes,
        .nodes_len = nodes_len,
        .nodes_cap = nodes_cap,
    };
}

void ai_mcts_state_cleanup(MCTSState *state) {
    if (state->nodes != NULL) {
        free(state->nodes);
    }
}

void ai_mcts_think(MCTSState *state, Game *game, Command *commands, int num_commands,
                   int iterations) {
    Node *nodes = state->nodes;
    uintptr_t nodes_len = state->nodes_len;
    uintptr_t nodes_cap = state->nodes_cap;

    uint32_t root_i = state->root;

    Command *unexpanded_commands = NULL;
    uintptr_t unexpanded_commands_len = 0;
    uintptr_t unexpanded_commands_cap = 0;

    double c = sqrt(2);
    double dpw_k = 1.0; // @note: tuneable
    double dpw_alpha = 0.5;

    Command *new_commands = malloc(1024 * sizeof(*new_commands));
    CommandBuf new_commands_buf = {
        .commands = new_commands,
        .count = 0,
        .cap = 1024,
    };

    for (int pass = 0; pass < iterations; pass++) {
        uint32_t node_i = root_i;
        while (true) {
            if (nodes[node_i].kind == NODE_OVER) {
                break;
            }

            if (nodes[node_i].kind == NODE_CHANCE) {
                double r = drand48();
                double cumulative = 0.0;
                uint32_t child_i = 0;
                assert(nodes[node_i].num_children == 2);
                for (uint32_t i = 0; i < nodes[node_i].num_children; i++) {
                    child_i = nodes[node_i].first_child_i + i;
                    cumulative += nodes[child_i].probability;
                    if (r <= cumulative) {
                        break;
                    }
                }
                node_i = child_i;
                continue;
            }

            assert(nodes[node_i].kind == NODE_DECISION);
            double allowed_children = dpw_k * pow((double)nodes[node_i].visits, dpw_alpha) + 1;
            if (nodes[node_i].num_children_to_expand > 0 &&
                nodes[node_i].num_children < allowed_children) {
                // Expand a new child.
                break;
            }

            // Pick child with highest UCT.
            assert(nodes[node_i].num_children > 0);
            double highest_uct = -INFINITY;
            uint32_t highest_uct_i = 0;
            for (uint32_t i = 0; i < nodes[node_i].num_children; i++) {
                uint32_t child_i = nodes[node_i].first_child_i + i;
                double child_uct = (nodes[child_i].total_reward / nodes[child_i].visits) +
                                   c * sqrt(log(nodes[node_i].visits) / nodes[child_i].visits);
                if (child_uct > highest_uct) {
                    highest_uct = child_uct;
                    highest_uct_i = child_i;
                }
            }
            node_i = highest_uct_i;
        }

        uint32_t nodes_to_simulate[2] = {node_i, 0};

        // Expansion
        if (nodes[node_i].kind != NODE_OVER) {
            assert(nodes[node_i].num_children_to_expand > 0);

            // If this is the first child, allocate the buffer.
            if (nodes[node_i].num_children == 0) {
                assert(nodes[node_i].first_child_i == 0);
                nodes[node_i].first_child_i = (uint32_t)nodes_len;
                for (uint32_t i = 0; i < nodes[node_i].num_children_to_expand; i++) {
                    push_node(&nodes, &nodes_len, &nodes_cap, zero_node);
                }
                assert(nodes_len ==
                       nodes[node_i].first_child_i + nodes[node_i].num_children_to_expand);
            }

            game_valid_commands(&new_commands_buf, &nodes[node_i].game);
            assert((uint32_t)new_commands_buf.count ==
                   nodes[node_i].num_children_to_expand + nodes[node_i].num_children);
            unexpanded_commands_len = 0;
            for (size_t i = 0; i < new_commands_buf.count; i++) {
                bool found = false;
                for (uint32_t j = 0; j < nodes[node_i].num_children; j++) {
                    uint32_t child_i = nodes[node_i].first_child_i + j;
                    if (command_eq(&(new_commands_buf.commands[i]), &(nodes[child_i].command))) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    push_command(&unexpanded_commands, &unexpanded_commands_len,
                                 &unexpanded_commands_cap, new_commands[i]);
                }
            }
            assert(unexpanded_commands_len > 0);
            Command child_command = ai_select_command_heuristic(
                &nodes[node_i].game, unexpanded_commands, unexpanded_commands_len);

            uint32_t next_child_i = nodes[node_i].first_child_i + nodes[node_i].num_children;
            Game child_game = nodes[node_i].game;

            if (child_command.kind == COMMAND_VOLLEY) {
                Game hit_game = child_game;
                game_apply_command(&hit_game, hit_game.turn.player, child_command, VOLLEY_HIT);
                game_valid_commands(&new_commands_buf, &hit_game);
                size_t num_new_hit_commands = new_commands_buf.count;

                uint32_t hit_child_i = (uint32_t)nodes_len;

                Game miss_game = child_game;
                game_apply_command(&miss_game, miss_game.turn.player, child_command, VOLLEY_MISS);
                game_valid_commands(&new_commands_buf, &miss_game);
                size_t num_new_miss_commands = new_commands_buf.count;

                uint32_t miss_child_i = hit_child_i + 1;

                nodes[next_child_i] = (Node){
                    .kind = NODE_CHANCE,
                    .game = child_game,
                    .command = child_command,
                    .parent_i = node_i,
                    .first_child_i = hit_child_i,
                    .num_children = 2,
                    .num_children_to_expand = 0,
                    .visits = 0,
                    .total_reward = 0,
                    .probability = 1.0,
                };
                nodes[node_i].num_children++;
                nodes[node_i].num_children_to_expand--;

                push_node(&nodes, &nodes_len, &nodes_cap,
                          (Node){
                              .kind = NODE_DECISION,
                              .game = hit_game,
                              .command = child_command,
                              .parent_i = next_child_i,
                              .first_child_i = 0,
                              .num_children = 0,
                              .num_children_to_expand = (uint32_t)num_new_hit_commands,
                              .visits = 0,
                              .total_reward = 0,
                              .probability = 0.4167,
                          });
                push_node(&nodes, &nodes_len, &nodes_cap,
                          (Node){
                              .kind = NODE_DECISION,
                              .game = miss_game,
                              .command = child_command,
                              .parent_i = next_child_i,
                              .first_child_i = 0,
                              .num_children = 0,
                              .num_children_to_expand = (uint32_t)num_new_miss_commands,
                              .visits = 0,
                              .total_reward = 0,
                              .probability = 1 - 0.4167,
                          });
                nodes_to_simulate[1] = miss_child_i;
            } else {
                game_apply_command(&child_game, child_game.turn.player, child_command, VOLLEY_ROLL);
                game_valid_commands(&new_commands_buf, &child_game);
                size_t num_commands = new_commands_buf.count;

                nodes[next_child_i] = (Node){
                    .kind = child_game.status == STATUS_OVER ? NODE_OVER : NODE_DECISION,
                    .game = child_game,
                    .command = child_command,
                    .parent_i = node_i,
                    .first_child_i = 0,
                    .num_children = 0,
                    .num_children_to_expand = (uint32_t)num_commands,
                    .visits = 0,
                    .total_reward = 0,
                    .probability = 1.0,
                };
                nodes[node_i].num_children++;
                nodes[node_i].num_children_to_expand--;
                nodes_to_simulate[0] = next_child_i;
                nodes_to_simulate[1] = 0;
            }
        }

        for (uint32_t i = 0; i < 2; i++) {
            uint32_t sim_i = nodes_to_simulate[i];
            if (sim_i == 0) {
                continue;
            }

            // Scored player is the player that will be scored at the end of the simulation.
            Player scored_player = nodes[root_i].game.turn.player;
            if (nodes[sim_i].parent_i != 0) {
                scored_player = nodes[nodes[sim_i].parent_i].game.turn.player;
            }

            // Simulation
            double score;
            if (nodes[sim_i].kind != NODE_OVER) {
                Game sim_game = nodes[sim_i].game;
                ai_rollout(&sim_game, (Command){0}, 300);
                score = heuristic_value(&sim_game, scored_player);
            } else {
                score = heuristic_value(&nodes[sim_i].game, scored_player);
            }

            // Backpropagation
            while (sim_i != 0) {
                if ((sim_i == root_i && scored_player == game->turn.player) ||
                    (nodes[nodes[sim_i].parent_i].game.turn.player == scored_player)) {
                    nodes[sim_i].total_reward += score;
                } else {
                    nodes[sim_i].total_reward -= score;
                }
                nodes[sim_i].visits++;
                sim_i = nodes[sim_i].parent_i;
            }
        }
    }

    state->nodes = nodes;
    state->nodes_len = nodes_len;
    state->nodes_cap = nodes_cap;

    state->root = root_i;

    if (unexpanded_commands != NULL) {
        free(unexpanded_commands);
    }
    free(new_commands);
}

Command ai_mcts_select_command(MCTSState *state, Game *game, Command *commands, int num_commands) {
    Node *nodes = state->nodes;
    uint32_t root_i = state->root;

    // Select best command.
    uint32_t most_visits = 0;
    uint32_t best_child_i = 0;

    assert(nodes[root_i].num_children > 0);
    assert(nodes[root_i].num_children == (uint32_t)num_commands);
    assert(nodes[root_i].num_children_to_expand == 0);
    for (uint32_t i = 0; i < nodes[root_i].num_children; i++) {
        uint32_t child_i = nodes[root_i].first_child_i + i;
        if (nodes[child_i].visits >= most_visits) {
            most_visits = nodes[child_i].visits;
            best_child_i = child_i;
        }
    }

    Command result = nodes[best_child_i].command;
    return result;
}
