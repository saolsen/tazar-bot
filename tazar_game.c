#include "tazar_game.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

V2 v2_from_cpos(CPos cpos) {
    i32 x = 2 * cpos.q + cpos.r;
    i32 y = cpos.r;
    return (V2){x, y};
}

CPos cpos_from_v2(V2 dpos) {
    i32 q = (dpos.x - dpos.y) / 2;
    i32 r = dpos.y;
    i32 s = -q - r;
    return (CPos){q, r, s};
}

bool cpos_eq(CPos a, CPos b) {
    return a.q == b.q && a.r == b.r && a.s == b.s;
}

CPos cpos_add(CPos a, CPos b) {
    return (CPos){a.q + b.q, a.r + b.r, a.s + b.s};
}

bool game_eq(Game *a, Game *b) {
    // @note: We don't check the tiles since they don't change.
    //        We also don't really need to check dpos width and height
    //        or game mode, but we do anyway because it's fast.
    if (a->dpos_width != b->dpos_width || a->dpos_height != b->dpos_height ||
        a->game_mode != b->game_mode || a->map != b->map || a->status != b->status ||
        a->winner != b->winner) {
        return false;
    }
    for (i32 i = 0; i < 3; i++) {
        if (a->gold[i] != b->gold[i]) {
            return false;
        }
        for (i32 ii = 0; ii < 5; ii++) {
            if (a->reserves[i][ii] != b->reserves[i][ii]) {
                return false;
            }
        }
    }

    if (a->turn.player != b->turn.player || a->turn.activation_i != b->turn.activation_i) {
        return false;
    }
    for (i32 i = 0; i < 2; i++) {
        if (a->turn.activations[i].piece_id != b->turn.activations[i].piece_id ||
            a->turn.activations[i].order_i != b->turn.activations[i].order_i) {
            return false;
        }
        for (int ii = 0; ii < 2; ii++) {
            if (a->turn.activations[i].orders[ii].kind != b->turn.activations[i].orders[ii].kind ||
                !cpos_eq(a->turn.activations[i].orders[ii].target,
                         b->turn.activations[i].orders[ii].target)) {
                return false;
            }
        }
    }

    // @todo: Compare the pieces.

    return true;
}

static Tile tile_null = TILE_NONE;
static Piece piece_null = {
    .kind = PIECE_NONE,
    .player = PLAYER_NONE,
    .pos = (CPos){0, 0, 0},
    .id = 0,
};

// The board is indexed by q and r
// but offset so that 0,0 is in the center.

Tile *game_tile(Game *game, CPos pos) {
    if (pos.q < -32 || pos.q > 31 || pos.r < -32 || pos.r > 31) {
        return &tile_null;
    }
    return &game->board[(pos.r + 32) * 64 + (pos.q + 32)];
}

Piece *game_piece(Game *game, CPos pos) {
    if (pos.q < -32 || pos.q > 31 || pos.r < -32 || pos.r > 31) {
        return &piece_null;
    }
    return &game->pieces[(pos.r + 32) * 64 + (pos.q + 32)];
}

