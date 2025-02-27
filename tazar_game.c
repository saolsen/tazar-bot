#include "tazar_game.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
# include <emscripten.h>

double random_prob() {
    float rand_f = emscripten_random();
    return (double)rand_f;
}

u32 rand_in_range(u32 min, u32 max) {
    float rand_f = emscripten_random();
    return (u32)(rand_f * (float)(max - min)) + min;
}

#else

double random_prob() {
    return drand48();
}

u32 rand_in_range(u32 min, u32 max) {
    return arc4random_uniform(max-min) + min;
}

#endif

bool cpos_eq(CPos a, CPos b) {
    return a.q == b.q && a.r == b.r && a.s == b.s;
}

CPos cpos_add(CPos a, CPos b) {
    return (CPos){a.q + b.q, a.r + b.r, a.s + b.s};
}

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

// todo: bring back game_eq for mcts tree re-use.
#if 0

static Tile tile_null = TILE_NONE;
static Piece piece_null = {
    .kind = PIECE_NONE,
    .pos = (CPos){0, 0, 0},
    .player = PLAYER_NONE,
    .id = 0,
};
static Piece piece_deleted = {
    .kind = PIECE_NONE,
    .pos = (CPos){0, 0, 0},
    .player = PLAYER_NONE,
    .id = -1,
};

// The board is indexed by q and r
// but offset so that 0,0 is in the center.
Tile *game_tile(Game *game, CPos pos) {
    if (pos.q < -32 || pos.q > 31 || pos.r < -32 || pos.r > 31) {
    return &tile_null;
}
return &game->board[(pos.r + 32) * 64 + (pos.q + 32)];
}

// id for never set pieces is 0.
// id for deleted pieces is -1.

const Piece *game_piece_set(Game *game, CPos pos, Piece piece) {
    assert(game->pieces_count < 64);
    assert(cpos_eq(piece.pos, pos) == true);
    assert(piece.kind != PIECE_NONE);
    assert(piece.id != 0);
    assert(piece.id != -1);
    u32 i = cpos_hash(pos, 64);
    for (u32 probe_inc = 1; probe_inc < 64; probe_inc++) {
        if (game->pieces[i].id == 0 ||
            game->pieces[i].id == -1 ||
            (game->pieces[i].id > 0 && cpos_eq(game->pieces[i].pos, pos))) {
            // @todo: Not sure if this should be an error or not.
            if (game->pieces[i].kind != PIECE_NONE && cpos_eq(game->pieces[i].pos, pos)) {
                assert("key already exists!" && false);
            }
            break;
        }
        i = (i + probe_inc) % 64;
    }
    if (game->pieces[i].id <= 0) {
        game->pieces_count++;
    }
    game->pieces[i] = piece;
    return &game->pieces[i];
}

const Piece *game_piece_get(Game *game, CPos pos) {
    assert(game->pieces_count < 64);
    u32 i = cpos_hash(pos, 64);
    for (u32 probe_inc = 1; probe_inc < 64; probe_inc++) {
        if ((game->pieces[i].id > 0 && cpos_eq(game->pieces[i].pos, pos))) {
            return &game->pieces[i];
        } else if (game->pieces[i].id == 0) {
            return &piece_null;
        }
        i = (i + probe_inc) % 64;
    }
    return &piece_null;
}

void game_piece_del(Game *game, CPos pos) {
    assert(game->pieces_count < 64);
    u32 i = cpos_hash(pos, 64);
    for (u32 probe_inc = 1; probe_inc < 64; probe_inc++) {
        if ((game->pieces[i].id > 0 && cpos_eq(game->pieces[i].pos, pos))) {
            game->pieces[i] = piece_deleted;
            game->pieces_count--;
            break;
        }
        i = (i + probe_inc) % 64;
    }
}

Game *game_alloc() {
    Game *game = calloc(sizeof(*game), 1);
    game->board = malloc(64 * 64 * sizeof(*game->board));
    game->board_count = 64 * 64;
    return game;
}

