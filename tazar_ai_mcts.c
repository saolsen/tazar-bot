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

// Next ideas.
// Chance nodes are not working that well for me. I think maybe because they don't get explored enough.
// What I could do is eliminate them and make it an "open loop" mcts.
// The problem here, is that the actions after the volley change based on if it hit or not.

// I guess, the children of a volley are all possible action for a hit or a miss.
// So if you hit there's maybe an extra move to where the piece was. And if you miss there are
// extra moves for the opponent because they can still use that piece. That seems kind of hard to
// keep track of. You don't always know the number of children anymore.
// When you hit a node, if dpw says you can add a child, maybe then you just see if there's a command
// for the current traversal that hasn't been added yet.
// That makes that part easier, but what about when you're traversing and there's a nodes that don't
// exist right now, I guess you have to make sure you don't select them. I think that would mean
// also generating valid next commands as you traverse, which seems a lot more expensive.

// Without doing that, maybe I'm still handling chance nodes badly. Their value should be a combination of
// their children's value, not it's own thing? How do you backprop and select nodes correctly through
// chance nodes?

// I guess you backprop up the value normall for both children up to the chance, then you combine them
// and backprop that up to the decision node. That makes sense for the first expansion, but then what
// do you do after that? You won't select both during selection, you'll only select one. So what do
// you do while backproping that new value?
// Do you say it's not a full visit? Do you only take a percentage of the value, thus making it closer
// to 0?

// what is the total value / visits, it's the expected value right?
// so for a chance node, it should be the expected value of the children weighed by their probability.
// backprop is updating the expected values of nodes.

// Maybe, you combine the new value as it comes up with the current expected value of the other branch and
// then backprop that value the rest of the way?

// so when you hit a chance node, instead of continuing with the same score, you compute a new score
// and then continue to backprop that?
// how does that work the first time when you don't have an expected value for the other branch, maybe
// you just don't backprop it that time. or just get a sim for both branches right away during expansion
// and then backprop that, don't try to be clever about it.

// 2 things
// there's for sure a bug with endgame states or something, because the AI is not capturing the crown.
//


