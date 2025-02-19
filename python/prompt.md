<context>
I am working on an AI that plays the game tazar. It's a two player strategy game that's played on a hex board.
Here is the code I have for it now.

tazar_game.h
```c
#ifndef TAZAR_GAME_H
#define TAZAR_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t u32;
typedef int32_t  i32;

u32 rand_in_range(u32 min, u32 max);
double random_prob();

typedef struct {
    i32 x;
    i32 y;
} V2;

typedef struct {
    i32 q;
    i32 r;
    i32 s;
} CPos;

#define CPOS_RIGHT_UP                                                                              \
    (CPos) {                                                                                       \
        1, -1, 0                                                                                   \
    }
#define CPOS_RIGHT                                                                                 \
    (CPos) {                                                                                       \
        1, 0, -1                                                                                   \
    }
#define CPOS_RIGHT_DOWN                                                                            \
    (CPos) {                                                                                       \
        0, 1, -1                                                                                   \
    }
#define CPOS_LEFT_DOWN                                                                             \
    (CPos) {                                                                                       \
        -1, 1, 0                                                                                   \
    }
#define CPOS_LEFT                                                                                  \
    (CPos) {                                                                                       \
        -1, 0, 1                                                                                   \
    }
#define CPOS_LEFT_UP                                                                               \
    (CPos) {                                                                                       \
        0, -1, 1                                                                                   \
    }

bool cpos_eq(CPos a, CPos b);
CPos cpos_add(CPos a, CPos b);
V2 v2_from_cpos(CPos cpos);
CPos cpos_from_v2(V2 dpos);

typedef enum { TILE_NONE = 0, TILE_NORMAL, TILE_HILL, TILE_MARSH } Tile;

typedef enum {
    PIECE_NONE = 0,
    PIECE_CROWN,
    PIECE_PIKE,
    PIECE_HORSE,
    PIECE_BOW,
} PieceKind;

typedef enum {
    PLAYER_NONE = 0,
    PLAYER_RED,
    PLAYER_BLUE,
} Player;

typedef struct {
    CPos pos;
    PieceKind kind;
    i32 id;
    Player player;
} Piece;

i32 piece_gold(PieceKind kind);

typedef enum {
    ORDER_NONE = 0,
    ORDER_MOVE,
    ORDER_VOLLEY,
    ORDER_MUSTER,
} OrderKind;

typedef struct {
    OrderKind kind;
    CPos target;
} Order;

typedef struct {
    i32 piece_id;
    Order orders[2];
    i32 order_i;
} Activation;

// A player's turn is broken up into activations and orders.
// You can activate up to two pieces per turn.
// Each activated piece can be given up to two orders.
typedef struct {
    Player player;
    Activation activations[2];
    i32 activation_i;
} Turn;

typedef enum {
    STATUS_NONE = 0,
    STATUS_IN_PROGRESS,
    STATUS_OVER,
} Status;

typedef enum {
    GAME_MODE_NONE = 0,
    GAME_MODE_ATTRITION,
    GAME_MODE_TOURNAMENT,
} GameMode;

typedef enum {
    MAP_NONE = 0,
    MAP_HEX_FIELD_SMALL,
} Map;

typedef struct {
    // Tiles of the board and pieces on the board.
    // Indexed by q and r but offset so that 0,0 is in the center.
    // So 0,0,0 is in slot board[32][32]
    // This is certainly a lot of empty space but a uniform representation
    // like this makes it easier to pass the same shape to the (future) nn.
    // @todo: This doesn't change. Should be a const pointer to board.
    Tile *board;
    i32 board_count; // always 4096

    Piece pieces[64];
    i32 pieces_count;

    // The height and width of the board in double positions.
    // Used by the UI to know how large to draw the board.
    // @todo: Use these when scanning board and pieces so you don't have
    //        to scan the whole thing.
    i32 dpos_width;
    i32 dpos_height;

    GameMode game_mode;
    Map map;
    Status status;
    Player winner;

    // @note: hardcoded to 2 players
    i32 gold[3];        // indexed by player.
    i32 reserves[3][5]; // indexed by player and piece kind.

    Turn turn;
} Game;

bool game_eq(Game *a, Game *b);
Tile *game_tile(Game *game, CPos pos);

const Piece *game_piece_set(Game *game, CPos pos, Piece piece);
const Piece *game_piece_get(Game *game, CPos pos);
void game_piece_del(Game *game, CPos pos);

Game *game_alloc();
void game_free(Game *game);
void game_init(Game *game, GameMode game_mode, Map map);
void game_clone(Game *to, Game *from);

typedef enum {
    COMMAND_NONE = 0,
    COMMAND_MOVE,
    COMMAND_VOLLEY,
    COMMAND_MUSTER,
    COMMAND_END_TURN,
} CommandKind;

// Orders are the moves that a piece makes.
// Commands represent what the player "says" to do.
// They are almost the same thing, except that commands have slightly
// more information like the type of piece to muster and there is an
// additional command "end turn".
// The `game_valid_commands` function, and the logic in `game_apply_command`
// ensure you can only command valid orders, and rules like orders per activation
// and number of activations are enforced.
typedef struct {
    CommandKind kind;
    CPos piece_pos;
    CPos target_pos;
    PieceKind muster_piece_kind;
} Command;

bool command_eq(Command *a, Command *b);

typedef struct {
    Command *commands;
    size_t count;
    size_t cap;
} CommandBuf;

void game_valid_commands(CommandBuf *command_buf, Game *game);

typedef enum {
    VOLLEY_ROLL,
    VOLLEY_HIT,
    VOLLEY_MISS,
} VolleyResult;

void game_apply_command(Game *game, Player player, Command command, VolleyResult volley_result);

#endif // TAZAR_GAME_H
```

