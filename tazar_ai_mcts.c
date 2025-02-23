// Rewrite of the MCTS AI for Tazar
// Plans
// * Faster implementation. Changes to gamestate to make it faster.
// * Better memory usage and re-use the search tree accross turns.
// * Better evaluation function.
// * Changes to chance and other things to better weigh volleys and charges.
// * Q: Does it make sense to handle the whole turn as a decision? It means that the tree branches
//      out way more, but it also means that the policy code would make more sense. The way it is
//      now I can't just handle threatened pieces and targets because what does that mean mid-turn?
//      I think probably no, just need a good evaluation function.
// * How do I handle chance nodes? In selection I'm selecting on probability which means I explore
//   the miss more, that's obv not good.
//   I could weigh the probability with the mct.
//   Is there also some usage of the probability I should be doing in backpropagation? Yeah
//   probably. Rollout policy and next child node selection are not the same thing. Think about
//   that.

// changes to game state
// pieces on a 16x16 board
// invalid, empty, id? What needs to be fast and what doesn't I guess is the question. Design around
//   that.
// if applying actions is super fast I think I can avoid cloning the game state. That would be
// great.

#include "tazar_ai_mcts.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <tgmath.h>
#include <unistd.h>

void nodes_graphviz(MCTSState *state) {
    // Write nodes to dot file.
    FILE *fp = fopen("graph.dot", "w");
    if (fp == NULL) {
        printf("Error opening file!\n");
        exit(1);
    }
    fprintf(fp, "digraph unix {\n"
    "  node [color=lightblue2, style=filled];\n");
    for (i32 i=1; i<state->nodes_len-1; i++) {
        Node *node = state->nodes + i;
        if (node->parent_i != 0) {
            char *label = node->kind == NODE_DECISION ? "decision" : node->kind == NODE_OVER ? "over" : "chance";
            char *color = node->kind == NODE_CHANCE ? "lightgreen" : node->scored_player == PLAYER_RED ? "red" : "blue";
            fprintf(fp, "%u [label= %s color=%s, style=filled]\n", i, label, color);
            fprintf(fp, "%u -> %u\n", node->parent_i, i);
        }
    }
    fprintf(fp, "}\n");
    fclose(fp);
}

double game_value_for_red(Game *game) {
    double weights[5] = {
        [PIECE_NONE] = exp(0.0),
        [PIECE_CROWN] = exp(4.0),
        [PIECE_PIKE] = exp(1.0),
        [PIECE_HORSE] = exp(3.0),
        [PIECE_BOW] = exp(2.0),
    };

    double max_score = weights[PIECE_CROWN] + 2 * weights[PIECE_HORSE] + 3 * weights[PIECE_BOW] + 5 * weights[PIECE_PIKE];
    double score = 0.0;

    for (u32 i=0; i<64; i++) {
        Piece *piece = game->pieces + i;
        if (piece->kind == PIECE_NONE) {
            continue;
        }
        double weight = weights[piece->kind];
        if (piece->player == PLAYER_RED) {
            score += weight;
        } else {
            score -= weight;
        }
    }

    double result = score / max_score;
    assert(result > -1.0 && result < 1.0);
    if (result != 0.0) {
        printf("result: %f\n", result);
    }
    return result;
}