void game_free(Game *game) {
    assert(game->board != NULL);
    free(game->board);
    free(game);
}

void game_clone(Game *to, Game *from) {
    Tile *to_board = to->board;
    memcpy(to, from, sizeof(Game));
    to->board = to_board;
    memcpy(to->board, from->board, 64 * 64 * sizeof(*to->board));
}

#endif

u8 piece_null = 0;

u8 *game_piece(Game *game, CPos pos) {
    if (pos.q < -4 || pos.q > 4 || pos.r < -4 || pos.r > 4) {
        return &piece_null;
    }
    return &game->board[(pos.r + 4) * 9 + (pos.q + 4)];
}

u8 piece_pack(Piece piece) {
    assert(piece.id < 16);
    return (u8)(piece.id << 4 | piece.player | piece.kind);
}

void game_init(Game *game, GameMode game_mode, Map map) {
    for (i32 i = 0; i < 81; i++) {
        game->board[i] = TILE_NULL;
    }

    // @note: Hardcoded to "Hex Field Small".
    for (i32 q = -4; q <= 4; q++) {
        for (i32 r = -4; r <= 4; r++) {
            for (i32 s = -4; s <= 4; s++) {
                if (q + r + s != 0) {
                    continue;
                }
                CPos cpos = {q, r, s};
                *game_piece(game, cpos) = TILE_EMPTY;
            }
        }
    }

    // @note: Hardcoded to "attrition" on "Hex Field Small".

    CPos p = (CPos){-4, 0, 4};
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_CROWN,
        .player = PLAYER_RED,
        .id = 1,
    });
    p = cpos_add(p, CPOS_RIGHT_UP);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_BOW,
        .player = PLAYER_RED,
        .id = 1,
    });
    p = cpos_add(p, CPOS_RIGHT_UP);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_HORSE,
        .player = PLAYER_RED,
        .id = 1,
    });
    p = cpos_add(p, CPOS_RIGHT);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_PIKE,
        .player = PLAYER_RED,
        .id = 1,
    });
    p = cpos_add(p, CPOS_LEFT_DOWN);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_PIKE,
        .player = PLAYER_RED,
        .id = 2,
    });
    p = cpos_add(p, CPOS_LEFT_DOWN);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_BOW,
        .player = PLAYER_RED,
        .id = 2,
    });
    p = cpos_add(p, CPOS_RIGHT);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_PIKE,
        .player = PLAYER_RED,
        .id = 3,
    });
    p = cpos_add(p, CPOS_LEFT_DOWN);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_PIKE,
        .player = PLAYER_RED,
        .id = 4,
    });
    p = cpos_add(p, CPOS_LEFT);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_BOW,
        .player = PLAYER_RED,
        .id = 3,
    });
    p = cpos_add(p, CPOS_RIGHT_DOWN);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_HORSE,
        .player = PLAYER_RED,
        .id = 2,
    });
    p = cpos_add(p, CPOS_RIGHT);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_PIKE,
        .player = PLAYER_RED,
        .id = 5,
    });
    p = (CPos){4, 0, -4};
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_CROWN,
        .player = PLAYER_BLUE,
        .id = 1,
    });
    p = cpos_add(p, CPOS_LEFT_UP);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_BOW,
        .player = PLAYER_BLUE,
        .id = 1,
    });
    p = cpos_add(p, CPOS_LEFT_UP);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_HORSE,
        .player = PLAYER_BLUE,
        .id = 1,
    });
    p = cpos_add(p, CPOS_LEFT);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_PIKE,
        .player = PLAYER_BLUE,
        .id = 1,
    });
    p = cpos_add(p, CPOS_RIGHT_DOWN);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_PIKE,
        .player = PLAYER_BLUE,
        .id = 2,
    });
    p = cpos_add(p, CPOS_RIGHT_DOWN);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_BOW,
        .player = PLAYER_BLUE,
        .id = 2,
    });
    p = cpos_add(p, CPOS_LEFT);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_PIKE,
        .player = PLAYER_BLUE,
        .id = 3,
    });
    p = cpos_add(p, CPOS_RIGHT_DOWN);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_PIKE,
        .player = PLAYER_BLUE,
        .id = 4,
    });
    p = cpos_add(p, CPOS_RIGHT);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_BOW,
        .player = PLAYER_BLUE,
        .id = 3,
    });
    p = cpos_add(p, CPOS_LEFT_DOWN);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_HORSE,
        .player = PLAYER_BLUE,
        .id = 2,
    });
    p = cpos_add(p, CPOS_LEFT);
    *game_piece(game, p) = piece_pack((Piece){
        .kind = PIECE_PIKE,
        .player = PLAYER_BLUE,
        .id = 5,
    });


    game->status = STATUS_IN_PROGRESS;

    game->turn.player = PLAYER_RED;
    for (i32 i = 0; i < 2; i++) {
        game->turn.activations[i].piece = 0;
        for (i32 ii = 0; ii < 2; ii++) {
            game->turn.activations[i].orders[ii].kind = ORDER_NONE;
            game->turn.activations[i].orders[ii].target = (CPos){0, 0, 0};
        }
        game->turn.activations[i].order_i = 0;
    }
    game->turn.activation_i = 1; // @note: Special case for attrition.
}