tazar_game.c
```c
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

u32 cpos_hash(CPos cpos, i32 range) {
    const size_t prime1 = 73856093;
    const size_t prime2 = 19349669;
    const size_t prime3 = 83492791;
    return (u32)((cpos.q * prime1) ^ (cpos.r * prime2) ^ (cpos.s * prime3)) % range;
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

// Note: must have be created with game_alloc so it has board and pieces buffers.
void game_init(Game *game, GameMode game_mode, Map map) {
    assert(game_mode == GAME_MODE_ATTRITION);
    assert(map == MAP_HEX_FIELD_SMALL);

    for (i32 i = 0; i < 4096; i++) {
        game->board[i] = TILE_NONE;
    }

    for (i32 i = 0; i < 64; i++) {
        game->pieces[i] = piece_null;
    }
    game->pieces_count = 0;

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
    CPos p = (CPos){-4, 0, 4};
    game_piece_set(game, p, (Piece){
        .kind = PIECE_CROWN,
        .pos = p,
        .id = id++,
        .player = PLAYER_RED,
    });
    p = cpos_add(p, CPOS_RIGHT_UP);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_BOW,
        .pos = p,
        .id = id++,
        .player = PLAYER_RED,
    });
    p = cpos_add(p, CPOS_RIGHT_UP);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_HORSE,
        .pos = p,
        .id = id++,
        .player = PLAYER_RED,
    });
    p = cpos_add(p, CPOS_RIGHT);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_PIKE,
        .pos = p,
        .id = id++,
        .player = PLAYER_RED,
    });
    p = cpos_add(p, CPOS_LEFT_DOWN);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_PIKE,
        .pos = p,
        .id = id++,
        .player = PLAYER_RED,
    });
    p = cpos_add(p, CPOS_LEFT_DOWN);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_BOW,
        .pos = p,
        .id = id++,
        .player = PLAYER_RED,
    });
    p = cpos_add(p, CPOS_RIGHT);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_PIKE,
        .pos = p,
        .id = id++,
        .player = PLAYER_RED,
    });
    p = cpos_add(p, CPOS_LEFT_DOWN);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_PIKE,
        .pos = p,
        .id = id++,
        .player = PLAYER_RED,
    });
    p = cpos_add(p, CPOS_LEFT);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_BOW,
        .pos = p,
        .id = id++,
        .player = PLAYER_RED,
    });
    p = cpos_add(p, CPOS_RIGHT_DOWN);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_HORSE,
        .pos = p,
        .id = id++,
        .player = PLAYER_RED,
    });
    p = cpos_add(p, CPOS_RIGHT);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_PIKE,
        .pos = p,
        .id = id++,
        .player = PLAYER_RED,
    });


    p = (CPos){4, 0, -4};
    game_piece_set(game, p, (Piece){
        .kind = PIECE_CROWN,
        .pos = p,
        .id = id++,
        .player = PLAYER_BLUE,
    });
    p = cpos_add(p, CPOS_LEFT_UP);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_BOW,
        .pos = p,
        .id = id++,
        .player = PLAYER_BLUE,
    });
    p = cpos_add(p, CPOS_LEFT_UP);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_HORSE,
        .pos = p,
        .id = id++,
        .player = PLAYER_BLUE,
    });
    p = cpos_add(p, CPOS_LEFT);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_PIKE,
        .pos = p,
        .id = id++,
        .player = PLAYER_BLUE,
    });
    p = cpos_add(p, CPOS_RIGHT_DOWN);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_PIKE,
        .pos = p,
        .id = id++,
        .player = PLAYER_BLUE,
    });
    p = cpos_add(p, CPOS_RIGHT_DOWN);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_BOW,
        .pos = p,
        .id = id++,
        .player = PLAYER_BLUE,
    });
    p = cpos_add(p, CPOS_LEFT);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_PIKE,
        .pos = p,
        .id = id++,
        .player = PLAYER_BLUE,
    });
    p = cpos_add(p, CPOS_RIGHT_DOWN);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_PIKE,
        .pos = p,
        .id = id++,
        .player = PLAYER_BLUE,
    });
    p = cpos_add(p, CPOS_RIGHT);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_BOW,
        .pos = p,
        .id = id++,
        .player = PLAYER_BLUE,
    });
    p = cpos_add(p, CPOS_LEFT_DOWN);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_HORSE,
        .pos = p,
        .id = id++,
        .player = PLAYER_BLUE,
    });
    p = cpos_add(p, CPOS_LEFT);
    game_piece_set(game, p, (Piece){
        .kind = PIECE_PIKE,
        .pos = p,
        .id = id++,
        .player = PLAYER_BLUE,
    });

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
    for (i32 i = 0; i < game->turn.activation_i; i++) {
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
    } else if (activation->piece_id == piece->id) {
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
    } else if (activation->piece_id != 0) {
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
    Piece *piece = game_piece_get(game, from);

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
                Piece *target_piece = game_piece_get(game, cpos);
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

    Piece *piece = game_piece_get(game, from);
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
            Piece *current_piece = game_piece_get(game, current);
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

                Piece *neighbor_piece = game_piece_get(game, neighbor);

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

    for (u32 p = 0; p < 64; p++) {
        Piece *piece = &(game->pieces[p]);

        // Can't use another player's piece.
        if (piece->id == 0 || piece->id == -1) {
            continue;
        }
        if (piece->player != game->turn.player) {
            continue;
        }

        AllowedOrderKinds piece_order_kinds = piece_allowed_order_kinds(game, piece);
        CPos cpos = piece->pos;

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

static bool game_command_is_valid(Game *game, Player player, Command command) {
    if (command.kind == COMMAND_NONE) {
        return false;
    }
    if (command.kind == COMMAND_END_TURN) {
        return true;
    }
    assert(command.kind == COMMAND_MOVE || command.kind == COMMAND_VOLLEY ||
           command.kind == COMMAND_MUSTER);

    Piece *piece = game_piece_get(game, command.piece_pos);
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

void game_end_turn(Game *game, Player player, Command command) {
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
    // @opt: If this is still slow, I can just watch for crown kills during update.
    int red_crowns = 0;
    int blue_crowns = 0;
    for (u32 i = 0; i < 64; i++) {
        Piece *p = &(game->pieces[i]);
        if (p->kind == PIECE_CROWN) {
            if (p->player == PLAYER_RED) {
                red_crowns++;
            } else if (p->player == PLAYER_BLUE) {
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

void game_apply_command(Game *game, Player player, Command command, VolleyResult volley_result) {
    if (!game_command_is_valid(game, player, command)) {
        return;
    }

    if (command.kind == COMMAND_END_TURN) {
        game->turn.activation_i = 2;
        game_end_turn(game, player, command);
        return;
    }

    Piece *piece = game_piece_get(game, command.piece_pos);
    Piece *target_piece = game_piece_get(game, command.target_pos);

    bool increment_activation = false;
    i32 activation_piece_id = game->turn.activations[game->turn.activation_i].piece_id;
    if (activation_piece_id != 0 && piece->id != 0 && activation_piece_id != piece->id) {
        increment_activation = true;
    }

    OrderKind order_kind = ORDER_NONE;
    i32 aquired_gold = 0;

    CPos del_pieces[2];
    u32 del_pieces_count = 0;

    Piece move_pieces[2];
    CPos move_from[2];
    CPos move_to[2];
    u32 move_pieces_count = 0;

    switch (command.kind) {
    case COMMAND_NONE: {
        break;
    }
    case COMMAND_MOVE: {
        order_kind = ORDER_MOVE;
        if (target_piece->kind != PIECE_NONE) {
            aquired_gold = piece_gold(target_piece->kind);
            del_pieces[del_pieces_count++] = target_piece->pos;
        }

        if (piece->kind == PIECE_HORSE &&
            piece_strength(target_piece->kind) >= piece_strength(piece->kind)) {
            // Horse charge, both die.
            del_pieces[del_pieces_count++] = piece->pos;

        } else {
            move_pieces[move_pieces_count] = *piece;
            move_from[move_pieces_count] = piece->pos;
            move_to[move_pieces_count] = command.target_pos;
            move_pieces_count++;
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
            aquired_gold = piece_gold(target_piece->kind);
            del_pieces[del_pieces_count++] = target_piece->pos;
        }
        break;
    }
    case COMMAND_MUSTER: {
        // @todo: Implement muster.
        // order_kind = ORDER_MUSTER;
        assert(false);
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
    activation->piece_id = piece->id;
    activation->orders[activation->order_i].kind = order_kind;
    activation->orders[activation->order_i].target = command.target_pos;
    activation->order_i++;

    // @todo: When muster is implemented, this applies to crown too.
    if (activation->order_i >= 2 || piece->kind != PIECE_BOW) {
        game->turn.activation_i++;
    }

    game->gold[game->turn.player] += aquired_gold;
    for (u32 i = 0; i < del_pieces_count; i++) {
        game_piece_del(game, del_pieces[i]);
    }
    for (u32 i = 0; i < move_pieces_count; i++) {
        Piece new_piece = move_pieces[i];
        new_piece.pos = move_to[i];
        game_piece_del(game, move_from[i]);
        game_piece_set(game, move_to[i], new_piece);
    }

    game_end_turn(game, player, command);
}
```

