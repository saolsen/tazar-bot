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

// tuning uct for more depth, is that a good idea?
// replacing rollouts with a short minimax of like 2 turns, is that a good idea?
// * that could be good, because it'd ensure that we don't miss obvious endgames.

#include "tazar_ai_mcts.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <tgmath.h>
#include <unistd.h>
#include <string.h>

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
    if (game->status == STATUS_OVER) {
        if (game->winner == PLAYER_RED) {
            return 1.0;
        } else {
            return -1.0;
        }
    }

//    double weights[5] = {
//        [PIECE_NONE] = exp(0.0),
//        [PIECE_CROWN] = exp(4.0),
//        [PIECE_PIKE] = exp(1.0),
//        [PIECE_HORSE] = exp(3.0),
//        [PIECE_BOW] = exp(2.0),
//    };
    double weights[5] = {
        [PIECE_NONE] = 0,
        [PIECE_CROWN] = 7,
        [PIECE_PIKE] = 1,
        [PIECE_HORSE] = 5,
        [PIECE_BOW] = 3,
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

// better ideas for easy-medium-hard
// easy would sort actions via the rollout policy below, just pick the heighest weight one.
// medium would just do a single expectimax to pick a command, so it's "looking" 4 moves ahead.
// hard uses the expectimax for rollouts and uses mcts to explore the tree.

// we probably also want to return what the expected value of each command because that will be
// the starting value for node_expansion.
// or, we undo the full expansion and go back to double progressive widening, each time
// using this policy to pick the best next command. That would probably work really well.
// it's basically equivalent, except that each command gets a rollout instead of just a minimax
// estimate to start with. It does make the mcts tree a lot smaller though, and I can maybe
// stop fully expanding and just allocate the nodes as I go. Doubtful I wanna do that though
// because it's good to have all children together for cache reasons.

typedef struct {
    double value;
    double hit_value;
    double miss_value;
} CommandValue;

// Use minimax (expectimax because chance nodes) to select the best command.
// Does a 4 deep search to pick the command that maximizes the value of the game.
// If I'm gonna do a-b pruning I probably want to sort by the old policy below.
// That's good for mcts too because it'll pick "better" commands for ones that have equal value.
// todo: make sure ucb uses > not >= if I do that.
typedef struct {
    u32 best_command_i;
    CommandValue command_values[512];
} ExpectiMaxResult;

// so, this plays really well, but I can only really do like 1 of these a frame.
// that means I can't really use them for rollouts, but I could at least use them for node eval.
// I think next step would be to take a look at doing normal mcts but with this for node eval.


// todo: this has to not be recursive because it uses way too much memory this way.
// or, game_valid_commands should just return a malloc'd buffer instead of being passed one.
// then on average it'll be a lot smaller.

typedef struct {
    Game game;
    Command command;
    Command children[512];
    size_t children_count;
    size_t children_processed;
    int depth;
} EMNode;

void push_em_node(EMNode **buf, uintptr_t *count, uintptr_t *cap, EMNode n) {
    if (*count >= *cap) {
        if (*cap == 0) {
            *cap = 2;
        } else {
            *cap *= 2;
        }
        *buf = realloc(*buf, *cap * sizeof(**buf));
    }
    (*buf)[*count] = n;
    *count += 1;
}

void push_value(CommandValue **buf, uintptr_t *count, uintptr_t *cap, CommandValue v) {
    if (*count >= *cap) {
        if (*cap == 0) {
            *cap = 2;
        } else {
            *cap *= 2;
        }
        *buf = realloc(*buf, *cap * sizeof(**buf));
    }
    (*buf)[*count] = v;
    *count += 1;
}

double expecti_max_node(ExpectiMaxResult *result, Game game, int depth) {
    if (result != NULL) {
        result->best_command_i = 0;
        memset(result->command_values, 0, sizeof(result->command_values));
    }

    Command *commands = malloc(512 * sizeof(Command));
    CommandBuf command_buf = {
        .commands = commands,
        .count = 0,
        .cap = 512,
    };

    EMNode *stack = NULL;
    uintptr_t stack_count = 0;
    uintptr_t stack_cap = 0;

    CommandValue *values = NULL;
    uintptr_t values_count = 0;
    uintptr_t values_cap = 0;

    push_em_node(&stack, &stack_count, &stack_cap, (EMNode){
        .game = game,
        .command = (Command){0},
        .children_count = 0,
        .children_processed = 0,
        .depth = depth,
    });

    // when you hit a node.
    // if children_count == 0, then you're either a game over, or haven't expanded children yet.
    // if game over, return value and pop.
    // if children_count == 0, expand children and save them, push first one.
    // if children_count > 0 and children_processed < children_count, push next child.
    // if children_count > 0 and children_processed == children_count, compute value and pop.

    uintptr_t prev_stack_count = -1;
    while (stack_count > 0) {
        uintptr_t top_i = stack_count - 1;
        if (stack_count == prev_stack_count) {
            printf("OH NO INFINATE LOOP");
            break;
        }
        assert(stack_count != prev_stack_count);
        prev_stack_count = stack_count;

        if (stack[top_i].depth == 0 || stack[top_i].game.status == STATUS_OVER) {
            // leaf node, compute value.
            double value = game_value_for_red(&stack[stack_count - 1].game);
            push_value(&values, &values_count, &values_cap,
                       (CommandValue){
                           .value = value,
                       });
            stack_count--;
        } else if (stack[top_i].children_count == 0) {
            // First time visiting this node, expand children.
            Command command = stack[top_i].command;
            if (command.kind == COMMAND_VOLLEY) {
                Game hit_game = stack[top_i].game;
                game_apply_command(&hit_game, hit_game.turn.player, command, VOLLEY_HIT);

                stack[top_i].children_count = 2;

                push_em_node(&stack, &stack_count, &stack_cap, (EMNode){
                    .game = hit_game,
                    .command = (Command){0},
                    .children_count = 0,
                    .children_processed = 0,
                    .depth = stack[top_i].depth,
                });
                stack[top_i].children_processed = 1;
            } else {
                // Apply the command.
                game_apply_command(&stack[top_i].game, stack[top_i].game.turn.player, command, VOLLEY_ROLL);
                game_valid_commands(&command_buf, &stack[top_i].game);
                for (size_t i=0; i<command_buf.count; i++) {
                    stack[top_i].children[i] = commands[i];
                }
                stack[top_i].children_count = command_buf.count;

                // Push first child.
                int child_depth = stack[top_i].depth - 1;
                if (stack[top_i].children_count < 12) {
                    child_depth++;
                }
                push_em_node(&stack, &stack_count, &stack_cap, (EMNode){
                    .game = stack[top_i].game,
                    .command = stack[top_i].children[0],
                    .children_count = 0,
                    .children_processed = 0,
                    .depth = child_depth,
                });
                stack[top_i].children_processed = 1;
            }
        } else if (stack[top_i].children_processed < stack[top_i].children_count) {
            // Push the next child.
            if (stack[top_i].command.kind == COMMAND_VOLLEY) {
                assert(stack[top_i].children_count == 2);
                assert(stack[top_i].children_processed == 1);

                Game miss_game = stack[top_i].game;
                game_apply_command(&miss_game, miss_game.turn.player, stack[top_i].command, VOLLEY_MISS);
                push_em_node(&stack, &stack_count, &stack_cap, (EMNode){
                    .game = miss_game,
                    .command = (Command){0},
                    .children_count = 0,
                    .children_processed = 0,
                    .depth = stack[top_i].depth,
                });
                stack[top_i].children_processed++;
            } else {
                Command child_command = stack[top_i].children[stack[top_i].children_processed];
                int child_depth = stack[top_i].depth - 1;
                if (stack[top_i].children_count < 12) {
                    child_depth++;
                }

                push_em_node(&stack, &stack_count, &stack_cap, (EMNode){
                    .game = stack[top_i].game,
                    .command = child_command,
                    .children_count = 0,
                    .children_processed = 0,
                    .depth = child_depth,
                });
                stack[top_i].children_processed++;
            }
        } else if (stack[top_i].children_processed == stack[top_i].children_count) {
            // Compute own value.
            if (stack[top_i].command.kind == COMMAND_VOLLEY) {
                assert(stack[top_i].children_count == 2);
                values_count -= 2;
                double hit_value = values[values_count].value;
                double miss_value = values[values_count + 1].value;
                assert((stack[top_i].game.turn.player == PLAYER_RED && hit_value > miss_value) ||
                       (stack[top_i].game.turn.player == PLAYER_BLUE && hit_value < miss_value));
                double value = 0.4167 * hit_value + (1.0 - 0.4167) * miss_value;
                push_value(&values, &values_count, &values_cap,
                           (CommandValue){
                               .value = value,
                               .hit_value = hit_value,
                               .miss_value = miss_value,
                           });
                stack_count--;
            } else {
                bool min_node = stack[top_i].game.turn.player == PLAYER_BLUE;
                double best_value = min_node ? INFINITY : -INFINITY;

                values_count -= stack[top_i].children_count;
                for (size_t i=0; i<stack[top_i].children_count; i++) {
                    CommandValue command_value = values[values_count + i];
                    if ((min_node && command_value.value <= best_value) || (!min_node && command_value.value >= best_value)) {
                        best_value = command_value.value;
                        if (result != NULL && stack[top_i].depth == depth) {
                            result->best_command_i = (u32)i;
                            result->command_values[i] = command_value;
                        }
                    }
                }
                push_value(&values, &values_count, &values_cap,
                           (CommandValue){
                               .value = best_value,
                           });
                stack_count--;
            }
        } else {
            assert(0);
        }
    }

    assert(values_count == 1);
    double score = values[0].value;

    free(commands);
    free(stack);
    free(values);

    return score;

#if 0
    if (depth == 0) {
        return game_value_for_red(&game);
    }

    Command *commands = malloc(512 * sizeof(Command));
    CommandBuf command_buf = {
        .commands = commands,
        .count = 0,
        .cap = 512,
    };
    game_valid_commands(&command_buf, &game);

    bool min_node = game.turn.player == PLAYER_BLUE;
    double best_value = min_node ? INFINITY : -INFINITY;

    for (u32 i=0; i < command_buf.count; i++) {
        Command command = commands[i];
        double value;
        if (command.kind == COMMAND_VOLLEY) {
            // chance node.
            Game hit_game = game;
            game_apply_command(&hit_game, hit_game.turn.player, commands[i], VOLLEY_HIT);
            double hit_value = expecti_max_node(NULL, hit_game, depth);
            Game miss_game = game;
            game_apply_command(&miss_game, miss_game.turn.player, commands[i], VOLLEY_MISS);
            double miss_value = expecti_max_node(NULL, miss_game, depth);
            value = 0.4167 * hit_value + (1.0 - 0.4167) * miss_value;
            if (result != NULL) {
                result->command_values[i].value = value;
                result->command_values[i].hit_value = hit_value;
                result->command_values[i].miss_value = miss_value;
            }
        } else {
            Game new_game = game;
            game_apply_command(&new_game, new_game.turn.player, commands[i], VOLLEY_ROLL);
            value = expecti_max_node(NULL, new_game, depth - 1);
            if (result != NULL) {
                result->command_values[i].value = value;
            }
        }

        if ((min_node && value <= best_value) || (!min_node && value >= best_value)) {
            best_value = value;
            if (result != NULL) {
                result->best_command_i = i;
            }
        }
    }
    free(commands);
    return best_value;
#endif
}

Command expecti_max_policy(Game *game, Command *commands, u32 num_commands) {
    assert(num_commands < 512);

    ExpectiMaxResult result = {0};
    //expecti_max_node(&result, *game, 3);
    expecti_max_node(&result, *game, 3);

    return commands[result.best_command_i];
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
        .visits = 1,
        .command = (Command){
            .kind = COMMAND_NONE,
            .piece_pos = (CPos){0, 0, 0},
            .target_pos = (CPos){0, 0, 0},
            .muster_piece_kind = PIECE_NONE,
        },
        .scored_player = game->turn.player,
        .volley_result = VOLLEY_ROLL,
        .total_reward = 0.5,
        .probability = 1.0,
    };

    MCTSState state = {
        .root = 1,
        .nodes = nodes,
        .nodes_len = nodes_len,
        .nodes_cap = nodes_cap,
    };

    return state;
}

