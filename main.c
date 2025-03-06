#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include "dcimgui.h"
#include "dcimgui_impl_sdl3.h"
#include "dcimgui_impl_sdlrenderer3.h"

#include "tazar.h"

#include <stdio.h>

// Now that the basics are working. Here's the next steps.
// * See if I can get my own font working, I like the berkeley one.
//   * Having problems with highdpi, not sure how to fix that yet.
//   * https://github.com/ocornut/imgui/issues/7779
// * Get SIMD working on the main platforms, and then get SIMD working in wasm.
// * Build an arena that I can use in wasm builds too.
// * Get CMAKE to build release bundles for the platforms that handle portable packaging and assets
// correctly.

typedef enum {
    UI_STATE_WAITING_FOR_SELECTION,
    UI_STATE_WAITING_FOR_COMMAND,
    UI_STATE_AI_THINKING,
    UI_STATE_AI_PREVIEW,
    UI_STATE_GAME_OVER,
} UIState;

typedef struct {
    int window_width;
    int window_height;

    SDL_Window *window;
    SDL_Renderer *renderer;

    ImGuiContext *imgui_context;
    ImGuiIO *io;

    AITurn ai_turn;
    SDL_Thread *ai_turn_thread;

    int ai_preview_frames_left;

    UIState ui_state;
    Game game;
    CommandBuf command_buf;

    u8 selected_piece;
    CPos selected_cpos;
    Command selected_command;

    int difficulty;
    char command_log[4096];
} AppState;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    AppState *app = SDL_malloc(sizeof(AppState));
    if (!app) {
        SDL_Log("Couldn't allocate memory for app state: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    *appstate = app;

    app->window_width = 1920;
    app->window_height = 1080;

    SDL_SetAppMetadata("tazar_bot", "1.0", "computer.steve.tazar-bot");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (!SDL_CreateWindowAndRenderer(
            "examples/renderer/debug-text", app->window_width, app->window_height,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &app->window, &app->renderer)) {
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    app->imgui_context = ImGui_CreateContext(NULL);
    app->io = ImGui_GetIO();
    app->io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    app->io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    ImFontAtlas_AddFontFromFileTTF(app->io->Fonts, "DroidSans.ttf", 24.0f, NULL, NULL);

    ImGui_StyleColorsLight(NULL);

    cImGui_ImplSDL3_InitForSDLRenderer(app->window, app->renderer);
    cImGui_ImplSDLRenderer3_Init(app->renderer);

    game_init(&app->game, GAME_MODE_ATTRITION, MAP_HEX_FIELD_SMALL);
    game_valid_commands(&app->command_buf, &app->game);
    assert(app->command_buf.count <= 1024);

    app->ui_state = UI_STATE_WAITING_FOR_SELECTION;
    app->selected_piece = 0;
    app->selected_cpos = (CPos){0, 0, 0};
    app->selected_command = (Command){0};

    app->difficulty = 0;
    app->command_log[0] = '\0';

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    AppState *app = (AppState *)appstate;

    cImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_WINDOW_RESIZED) {
        app->window_width = event->window.data1;
        app->window_height = event->window.data2;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState *app = (AppState *)appstate;

    bool apply_command = false;

    cImGui_ImplSDLRenderer3_NewFrame();
    cImGui_ImplSDL3_NewFrame();
    ImGui_NewFrame();

    float scale_x, scale_y;
    SDL_GetRenderScale(app->renderer, &scale_x, &scale_y);
    assert(scale_x == scale_y);

    // ImVec2 window_size = ImGui_GetContentRegionAvail();
    ImGui_SetNextWindowSize(app->io->DisplaySize, ImGuiCond_Always);
    ImGui_SetNextWindowPos((ImVec2){0, 0}, ImGuiCond_Always);
    if (ImGui_Begin("Tazar Bot", NULL,
                    ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoTitleBar |
                        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoResize)) {
        if (ImGui_BeginMenuBar()) {
            if (ImGui_BeginMenu("Game")) {
                if (ImGui_MenuItem("New Game")) {
                    game_init(&app->game, GAME_MODE_ATTRITION, MAP_HEX_FIELD_SMALL);
                    game_valid_commands(&app->command_buf, &app->game);
                    app->ui_state = UI_STATE_WAITING_FOR_SELECTION;
                    app->selected_piece = 0;
                    app->selected_cpos = (CPos){0, 0, 0};
                    app->selected_command = (Command){0};
                    app->command_log[0] = '\0';
                }
                ImGui_EndMenu();
            }
            ImGui_EndMenuBar();
        }

        // Left
        {
            ImGui_BeginChild("left pane", (ImVec2){400, 0},
                             ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX, 0);

            // Difficulty selector
            ImGui_Text("Difficulty");
            ImGui_Combo("##difficulty", &app->difficulty, "Human\0Easy\0Medium\0");
            ImGui_Separator();

            // Command log
            ImGui_Text("Game Log");
            ImGui_BeginChild("command log", (ImVec2){0, 0}, ImGuiChildFlags_Borders, 0);
            ImGui_TextWrapped("%s", app->command_log);

            // Auto-scroll to bottom
            if (ImGui_GetScrollY() >= ImGui_GetScrollMaxY() - 20)
                ImGui_SetScrollHereY(1.0f);

            ImGui_EndChild();
            ImGui_EndChild();
        }
        ImGui_SameLine();

        // Right
        {
            ImGui_BeginGroup();
            // Leave room for 1 line below.
            ImGui_BeginChild("Board View", (ImVec2){0, -ImGui_GetFrameHeightWithSpacing()}, 0, 0);

            if (app->game.status == STATUS_OVER) {
                ImGui_Text("GAME OVER");
                ImGui_SameLine();
                if (app->game.winner == PLAYER_RED) {
                    ImGui_Text("Red Wins");
                } else {
                    ImGui_Text("Blue Wins");
                }
            } else if (app->game.turn.player == PLAYER_RED) {
                ImGui_Text("Red's Turn");
            } else {
                ImGui_Text("Blue's Turn");
            }
            if (app->ui_state == UI_STATE_WAITING_FOR_COMMAND ||
                app->ui_state == UI_STATE_WAITING_FOR_SELECTION) {
                ImGui_SameLine();
                if (ImGui_Button("End Turn")) {
                    app->ui_state = UI_STATE_WAITING_FOR_COMMAND;
                    app->selected_command = (Command){
                        .kind = COMMAND_END_TURN,
                        .piece_pos = (CPos){0, 0, 0},
                        .target_pos = (CPos){0, 0, 0},
                    };
                    apply_command = true;
                }
            }

            // Game Board.
            {
                ImGui_PushStyleVarImVec2(ImGuiStyleVar_WindowPadding,
                                         (ImVec2){0, 0}); // Disable padding
                ImGui_PushStyleColor(ImGuiCol_ChildBg,
                                     IM_COL32(50, 50, 50, 255)); // Set a background color
                ImGui_BeginChild("canvas", (ImVec2){0.0f, 0.0f}, ImGuiChildFlags_Borders,
                                 ImGuiWindowFlags_NoMove);

                ImVec2 window_pos = ImGui_GetWindowPos();
                ImVec2 window_size = ImGui_GetWindowSize();
                ImVec2 window_center = {window_pos.x + window_size.x / 2,
                                        window_pos.y + window_size.y / 2};

                float hexes_across = 14.0f;
                float hex_radius = SDL_floorf(window_size.x * SDL_sqrtf(3.0f) / (3 * hexes_across));
                float horizontal_offset =
                    SDL_sqrtf(hex_radius * hex_radius - (hex_radius / 2) * (hex_radius / 2));
                float vertical_offset = hex_radius * 1.5f;
                V2 dpos_center = {10, 4};

                ImDrawList *draw_list = ImGui_GetWindowDrawList();

                // Track if click was handled by any tile and if mouse is in canvas
                bool click_handled_by_tile = false;
                bool mouse_is_in_canvas = ImGui_IsWindowHovered(0);
                bool mouse_on_tile = false;
                CPos mouse_cpos = cpos_from_v2((V2){0, 0});
                for (int32_t row = 0; row <= 8; row++) {
                    for (int32_t col = 0; col <= 20; col++) {
                        if ((row % 2 == 0 && col % 2 != 0) || (row % 2 == 1 && col % 2 != 1)) {
                            // Not a valid double coordinate.
                            continue;
                        }

                        V2 hex_dpos = {col - dpos_center.x, row - dpos_center.y};
                        CPos cpos = cpos_from_v2(hex_dpos);
                        u8 piece = *game_piece(&app->game, cpos);
                        if (piece == TILE_NULL) {
                            continue;
                        }

                        ImVec2 hex_screen_pos = {
                            window_center.x + (horizontal_offset * (float)hex_dpos.x),
                            window_center.y + vertical_offset * (float)hex_dpos.y};

                        // Hex Outline.
                        ImDrawList_AddEllipseFilledEx(draw_list, hex_screen_pos,
                                                      (ImVec2){hex_radius, hex_radius},
                                                      // 0xFFEFF9FE,
                                                      0xFFC8C8C8,
                                                      // background_color,
                                                      3.14f / 2.0f, 6);
                        ImDrawList_PathEllipticalArcToEx(draw_list, hex_screen_pos,
                                                         (ImVec2){hex_radius, hex_radius},
                                                         3.14f / 2.0f, 0.0f, 3.14f * 2.0f, 6);
                        ImDrawList_PathStroke(draw_list, 0xFF000000, ImDrawFlags_Closed, 1.0f);

                        ImVec2 mouse_pos = ImGui_GetMousePos();
                        ImVec2 hex_distance = {mouse_pos.x - hex_screen_pos.x,
                                               mouse_pos.y - hex_screen_pos.y};
                        if (SDL_sqrtf(hex_distance.x * hex_distance.x +
                                      hex_distance.y * hex_distance.y) < horizontal_offset) {
                            mouse_on_tile = true;
                            mouse_cpos = cpos;

                            // ImDrawList_AddCircleEx(draw_list, hex_screen_pos,
                            //                        horizontal_offset - 1.0f, 0xFFFF00FF,
                            //                        15, 1.0f);

                            if (ImGui_IsMouseClicked(ImGuiMouseButton_Left)) {
                                click_handled_by_tile = true; // Mark that a tile handled this click

                                bool valid_click_found = false;

                                // Check if clicking on already selected piece (to toggle off)
                                bool clicking_selected_piece =
                                    (app->ui_state == UI_STATE_WAITING_FOR_COMMAND &&
                                     cpos_eq(mouse_cpos, app->selected_cpos));

                                if (clicking_selected_piece) {
                                    // Clicking the already selected piece should deselect it
                                    app->ui_state = UI_STATE_WAITING_FOR_SELECTION;
                                    app->selected_piece = 0;
                                    app->selected_cpos = (CPos){0, 0, 0};
                                    valid_click_found = true;
                                } else {
                                    // Process normal selection/command logic
                                    for (size_t i = 0; i < app->command_buf.count; i++) {
                                        if (app->ui_state == UI_STATE_WAITING_FOR_SELECTION) {
                                            if (app->command_buf.commands[i].kind !=
                                                    COMMAND_END_TURN &&
                                                cpos_eq(app->command_buf.commands[i].piece_pos,
                                                        mouse_cpos)) {
                                                app->selected_piece =
                                                    *game_piece(&app->game, mouse_cpos);
                                                app->selected_cpos = mouse_cpos;
                                                app->ui_state = UI_STATE_WAITING_FOR_COMMAND;
                                                valid_click_found = true;
                                                break;
                                            }
                                        } else if (app->ui_state == UI_STATE_WAITING_FOR_COMMAND) {
                                            if (app->command_buf.commands[i].kind !=
                                                    COMMAND_END_TURN &&
                                                cpos_eq(app->command_buf.commands[i].piece_pos,
                                                        app->selected_cpos) &&
                                                cpos_eq(app->command_buf.commands[i].target_pos,
                                                        mouse_cpos)) {
                                                app->selected_command =
                                                    app->command_buf.commands[i];
                                                apply_command = true;
                                                valid_click_found = true;
                                                break;
                                            } else if (app->command_buf.commands[i].kind !=
                                                           COMMAND_END_TURN &&
                                                       cpos_eq(
                                                           app->command_buf.commands[i].piece_pos,
                                                           mouse_cpos)) {
                                                app->selected_piece =
                                                    *game_piece(&app->game, mouse_cpos);
                                                app->selected_cpos = mouse_cpos;
                                                app->ui_state = UI_STATE_WAITING_FOR_COMMAND;
                                                valid_click_found = true;
                                                break;
                                            }
                                        }
                                    }
                                }

                                // If in command state and clicked on an invalid location, deselect
                                if (app->ui_state == UI_STATE_WAITING_FOR_COMMAND &&
                                    !valid_click_found) {
                                    app->ui_state = UI_STATE_WAITING_FOR_SELECTION;
                                    app->selected_piece = 0;
                                    app->selected_cpos = (CPos){0, 0, 0};
                                }
                            }
                        }

                        if (piece != TILE_EMPTY) {
                            ImU32 color;
                            if ((piece & PLAYER_MASK) == PLAYER_RED) {
                                color = 0xFF1f12c1;
                                // color = 0xFF000078; // maroon
                            } else if ((piece & PLAYER_MASK) == PLAYER_BLUE) {
                                color = 0xFFbc9b66;
                                // color = 0xFF493000; // navy
                            } else {
                                color = 0xFF0000FF;
                            }

                            if ((app->ui_state == UI_STATE_WAITING_FOR_COMMAND ||
                                 app->ui_state == UI_STATE_AI_PREVIEW) &&
                                cpos_eq(cpos, app->selected_cpos)) {
                                color = 0xFFf5f5f5;
                            }

                            switch (piece & PIECE_KIND_MASK) {
                            case PIECE_CROWN: {
                                ImDrawList_AddTriangleFilled(
                                    draw_list,
                                    (ImVec2){hex_screen_pos.x, hex_screen_pos.y - hex_radius / 2},
                                    (ImVec2){hex_screen_pos.x - hex_radius / 2,
                                             hex_screen_pos.y + hex_radius / 4},
                                    (ImVec2){hex_screen_pos.x + hex_radius / 2,
                                             hex_screen_pos.y + hex_radius / 4},
                                    color);
                                ImDrawList_AddTriangleFilled(
                                    draw_list,
                                    (ImVec2){hex_screen_pos.x - hex_radius / 2,
                                             hex_screen_pos.y - hex_radius / 4},
                                    (ImVec2){hex_screen_pos.x, hex_screen_pos.y + hex_radius / 2},
                                    (ImVec2){hex_screen_pos.x + hex_radius / 2,
                                             hex_screen_pos.y - hex_radius / 4},
                                    color);
                                break;
                            }
                            case PIECE_PIKE: {
                                ImDrawList_AddRectFilled(
                                    draw_list,
                                    (ImVec2){hex_screen_pos.x - hex_radius / 2,
                                             hex_screen_pos.y - hex_radius / 2},
                                    (ImVec2){hex_screen_pos.x + hex_radius / 2,
                                             hex_screen_pos.y + hex_radius / 2},
                                    color);
                                break;
                            }
                            case PIECE_HORSE: {
                                ImDrawList_AddTriangleFilled(
                                    draw_list,
                                    (ImVec2){hex_screen_pos.x, hex_screen_pos.y - hex_radius / 2},
                                    (ImVec2){hex_screen_pos.x - hex_radius / 2,
                                             hex_screen_pos.y + hex_radius / 4},
                                    (ImVec2){hex_screen_pos.x + hex_radius / 2,
                                             hex_screen_pos.y + hex_radius / 4},
                                    color);
                                break;
                            }
                            case PIECE_BOW: {
                                ImDrawList_AddCircleFilled(draw_list, hex_screen_pos,
                                                           hex_radius / 2, color, 18);
                                break;
                            }
                            default:
                                break;
                            }
                        }
                    }
                }

                // Handle click outside of any tile but inside the canvas
                if (mouse_is_in_canvas && ImGui_IsMouseClicked(ImGuiMouseButton_Left) &&
                    !click_handled_by_tile && app->ui_state == UI_STATE_WAITING_FOR_COMMAND) {
                    app->ui_state = UI_STATE_WAITING_FOR_SELECTION;
                    app->selected_piece = 0;
                    app->selected_cpos = (CPos){0, 0, 0};
                }

                // Preview selected piece actions.
                if (app->ui_state == UI_STATE_WAITING_FOR_COMMAND ||
                    app->ui_state == UI_STATE_AI_PREVIEW) {
                    for (size_t i = 0; i < app->command_buf.count; i++) {
                        Command command = app->command_buf.commands[i];
                        if ((app->ui_state == UI_STATE_WAITING_FOR_COMMAND ||
                             app->ui_state == UI_STATE_AI_PREVIEW) &&
                            cpos_eq(command.piece_pos, app->selected_cpos)) {
                            V2 target_dpos = v2_from_cpos(command.target_pos);
                            ImVec2 target_screen_pos = {
                                window_center.x + (horizontal_offset * (float)target_dpos.x),
                                window_center.y + vertical_offset * (float)target_dpos.y};
                            if (command.kind == COMMAND_MOVE) {
                                ImDrawList_PathEllipticalArcToEx(
                                    draw_list, target_screen_pos,
                                    (ImVec2){hex_radius - 4, hex_radius - 4}, 3.14f / 2.0f, 0.0f,
                                    3.14f * 2.0f, 6);
                                ImDrawList_PathStroke(draw_list, 0xFFF5F5F5, ImDrawFlags_Closed,
                                                      4.0f);
                                ImDrawList_PathEllipticalArcToEx(
                                    draw_list, target_screen_pos, (ImVec2){hex_radius, hex_radius},
                                    3.14f / 2.0f, 0.0f, 3.14f * 2.0f, 6);
                                ImDrawList_PathStroke(draw_list, 0xFF000000, ImDrawFlags_Closed,
                                                      1.0f);
                            } else if (command.kind == COMMAND_VOLLEY) {
                                ImDrawList_AddCircleFilled(draw_list, target_screen_pos,
                                                           hex_radius / 4, 0xFFF5F5F5, 18);
                            }
                        }
                    }
                }

                // Preview hovered piece actions.
                if (app->ui_state == UI_STATE_WAITING_FOR_SELECTION ||
                    app->ui_state == UI_STATE_AI_PREVIEW) {
                    for (size_t i = 0; i < app->command_buf.count; i++) {
                        Command command = app->command_buf.commands[i];
                        if ((app->ui_state == UI_STATE_WAITING_FOR_SELECTION &&
                             cpos_eq(command.piece_pos, mouse_cpos))
                            // ||
                            // (ui_state == UI_STATE_AI_PREVIEW && cpos_eq(command.piece_pos,
                            // chosen_ai_command.piece_pos))
                        ) {
                            V2 target_dpos = v2_from_cpos(command.target_pos);
                            ImVec2 target_screen_pos = {
                                window_center.x + (horizontal_offset * (float)target_dpos.x),
                                window_center.y + vertical_offset * (float)target_dpos.y};
                            if (command.kind == COMMAND_MOVE) {
                                ImDrawList_PathEllipticalArcToEx(
                                    draw_list, target_screen_pos,
                                    (ImVec2){hex_radius - 4, hex_radius - 4}, 3.14f / 2.0f, 0.0f,
                                    3.14f * 2.0f, 6);
                                ImDrawList_PathStroke(draw_list, 0xFFF5F5F5, ImDrawFlags_Closed,
                                                      4.0f);
                                ImDrawList_PathEllipticalArcToEx(
                                    draw_list, target_screen_pos, (ImVec2){hex_radius, hex_radius},
                                    3.14f / 2.0f, 0.0f, 3.14f * 2.0f, 6);
                                ImDrawList_PathStroke(draw_list, 0xFF000000, ImDrawFlags_Closed,
                                                      1.0f);
                            } else if (command.kind == COMMAND_VOLLEY) {
                                ImDrawList_AddCircleFilled(draw_list, target_screen_pos,
                                                           hex_radius / 4, 0xFFF5F5F5, 18);
                            }
                        }
                    }
                }

                ImVec2 overlay_pos = (ImVec2){window_pos.x + window_size.x - 75, window_pos.y};

                // Show the mouse position
                ImGuiWindowFlags window_flags =
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
                ImGui_SetNextWindowPos(overlay_pos, ImGuiCond_Always);
                ImGui_SetNextWindowBgAlpha(0.35f); // Transparent background
                if (mouse_on_tile) {
                    ImGui_Begin("Example: Simple overlay", &mouse_on_tile, window_flags);
                    {
                        ImGui_Text("(%i, %i, %i)", mouse_cpos.q, mouse_cpos.r, mouse_cpos.s);
                    }
                    ImGui_End();
                }
            }
            ImGui_EndChild();
            ImGui_PopStyleColor();
            ImGui_PopStyleVar();
        }

        ImGui_EndChild();

        // u32 counter = SDL_GetAtomicU32(&app->counter);
        if (app->ui_state == UI_STATE_AI_THINKING) {
            ImGui_ProgressBar(-1.0f * (float)ImGui_GetTime(), (ImVec2){-1, 0}, "AI Thinking...");
        }
        ImGui_EndGroup();
    }

    ImGui_End();

    // bool show_demo = true;
    // ImGui_ShowDemoWindow(&show_demo);

    ImGui_Render();
    SDL_SetRenderScale(app->renderer, app->io->DisplayFramebufferScale.x,
                       app->io->DisplayFramebufferScale.y);
    ImVec4 clear_color = (ImVec4){0.45f, 0.55f, 0.60f, 1.00f};
    SDL_SetRenderDrawColorFloat(app->renderer, clear_color.x, clear_color.y, clear_color.z,
                                clear_color.w);
    SDL_RenderClear(app->renderer);
    cImGui_ImplSDLRenderer3_RenderDrawData(ImGui_GetDrawData(), app->renderer);
    SDL_RenderPresent(app->renderer);

    // AI Turn
    if (app->ui_state == UI_STATE_AI_THINKING) {
        SDL_ThreadState state = SDL_GetThreadState(app->ai_turn_thread);
        if (state == SDL_THREAD_ALIVE) {
            // AI is still thinking.
        } else if (state == SDL_THREAD_COMPLETE) {
            // AI has finished thinking.
            int thread_result;
            SDL_WaitThread(app->ai_turn_thread, &thread_result);
            if (thread_result != 0) {
                // Something went wrong.
                assert(false);
            }
            app->ai_turn_thread = NULL;
            u32 selected_command_i = app->ai_turn.selected_command_i;
            app->selected_command = app->command_buf.commands[selected_command_i];
            app->selected_piece = *game_piece(&app->game, app->selected_command.piece_pos);
            app->selected_cpos = app->selected_command.piece_pos;

            app->ai_preview_frames_left = 60 * 3;
            app->ui_state = UI_STATE_AI_PREVIEW;
        } else {
            // Something went wrong.
            assert(false);
        }
    }

    if (app->ui_state == UI_STATE_AI_PREVIEW) {
        if (app->ai_preview_frames_left-- <= 0) {
            apply_command = true;
        }
    }

    if (apply_command) {
        // Log the command
        char log_entry[256];
        const char *player = (app->game.turn.player == PLAYER_RED) ? "Red" : "Blue";
        const char *piece_type;

        switch (app->selected_piece & PIECE_KIND_MASK) {
        case PIECE_CROWN:
            piece_type = "Crown";
            break;
        case PIECE_PIKE:
            piece_type = "Pike";
            break;
        case PIECE_HORSE:
            piece_type = "Horse";
            break;
        case PIECE_BOW:
            piece_type = "Bow";
            break;
        default:
            piece_type = "Unknown";
            break;
        }

        if (app->selected_command.kind == COMMAND_MOVE) {
            snprintf(log_entry, sizeof(log_entry), "%s %s moved from (%d,%d,%d) to (%d,%d,%d)\n",
                     player, piece_type, app->selected_command.piece_pos.q,
                     app->selected_command.piece_pos.r, app->selected_command.piece_pos.s,
                     app->selected_command.target_pos.q, app->selected_command.target_pos.r,
                     app->selected_command.target_pos.s);
        } else if (app->selected_command.kind == COMMAND_VOLLEY) {
            snprintf(log_entry, sizeof(log_entry), "%s %s volleyed at (%d,%d,%d)\n", player,
                     piece_type, app->selected_command.target_pos.q,
                     app->selected_command.target_pos.r, app->selected_command.target_pos.s);
        } else if (app->selected_command.kind == COMMAND_END_TURN) {
            snprintf(log_entry, sizeof(log_entry), "%s ended their turn\n", player);
        }

        // todo: just let the log grow.

        size_t current_len = strlen(app->command_log);
        size_t entry_len = strlen(log_entry);

        if (current_len + entry_len >= sizeof(app->command_log) - 1) {
            // Find a position to truncate from (about 1/4 of the buffer)
            size_t truncate_pos = sizeof(app->command_log) / 4;
            while (truncate_pos < current_len && app->command_log[truncate_pos] != '\n') {
                truncate_pos++;
            }
            if (truncate_pos < current_len) {
                truncate_pos++; // Move past the newline
                memmove(app->command_log, app->command_log + truncate_pos,
                        current_len - truncate_pos + 1);
                current_len -= truncate_pos;
            }
        }

        // Append the new entry
        strncat(app->command_log, log_entry, sizeof(app->command_log) - current_len - 1);

        game_apply_command(&app->game, app->game.turn.player, app->selected_command, VOLLEY_ROLL);
        game_valid_commands(&app->command_buf, &app->game);
        assert(app->command_buf.count <= 1024);

        bool piece_still_exists = false;
        CPos new_piece_cpos = (CPos){0, 0, 0};
        if (app->selected_command.kind != COMMAND_END_TURN) {
            if (*game_piece(&app->game, app->selected_command.piece_pos) == app->selected_piece) {
                piece_still_exists = true;
                new_piece_cpos = app->selected_command.piece_pos;
            } else if (*game_piece(&app->game, app->selected_command.target_pos) ==
                       app->selected_piece) {
                piece_still_exists = true;
                new_piece_cpos = app->selected_command.target_pos;
            }
        }

        if (app->command_buf.count == 1) {
            // If there are no more commands for this player, end the turn.
            assert(app->command_buf.commands[0].kind == COMMAND_END_TURN);
            game_apply_command(&app->game, app->game.turn.player,
                               (Command){
                                   .kind = COMMAND_END_TURN,
                                   .piece_pos = (CPos){0, 0, 0},
                                   .target_pos = (CPos){0, 0, 0},
                               },
                               VOLLEY_ROLL);
            game_valid_commands(&app->command_buf, &app->game);
            assert(app->command_buf.count <= 1024);
            app->ui_state = UI_STATE_WAITING_FOR_SELECTION;
            app->selected_piece = 0;
            app->selected_cpos = (CPos){0, 0, 0};
        } else if (piece_still_exists) {
            // If there are more commands for this piece, select it.
            bool has_another_command = false;
            for (size_t i = 0; i < app->command_buf.count; i++) {
                Command next_command = app->command_buf.commands[i];
                if (cpos_eq(next_command.piece_pos, new_piece_cpos) &&
                    next_command.kind != COMMAND_END_TURN) {
                    has_another_command = true;
                    break;
                }
            }
            if (has_another_command) {
                if (app->ui_state == UI_STATE_AI_PREVIEW) {
                    app->ui_state = UI_STATE_WAITING_FOR_SELECTION;
                } else {
                    app->ui_state = UI_STATE_WAITING_FOR_COMMAND;
                }
                app->selected_piece = *game_piece(&app->game, new_piece_cpos);
                app->selected_cpos = new_piece_cpos;
            } else {
                app->ui_state = UI_STATE_WAITING_FOR_SELECTION;
                app->selected_piece = 0;
                app->selected_cpos = (CPos){0, 0, 0};
            }
        } else {
            app->ui_state = UI_STATE_WAITING_FOR_SELECTION;
            app->selected_piece = 0;
            app->selected_cpos = (CPos){0, 0, 0};
        }
    }

    if (app->ui_state == UI_STATE_WAITING_FOR_SELECTION && app->game.status != STATUS_OVER &&
        app->game.turn.player == PLAYER_BLUE && app->difficulty != 0) {
        // It's now AI's turn.
        AIDifficulty ai_difficulty = (AIDifficulty)app->difficulty;
        app->ai_preview_frames_left = 0;

        app->ai_turn.game = app->game;
        app->ai_turn.difficulty = ai_difficulty;
        app->ai_turn.selected_command_i = 0;

        app->ai_turn_thread =
            SDL_CreateThread(ai_select_command, "ai_select_command", &app->ai_turn);
        app->ui_state = UI_STATE_AI_THINKING;
    }

    if (app->game.status == STATUS_OVER) {
        app->ui_state = UI_STATE_GAME_OVER;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    UNUSED(result);

    AppState *app = (AppState *)appstate;

    cImGui_ImplSDLRenderer3_Shutdown();
    cImGui_ImplSDL3_Shutdown();
    ImGui_DestroyContext(app->imgui_context);
}
