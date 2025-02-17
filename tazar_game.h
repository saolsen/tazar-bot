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
    PieceKind kind;
    Player player;
    i32 id;
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
    Tile board[4096];

    // @todo: Store this in a hashmap or something.
    //        This is so sparse it's not worth storing this way.
    Piece pieces[4096];

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
Piece *game_piece(Game *game, CPos pos);

void game_init(Game *game, GameMode game_mode, Map map);

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