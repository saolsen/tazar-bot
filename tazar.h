#ifndef TAZAR_H
#define TAZAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ignore unused parameters.
#define UNUSED(x) (void)(x)

typedef uint8_t u8;
typedef uint32_t u32;
typedef int32_t i32;

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

#define CPOS_RIGHT_UP (CPos){1, -1, 0}
#define CPOS_RIGHT (CPos){1, 0, -1}
#define CPOS_RIGHT_DOWN (CPos){0, 1, -1}
#define CPOS_LEFT_DOWN (CPos){-1, 1, 0}
#define CPOS_LEFT (CPos){-1, 0, 1}
#define CPOS_LEFT_UP (CPos){0, -1, 1}

bool cpos_eq(CPos a, CPos b);

CPos cpos_add(CPos a, CPos b);

V2 v2_from_cpos(CPos cpos);

CPos cpos_from_v2(V2 dpos);

typedef enum : u8 {
    PLAYER_RED = 0b00000000,
    PLAYER_BLUE = 0b00001000,
} Player;

#define PLAYER_MASK 0b00001000

typedef enum : u8 {
    PIECE_NULL = 0b00000000,
    PIECE_EMPTY = 0b00000111,
    PIECE_PIKE = 0b00000001,
    PIECE_BOW = 0b00000010,
    PIECE_HORSE = 0b00000011,
    PIECE_CROWN = 0b00000100,
} PieceKind;

#define PIECE_KIND_MASK 0b00000111

typedef enum : u8 {
    TILE_NULL = 0b00000000,
    TILE_EMPTY = 0b00000111,
    TILE_PIKE_RED = 0b00000001,
    TILE_PIKE_BLUE = 0b00001001,
    TILE_BOW_RED = 0b00000010,
    TILE_BOW_BLUE = 0b00001010,
    TILE_HORSE_RED = 0b00000011,
    TILE_HORSE_BLUE = 0b00001011,
    TILE_CROWN_RED = 0b00000100,
    TILE_CROWN_BLUE = 0b00001100,

    // 3 invalid tiles
    TILE_INVALID_1 = 0b00000101, // red invalid piece
    TILE_INVALID_2 = 0b00001101, // blue invalid piece
    TILE_INVALID_3 = 0b00000110, // red invalid piece
    TILE_INVALID_4 = 0b00001110, // blue invalid piece
    TILE_INVALID_5 = 0b00001111, // empty but with blue player bit set.
} TileKind;

#define TILE_KIND_MASK 0b00001111
#define PIECE_ID_MAST 0b11110000

typedef struct {
    PieceKind kind;
    Player player;
    u8 id;
} Piece;

typedef enum {
    ORDER_NONE = 0,
    ORDER_MOVE,
    ORDER_VOLLEY,
    ORDER_MUSTER,
} OrderKind;

typedef struct {
    OrderKind kind;
    CPos target; // todo: maybe this should be an id also.
} Order;

typedef struct {
    u8 piece;
    Order orders[2];
    u8 order_i;
} Activation;

// A player's turn is broken up into activations and orders.
// You can activate up to two pieces per turn.
// Each activated piece can be given up to two orders.
typedef struct {
    Player player;
    Activation activations[2];
    u8 activation_i;
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
    // Stored in a 8x8 array.
    // Indexed by q and r but offset so that 0,0 is in the center.
    // So 0,0,0 is in slot board[4][4]
    u8 board[81];
    Status status;
    Player winner;
    Turn turn;
} Game;

u8 *game_piece(Game *game, CPos pos);

void game_init(Game *game, GameMode game_mode, Map map);

typedef enum {
    COMMAND_NONE = 0,
    COMMAND_MOVE,
    COMMAND_VOLLEY,
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
} Command;

// bool command_eq(Command *a, Command *b);

typedef struct {
    Command *commands;
    size_t count;
    size_t capacity;
} CommandBuf;

void game_valid_commands(CommandBuf *command_buf, Game *game);

typedef enum {
    VOLLEY_ROLL,
    VOLLEY_HIT,
    VOLLEY_MISS,
} VolleyResult;

typedef struct {
    Turn prev_turn;
    u8 prev_pieces[2];
    CPos prev_pieces_pos[2];
    u8 prev_pieces_count;
} UndoCommand;

UndoCommand game_apply_command(Game *game, Player player, Command command,
                               VolleyResult volley_result);

void game_undo_command(Game *game, UndoCommand undo);

typedef enum {
    AIDIFF_HUMAN = 0,
    AIDIFF_EASY = 1,
    AIDIFF_MEDIUM = 2,
    AIDIFF_HARD = 3,
} AIDifficulty;

typedef struct {
    Game game;
    AIDifficulty difficulty;
    void *ai_state; // Pointer to save ai state between turns if the difficulty doesn't change.
    u32 selected_command_i;
} AITurn;

int ai_select_command(void *ptr);

#endif // TAZAR_H