#if 0
bool command_eq(Command *a, Command *b) {
    return a->kind == b->kind && cpos_eq(a->piece_pos, b->piece_pos) &&
           cpos_eq(a->target_pos, b->target_pos) && a->muster_piece_kind == b->muster_piece_kind;
}
#endif

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
static AllowedOrderKinds piece_allowed_order_kinds(Game *game, u8 piece) {
    // Can't do anything with a piece that was already activated this turn.
    bool piece_already_used = false;
    for (i32 i = 0; i < game->turn.activation_i; i++) {
        if (game->turn.activations[i].piece == piece) {
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

    if (activation->piece == 0) {
        // New activation, can do either move or action.
        piece_can_move = true;
        piece_can_action = true;
    } else if (activation->piece == piece) {
        // If piece_id is set we must have done something.
        assert(activation->order_i > 0);
        piece_can_move = true;
        piece_can_action = true;
        // We can only do any not already done orders.
        for (i32 i = 0; i < activation->order_i; i++) {
            if (activation->orders[i].kind == ORDER_MOVE) {
                piece_can_move = false;
            } else if (activation->orders[i].kind == ORDER_VOLLEY ||
                       activation->orders[i].kind == ORDER_MUSTER) {
                piece_can_action = false;
            }
        }
    } else if (activation->piece != 0) {
        // Using this piece would end the current activation and start a new one.
        // Can only do this if there are more activations left.
        if (game->turn.activation_i + 1 >= 2) {
            piece_can_move = false;
            piece_can_action = false;
        }

    } else {
        assert(false);
    }

    // Horses and pikes don't have an action, they can only move.
    if ((piece & PIECE_KIND_MASK) == PIECE_PIKE || (piece & PIECE_KIND_MASK) == PIECE_HORSE) {
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
    u8 piece = *game_piece(game, from);

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
                u8 target_piece = *game_piece(game, cpos);
                if (target_piece == TILE_NULL || target_piece == TILE_EMPTY) {
                    continue;
                }

                if ((target_piece & PLAYER_MASK) != (piece & PLAYER_MASK)) {
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

    u8 piece = *game_piece(game, from);
    PieceKind kind = piece & PIECE_KIND_MASK;
    assert(piece != 0);

    i32 movement = piece_movement(kind);
    i32 max_strength = piece_strength(kind) - 1;
    // Horses can move into the tile of any other piece.
    if (kind == PIECE_HORSE) {
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

            // Don't continue moving through another piece.
            u8 current_piece = *game_piece(game, current);
            if (i > 0 && current_piece != TILE_EMPTY) {
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
                u8 neighbor_piece = *game_piece(game, neighbor);
                if (neighbor_piece == 0) {
                    continue;
                }
                if (neighbor_piece != TILE_EMPTY) {
                    // Don't move into a tile with a piece of the same player.
                    if ((neighbor_piece & PLAYER_MASK) == (piece & PLAYER_MASK)) {
                        continue;
                    }
                    // If neighbor is occupied by a piece, don't move there if we
                    // can't kill it.
                    if (piece_strength(neighbor_piece & PIECE_KIND_MASK) > max_strength) {
                        continue;
                    }
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

void push_command(CommandBuf *command_buf, Command command) {
    if (command_buf->count >= command_buf->capacity) {
        if (command_buf->capacity == 0) {
            command_buf->capacity = 2;
        } else {
            command_buf->capacity *= 2;
        }
        Command *new_buf = realloc(command_buf->commands, command_buf->capacity * sizeof(Command));
        assert(new_buf != NULL);
        command_buf->commands = new_buf;
    }
    command_buf->commands[command_buf->count++] = command;
}

void game_valid_commands(CommandBuf *command_buf, Game *game) {
    command_buf->count = 0;

    if (game->status != STATUS_IN_PROGRESS) {
        return;
    }

    // You can always end your turn.
    push_command(command_buf, (Command){
        .kind = COMMAND_END_TURN,
        .piece_pos = (CPos){0, 0, 0},
        .target_pos = (CPos){0, 0, 0},
    });

    for (i32 q = -4; q <= 4; q++) {
        for (i32 r = -4; r <= 4; r++) {
            for (i32 s = -4; s <= 4; s++) {
                if (q + r + s != 0) {
                    continue;
                }
                CPos cpos = {q, r, s};
                u8 piece = *game_piece(game, cpos);

                if (piece == TILE_NULL || piece == TILE_EMPTY) {
                    continue;
                }

                // Can't use another player's piece.
                if ((piece & PLAYER_MASK) != game->turn.player) {
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
                        push_command(command_buf, (Command){
                            .kind = COMMAND_MOVE,
                            .piece_pos = cpos,
                            .target_pos = target,
                        });
                    }
                }
                if (piece_order_kinds.piece_can_action) {
                    if ((piece & PIECE_KIND_MASK) == PIECE_BOW) {
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
                            push_command(command_buf, (Command){
                                .kind = COMMAND_VOLLEY,
                                .piece_pos = cpos,
                                .target_pos = target,
                            });
                        }
                    } else if ((piece & PIECE_KIND_MASK) == PIECE_CROWN) {
                        // @todo: Implement muster.
                    } else {
                        assert(false);
                    }
                }
            }
        }
    }
}

void game_end_turn(Game *game, Player player, Command command) {
    if (game->turn.activation_i >= 2) {
        // Turn is over.
        game->turn.player = game->turn.player == PLAYER_RED ? PLAYER_BLUE : PLAYER_RED;
        for (u8 i = 0; i < 2; i++) {
            game->turn.activations[i].piece = 0;
            for (size_t ii = 0; ii < 2; ii++) {
                game->turn.activations[i].orders[ii].kind = ORDER_NONE;
                game->turn.activations[i].orders[ii].target = (CPos){0, 0, 0};
            }
            game->turn.activations[i].order_i = 0;
        }
        game->turn.activation_i = 0;
    }

    // Check for game over.
    // @opt: If this is still slow, I can just watch for crown kills during update.
    i32 red_crowns = 0;
    i32 blue_crowns = 0;
    for (u32 i = 0; i < 81; i++) {
        u8 p = game->board[i];
        if ((p & PIECE_KIND_MASK) == PIECE_CROWN) {
            if ((p & PLAYER_MASK) == PLAYER_RED) {
                red_crowns++;
            } else if ((p & PLAYER_MASK) == PLAYER_BLUE) {
                blue_crowns++;
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

UndoCommand game_apply_command(Game *game, Player player, Command command, VolleyResult volley_result) {
    UndoCommand undo = {
        .prev_turn = game->turn,
        .prev_pieces = {0, 0},
        .prev_pieces_pos = {{0, 0, 0}, {0, 0, 0}},
        .prev_pieces_count = 0,
    };

    if (command.kind == COMMAND_NONE) {
        return undo;
    }

    if (command.kind == COMMAND_END_TURN) {
        game->turn.activation_i = 2;
        game_end_turn(game, player, command);
        return undo;
    }

    u8 *piece = game_piece(game, command.piece_pos);
    u8 *target_piece = game_piece(game, command.target_pos);

    undo.prev_pieces[0] = *piece;
    undo.prev_pieces_pos[0] = command.piece_pos;
    undo.prev_pieces[1] = *target_piece;
    undo.prev_pieces_pos[1] = command.target_pos;
    undo.prev_pieces_count = 2;

    bool increment_activation = false;
    u8 activation_piece = game->turn.activations[game->turn.activation_i].piece;
    if (activation_piece != 0 && *piece != 0 && activation_piece != *piece) {
        increment_activation = true;
    }

    OrderKind order_kind = ORDER_NONE;

    u8 set_pieces[2];
    CPos set_pieces_pos[2];
    u8 set_pieces_count = 0;

    switch (command.kind) {
    case COMMAND_NONE: {
        break;
    }
    case COMMAND_MOVE: {
        order_kind = ORDER_MOVE;
        set_pieces[set_pieces_count] = TILE_EMPTY;
        set_pieces_pos[set_pieces_count] = command.piece_pos;
        set_pieces_count++;

        if ((*piece & PIECE_KIND_MASK) == PIECE_HORSE &&
            *target_piece != TILE_EMPTY &&
            piece_strength(*target_piece & PIECE_KIND_MASK) >= piece_strength(PIECE_HORSE)) {

            // Horse charge, both die.
            set_pieces[set_pieces_count] = TILE_EMPTY;
            set_pieces_pos[set_pieces_count] = command.target_pos;
            set_pieces_count++;
        } else {
            set_pieces[set_pieces_count] = *piece;
            set_pieces_pos[set_pieces_count] = command.target_pos;
            set_pieces_count++;
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
            u32 die_1 = rand_in_range(1, 6);
            u32 die_2 = rand_in_range(1, 6);
            u32 roll = die_1 + die_2;
            volley_hits = roll < 7;
            break;
        }
        default: {
            assert(0);
        }
        }
        if (volley_hits) {
            set_pieces[set_pieces_count] = TILE_EMPTY;
            set_pieces_pos[set_pieces_count] = command.target_pos;
            set_pieces_count++;
        }
        break;
    }
    case COMMAND_END_TURN: {
        // handled already.
        assert(false);
    }
    }

    // Update Game.
    if (increment_activation) {
        game->turn.activation_i++;
    }

    Activation *activation = &(game->turn.activations[game->turn.activation_i]);
    activation->piece = *piece;
    activation->orders[activation->order_i].kind = order_kind;
    activation->orders[activation->order_i].target = command.target_pos;
    activation->order_i++;

    // @todo: When muster is implemented, this applies to crown too.
    if (activation->order_i >= 2 || (*piece & PIECE_KIND_MASK) != PIECE_BOW) {
        game->turn.activation_i++;
    }

    for (u8 i = 0; i < set_pieces_count; i++) {
        *game_piece(game, set_pieces_pos[i]) = set_pieces[i];
    }

    game_end_turn(game, player, command);
    return undo;
}

void game_undo_command(Game *game, UndoCommand undo) {
    game->status = STATUS_IN_PROGRESS;
    game->turn = undo.prev_turn;

    for (u8 i = 0; i < undo.prev_pieces_count; i++) {
        *game_piece(game, undo.prev_pieces_pos[i]) = undo.prev_pieces[i];
    }
}