// ok, next idea
// progressive widening is bullshit. I'm missing so many moves that way, obvious good ones.
// I think when we expand a node, we give every child a score based on the heiristic.
// then we run a rollout for the node and backpropagate that value.
// This way, during node selection we can actually have some idea of what good moves are even before
// simulating and we won't miss obviously good stuff like killing the crown.
// do my eval's make sense?
// like estimate for kill a bow would be 0.5 or something right? well, it depends on how bad we're doing but you know.
// then, maybe we sim it and lose, now we have a -1, and averages to a -0.25, well that's kinda shit.
// sim of a random other bad move might hit a 1 and give us a 0.25, and now this bad move looks way better
// than a guaranteed bow kill. But then of course, the idea is that you do this stuff a lot and it averages to good
// stuff. I'm not so sure. I think I don't actually go deep enough in the tree for most of these to matter at all.
// I'm only going like 3 steps and using 300 step rollout randomness. I gotta go like at least 100 into the depth
// for this to really make any sense don't I?



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
    "  rankdir=LR;\n"
    "  node [color=lightblue2, style=filled];\n");
    for (i32 i=1; i<state->nodes_len-1; i++) {
        Node *node = state->nodes + i;
        if (node->parent_i != 0) {
            char *color = node->kind == NODE_CHANCE ? "lightgreen" : node->scored_player == PLAYER_RED ? "red" : "blue";
            fprintf(fp, "%u [label= %f color=%s, style=filled]\n", i, node->total_reward/node->visits, color);
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
    return result;
}

// Select next command, used during node expansion and rollout.
Command rollout_policy(Game *game, Command *commands, u32 num_commands) {
    assert(num_commands < 512);
    double command_values[512];
    double total_value = 0;

    // Only really want to end turn if there are no other commands.
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
        // Need to find the new root, then copy over all it's children.
        // All pointers to parents and children need to be updated to be valid.
        // Then I can free the old nodes.
        // This should give a good starting point.
        // How does this affect some of the selection stuff? Visits will be way higher for existing
        // stuff, is that bad? Or is that fine because of how the selection works.
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

double ai_mcts_rollout(Game *sim_game, CommandBuf *new_commands_buf, Player scored_player) {
    double score;
    if (sim_game->status != STATUS_OVER) {
        // rollout
        i32 depth = 300;
        while (
            sim_game->status == STATUS_IN_PROGRESS
            && (depth-- > 0 || sim_game->turn.activation_i != 0)
            ) {
            game_valid_commands(new_commands_buf, sim_game);
            Command next_command = rollout_policy(sim_game, new_commands_buf->commands, new_commands_buf->count);
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
    return score;
}

void ai_mcts_backprop(Node *nodes, u32 node_i, double score, Player scored_player) {
    while (node_i != 0) {
        if (nodes[node_i].kind == NODE_CHANCE) {
            Node *hit_child = nodes + nodes[node_i].first_child_i;
            Node *miss_child = hit_child + 1;
            double hit_score = hit_child->total_reward / hit_child->visits;
            double miss_score = miss_child->total_reward / miss_child->visits;
            double new_score = hit_score * hit_child->probability + miss_score * miss_child->probability;
            nodes[node_i].total_reward = new_score * nodes[node_i].visits;
            if (nodes[node_i].scored_player == scored_player) {
                score = new_score;
            } else {
                score = -new_score;
            }
        }
        if (nodes[node_i].scored_player == scored_player) {
            nodes[node_i].total_reward += score;
        } else {
            nodes[node_i].total_reward -= score;
        }
        nodes[node_i].visits++;
        node_i = nodes[node_i].parent_i;
    }
}


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
    double dpw_k = 2.0; // 1
    double dpw_alpha = 0.5; // 0.5, 0.25 looked good too

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

        if (nodes[selected_node_i].kind == NODE_OVER) {
            assert(selected_node_game.winner != PLAYER_NONE);
            double score = 1.0;
            Player scored_player = selected_node_game.winner;
            nodes[selected_node_i].visits++;
            ai_mcts_backprop(nodes, nodes[selected_node_i].parent_i, score, scored_player);
        } else {
            assert(nodes[selected_node_i].num_children_to_expand > 0);

            // If this is the first child, allocate space in the buffer.
            if (nodes[selected_node_i].first_child_i == 0) {
                nodes[selected_node_i].first_child_i = (u32)nodes_len;
                extend_nodes(&nodes, &nodes_len, &nodes_cap, nodes[selected_node_i].num_children_to_expand);

                // debugging, set them to 0
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

                Game hit_game = selected_node_game;
                Game miss_game = selected_node_game;

                game_apply_command(&hit_game, scored_player, new_command, VOLLEY_HIT);
                game_valid_commands(&new_commands_buf, &hit_game);
                u32 num_new_hit_commands = new_commands_buf.count;
                NodeKind hit_kind = hit_game.winner == PLAYER_NONE ? NODE_DECISION : NODE_OVER;
                double hit_score = ai_mcts_rollout(&hit_game, &new_commands_buf, scored_player);

                game_apply_command(&miss_game, scored_player, new_command, VOLLEY_MISS);
                game_valid_commands(&new_commands_buf, &miss_game);
                u32 num_new_miss_commands = new_commands_buf.count;
                NodeKind miss_kind = miss_game.winner == PLAYER_NONE ? NODE_DECISION : NODE_OVER;
                double miss_score = ai_mcts_rollout(&miss_game, &new_commands_buf, scored_player);

                double chance_score = hit_score * 0.4167 + miss_score * (1.0 - 0.4167);

                nodes[hit_child_i] = (Node) {
                    .kind = hit_kind,
                    .parent_i = next_child_i,
                    .first_child_i = 0,
                    .num_children = 0,
                    .num_children_to_expand = num_new_hit_commands,
                    .visits = 1,
                    .command = new_command,
                    .scored_player = scored_player,
                    .volley_result = VOLLEY_HIT,
                    .total_reward = hit_score,
                    .probability = 0.4167,
                };
                nodes[miss_child_i] = (Node) {
                    .kind = miss_kind,
                    .parent_i = next_child_i,
                    .first_child_i = 0,
                    .num_children = 0,
                    .num_children_to_expand = num_new_miss_commands,
                    .visits = 1,
                    .command = new_command,
                    .scored_player = scored_player,
                    .volley_result = VOLLEY_MISS,
                    .total_reward = miss_score,
                    .probability = 1.0 - 0.4167,
                };
                nodes[next_child_i] = (Node) {
                    .kind = NODE_CHANCE,
                    .parent_i = selected_node_i,
                    .first_child_i = hit_child_i,
                    .num_children = 2,
                    .num_children_to_expand = 0,
                    .visits = 1,
                    .command = new_command,
                    .scored_player = scored_player,
                    .volley_result = VOLLEY_ROLL,
                    .total_reward = chance_score,
                    .probability = 1.0,
                };
                nodes[selected_node_i].num_children++;
                nodes[selected_node_i].num_children_to_expand--;

                ai_mcts_backprop(nodes, selected_node_i, chance_score, scored_player);
            } else {
                Player scored_player = selected_node_game.turn.player;
                game_apply_command(&selected_node_game, scored_player, new_command, VOLLEY_ROLL);

                if (selected_node_game.status == STATUS_OVER) {
                    assert(selected_node_game.winner == scored_player);
                    nodes[next_child_i] = (Node) {
                        .kind = NODE_OVER,
                        .parent_i = selected_node_i,
                        .first_child_i = 0,
                        .num_children = 0,
                        .num_children_to_expand = 0,
                        .visits = 0,
                        .command = new_command,
                        .scored_player = scored_player,
                        .volley_result = VOLLEY_ROLL,
                        .total_reward = 1.0,
                        .probability = 1.0,
                    };
                    nodes[selected_node_i].num_children++;
                    nodes[selected_node_i].num_children_to_expand--;

                    ai_mcts_backprop(nodes, selected_node_i, 1.0, scored_player);
                } else {
                    game_valid_commands(&new_commands_buf, &selected_node_game);
                    u32 num_new_commands = new_commands_buf.count;
                    NodeKind new_kind = selected_node_game.winner == PLAYER_NONE ? NODE_DECISION : NODE_OVER;
                    double score = ai_mcts_rollout(&selected_node_game, &new_commands_buf, scored_player);

                    nodes[next_child_i] = (Node) {
                        .kind = new_kind,
                        .parent_i = selected_node_i,
                        .first_child_i = 0,
                        .num_children = 0,
                        .num_children_to_expand = num_new_commands,
                        .visits = 1,
                        .command = new_command,
                        .scored_player = scored_player,
                        .volley_result = VOLLEY_ROLL,
                        .total_reward = score,
                        .probability = 1.0,
                    };

                    nodes[selected_node_i].num_children++;
                    nodes[selected_node_i].num_children_to_expand--;

                    ai_mcts_backprop(nodes, selected_node_i, score, scored_player);
                }
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