void ai_mcts_state_cleanup(MCTSState *state) {
    if (state->root != 0) {
        if (state->nodes != NULL) {
            free(state->nodes);
            state->nodes = NULL;
            state->root = 0;
        }
    }
}

double ai_mcts_rollout(Game *sim_game, CommandBuf *new_commands_buf, Player scored_player) {
    double score;
    if (sim_game->status != STATUS_OVER) {
        // rollout
        i32 depth = 10;
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

// could go back to dpw, but picking an action must be based on minimax
// and rollouts also should either pick via minimax or be a minimax instead.

// Tree re-use would still be a massive boost. Should probably do that first.

double uct(Node *nodes, u32 parent_i, u32 node_i) {
    Node *parent = nodes + parent_i;
    Node *node = nodes + node_i;
    double c = 0.25; // depth 9, that seems better to me.
    //double c = sqrt(2); // 1.4, depth 4
    //double c = 0.5; // depth was 5
    double child_uct = (node->total_reward / node->visits) +
                       c * sqrt(log(parent->visits) / node->visits);
    return child_uct;
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

    //double c = sqrt(2);

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
                if (nodes[selected_node_i].visits == 1) {
                    assert(nodes[selected_node_i].num_children == 0);
                    assert(nodes[selected_node_i].first_child_i == 0);
                   break;
                }

                // Pick child with highest UCT.
                assert(nodes[selected_node_i].num_children > 0);
                double highest_uct = -INFINITY;
                uint32_t highest_uct_i = 0;
                for (uint32_t i = 0; i < nodes[selected_node_i].num_children; i++) {
                    uint32_t child_i = nodes[selected_node_i].first_child_i + i;
                    double child_uct = uct(nodes, selected_node_i, child_i);
                    if (child_uct >= highest_uct) {
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
            ai_mcts_backprop(nodes, selected_node_i, score, scored_player);
        } else {
            assert(nodes[selected_node_i].visits == 1);
            Player scored_player = selected_node_game.turn.player;

            // create the child nodes.
            nodes[selected_node_i].first_child_i = (u32)nodes_len;
            game_valid_commands(&new_commands_buf, &selected_node_game);
            nodes[selected_node_i].num_children = new_commands_buf.count;
            extend_nodes(&nodes, &nodes_len, &nodes_cap, new_commands_buf.count);

            ExpectiMaxResult result = {0};

            // 3 is the most that's actually fast enough to do in a frame.
            // But it kinda isn't enough, because you need 4 or 5 to really get the best move.
            double minimax_value = expecti_max_node(&result, selected_node_game, 3);

            for (u32 i = 0; i < new_commands_buf.count; i++) {
                u32 child_i = nodes[selected_node_i].first_child_i + i;
                Command child_command = new_commands_buf.commands[i];

                double child_score = result.command_values[i].value;
                double chance_score_hit = result.command_values[i].hit_value;
                double chance_score_miss = result.command_values[i].miss_value;
                if (scored_player == PLAYER_BLUE) {
                    child_score = -child_score;
                    chance_score_hit = -chance_score_hit;
                    chance_score_miss = -chance_score_miss;
                }

                if (child_command.kind == COMMAND_VOLLEY) {
                    u32 hit_child_i = (u32)nodes_len;
                    u32 miss_child_i = (u32)nodes_len + 1;

                    extend_nodes(&nodes, &nodes_len, &nodes_cap, 2);
                    assert(nodes_len == miss_child_i + 1);

                    NodeKind hit_kind = chance_score_hit <= -1 || chance_score_hit >= 1 ? NODE_OVER : NODE_DECISION;
                    NodeKind miss_kind = chance_score_miss <= -1 || chance_score_miss >= 1 ? NODE_OVER : NODE_DECISION;

                    nodes[hit_child_i] = (Node) {
                        .kind = hit_kind,
                        .parent_i = child_i,
                        .first_child_i = 0,
                        .num_children = 0,
                        .visits = 1,
                        .command = child_command,
                        .scored_player = scored_player,
                        .volley_result = VOLLEY_HIT,
                        .total_reward = chance_score_hit,
                        .probability = 0.4167,
                    };
                    nodes[miss_child_i] = (Node) {
                        .kind = miss_kind,
                        .parent_i = child_i,
                        .first_child_i = 0,
                        .num_children = 0,
                        .visits = 1,
                        .command = child_command,
                        .scored_player = scored_player,
                        .volley_result = VOLLEY_MISS,
                        .total_reward = chance_score_miss,
                        .probability = 1.0 - 0.4167,
                    };
                    nodes[child_i] = (Node) {
                        .kind = NODE_CHANCE,
                        .parent_i = selected_node_i,
                        .first_child_i = hit_child_i,
                        .num_children = 2,
                        .visits = 1,
                        .command = child_command,
                        .scored_player = scored_player,
                        .volley_result = VOLLEY_ROLL,
                        .total_reward = child_score,
                        .probability = 1.0,
                    };
                } else {
                    NodeKind child_kind = child_score <= -1 || child_score >= 1 ? NODE_OVER : NODE_DECISION;

                    nodes[child_i] = (Node) {
                        .kind = child_kind,
                        .parent_i = selected_node_i,
                        .first_child_i = 0,
                        .num_children = 0,
                        .visits = 1,
                        .command = child_command,
                        .scored_player = scored_player,
                        .volley_result = VOLLEY_ROLL,
                        .total_reward = child_score,
                        .probability = 1.0,
                    };
                }
            }

            // Simulate.
            double sim_score = minimax_value;
            if (nodes[selected_node_i].scored_player == PLAYER_BLUE) {
                sim_score = -sim_score;
            }
            if (sim_score > -1 || sim_score < 1) {
                 double rollout_score = ai_mcts_rollout(&selected_node_game, &new_commands_buf,
                                             nodes[selected_node_i].scored_player);
                 //sim_score = rollout_score;
                 sim_score = (sim_score + rollout_score) / 2;
            }

            // Backprop.
            ai_mcts_backprop(nodes, selected_node_i, sim_score,
                             nodes[selected_node_i].scored_player);
        }
    }

    free(new_commands);

    state->nodes = nodes;
    state->nodes_len = nodes_len;
    state->nodes_cap = nodes_cap;
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


// ok, so more ideas
// Break ai_think into two steps, selection and expansion. Each of these should finish in a frame.
// Expand the whole minimax tree? I'm only saving the nodes for the first layer right now, but I
// could fully build out the whole subtree during expansion instead.
// re-use the same nodes for each turn, that's a big one.