void game_init(Game *game, GameMode game_mode, Map map) {
    assert(game_mode == GAME_MODE_ATTRITION);
    assert(map == MAP_HEX_FIELD_SMALL);

    // Clear out the board and pieces.
    for (i32 i = 0; i < 4096; i++) {
        game->board[i] = TILE_NONE;
        game->pieces[i] = (Piece){
            .kind = PIECE_NONE,
            .player = PLAYER_NONE,
            .pos = (CPos){0, 0, 0},
            .id = 0,
        };
    }

    // @note: Hardcoded to "Hex Field Small".
    for (i32 q = -4; q <= 4; q++) {
        for (i32 r = -4; r <= 4; r++) {
            for (i32 s = -4; s <= 4; s++) {
                if (q + r + s != 0) {
                    continue;
                }
                CPos cpos = {q, r, s};
                Tile *tile = game_tile(game, cpos);
                *tile = TILE_NORMAL;
            }
        }
    }

    // @note: Hardcoded to "attrition" on "Hex Field Small".
    i32 id = 1;
    Piece red_crown = {
        .kind = PIECE_CROWN,
        .player = PLAYER_RED,
    };
    Piece red_pike = {
        .kind = PIECE_PIKE,
        .player = PLAYER_RED,
    };
    Piece red_horse = {
        .kind = PIECE_HORSE,
        .player = PLAYER_RED,
    };
    Piece red_bow = {
        .kind = PIECE_BOW,
        .player = PLAYER_RED,
    };
    CPos p = (CPos){-4, 0, 4};
    *game_piece(game, p) = red_crown;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_RIGHT_UP);
    *game_piece(game, p) = red_bow;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_RIGHT_UP);
    *game_piece(game, p) = red_horse;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_RIGHT);
    *game_piece(game, p) = red_pike;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_LEFT_DOWN);
    *game_piece(game, p) = red_pike;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_LEFT_DOWN);
    *game_piece(game, p) = red_bow;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_RIGHT);
    *game_piece(game, p) = red_pike;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_LEFT_DOWN);
    *game_piece(game, p) = red_pike;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_LEFT);
    *game_piece(game, p) = red_bow;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_RIGHT_DOWN);
    *game_piece(game, p) = red_horse;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_RIGHT);
    *game_piece(game, p) = red_pike;
    game_piece(game, p)->id = id++;

    Piece blue_crown = {
        .kind = PIECE_CROWN,
        .player = PLAYER_BLUE,
    };
    Piece blue_pike = {
        .kind = PIECE_PIKE,
        .player = PLAYER_BLUE,
    };
    Piece blue_horse = {
        .kind = PIECE_HORSE,
        .player = PLAYER_BLUE,
    };
    Piece blue_bow = {
        .kind = PIECE_BOW,
        .player = PLAYER_BLUE,
    };
    p = (CPos){4, 0, -4};
    *game_piece(game, p) = blue_crown;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_LEFT_UP);
    *game_piece(game, p) = blue_bow;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_LEFT_UP);
    *game_piece(game, p) = blue_horse;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_LEFT);
    *game_piece(game, p) = blue_pike;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_RIGHT_DOWN);
    *game_piece(game, p) = blue_pike;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_RIGHT_DOWN);
    *game_piece(game, p) = blue_bow;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_LEFT);
    *game_piece(game, p) = blue_pike;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_RIGHT_DOWN);
    *game_piece(game, p) = blue_pike;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_RIGHT);
    *game_piece(game, p) = blue_bow;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_LEFT_DOWN);
    *game_piece(game, p) = blue_horse;
    game_piece(game, p)->id = id++;
    p = cpos_add(p, CPOS_LEFT);
    *game_piece(game, p) = blue_pike;
    game_piece(game, p)->id = id++;

    game->dpos_width = 17;
    game->dpos_height = 9;

    game->game_mode = game_mode;
    game->map = map;
    game->status = STATUS_IN_PROGRESS;
    game->winner = PLAYER_NONE;

    for (i32 i = 0; i < 3; i++) {
        game->gold[i] = 0;
        for (i32 ii = 0; ii < 5; ii++) {
            game->reserves[i][ii] = 0;
        }
    }

    game->turn.player = PLAYER_RED;
    for (i32 i = 0; i < 2; i++) {
        game->turn.activations[i].piece_id = 0;
        for (i32 ii = 0; ii < 2; ii++) {
            game->turn.activations[i].orders[ii].kind = ORDER_NONE;
            game->turn.activations[i].orders[ii].target = (CPos){0, 0, 0};
        }
        game->turn.activations[i].order_i = 0;
    }
    game->turn.activation_i = 1; // @note: Special case for attrition.
}

bool command_eq(Command *a, Command *b) {
    return a->kind == b->kind && cpos_eq(a->piece_pos, b->piece_pos) &&
           cpos_eq(a->target_pos, b->target_pos) && a->muster_piece_kind == b->muster_piece_kind;
}

static i32 piece_movement(PieceKind kind) {
    switch (kind) {
    case PIECE_CROWN:
        return 1;
    case PIECE_PIKE:
        return 2;
    case PIECE_HORSE:
        return 4;
    case PIECE_BOW:
        return 2;
    default:
        return 0;
    }
}

