cmake_minimum_required(VERSION 3.26.4)
project(tazar-bot C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_PDB_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(
        FATAL_ERROR
        "In-source builds not allowed."
    )
endif()

file(
    DOWNLOAD
    https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.40.5/CPM.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake
)
include(${CMAKE_CURRENT_BINARY_DIR}/cmake/CPM.cmake)

if (EMSCRIPTEN)
    CPMAddPackage(
        NAME sdl
        GITHUB_REPOSITORY libsdl-org/SDL
        GIT_TAG release-3.2.4
        OPTIONS
        "SDL_STATIC ON" "SDL_SHARED OFF" "SDL_PLATFORM_WEB ON" "SDL_PTHREADS ON"
    )
else ()
    CPMAddPackage(
        NAME sdl
        GITHUB_REPOSITORY libsdl-org/SDL
        GIT_TAG release-3.2.4
    )
endif ()

CPMAddPackage("gh:ocornut/imgui#v1.91.8-docking")
CPMAddPackage("gh:dearimgui/dear_bindings#f6e8ea7")

# Generate dear bindings
include(FindPython3)
set(VENV_STAMP_FILE ${CMAKE_CURRENT_BINARY_DIR}/venv_setup.stamp)

add_custom_command(
    OUTPUT ${VENV_STAMP_FILE}
    COMMAND ${CMAKE_COMMAND} -E echo "Setting up Python venv"
    COMMAND python3 -m venv venv
    COMMAND venv/bin/pip3 install -r requirements.txt
    COMMAND ${CMAKE_COMMAND} -E touch ${VENV_STAMP_FILE}
    WORKING_DIRECTORY ${dear_bindings_SOURCE_DIR}
    COMMENT "Setting up Python virtual environment"
)

# Generate base ImGui bindings
set(DCIMGUI_OUTPUT_FILES
    ${dear_bindings_SOURCE_DIR}/dcimgui.h
    ${dear_bindings_SOURCE_DIR}/dcimgui.cpp
)
add_custom_command(
    OUTPUT ${DCIMGUI_OUTPUT_FILES}
    COMMAND venv/bin/python3 dear_bindings.py -o dcimgui ../imgui-src/imgui.h
    DEPENDS ${imgui_SOURCE_DIR}/imgui.h ${VENV_STAMP_FILE}
    WORKING_DIRECTORY ${dear_bindings_SOURCE_DIR}
    COMMENT "Generating ImGui bindings"
)

# Generate SDL3 backend bindings
set(DCIMGUI_SDL3_OUTPUT_FILES
    ${dear_bindings_SOURCE_DIR}/dcimgui_impl_sdl3.h
    ${dear_bindings_SOURCE_DIR}/dcimgui_impl_sdl3.cpp
)
add_custom_command(
    OUTPUT ${DCIMGUI_SDL3_OUTPUT_FILES}
    COMMAND venv/bin/python3 dear_bindings.py --backend --include ../imgui-src/imgui.h -o dcimgui_impl_sdl3 ../imgui-src/backends/imgui_impl_sdl3.h
    DEPENDS ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.h ${DCIMGUI_OUTPUT_FILES}
    WORKING_DIRECTORY ${dear_bindings_SOURCE_DIR}
    COMMENT "Generating SDL3 backend bindings"
)

# Generate SDLRenderer3 backend bindings
set(DCIMGUI_SDLRENDERER3_OUTPUT_FILES
    ${dear_bindings_SOURCE_DIR}/dcimgui_impl_sdlrenderer3.h
    ${dear_bindings_SOURCE_DIR}/dcimgui_impl_sdlrenderer3.cpp
)
add_custom_command(
    OUTPUT ${DCIMGUI_SDLRENDERER3_OUTPUT_FILES}
    COMMAND venv/bin/python3 dear_bindings.py --backend --include ../imgui-src/imgui.h -o dcimgui_impl_sdlrenderer3 ../imgui-src/backends/imgui_impl_sdlrenderer3.h
    DEPENDS ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.h ${DCIMGUI_OUTPUT_FILES}
    WORKING_DIRECTORY ${dear_bindings_SOURCE_DIR}
    COMMENT "Generating SDLRenderer3 backend bindings"
)

add_custom_target(generate_bindings
    DEPENDS ${DCIMGUI_OUTPUT_FILES} ${DCIMGUI_SDL3_OUTPUT_FILES} ${DCIMGUI_SDLRENDERER3_OUTPUT_FILES}
)

add_library(${PROJECT_NAME}_deps
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
    ${dear_bindings_SOURCE_DIR}/dcimgui.cpp
    ${dear_bindings_SOURCE_DIR}/dcimgui_impl_sdl3.cpp
    ${dear_bindings_SOURCE_DIR}/dcimgui_impl_sdlrenderer3.cpp
)
add_dependencies(${PROJECT_NAME}_deps generate_bindings)
target_include_directories(${PROJECT_NAME}_deps PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${dear_bindings_SOURCE_DIR}
)
target_link_libraries(${PROJECT_NAME}_deps PRIVATE
    SDL3::SDL3
)

if (PROJECT_IS_TOP_LEVEL)
    if (EMSCRIPTEN)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread -s PTHREAD_POOL_SIZE=4 -sMALLOC=dlmalloc -s ASSERTIONS=1 -s WASM=1 -s ALLOW_MEMORY_GROWTH -s STACK_SIZE=131072 -s MAXIMUM_MEMORY=4GB --shell-file=shell.html")
        set(CMAKE_EXECUTABLE_SUFFIX ".html")
        configure_file(shell.html shell.html COPYONLY)
    endif ()
    add_executable(${PROJECT_NAME} main.c
        tazar.c
        tazar.h
        tazar_ai.c
    )
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wconversion)# -Werror)
    target_include_directories(${PROJECT_NAME} PRIVATE ${dear_bindings_SOURCE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_deps SDL3::SDL3)
    configure_file(DroidSans.ttf ${CMAKE_OUTPUT_DIRECTORY}/DroidSans.ttf COPYONLY)
endif ()
