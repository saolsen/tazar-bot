// A really basic implementation of Tazar and an AI player, so I have someone to play with.
// https://kelleherbros.itch.io/tazar

#include "tazar_game.h"
#include "tazar_ai.h"
#include "tazar_ai_mcts.h"

#include "raylib.h"
#define RAYGUI_IMPLEMENTATION
#include "raygui.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>

EMSCRIPTEN_KEEPALIVE
void set_window_size(int width, int height) {
    SetWindowSize(width, height);
}

//double random_prob() {
//    float rand_f = emscripten_random();
//    return (double)rand_f;
//}

#endif

#include <assert.h>
#include <math.h>
#include <time.h>

typedef enum {
    UI_STATE_CONFIGURING,
    UI_STATE_WAITING_FOR_SELECTION,
    UI_STATE_WAITING_FOR_COMMAND,
    UI_STATE_AI_THINKING,
    UI_STATE_AI_PREVIEW,
    UI_STATE_GAME_OVER,
} UIState;

typedef enum {
    DIFFICULTY_NONE,
    DIFFICULTY_EASY,
    DIFFICULTY_MEDIUM,
    DIFFICULTY_HARD,
} Difficulty;

UIState ui_state = UI_STATE_CONFIGURING;

// Configuring State
int selected_game_mode = 0;
int selected_map = 0;

// Game State
Game *game = NULL;
CommandBuf command_buf = {
    .commands = NULL,
    .count = 0,
    .cap = 0,
};

CPos selected_cpos = {0, 0, 0};

int selected_difficulty = 0;
Difficulty difficulty = DIFFICULTY_NONE;

Command chosen_ai_command = (Command) {
    .kind = COMMAND_NONE,
    .piece_pos = {0, 0, 0},
    .target_pos = {0, 0, 0},
    .muster_piece_kind = PIECE_NONE,
};

int ai_thinking_frames_left = 0;
int ai_lag_frames_left = 0;

MCState mc_state = {0};
MCTSState mcts_state = {0};