static i32 piece_strength(PieceKind kind) {
    switch (kind) {
    case PIECE_CROWN:
        return 0;
    case PIECE_PIKE:
        return 3;
    case PIECE_HORSE:
        return 2;
    case PIECE_BOW:
        return 1;
    default:
        return -1;
    }
}

i32 piece_gold(PieceKind kind) {
    switch (kind) {
    case PIECE_CROWN:
        return 6;
    case PIECE_PIKE:
        return 1;
    case PIECE_HORSE:
        return 3;
    case PIECE_BOW:
        return 2;
    default:
        return 0;
    }
}

typedef struct {
    bool piece_can_move;
    bool piece_can_action;
} AllowedOrderKinds;

// Return which kinds of orders a piece can do right now.
static AllowedOrderKinds piece_allowed_order_kinds(Game *game, Piece *piece) {
    // Can't do anything with a piece that was already activated this turn.
    bool piece_already_used = false;
    for (size_t i = 0; i < game->turn.activation_i; i++) {
        if (game->turn.activations[i].piece_id == piece->id) {
            piece_already_used = true;
            break;
        }
    }
    if (piece_already_used) {
        return (AllowedOrderKinds){
            .piece_can_move = false,
            .piece_can_action = false,
        };
    }

    bool piece_can_move = true;
    bool piece_can_action = true;

    assert(game->turn.activation_i < 2);
    Activation *activation = &(game->turn.activations[game->turn.activation_i]);

    if (activation->piece_id == 0) {
        // New activation, can do either move or action.
        piece_can_move = true;
        piece_can_action = true;
    } else if (activation->piece_id != 0) {
        // Using this piece would end the current activation and start a new one.
        // Can only do this if there are more activations left.
        if (game->turn.activation_i + 1 >= 2) {
            piece_can_move = false;
            piece_can_action = false;
        }
    } else if (activation->piece_id == piece->id) {
        // If piece_id is set we must have done something.
        assert(activation->order_i > 0);
        piece_can_move = true;
        piece_can_action = true;
        // We can only do any not already done orders.
        for (size_t i = 0; i < activation->order_i; i++) {
            if (activation->orders[i].kind == ORDER_MOVE) {
                piece_can_move = false;
            } else if (activation->orders[i].kind == ORDER_VOLLEY ||
                       activation->orders[i].kind == ORDER_MUSTER) {
                piece_can_action = false;
            }
        }
    } else {
        assert(false);
    }

    // Horses and pikes don't have an action, they can only move.
    if (piece->kind == PIECE_PIKE || piece->kind == PIECE_HORSE) {
        piece_can_action = false;
    }

    return (AllowedOrderKinds){
        .piece_can_move = piece_can_move,
        .piece_can_action = piece_can_action,
    };
}

typedef struct {
    CPos *targets;
    size_t count;
    size_t cap;
} CPosBuf;

// Check the 18 tiles around the piece for other players pieces.
static size_t volley_targets(CPosBuf *target_buf, Game *game, CPos from) {
    assert(target_buf->cap >= 18);
    size_t num_targets = 0;
    Piece *piece = game_piece(game, from);

    for (i32 r = -2; r <= 2; r++) {
        for (i32 q = -2; q <= 2; q++) {
            for (i32 s = -2; s <= 2; s++) {
                if (q + r + s != 0) {
                    continue;
                }
                CPos offset = {q, r, -q - r};

                // Can't shoot yourself.
                if (cpos_eq(offset, (CPos){0, 0, 0})) {
                    continue;
                }

                CPos cpos = cpos_add(from, offset);
                Piece *target_piece = game_piece(game, cpos);
                if (target_piece->player != PLAYER_NONE && target_piece->player != piece->player) {
                    assert(num_targets == target_buf->count);
                    assert(target_buf->count < target_buf->cap);
                    target_buf->targets[num_targets] = cpos;
                    target_buf->count++;
                    num_targets++;
                    assert(target_buf->count == num_targets);
                }
            }
        }
    }
    assert(num_targets == target_buf->count);
    assert(target_buf->count <= target_buf->cap);
    return num_targets;
}

