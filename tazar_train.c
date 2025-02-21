#include "tazar_game.h"
#include "tazar_ai.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

const char* PIECE_KIND_STRINGS[] = {
    "NONE",
    "CROWN",
    "PIKE",
    "HORSE",
    "BOW",
};

const char* PLAYER_STRINGS[] = {
    "NONE",
    "RED",
    "BLUE",
};

const char* ORDER_KIND_STRINGS[] = {
    "NONE",
    "MOVE",
    "VOLLEY",
    "MUSTER",
};

const char* STATUS_STRINGS[] = {
    "NONE",
    "IN_PROGRESS",
    "OVER",
};

void record_game_state(FILE *fp, Game *game) {
    fprintf(fp, ""
                "Game(\n"
                "    pieces={\n");

    for (int i = 0; i < 64; i++) {
        Piece *p = &(game->pieces[i]);
        if (p->kind == PIECE_NONE) {
            continue;
        }
        fprintf(fp, "        CPos(q=%d, r=%d, s=%d) : Piece(\n"
                    "            kind=PieceKind.%s,\n"
                    "            player=Player.%s,\n"
                    "            id=%d,\n"
                    "        ),\n",
                p->pos.q,
                p->pos.r,
                p->pos.s,
                PIECE_KIND_STRINGS[p->kind],
                PLAYER_STRINGS[p->player],
                p->id);
    }

    fprintf(fp, "    },\n"
                "    status=Status.%s,\n"
                "    winner=Player.%s,\n"
                "    turn=Turn(\n"
                "        player=Player.%s,\n"
                "        activations=[\n"
                "            Activation(\n"
                "                piece_id=%d,\n"
                "                orders=[\n"
                "                    Order(\n"
                "                        kind=OrderKind.%s,\n"
                "                        target=CPos(q=%d, r=%d, s=%d),\n"
                "                    ),\n"
                "                    Order(\n"
                "                        kind=OrderKind.%s,\n"
                "                        target=CPos(q=%d, r=%d, s=%d),\n"
                "                    ),\n"
                "                ],\n"
                "                order_i=%d,\n"
                "            ),\n"
                "            Activation(\n"
                "                piece_id=%d,\n"
                "                orders=[\n"
                "                    Order(\n"
                "                        kind=OrderKind.%s,\n"
                "                        target=CPos(q=%d, r=%d, s=%d),\n"
                "                    ),\n"
                "                    Order(\n"
                "                        kind=OrderKind.%s,\n"
                "                        target=CPos(q=%d, r=%d, s=%d),\n"
                "                    ),\n"
                "                ],\n"
                "                order_i=%d,\n"
                "            ),\n"
                "        ],\n"
                "        activation_i=%d,\n"
                "    ),\n"
                ")\n",
            STATUS_STRINGS[game->status],
            PLAYER_STRINGS[game->winner],
            PLAYER_STRINGS[game->turn.player],

            game->turn.activations[0].piece_id,
            ORDER_KIND_STRINGS[game->turn.activations[0].orders[0].kind],
            game->turn.activations[0].orders[0].target.q,
            game->turn.activations[0].orders[0].target.r,
            game->turn.activations[0].orders[0].target.s,
            ORDER_KIND_STRINGS[game->turn.activations[0].orders[1].kind],
            game->turn.activations[0].orders[1].target.q,
            game->turn.activations[0].orders[1].target.r,
            game->turn.activations[0].orders[1].target.s,
            game->turn.activations[0].order_i,

            game->turn.activations[1].piece_id,
            ORDER_KIND_STRINGS[game->turn.activations[1].orders[0].kind],
            game->turn.activations[1].orders[0].target.q,
            game->turn.activations[1].orders[0].target.r,
            game->turn.activations[1].orders[0].target.s,
            ORDER_KIND_STRINGS[game->turn.activations[1].orders[1].kind],
            game->turn.activations[1].orders[1].target.q,
            game->turn.activations[1].orders[1].target.r,
            game->turn.activations[1].orders[1].target.s,
            game->turn.activations[1].order_i,
            game->turn.activation_i
            );
}

// @todo: This is not a thing yet, but will be once we have a model to train to the "very hard" AI.
int main() {
    srand48(time(0));
    printf("Train Tazar\n");
    MCState mc_state = {0};

    Game *game = game_alloc();
    CommandBuf command_buf = (CommandBuf){
        .commands = malloc(1024 * sizeof(Command)),
        .count = 0,
        .cap = 1024,
    };
    int max_turns = 300;
    for (int i=0; i< 100; i++) {
        printf("Game %d\n", i);

        char filename[128];
        snprintf(filename, 128, "game_states_4_%d.py", i);
        FILE *fp = fopen(filename, "w");
        if (fp == NULL) {
            printf("Error opening file!\n");
            exit(1);
        }

        fprintf(fp, "from tazar import *\n");
        fprintf(fp, "games = [\n");

        game_init(game, GAME_MODE_ATTRITION, MAP_HEX_FIELD_SMALL);
        int turn = 0;
        while (game->status != STATUS_OVER) {
            if (turn++ > max_turns) {
                break;
            }
            record_game_state(fp, game);
            fprintf(fp, "\n,\n");

            game_valid_commands(&command_buf, game);
            mc_state = ai_mc_state_init(game, command_buf.commands, (int)command_buf.count);
            ai_mc_think(&mc_state, game, command_buf.commands, (int)command_buf.count, 500);
            Command command =
                ai_mc_select_command(&mc_state, game, command_buf.commands, (int)command_buf.count);
            game_apply_command(game, game->turn.player, command, VOLLEY_ROLL);
        }
        record_game_state(fp, game);
        fprintf(fp, "]\n");
        if (turn > max_turns) {
            printf("No Winner, max turns reached.\n");
        } else {
            printf("Winner: %s\n", game->winner == PLAYER_RED ? "Red" : "Blue");
        }
        fclose(fp);
    }
    return 0;
}