void ui_update_draw() {
#ifdef __EMSCRIPTEN__
    double w, h;
    emscripten_get_element_css_size( "#canvas", &w, &h );
    SetWindowSize((int)w, (int)h);
#endif

    int width = GetScreenWidth();
    int height = GetScreenHeight();

    Vector2 mouse_position = GetMousePosition();

    // Draw
    BeginDrawing();
    ClearBackground(RAYWHITE);
    if (ui_state == UI_STATE_CONFIGURING) {

        GuiLabel((Rectangle){20, 20, 200, 20}, "Game Mode");
        GuiDropdownBox((Rectangle){20, 40, 200, 20}, "Attrition", &selected_game_mode, 1);

        GuiLabel((Rectangle){20, 90, 200, 20}, "Map");
        GuiDropdownBox((Rectangle){20, 110, 200, 20}, "Hex Field Small", &selected_map, 1);

        if (GuiButton((Rectangle){20, 170, 200, 40}, "Start Game")) {
            assert(selected_game_mode >= 0 && selected_game_mode < 1);
            assert(selected_map >= 0 && selected_map < 1);

            GameMode game_mode = (GameMode)(selected_game_mode + 1);
            Map map = (Map)(selected_map + 1);

            game_init(game, game_mode, map);

            ui_state = UI_STATE_WAITING_FOR_SELECTION;
        };
    } else {
        game_valid_commands(&command_buf, game);
        assert(command_buf.count <= 1024);

        Rectangle game_area = {0, 0, (float)width, (float)height - 120.0f};
        Vector2 game_center = {game_area.x + game_area.width / 2, game_area.y + game_area.height / 2};

        Rectangle control_area = {0, (float)height-120.0f, (float)width, 120.0f};
        Vector2 control_center = {control_area.x + control_area.width / 2, control_area.y + control_area.height / 2};

        DrawRectangleRec(game_area, DARKGRAY);
        DrawCircle((int)game_center.x, (int)game_center.y, 10, RED);

        // Controls
        DrawRectangleRec(control_area, RAYWHITE);
        GuiLabel((Rectangle){20, control_area.y, 100, 20}, "Bot Difficulty");
        GuiDropdownBox((Rectangle){20, control_area.y + 20, 100, 20}, "Easy;Medium;Hard", &selected_difficulty,
                       ui_state == UI_STATE_WAITING_FOR_SELECTION || ui_state == UI_STATE_WAITING_FOR_COMMAND ? 1 : 0);

        if (game->winner != PLAYER_NONE) {
            DrawText("GAME OVER", (int)(control_center.x - 60), (int)control_area.y+20, 19, BLACK);
            if (game->winner == PLAYER_RED) {
                DrawText("RED WINS", (int)(control_center.x - 60), (int)control_area.y + 50, 19, RED);
            } else {
                DrawText("BLUE WINS", (int)(control_center.x - 60), (int)control_area.y + 50, 19, BLUE);
            }
        } else if (game->turn.player == PLAYER_RED) {
            DrawText("RED's TURN", (int)(control_center.x - 60), (int)control_area.y+20, 19, RED);
        } else {
            DrawText("BLUE's TURN", (int)(control_center.x - 60), (int)control_area.y+20, 19, BLUE);
        }

        //AI Progress Bar
        if (ui_state == UI_STATE_AI_THINKING) {
            float progress =
                1.0f - ((float)ai_thinking_frames_left / (float)(60*5));
            Rectangle progress_bar_bg = {control_center.x - 60, control_area.y + 50, 232, 20};
            Rectangle progress_bar = {control_center.x - 60, control_area.y + 50, 232 * progress, 20};

            DrawRectangleRec(progress_bar_bg, LIGHTGRAY);
            DrawRectangleRec(progress_bar, BLUE);
            DrawRectangleLines((int)progress_bar_bg.x, (int)progress_bar_bg.y,
                               (int)progress_bar_bg.width, (int)progress_bar_bg.height, GRAY);
            DrawText("AI THINKING...", (int)control_center.x - 60, (int)control_area.y + 80, 19, GRAY);
        }

        bool clicked_end_turn = false;
        if (ui_state == UI_STATE_WAITING_FOR_COMMAND ||
            ui_state == UI_STATE_WAITING_FOR_SELECTION) {
            if (GuiButton((Rectangle){(float)width - 120, control_area.y + 20, 100, 20}, "END TURN")) {
                clicked_end_turn = true;
            }
        }

        bool mouse_in_game_area = CheckCollisionPointRec(mouse_position, game_area);
        bool mouse_in_board = false;
        CPos mouse_cpos = {0, 0, 0};
        bool mouse_clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

        // @todo: This has to take into account height and width to fit the map correctly.
        float hexes_across = 14.0f;
        float hex_radius = floorf(game_area.width * sqrtf(3.0f) / (3 * hexes_across));
        float horizontal_offset = sqrtf(hex_radius * hex_radius - (hex_radius / 2) * (hex_radius / 2));
        float vertical_offset = hex_radius * 1.5f;
        V2 screen_center = {10, 4};

        // @note: Hardcoded to hex field small still.
        for (int row = 0; row <= 8; row++) {
            for (int col = 0; col <= 20; col++) {
                if ((row % 2 == 0 && col % 2 != 0) || (row % 2 == 1 && col % 2 != 1)) {
                    // Not a valid double coordinate.
                    continue;
                }
                V2 dpos = {col - screen_center.x, row - screen_center.y};
                CPos cpos = cpos_from_v2(dpos);
                Tile tile = *game_tile(game, cpos);
                if (tile == TILE_NONE) {
                    continue;
                }

                // Check Mouse Position.
                if (CheckCollisionPointCircle(
                        mouse_position,
                        (Vector2){game_center.x + horizontal_offset * (float)dpos.x,
                                  game_center.y + vertical_offset * (float)dpos.y},
                        horizontal_offset)) {
                    mouse_in_board = true;
                    mouse_cpos = cpos;
                }
            }
        }

        // Update
        if (clicked_end_turn) {
            game_apply_command(game, game->turn.player,
                               ((Command){
                                   .kind = COMMAND_END_TURN,
                                   .piece_pos = (CPos){0, 0, 0},
                                   .target_pos = (CPos){0, 0, 0},
                               }),
                               VOLLEY_ROLL);
            game_valid_commands(&command_buf, game);
            assert(command_buf.count <= 1024);
            ui_state = UI_STATE_WAITING_FOR_SELECTION;
            selected_cpos = (CPos){0, 0, 0};
        }
        if (ui_state == UI_STATE_WAITING_FOR_COMMAND) {
            if (mouse_clicked && mouse_in_game_area && !mouse_in_board) {
                ui_state = UI_STATE_WAITING_FOR_SELECTION;
                selected_cpos = (CPos){0, 0, 0};
            } else if (mouse_clicked && mouse_in_board) {
                bool matched_command = false;
                Command command;
                for (size_t i = 0; i < command_buf.count; i++) {
                    command = command_buf.commands[i];
                    if (cpos_eq(command.piece_pos, selected_cpos) &&
                        cpos_eq(command.target_pos, mouse_cpos)) {
                        matched_command = true;
                        break;
                    }
                }
                if (matched_command) {
                    Piece selected_piece = *game_piece_get(game, selected_cpos);
                    i32 selected_piece_id = selected_piece.id;
                    assert(selected_piece_id != 0);

                    // Apply the command.
                    game_apply_command(game, game->turn.player, command, VOLLEY_ROLL);

                    // See if the piece still exists.
                    bool piece_still_exists = false;
                    CPos new_piece_cpos = (CPos){0, 0, 0};
                    if (game_piece_get(game, command.piece_pos)->id == selected_piece_id) {
                        piece_still_exists = true;
                        new_piece_cpos = command.piece_pos;
                    } else if (game_piece_get(game, command.target_pos)->id == selected_piece_id) {
                        piece_still_exists = true;
                        new_piece_cpos = command.target_pos;
                    }

                    // Get the new commands.
                    game_valid_commands(&command_buf, game);
                    assert(command_buf.count <= 1024);

                    if (command_buf.count == 1) {
                        // If there are no more commands for this player, end the turn.
                        assert(command_buf.commands[0].kind == COMMAND_END_TURN);
                        game_apply_command(game, game->turn.player,
                                           (Command){
                                               .kind = COMMAND_END_TURN,
                                               .piece_pos = (CPos){0, 0, 0},
                                               .target_pos = (CPos){0, 0, 0},
                                           },
                                           VOLLEY_ROLL);
                        game_valid_commands(&command_buf, game);
                        assert(command_buf.count <= 1024);
                        ui_state = UI_STATE_WAITING_FOR_SELECTION;
                        selected_cpos = (CPos){0, 0, 0};
                    } else if (piece_still_exists) {
                        // If there are more commands for this piece, select it.
                        bool has_another_command = false;
                        for (size_t i = 0; i < command_buf.count; i++) {
                            Command next_command = command_buf.commands[i];
                            if (cpos_eq(next_command.piece_pos, new_piece_cpos) && next_command.kind != COMMAND_END_TURN) {
                                has_another_command = true;
                                break;
                            }
                        }
                        if (has_another_command) {
                            ui_state = UI_STATE_WAITING_FOR_COMMAND;
                            selected_cpos = new_piece_cpos;
                        } else {
                            ui_state = UI_STATE_WAITING_FOR_SELECTION;
                            selected_cpos = (CPos){0, 0, 0};
                        }
                    } else {
                        ui_state = UI_STATE_WAITING_FOR_SELECTION;
                        selected_cpos = (CPos){0, 0, 0};
                    }
                } else {
                    ui_state = UI_STATE_WAITING_FOR_SELECTION;
                    selected_cpos = (CPos){0, 0, 0};
                }
            }
        } else if (ui_state == UI_STATE_WAITING_FOR_SELECTION) {
            if (mouse_clicked && mouse_in_board) {
                Piece selected_piece = *game_piece_get(game, mouse_cpos);
                if (selected_piece.player == game->turn.player) {
                    for (size_t i = 0; i < command_buf.count; i++) {
                        Command command = command_buf.commands[i];
                        if (cpos_eq(command.piece_pos, mouse_cpos)) {
                            ui_state = UI_STATE_WAITING_FOR_COMMAND;
                            selected_cpos = mouse_cpos;
                            break;
                        }
                    }
                }
            }
        }

        // AI Turn
        if (ui_state == UI_STATE_AI_THINKING) {
            // Think if there's still time left.
            if (ai_thinking_frames_left-- > 0) {
                switch (difficulty) {
                case DIFFICULTY_MEDIUM: {
                    ai_mc_think(&mc_state, game, command_buf.commands, (int)command_buf.count,
                                10);
                    break;
                }
                case DIFFICULTY_HARD: {
                    ai_mcts_think(&mcts_state, game, command_buf.commands,
                                  (int)command_buf.count, 10);
                    break;
                }
                default:
                    break;
                }
            }

            // Pick the command when time is up.
            if (ai_thinking_frames_left <= 0) {
                switch (difficulty) {
                case DIFFICULTY_EASY: {
                    chosen_ai_command =
                        ai_select_command_heuristic(game, command_buf.commands, command_buf.count);
                    break;
                }
                case DIFFICULTY_MEDIUM: {
                    chosen_ai_command =
                        ai_mc_select_command(&mc_state, game, command_buf.commands, (int)command_buf.count);
                    ai_mc_state_cleanup(&mc_state);
                    break;
                }
                case DIFFICULTY_HARD: {
                    chosen_ai_command =
                        ai_mcts_select_command(&mcts_state, game, command_buf.commands, (int)command_buf.count);
                    ai_mcts_state_cleanup(&mcts_state);
                    break;
                }
                default: {
                    assert(false);
                }
                }
                ui_state = UI_STATE_AI_PREVIEW;
                selected_cpos = chosen_ai_command.piece_pos;
                ai_lag_frames_left = 45;
            }
        }

        if (ui_state == UI_STATE_AI_PREVIEW) {
            if (ai_lag_frames_left-- <= 0) {
                // Apply the command.
                game_apply_command(game, game->turn.player, chosen_ai_command, VOLLEY_ROLL);
                game_valid_commands(&command_buf, game);
                assert(command_buf.count <= 1024);

                chosen_ai_command = (Command){0};
                selected_cpos = (CPos){0, 0, 0};

                if (game->status == STATUS_OVER) {
                    ui_state = UI_STATE_GAME_OVER;
                } else {
                    ui_state = UI_STATE_WAITING_FOR_SELECTION;
                }
            }
        }

        if (ui_state == UI_STATE_WAITING_FOR_SELECTION && game->status != STATUS_OVER &&
            game->turn.player == PLAYER_BLUE) {
            // It's now AI's turn.
            chosen_ai_command = (Command) {
                .kind = COMMAND_NONE,
                .piece_pos = {0, 0, 0},
                .target_pos = {0, 0, 0},
                .muster_piece_kind = PIECE_NONE,
            };

            // Initialize the AI state and start thinking.
            difficulty = (Difficulty)selected_difficulty + 1;
            switch (difficulty) {
            case DIFFICULTY_EASY: {
                // Easy doesn't need any time to think.
                ai_thinking_frames_left = 0;
                break;
            }
            case DIFFICULTY_MEDIUM: {
                mc_state = ai_mc_state_init(game, command_buf.commands, (int)command_buf.count);
                ai_thinking_frames_left = 60 * 5;
                break;
            }
            case DIFFICULTY_HARD: {
                mcts_state = ai_mcts_state_init(&mcts_state, game, command_buf.commands, (int)command_buf.count);
                ai_thinking_frames_left = 60 * 5;
                break;
            }
            default: {
                assert(false);
            }
            }
            ui_state = UI_STATE_AI_THINKING;
        }

        // @note: Hardcoded to hex field small still.
        for (i32 row = 0; row <= 8; row++) {
            for (i32 col = 0; col <= 20; col++) {
                if ((row % 2 == 0 && col % 2 != 0) || (row % 2 == 1 && col % 2 != 1)) {
                    // Not a valid double coordinate.
                    continue;
                }

                V2 dpos = {col - screen_center.x, row - screen_center.y};
                CPos cpos = cpos_from_v2(dpos);
                Tile tile = *game_tile(game, cpos);
                Piece piece = *game_piece_get(game, cpos);
                if (tile == TILE_NONE) {
                    continue;
                }

                Vector2 screen_pos = (Vector2){game_center.x + (horizontal_offset * (float)dpos.x),
                                               game_center.y + vertical_offset * (float)dpos.y};

                DrawPoly(screen_pos, 6, hex_radius, 90, LIGHTGRAY);
                DrawPolyLines(screen_pos, 6, hex_radius, 90, BLACK);

                Color color;
                if (piece.player == PLAYER_RED) {
                    color = RED;
                } else if (piece.player == PLAYER_BLUE) {
                    color = BLUE;
                } else {
                    color = PINK; // draw pink for invalid stuff, helps with debugging.
                }

                if (ui_state == UI_STATE_WAITING_FOR_COMMAND && cpos_eq(cpos, selected_cpos)) {
                    color = RAYWHITE;
                } else if (ui_state == UI_STATE_AI_PREVIEW && cpos_eq(cpos, chosen_ai_command.piece_pos)) {
                    color = RAYWHITE;
                }

                switch (piece.kind) {
                case PIECE_CROWN: {
                    DrawTriangle(
                        (Vector2){screen_pos.x, screen_pos.y - hex_radius / 2},
                        (Vector2){screen_pos.x - hex_radius / 2, screen_pos.y + hex_radius / 4},
                        (Vector2){screen_pos.x + hex_radius / 2, screen_pos.y + hex_radius / 4},
                        color);
                    DrawTriangle(
                        (Vector2){screen_pos.x - hex_radius / 2, screen_pos.y - hex_radius / 4},
                        (Vector2){screen_pos.x, screen_pos.y + hex_radius / 2},
                        (Vector2){screen_pos.x + hex_radius / 2, screen_pos.y - hex_radius / 4},
                        color);
                    break;
                }
                case PIECE_PIKE:
                    DrawRectangleV(
                        (Vector2){screen_pos.x - hex_radius / 2, screen_pos.y - hex_radius / 2},
                        (Vector2){hex_radius, hex_radius}, color);
                    break;
                case PIECE_HORSE:
                    DrawTriangle(
                        (Vector2){screen_pos.x, screen_pos.y - hex_radius / 2},
                        (Vector2){screen_pos.x - hex_radius / 2, screen_pos.y + hex_radius / 4},
                        (Vector2){screen_pos.x + hex_radius / 2, screen_pos.y + hex_radius / 4},
                        color);
                    break;
                case PIECE_BOW:
                    DrawCircleV(screen_pos, hex_radius / 2, color);
                    break;
                default:
                    break;
                }
            }
        }

        // Preview actions on hover.
        if (mouse_in_board && (ui_state == UI_STATE_WAITING_FOR_SELECTION || ui_state == UI_STATE_WAITING_FOR_COMMAND)) {
            Tile hovered_tile = *game_tile(game, mouse_cpos);
            Piece hovered_piece = *game_piece_get(game, mouse_cpos);
            if (!(ui_state == UI_STATE_WAITING_FOR_COMMAND && cpos_eq(mouse_cpos, selected_cpos))
                && !(hovered_tile == TILE_NONE || hovered_piece.kind == PIECE_NONE)
                && hovered_piece.player == game->turn.player) {
                Color color = hovered_piece.player == PLAYER_RED ? MAROON : DARKBLUE;

                for (size_t i = 0; i < command_buf.count; i++) {
                    Command command = command_buf.commands[i];
                    if (cpos_eq(command.piece_pos, mouse_cpos)) {
                        V2 target_dpos = v2_from_cpos(command.target_pos);
                        Vector2 target_screen_pos = (Vector2){game_center.x + (horizontal_offset * (float)target_dpos.x),
                                                              game_center.y + vertical_offset * (float)target_dpos.y};
                        if (command.kind == COMMAND_MOVE) {
                            DrawPolyLinesEx(target_screen_pos, 6, hex_radius - 2, 90, 2, color);
                        } else if (command.kind == COMMAND_VOLLEY) {
                            DrawCircle((int)target_screen_pos.x, (int)target_screen_pos.y, hex_radius / 4, color);
                        }
                    }
                }
            }
        }

        // Preview selected piece actions.
        if (ui_state == UI_STATE_WAITING_FOR_COMMAND || ui_state == UI_STATE_AI_PREVIEW) {
            for (size_t i = 0; i < command_buf.count; i++) {
                Command command = command_buf.commands[i];
                if ((ui_state == UI_STATE_WAITING_FOR_COMMAND && cpos_eq(command.piece_pos, selected_cpos)) ||
                    (ui_state == UI_STATE_AI_PREVIEW && cpos_eq(command.piece_pos, chosen_ai_command.piece_pos))) {
                    V2 target_dpos = v2_from_cpos(command.target_pos);
                    Vector2 target_pos =
                        (Vector2){game_center.x + (horizontal_offset * (float)target_dpos.x),
                                  game_center.y + vertical_offset * (float)target_dpos.y};
                    if (command.kind == COMMAND_MOVE) {
                        DrawPolyLinesEx(target_pos, 6, hex_radius - 1, 90, 4, RAYWHITE);
                    } else if (command.kind == COMMAND_VOLLEY) {
                        DrawCircle((int)target_pos.x, (int)target_pos.y, hex_radius / 4, RAYWHITE);
                    }
                }
            }
        }
    }
    EndDrawing();
}

int main(void) {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1024, 768, "Tazar Bot");
    SetExitKey(0); // disable "ESC" closing the app.

    game = game_alloc();
    command_buf = (CommandBuf){
        .commands = malloc(1024 * sizeof(Command)),
        .count = 0,
        .cap = 1024,
    };

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(ui_update_draw, 0, 1);
#else
    SetTargetFPS(60);   // Set our game to run at 60 frames-per-second
    //--------------------------------------------------------------------------------------

    // Main game loop
    while (!WindowShouldClose()) {
        ui_update_draw();
    }
#endif

    CloseWindow();
    return 0;
}