// Walk in all directions from the piece and add all valid targets to the buffer.
static size_t move_targets(CPosBuf *targets_buf, Game *game, CPos from) {
    assert(targets_buf->cap >= 64);

    Piece *piece = game_piece(game, from);
    assert(piece->kind != PIECE_NONE);

    i32 movement = piece_movement(piece->kind);
    i32 max_strength = piece_strength(piece->kind) - 1;
    // Horses can move into the tile of any other piece.
    if (piece->kind == PIECE_HORSE) {
        max_strength = 3;
    }

    // @todo: Pending response from the bros, max_strength for crown might be 0
    //       if it can kill another crown.

    CPos visited[64];
    size_t visited_count = 0;
    visited[visited_count++] = from;

    size_t i = 0;
    for (i32 steps = 0; steps < movement; steps++) {
        for (size_t current_count = visited_count; i < current_count; i++) {
            CPos current = visited[i];

            // @todo: Handle terrain movement rules.
            // Tile *tile = game_tile(game, current);

            // Don't continue moving through another piece.
            Piece *current_piece = game_piece(game, current);
            if (i > 0 && current_piece->kind != PIECE_NONE) {
                continue;
            }

            // Check neighboring tiles.
            CPos neighbors[6] = {
                cpos_add(current, CPOS_RIGHT_UP),   cpos_add(current, CPOS_RIGHT),
                cpos_add(current, CPOS_RIGHT_DOWN), cpos_add(current, CPOS_LEFT_DOWN),
                cpos_add(current, CPOS_LEFT),       cpos_add(current, CPOS_LEFT_UP),
            };
            for (size_t n = 0; n < 6; n++) {
                CPos neighbor = neighbors[n];

                // Skip if we've already checked this neighbor.
                bool already_checked = false;
                for (size_t ii = 0; ii < visited_count; ii++) {
                    if (cpos_eq(visited[ii], neighbor)) {
                        already_checked = true;
                        break;
                    }
                }
                if (already_checked) {
                    continue;
                }

                // Don't step off the board.
                Tile *neighbor_tile = game_tile(game, neighbor);
                if (*neighbor_tile == TILE_NONE) {
                    continue;
                }

                Piece *neighbor_piece = game_piece(game, neighbor);

                // Don't move into a tile with a piece of the same player.
                if (neighbor_piece->player == piece->player) {
                    continue;
                }

                // If neighbor is occupied by a piece, don't move there if we
                // can't kill it.
                if (piece_strength(neighbor_piece->kind) > max_strength) {
                    continue;
                }

                // We can move here.
                visited[visited_count++] = neighbor;
            }
        }
    }

    // Copy results to targets_buf.
    // Skip the first target since it's the starting position.
    assert(visited_count <= targets_buf->cap);
    targets_buf->count = visited_count - 1;
    for (size_t visited_i = 1; visited_i < visited_count; visited_i++) {
        targets_buf->targets[visited_i - 1] = visited[visited_i];
    }
    return visited_count - 1;
}

// @todo: I might want to let this function realloc the buf if it's too small.