// Select next command, used during node expansion and rollout.
Command rollout_policy(Game *game, Command *commands, u32 num_commands) {
    assert(num_commands < 512);
    double command_values[512];
    double total_value = 0;

    // Only really wanna end turn if there are no other commands.
    if (num_commands == 1) {
        return commands[0];
    }

    double weights[7] = {
        exp(0), // end turn
        exp(1), // move
        exp(2), // horse_charge
        exp(3), // kill bow
        exp(4), // kill horse
        exp(5), // volley
        exp(6), // kill crown
    };

    for (u32 i=1; i<num_commands; i++) {
        Command *command = commands + i;
        switch (command->kind) {
        case COMMAND_NONE: {
            assert(0);
            break;
        }
        case COMMAND_MOVE: {
            const Piece *source_piece = game_piece_get(game, command->piece_pos);
            assert(source_piece->kind != PIECE_NONE);
            const Piece *target_piece = game_piece_get(game, command->target_pos);

            if (target_piece->kind == PIECE_CROWN) {
                command_values[i] = weights[6];
                total_value += weights[6];
            } else if (source_piece->kind == PIECE_HORSE) {
                if (target_piece->kind == PIECE_PIKE || target_piece->kind == PIECE_HORSE) {
                    command_values[i] = weights[2];
                    total_value += weights[2];
                } else if (target_piece->kind == PIECE_BOW) {
                    command_values[i] = weights[3];
                    total_value += weights[3];
                } else {
                    assert(target_piece->kind == PIECE_NONE);
                    command_values[i] = weights[1];
                    total_value += weights[1];
                }
            } else if (target_piece->kind == PIECE_HORSE) {
                command_values[i] = weights[4];
                total_value += weights[4];
            } else if (target_piece->kind == PIECE_BOW) {
                command_values[i] = weights[3];
                total_value += weights[3];
            } else if (target_piece->kind == PIECE_NONE) {
                command_values[i] = weights[1];
                total_value += weights[1];
            } else {
                assert(0);
            }
            break;
        }
        case COMMAND_VOLLEY: {
            command_values[i] = weights[5];
            total_value += weights[5];
            break;
        }
        case COMMAND_MUSTER: {
            assert(0);
            break;
        }
        case COMMAND_END_TURN: {
            assert(i == 0);
            command_values[i] = weights[1];
            total_value += weights[1];
            break;
        }
        }
    }

    // Softmax to pick command.
    double r = random_prob();
    double cumulative = 0.0;
    for (u32 i=0; i<num_commands; i++) {
        cumulative += command_values[i] / total_value;
        if (r <= cumulative) {
            return commands[i];
        }
    }
    return commands[0];
}


void extend_nodes(Node **buf, uintptr_t *len, uintptr_t *cap, size_t n) {
    if (*len + n >= *cap) {
        if (*cap == 0) {
            *cap = 1024;
        } else {
            *cap *= 2;
        }
        *buf = realloc(*buf, *cap * sizeof(**buf));
    }
    *len += n;
}

MCTSState ai_mcts_state_init(MCTSState *prev_state, Game *game, Command *commands, int num_commands) {
    Node null_node = (Node){
        .kind = NODE_NONE,
        .parent_i = 0,
        .first_child_i = 0,
        .num_children = 0,
        .num_children_to_expand = 0,
        .visits = 0,
        .command = (Command){
            .kind = COMMAND_NONE,
            .piece_pos = (CPos){0, 0, 0},
            .target_pos = (CPos){0, 0, 0},
            .muster_piece_kind = PIECE_NONE,
        },
        .scored_player = PLAYER_NONE,
        .volley_result = VOLLEY_ROLL,
        .total_reward = 0.0,
        .probability = 0.0,
    };

    if (prev_state->root != 0) {
        // todo: reuse the tree
        if (prev_state->nodes != NULL) {
            free(prev_state->nodes);
        }
    }

    Node *nodes = NULL;
    uintptr_t nodes_len = 0;
    uintptr_t nodes_cap = 0;

    extend_nodes(&nodes, &nodes_len, &nodes_cap, 2);
    nodes[0] = null_node;
    nodes[1] = (Node) {
        .kind = NODE_DECISION,
        .parent_i = 0,
        .first_child_i = 0,
        .num_children = 0,
        .num_children_to_expand = num_commands,
        .visits = 0,
        .command = (Command){
            .kind = COMMAND_NONE,
            .piece_pos = (CPos){0, 0, 0},
            .target_pos = (CPos){0, 0, 0},
            .muster_piece_kind = PIECE_NONE,
        },
        .scored_player = game->turn.player,
        .volley_result = VOLLEY_ROLL,
        .total_reward = 0.0,
        .probability = 0.0,
    };

    MCTSState state = {
        .root = 1,
        .nodes = nodes,
        .nodes_len = nodes_len,
        .nodes_cap = nodes_cap,
    };

    return state;
}

