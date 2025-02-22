cmake_minimum_required(VERSION 3.26)
project(tazar_bot C)

set(CMAKE_C_STANDARD 17)

add_library(compile-options INTERFACE)

if (CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(compile-options INTERFACE -Wall -Wextra -Wconversion)# -Werror)
elseif (CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(compile-options INTERFACE /TC /W4 /wd4200 /WX)
endif ()

# ASAN
if ((NOT CMAKE_SYSTEM_NAME STREQUAL "Windows") AND (CMAKE_C_COMPILER_ID STREQUAL "Clang") AND (CMAKE_BUILD_TYPE STREQUAL "Debug"))
    target_compile_options(compile-options INTERFACE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(compile-options INTERFACE -fsanitize=address)
endif ()

enable_testing()

add_executable(tazar_test tazar_game.c tazar_test.c)
target_link_libraries(tazar_test compile-options)
add_test(NAME tazar_test COMMAND tazar_test)

add_executable(tazar_train tazar_train.c)
target_link_libraries(tazar_train compile-options)

# RAYLIB
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        raylib
        GIT_REPOSITORY "https://github.com/raysan5/raylib.git"
        GIT_TAG "master"
        GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(raylib)

if (EMSCRIPTEN)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -s USE_GLFW=3 -s ASSERTIONS=1 -s WASM=1 -s GL_ENABLE_GET_PROC_ADDRESS=1 -s ALLOW_MEMORY_GROWTH -s STACK_SIZE=131072 --shell-file=shell.html")
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    configure_file(shell.html shell.html COPYONLY)
endif ()

add_executable(tazar_ui tazar_game.c tazar_ai.c tazar_ui.c tazar_ai_mcts.c)
target_link_libraries(tazar_ui PRIVATE compile-options raylib)