void game_valid_commands(CommandBuf *command_buf, Game *game) {
    command_buf->count = 0;

    // You can always end your turn.
    if (command_buf->count < command_buf->cap) {
        command_buf->commands[command_buf->count++] = (Command){
            .kind = COMMAND_END_TURN,
            .piece_pos = (CPos){0, 0, 0},
            .target_pos = (CPos){0, 0, 0},
            .muster_piece_kind = PIECE_NONE,
        };
    }

    // Iterate through all the pieces on the board.
    // @todo: Bound this search with dpos_width and dpos_height.
    // @todo: Might be faster to collect the cpos in one pass and then
    //        evaluate them.
    for (i32 r = -32; r <= 31; r++) {
        for (i32 q = -32; q <= 31; q++) {
            for (i32 s = -32; s <= 31; s++) {
                if (q + r + s != 0) {
                    continue;
                }
                CPos cpos = {q, r, -q - r};
                Piece *piece = game_piece(game, cpos);

                // Can't use another player's piece.
                if (piece->player != game->turn.player) {
                    continue;
                }

                AllowedOrderKinds piece_order_kinds = piece_allowed_order_kinds(game, piece);

                if (piece_order_kinds.piece_can_move) {
                    CPos targets[64];
                    CPosBuf targets_buf = {
                        .targets = &(targets[0]),
                        .count = 0,
                        .cap = 64,
                    };
                    size_t targets_count = move_targets(&targets_buf, game, cpos);
                    assert(targets_count <= 64);
                    assert(targets_count == targets_buf.count);
                    for (size_t i = 0; i < targets_count; i++) {
                        CPos target = targets[i];
                        if (command_buf->count < command_buf->cap) {
                            command_buf->commands[command_buf->count++] = (Command){
                                .kind = COMMAND_MOVE,
                                .piece_pos = cpos,
                                .target_pos = target,
                                .muster_piece_kind = PIECE_NONE,
                            };
                        }
                    }
                }

                if (piece_order_kinds.piece_can_action) {
                    if (piece->kind == PIECE_BOW) {
                        CPos targets[18];
                        CPosBuf targets_buf = {
                            .targets = &(targets[0]),
                            .count = 0,
                            .cap = 18,
                        };
                        size_t targets_count = volley_targets(&targets_buf, game, cpos);
                        assert(targets_count <= 18);
                        assert(targets_count == targets_buf.count);
                        for (size_t i = 0; i < targets_count; i++) {
                            CPos target = targets[i];
                            if (command_buf->count < command_buf->cap) {
                                command_buf->commands[command_buf->count++] = (Command){
                                    .kind = COMMAND_VOLLEY,
                                    .piece_pos = cpos,
                                    .target_pos = target,
                                    .muster_piece_kind = PIECE_NONE,
                                };
                            }
                        }
                    } else if (piece->kind == PIECE_CROWN) {
                        // @todo: Implement muster.
                    } else {
                        assert(false);
                    }
                }
            }
        }
    }
}

static bool game_command_is_valid(Game *game, Player player, Command command,
                                  VolleyResult volley_result) {
    if (command.kind == COMMAND_NONE) {
        return false;
    }
    if (command.kind == COMMAND_END_TURN) {
        return true;
    }
    assert(command.kind == COMMAND_MOVE || command.kind == COMMAND_VOLLEY ||
           command.kind == COMMAND_MUSTER);

    Piece *piece = game_piece(game, command.piece_pos);
    if (piece->player != player) {
        printf("Not your piece\n");
        return false;
    }

    AllowedOrderKinds piece_order_kinds = piece_allowed_order_kinds(game, piece);
    if (command.kind == COMMAND_MOVE && !piece_order_kinds.piece_can_move) {
        return false;
    }
    if (command.kind == COMMAND_VOLLEY && !piece_order_kinds.piece_can_action) {
        return false;
    }
    if (command.kind == COMMAND_MUSTER && !piece_order_kinds.piece_can_action) {
        return false;
    }

    // @todo: Check the target.
    //        See if it's out of bounds.
    //        See if it's in range for the order.
    return true;
}

void game_end_turn(Game *game, Player player, Command command, VolleyResult volley_result) {
    if (game->turn.activation_i >= 2) {
        // Turn is over.
        game->turn.player = game->turn.player == PLAYER_RED ? PLAYER_BLUE : PLAYER_RED;
        for (size_t i = 0; i < 2; i++) {
            game->turn.activations[i].piece_id = 0;
            for (size_t ii = 0; ii < 2; ii++) {
                game->turn.activations[i].orders[ii].kind = ORDER_NONE;
                game->turn.activations[i].orders[ii].target = (CPos){0, 0, 0};
            }
            game->turn.activations[i].order_i = 0;
        }
        game->turn.activation_i = 0;
    }

    // Check for game over.
    // @todo: This will also be much faster with better piece storage.
    int red_crowns = 0;
    int blue_crowns = 0;
    for (i32 r = -32; r <= 31; r++) {
        for (i32 q = -32; q <= 31; q++) {
            for (i32 s = -32; s <= 31; s++) {
                if (q + r + s != 0) {
                    continue;
                }
                CPos cpos = {q, r, -q - r};
                Piece *p = game_piece(game, cpos);
                if (p->kind == PIECE_CROWN) {
                    if (p->player == PLAYER_RED) {
                        red_crowns++;
                    } else if (p->player == PLAYER_BLUE) {
                        blue_crowns++;
                    }
                }
            }
        }
    }
    if (red_crowns == 0) {
        game->status = STATUS_OVER;
        game->winner = PLAYER_BLUE;
    } else if (blue_crowns == 0) {
        game->status = STATUS_OVER;
        game->winner = PLAYER_RED;
    }
}