void ai_mcts_state_cleanup(MCTSState *state) {}

void ai_mcts_think(MCTSState *state, Game *game, Command *commands, int num_commands,
                   int iterations) {
    Node *nodes = state->nodes;
    uintptr_t nodes_len = state->nodes_len;
    uintptr_t nodes_cap = state->nodes_cap;
    uint32_t root_i = state->root;

    Command *new_commands = malloc(1024 * sizeof(*new_commands));
    CommandBuf new_commands_buf = {
        .commands = new_commands,
        .count = 0,
        .cap = 1024,
    };

    Command *unexpanded_commands = malloc(1024 * sizeof(*unexpanded_commands));
    CommandBuf unexpanded_commands_buf = {
        .commands = unexpanded_commands,
        .count = 0,
        .cap = 1024,
    };

    double c = sqrt(2);
    double dpw_k = 1.0; // 1
    double dpw_alpha = 0.1; // 0.5, 0.25 looked good too

    for (int pass=0; pass < iterations; pass++) {
        u32 selected_node_i = root_i;
        Game selected_node_game = *game;

        // Select a node to simulate.
        while (true) {
            if (nodes[selected_node_i].kind != NODE_CHANCE) {
                game_apply_command(&selected_node_game, selected_node_game.turn.player, nodes[selected_node_i].command, nodes[selected_node_i].volley_result);
            }

            if (nodes[selected_node_i].kind == NODE_OVER) {
                assert(selected_node_game.winner != PLAYER_NONE);
                break;
            }

            if (nodes[selected_node_i].kind == NODE_CHANCE) {
                double r = random_prob();
                double cumulative = 0.0;
                u32 child_i = 0;
                assert(nodes[selected_node_i].num_children == 2);
                for (u32 i = 0; i < nodes[selected_node_i].num_children; i++) {
                    child_i = nodes[selected_node_i].first_child_i + i;
                    cumulative += nodes[child_i].probability;
                    if (r <= cumulative) {
                        break;
                    }
                }
                selected_node_i = child_i;
                continue;
            }

            if (nodes[selected_node_i].kind == NODE_DECISION) {
                double allowed_children = dpw_k * pow((double)nodes[selected_node_i].visits, dpw_alpha) + 1;
                if (nodes[selected_node_i].num_children_to_expand > 0 &&
                    nodes[selected_node_i].num_children < allowed_children) {
                    // Expand a new child.
                    break;
                }

                // Pick child with highest UCT.
                assert(nodes[selected_node_i].num_children > 0);
                double highest_uct = -INFINITY;
                uint32_t highest_uct_i = 0;
                for (uint32_t i = 0; i < nodes[selected_node_i].num_children; i++) {
                    uint32_t child_i = nodes[selected_node_i].first_child_i + i;
                    double child_uct = (nodes[child_i].total_reward / nodes[child_i].visits) +
                                       c * sqrt(log(nodes[selected_node_i].visits) / nodes[child_i].visits);
                    if (child_uct > highest_uct) {
                        highest_uct = child_uct;
                        highest_uct_i = child_i;
                    }
                }
                selected_node_i = highest_uct_i;
                continue;
            }

            assert(0);
        }

        u32 nodes_to_simulate[2] = {selected_node_i, 0};
        Game simulate_games[2] = {selected_node_game, selected_node_game};

        // Expand Child.
        if (nodes[selected_node_i].kind != NODE_OVER) {
            assert(nodes[selected_node_i].num_children_to_expand > 0);

            // If this is the first child, allocate space in the buffer.
            if (nodes[selected_node_i].first_child_i == 0) {
                nodes[selected_node_i].first_child_i = (u32)nodes_len;
                extend_nodes(&nodes, &nodes_len, &nodes_cap, nodes[selected_node_i].num_children_to_expand);

                // debuging, set them to 0
                for (u32 i = 0; i < nodes[selected_node_i].num_children_to_expand; i++) {
                    nodes[nodes[selected_node_i].first_child_i + i] = (Node) {
                        .kind = NODE_NONE,
                        .parent_i = 0,
                        .first_child_i = 0,
                        .num_children = 0,
                        .num_children_to_expand = 0,
                        .visits = 0,
                        .command = (Command){
                            .kind = COMMAND_NONE,
                            .piece_pos = (CPos){0, 0, 0},
                            .target_pos = (CPos){0, 0, 0},
                            .muster_piece_kind = PIECE_NONE,
                        },
                        .scored_player = PLAYER_NONE,
                        .volley_result = VOLLEY_ROLL,
                        .total_reward = 0.0,
                        .probability = 0.0,
                    };
                }

                assert(nodes_len == nodes[selected_node_i].first_child_i + nodes[selected_node_i].num_children_to_expand);
            }

            // Get valid next commands.
            game_valid_commands(&new_commands_buf, &selected_node_game);
            assert((u32)new_commands_buf.count == nodes[selected_node_i].num_children + nodes[selected_node_i].num_children_to_expand);
            unexpanded_commands_buf.count = 0;
            for (size_t i = 0; i < new_commands_buf.count; i++) {
                bool found = false;
                for (size_t j = 0; j<nodes[selected_node_i].num_children; j++) {
                    if (command_eq(&new_commands_buf.commands[i], &nodes[nodes[selected_node_i].first_child_i + j].command)) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    unexpanded_commands_buf.commands[unexpanded_commands_buf.count++] = new_commands_buf.commands[i];
                }
            }
            assert(unexpanded_commands_buf.count == nodes[selected_node_i].num_children_to_expand);

            // Select a new command to expand.
            Command new_command = rollout_policy(&selected_node_game, unexpanded_commands_buf.commands, unexpanded_commands_buf.count);
            u32 next_child_i = nodes[selected_node_i].first_child_i + nodes[selected_node_i].num_children;

            if (new_command.kind == COMMAND_VOLLEY) {
                Player scored_player = selected_node_game.turn.player;
                u32 hit_child_i = nodes_len;
                u32 miss_child_i = nodes_len + 1;

                extend_nodes(&nodes, &nodes_len, &nodes_cap, 2);
                assert(nodes_len == miss_child_i + 1);

                nodes[next_child_i] = (Node) {
                    .kind = NODE_CHANCE,
                    .parent_i = selected_node_i,
                    .first_child_i = hit_child_i,
                    .num_children = 2,
                    .num_children_to_expand = 0,
                    .visits = 0,
                    .command = new_command,
                    .scored_player = scored_player,
                    .volley_result = VOLLEY_ROLL,
                    .total_reward = 0.0,
                    .probability = 1.0,
                };
                nodes[selected_node_i].num_children++;
                nodes[selected_node_i].num_children_to_expand--;

                nodes_to_simulate[0] = hit_child_i;
                nodes_to_simulate[1] = miss_child_i;

                game_apply_command(&simulate_games[0], scored_player, new_command, VOLLEY_HIT);
                game_apply_command(&simulate_games[1], scored_player, new_command, VOLLEY_MISS);

                game_valid_commands(&new_commands_buf, &simulate_games[0]);
                u32 num_new_hit_commands = new_commands_buf.count;
                game_valid_commands(&new_commands_buf, &simulate_games[1]);
                u32 num_new_miss_commands = new_commands_buf.count;

                nodes[hit_child_i] = (Node) {
                    .kind = simulate_games[0].winner == PLAYER_NONE ? NODE_DECISION : NODE_OVER,
                    .parent_i = next_child_i,
                    .first_child_i = 0,
                    .num_children = 0,
                    .num_children_to_expand = num_new_hit_commands,
                    .visits = 0,
                    .command = new_command,
                    .scored_player = scored_player,
                    .volley_result = VOLLEY_HIT,
                    .total_reward = 0.0,
                    .probability = 0.4167,
                };
                nodes[miss_child_i] = (Node) {
                    .kind = simulate_games[1].winner == PLAYER_NONE ? NODE_DECISION : NODE_OVER,
                    .parent_i = next_child_i,
                    .first_child_i = 0,
                    .num_children = 0,
                    .num_children_to_expand = num_new_miss_commands,
                    .visits = 0,
                    .command = new_command,
                    .scored_player = scored_player,
                    .volley_result = VOLLEY_MISS,
                    .total_reward = 0.0,
                    .probability = 1.0 - 0.4167,
                };

            } else {
                Player scored_player = simulate_games[0].turn.player;
                game_apply_command(&simulate_games[0], scored_player, new_command, VOLLEY_ROLL);
                game_valid_commands(&new_commands_buf, &simulate_games[0]);
                u32 num_new_commands = new_commands_buf.count;

                nodes[next_child_i] = (Node) {
                    .kind = simulate_games[0].winner == PLAYER_NONE ? NODE_DECISION : NODE_OVER,
                    .parent_i = selected_node_i,
                    .first_child_i = 0,
                    .num_children = 0,
                    .num_children_to_expand = num_new_commands,
                    .visits = 0,
                    .command = new_command,
                    .scored_player = scored_player,
                    .volley_result = VOLLEY_ROLL,
                    .total_reward = 0.0,
                    .probability = 1.0,
                };

                nodes[selected_node_i].num_children++;
                nodes[selected_node_i].num_children_to_expand--;
                nodes_to_simulate[0] = next_child_i;
                nodes_to_simulate[1] = 0;
            }
        }

        for (u32 i=0; i<2; i++) {
            u32 sim_i = nodes_to_simulate[i];
            if (sim_i == 0) {
                continue;
            }
            Game *sim_game = simulate_games + i;

            Player scored_player = nodes[sim_i].scored_player;
            double score;
            if (sim_game->status != STATUS_OVER) {
                // rollout
                i32 depth = 300;
                while (
                    sim_game->status == STATUS_IN_PROGRESS
                    && (depth-- > 0 || sim_game->turn.activation_i != 0)
                    ) {
                    game_valid_commands(&new_commands_buf, sim_game);
                    Command next_command = rollout_policy(sim_game, new_commands_buf.commands, new_commands_buf.count);
                    game_apply_command(sim_game, sim_game->turn.player, next_command, VOLLEY_ROLL);
                }
            }
            if (sim_game->status != STATUS_OVER) {
                if (sim_game->winner == scored_player) {
                    score = 1.0;
                } else {
                    score = -1.0;
                }
            } else {
                score = game_value_for_red(sim_game);
                if (scored_player == PLAYER_BLUE) {
                    score = -score;
                }
            }

            // backpropagation
            while (sim_i != 0) {
                if (nodes[sim_i].kind == NODE_CHANCE) {
                    //printf("change");
                }

                if (nodes[sim_i].scored_player == scored_player) {
                    nodes[sim_i].total_reward += score;
                } else {
                    nodes[sim_i].total_reward -= score;
                }
                nodes[sim_i].visits++;
                sim_i = nodes[sim_i].parent_i;
            }

        }

    }

    free(new_commands);
    free(unexpanded_commands);

    state->nodes = nodes;
    state->nodes_len = nodes_len;
    state->nodes_cap = nodes_cap;
    nodes_graphviz(state);
}

Command ai_mcts_select_command(MCTSState *state, Game *game, Command *commands, int num_commands) {
    Node *nodes = state->nodes;
    u32 root_i = state->root;

    u32 most_visits = 0;
    u32 best_child_i = 0;

    assert(nodes[root_i].num_children > 0);
    //assert(nodes[root_i].num_children == (uint32_t)num_commands);
    //assert(nodes[root_i].num_children_to_expand == 0);
    for (u32 i = 0; i < nodes[root_i].num_children; i++) {
        u32 child_i = nodes[root_i].first_child_i + i;
        if (nodes[child_i].visits >= most_visits) {
            most_visits = nodes[child_i].visits;
            best_child_i = child_i;
        }
    }

    return nodes[best_child_i].command;
}