tazar_ai.h
```c
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
    Game *game;
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
```

tazar_ai.c
```c
#include "tazar_ai.h"
#include "tazar_game.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

i32 piece_value(PieceKind kind) {
    switch (kind) {
    case PIECE_CROWN:
        return 5;
    case PIECE_HORSE:
        return 3;
    case PIECE_BOW:
        return 2;
    case PIECE_PIKE:
        return 1;
    default:
        return 0;
    }
}

double heuristic_value(Game *game, Player player) {
    Player opponent = (player == PLAYER_RED ? PLAYER_BLUE : PLAYER_RED);

    if (game->status == STATUS_OVER) {
        if (game->winner == player) {
            return 22;
        } else {
            return -22;
        }
    }

    i32 player_value = 0;
    i32 opponent_value = 0;

    // Iterate over all positions on the board.
    for (u32 i = 0; i < 64; i++) {
        Piece *p = &(game->pieces[i]);
        if (p->kind == PIECE_NONE) {
            continue;
        }
        if (p->player == player) {
            player_value += piece_value(p->kind);
        } else if (p->player == opponent) {
            opponent_value += piece_value(p->kind);
        }
    }

    double eval = player_value - opponent_value;
    return eval;
}

// probability distribution over commands that could be chosen.
void heuristic_policy(double *weights, Game *game, Command *commands, size_t num_commands) {
    double temperature = 0.1;
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

    Game *root_game = game_alloc();
    game_clone(root_game, game);

    push_node(&nodes, &nodes_len, &nodes_cap, zero_node);
    push_node(&nodes, &nodes_len, &nodes_cap,
              (Node){
                  .kind = NODE_DECISION,
                  .game = root_game,
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
        for (size_t i=0; i < state->nodes_len; i++) {
            if (state->nodes[i].game != NULL) {
                game_free(state->nodes[i].game);
            }
        }
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
                double r = random_prob();
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

            game_valid_commands(&new_commands_buf, nodes[node_i].game);
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
                nodes[node_i].game, unexpanded_commands, unexpanded_commands_len);

            uint32_t next_child_i = nodes[node_i].first_child_i + nodes[node_i].num_children;
            Game *child_game = game_alloc();
            game_clone(child_game,nodes[node_i].game);

            if (child_command.kind == COMMAND_VOLLEY) {
                Game *hit_game = game_alloc();
                game_clone(hit_game, child_game);

                game_apply_command(hit_game, hit_game->turn.player, child_command, VOLLEY_HIT);
                game_valid_commands(&new_commands_buf, hit_game);
                size_t num_new_hit_commands = new_commands_buf.count;

                uint32_t hit_child_i = (uint32_t)nodes_len;

                Game *miss_game = game_alloc();
                game_clone(miss_game, child_game);

                game_apply_command(miss_game, miss_game->turn.player, child_command, VOLLEY_MISS);
                game_valid_commands(&new_commands_buf, miss_game);
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
                game_apply_command(child_game, child_game->turn.player, child_command, VOLLEY_ROLL);
                game_valid_commands(&new_commands_buf, child_game);
                size_t num_new_commands = new_commands_buf.count;

                nodes[next_child_i] = (Node){
                    .kind = child_game->status == STATUS_OVER ? NODE_OVER : NODE_DECISION,
                    .game = child_game,
                    .command = child_command,
                    .parent_i = node_i,
                    .first_child_i = 0,
                    .num_children = 0,
                    .num_children_to_expand = (uint32_t)num_new_commands,
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
            Player scored_player = nodes[root_i].game->turn.player;
            if (nodes[sim_i].parent_i != 0) {
                scored_player = nodes[nodes[sim_i].parent_i].game->turn.player;
            }

            // Simulation
            double score;
            if (nodes[sim_i].kind != NODE_OVER) {
                Game *sim_game = game_alloc();
                game_clone(sim_game,nodes[sim_i].game);
                ai_rollout(sim_game, (Command){0}, 300);
                score = heuristic_value(sim_game, scored_player);
                game_free(sim_game);
            } else {
                score = heuristic_value(nodes[sim_i].game, scored_player);
            }

            // Backpropagation
            while (sim_i != 0) {
                if (sim_i == root_i) {
                    if (scored_player == game->turn.player) {
                        nodes[sim_i].total_reward += score;
                    } else {
                        nodes[sim_i].total_reward -= score;
                    }
                } else if (nodes[nodes[sim_i].parent_i].game->turn.player == scored_player) {
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
```
</context>