void game_apply_command(Game *game, Player player, Command command, VolleyResult volley_result) {
    if (!game_command_is_valid(game, player, command, volley_result)) {
        return;
    }

    if (command.kind == COMMAND_END_TURN) {
        game->turn.activation_i = 2;
        game_end_turn(game, player, command, volley_result);
        return;
    }

    Piece *piece = game_piece(game, command.piece_pos);
    Piece *target_piece = game_piece(game, command.target_pos);

    bool increment_activation = false;
    i32 activation_piece_id = game->turn.activations[game->turn.activation_i].piece_id;
    if (activation_piece_id != 0 && piece->id != 0 && activation_piece_id != piece->id) {
        increment_activation = true;
    }

    OrderKind order_kind = ORDER_NONE;
    i32 aquired_gold = 0;
    Piece new_piece = *piece;
    Piece new_target_piece = *target_piece;

    switch (command.kind) {
    case COMMAND_NONE: {
        break;
    }
    case COMMAND_MOVE: {
        order_kind = ORDER_MOVE;
        aquired_gold = piece_gold(target_piece->kind);
        new_piece = (Piece){.kind = PIECE_NONE, .player = PLAYER_NONE, .id = 0};

        if (piece->kind == PIECE_HORSE &&
            piece_strength(target_piece->kind) >= piece_strength(piece->kind)) {
            // Horse charge, both die.
            new_target_piece = (Piece){.kind = PIECE_NONE, .player = PLAYER_NONE, .id = 0};
        } else {
            new_target_piece = *piece;
        }
        break;
    }
    case COMMAND_VOLLEY: {
        order_kind = ORDER_VOLLEY;
        bool volley_hits;
        switch (volley_result) {
        case VOLLEY_HIT: {
            volley_hits = true;
            break;
        }
        case VOLLEY_MISS: {
            volley_hits = false;
            break;
        }
        case VOLLEY_ROLL: {
            int die_1 = 1 + rand() / (RAND_MAX / (6 - 1 + 1) + 1);
            int die_2 = 1 + rand() / (RAND_MAX / (6 - 1 + 1) + 1);
            int roll = die_1 + die_2;
            volley_hits = roll < 7;
            break;
        }
        }
        if (volley_hits) {
            aquired_gold = piece_gold(target_piece->kind);
            new_target_piece = (Piece){.kind = PIECE_NONE, .player = PLAYER_NONE, .id = 0};
        }
        break;
    }
    case COMMAND_MUSTER: {
        order_kind = ORDER_MUSTER;
        // @todo: Implement muster.
        assert(false);
        break;
    }
    case COMMAND_END_TURN: {
        // handled already.
        assert(false);
        break;
    }
    }

    // Update Game.
    if (increment_activation) {
        game->turn.activation_i++;
    }

    Activation *activation = &(game->turn.activations[game->turn.activation_i]);
    activation->piece_id = piece->id;
    activation->orders[activation->order_i].kind = order_kind;
    activation->orders[activation->order_i].target = command.target_pos;
    activation->order_i++;

    // @todo: When muster is implemented, this applies to crown too.
    if (activation->order_i >= 2 || piece->kind != PIECE_BOW) {
        game->turn.activation_i++;
    }

    game->gold[game->turn.player] += aquired_gold;
    *piece = new_piece;
    *target_piece = new_target_piece;

    game_end_turn(game, player, command, volley_result);
}
