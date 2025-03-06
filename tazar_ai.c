#include "tazar.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

double game_value_for_red(Game *game) {
    if (game->status == STATUS_OVER) {
        if (game->winner == PLAYER_RED) {
            return 1.0;
        } else {
            return -1.0;
        }
    }

    double weights[] = {
        [PIECE_NULL] = 0, [PIECE_EMPTY] = 0, [PIECE_CROWN] = 7,
        [PIECE_PIKE] = 1, [PIECE_HORSE] = 5, [PIECE_BOW] = 3,
    };

    double max_score = weights[PIECE_CROWN] + 2 * weights[PIECE_HORSE] + 3 * weights[PIECE_BOW] +
                       5 * weights[PIECE_PIKE];
    double score = 0.0;

    for (u32 i = 0; i < 81; i++) {
        u8 piece = game->board[i];
        if (piece == PIECE_NULL || piece == PIECE_EMPTY) {
            continue;
        }
        double weight = weights[piece & PIECE_KIND_MASK];
        if ((piece & PLAYER_MASK) == PLAYER_RED) {
            score += weight;
        } else {
            score -= weight;
        }
    }

    double result = score / max_score;
    assert(result > -1.0 && result < 1.0);
    return result;
}

typedef struct {
    double value;
    double hit_value;
    double miss_value;
} CommandValue;

typedef struct {
    u32 best_command_i;
    CommandValue command_values[512];
} ExpectiMaxResult;