My MCTS version is pretty good but I want to mak it a lot better by replacing `heuristic_value` with a deep learning model. From some previous research I think that a good model for this would be similar to the one that is used by chess engines like alphazero. My plan is to develop a model for the value function that takes a game state and predicts it's value, where -1 would be a loss and 1 would be a win.

Since the game is played on a 2d board and the piece locations are important I think it probably makes sense to use some convolutional layers or attention layers (but if that's not true tell me). The game state should probably then be some kind of 3d tensor board representation with channels for each important feature.

My initial thoughts are to make it a 64x64x13 tensor. 64 by 64 is the max board size. The "feature" channels would be.

* 1 channel for the tiles, 1 if it's a a valid tile, 0 if it's TILE_NONE.
* 1 channel for the tiles that are hills.
* 1 channel for the tiles that are mountains.
* 8 channels for each piece type. red-crown, red-horse, red-bow, red-pike, blue-crown, blue-horse, blue-bow,   blue-pike. Where there's a 1 if a piece of that kind is present and 0 otherwise.
* 1 channel to show which pieces can be moved. For example:
  * If it's the beginning of blue's turn, each blue piece would have a 1 and the rest would be 0's.
  * If it's the second activation of red's turn only the un-used red pieces would have a 1 and the rest would be 0's.
* 1 channel to show which pieces can do their action. This would be 1's for all the current player's bows that can still volley this turn. (In the future it would also have a 1 for the current player's crown if it can still muster).

Does this seem like a good board representation?
