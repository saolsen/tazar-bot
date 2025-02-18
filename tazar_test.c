#include "tazar_game.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main() {
    srand48(time(0));
    assert(1 == 1);
    printf("Tazar Tests\n");

    Game *game = game_alloc();
    assert(game != NULL);
    game_init(game, GAME_MODE_ATTRITION, MAP_HEX_FIELD_SMALL);
    const Piece *piece = game_piece_get(game, (CPos){-4, 0, 4});
    assert(piece->kind == PIECE_CROWN);

    return 0;
}