typedef struct {
    CommandBuf children;
    size_t children_processed;
    UndoCommand undo_child;
    int depth;
    Command chance_command;
    double alpha; // Best already found for RED (max player)
    double beta;  // Best already found for BLUE (min player)
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

double expecti_max_node(ExpectiMaxResult *result, Game *game, int depth) {
    if (result != NULL) {
        result->best_command_i = 0;
        memset(result->command_values, 0, sizeof(result->command_values));
    }

    EMNode *stack = NULL;
    uintptr_t stack_count = 0;
    uintptr_t stack_cap = 0;

    CommandValue *values = NULL;
    uintptr_t values_count = 0;
    uintptr_t values_cap = 0;

    push_em_node(&stack, &stack_count, &stack_cap,
                 (EMNode){
                     .children = (CommandBuf){0},
                     .children_processed = 0,
                     .undo_child = (UndoCommand){0},
                     .depth = depth,
                     .chance_command = (Command){0},
                     .alpha = -INFINITY, // Initialize alpha for root node
                     .beta = INFINITY,   // Initialize beta for root node
                 });

    while (stack_count > 0) {
        uintptr_t top_i = stack_count - 1;

        if (stack[top_i].children.count == 0) {
            if (stack[top_i].depth == 0 || game->status == STATUS_OVER) {
                assert(stack[top_i].children.count == 0);

                if (stack[top_i].chance_command.kind != COMMAND_NONE) {
                    assert(false);
                }

                // leaf node, compute value.
                double value = game_value_for_red(game);
                push_value(&values, &values_count, &values_cap,
                           (CommandValue){
                               .value = value,
                           });
                stack_count--;
            } else {
                // First time visiting this node, expand children, next iteration will push first
                // one.
                game_valid_commands(&stack[top_i].children, game);
                assert(stack[top_i].children.count > 0);
            }
        } else if (stack[top_i].children_processed < stack[top_i].children.count) {
            // Check for pruning before processing the next child
            bool should_prune = false;

            // We can prune if alpha >= beta (cutoff)
            if (stack[top_i].alpha >= stack[top_i].beta) {
                // Don't prune chance nodes
                if (stack[top_i].chance_command.kind != COMMAND_VOLLEY) {
                    should_prune = true;
                }
            }

            if (should_prune) {
                // Skip to processing the results since we're pruning
                stack[top_i].children_processed = stack[top_i].children.count;
            } else if (stack[top_i].chance_command.kind == COMMAND_VOLLEY) {
                assert(stack[top_i].children.count == 2);
                Command child_command = stack[top_i].chance_command;
                if (stack[top_i].children_processed == 0) {
                    stack[top_i].undo_child =
                        game_apply_command(game, game->turn.player, child_command, VOLLEY_HIT);
                    push_em_node(&stack, &stack_count, &stack_cap,
                                 (EMNode){
                                     .children = (CommandBuf){0},
                                     .children_processed = 0,
                                     .undo_child = (UndoCommand){0},
                                     .depth = stack[top_i].depth - 1,
                                     .chance_command = (Command){0},
                                     .alpha = stack[top_i].alpha,
                                     .beta = stack[top_i].beta,
                                 });
                    stack[top_i].children_processed++;
                } else {
                    game_undo_command(game, stack[top_i].undo_child);
                    stack[top_i].undo_child =
                        game_apply_command(game, game->turn.player, child_command, VOLLEY_MISS);
                    push_em_node(&stack, &stack_count, &stack_cap,
                                 (EMNode){
                                     .children = (CommandBuf){0},
                                     .children_processed = 0,
                                     .undo_child = (UndoCommand){0},
                                     .depth = stack[top_i].depth - 1,
                                     .chance_command = (Command){0},
                                     .alpha = stack[top_i].alpha,
                                     .beta = stack[top_i].beta,
                                 });
                    stack[top_i].children_processed++;
                }
            } else {
                if (stack[top_i].children_processed > 0) {
                    game_undo_command(game, stack[top_i].undo_child);
                }
                Command child_command =
                    stack[top_i].children.commands[stack[top_i].children_processed];
                if (child_command.kind == COMMAND_VOLLEY) {
                    // Don't apply the command, push a chance node instead.
                    push_em_node(&stack, &stack_count, &stack_cap,
                                 (EMNode){
                                     .children = (CommandBuf){.count = 2},
                                     .children_processed = 0,
                                     .undo_child = (UndoCommand){.prev_turn = game->turn},
                                     .depth = stack[top_i].depth,
                                     .chance_command = child_command,
                                     .alpha = stack[top_i].alpha,
                                     .beta = stack[top_i].beta,
                                 });
                    stack[top_i].children_processed++;
                } else {
                    stack[top_i].undo_child =
                        game_apply_command(game, game->turn.player, child_command, VOLLEY_ROLL);
                    push_em_node(&stack, &stack_count, &stack_cap,
                                 (EMNode){
                                     .children = (CommandBuf){0},
                                     .children_processed = 0,
                                     .undo_child = (UndoCommand){0},
                                     .depth = stack[top_i].depth - 1,
                                     .chance_command = (Command){0},
                                     .alpha = stack[top_i].alpha,
                                     .beta = stack[top_i].beta,
                                 });
                    stack[top_i].children_processed++;
                }
            }
        } else if (stack[top_i].children_processed == stack[top_i].children.count) {
            // Compute own value.
            if (stack[top_i].chance_command.kind == COMMAND_VOLLEY) {
                assert(stack[top_i].children.commands == NULL);
                assert(stack[top_i].children.count == 2);
                game_undo_command(game, stack[top_i].undo_child);
                values_count -= 2;
                double hit_value = values[values_count].value;
                double miss_value = values[values_count + 1].value;
                double value = 0.4167 * hit_value + (1.0 - 0.4167) * miss_value;
                push_value(&values, &values_count, &values_cap,
                           (CommandValue){
                               .value = value,
                               .hit_value = hit_value,
                               .miss_value = miss_value,
                           });
                stack_count--;
            } else {
                if (stack[top_i].children.count > 0) {
                    game_undo_command(game, stack[top_i].undo_child);
                }
                bool min_node = game->turn.player == PLAYER_BLUE;
                double best_value = min_node ? INFINITY : -INFINITY;

                // If we pruned, we might not have values for all children
                size_t values_to_pop = stack[top_i].children_processed;
                if (values_to_pop > stack[top_i].children.count) {
                    values_to_pop = stack[top_i].children.count;
                }

                // If no children were processed (due to pruning at the start), use the
                // the bound value that caused pruning
                if (values_to_pop == 0) {
                    if (min_node) {
                        best_value = stack[top_i].beta;
                    } else {
                        best_value = stack[top_i].alpha;
                    }
                } else {
                    values_count -= values_to_pop;
                    for (size_t i = 0; i < values_to_pop; i++) {
                        CommandValue command_value = values[values_count + i];

                        if (min_node) {
                            if (command_value.value <= best_value) {
                                best_value = command_value.value;
                                if (result != NULL && stack[top_i].depth == depth) {
                                    result->best_command_i = (u32)i;
                                    result->command_values[i] = command_value;
                                }
                            }
                            // Update beta (for min node)
                            if (best_value < stack[top_i].beta) {
                                stack[top_i].beta = best_value;
                            }
                        } else {
                            if (command_value.value >= best_value) {
                                best_value = command_value.value;
                                if (result != NULL && stack[top_i].depth == depth) {
                                    result->best_command_i = (u32)i;
                                    result->command_values[i] = command_value;
                                }
                            }
                            // Update alpha (for max node)
                            if (best_value > stack[top_i].alpha) {
                                stack[top_i].alpha = best_value;
                            }
                        }
                    }
                }

                push_value(&values, &values_count, &values_cap,
                           (CommandValue){
                               .value = best_value,
                           });
                if (stack[top_i].children.commands != NULL) {
                    free(stack[top_i].children.commands);
                }
                stack_count--;
            }
        } else {
            assert(0);
        }
    }

    assert(values_count == 1);
    double score = values[0].value;

    free(stack);
    free(values);

    return score;
}

u32 ai_select_command_easy(Game *game) {
    ExpectiMaxResult result = {0};
    expecti_max_node(&result, game, 3);
    return result.best_command_i;
}

u32 ai_select_command_medium(Game *game) {
    ExpectiMaxResult result = {0};
    expecti_max_node(&result, game, 4);
    return result.best_command_i;
}

u32 ai_select_command_hard(Game *game) {
    ExpectiMaxResult result = {0};
    expecti_max_node(&result, game, 5);
    return result.best_command_i;
}

int ai_select_command(void *ptr) {
    AITurn *ai_turn = (AITurn *)ptr;
    Game *game = &ai_turn->game;
    switch (ai_turn->difficulty) {
    case AIDIFF_EASY:
        ai_turn->selected_command_i = ai_select_command_easy(game);
        break;
    case AIDIFF_MEDIUM:
        ai_turn->selected_command_i = ai_select_command_medium(game);
        break;
    case AIDIFF_HARD:
        ai_turn->selected_command_i = ai_select_command_hard(game);
        break;
    default:
        assert(false);
        return -1;
    }
    return 0;
}
