#!/bin/bash

emcmake cmake -S . -B cmake-web-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-web-release --target tazar_sdl_main
pushd cmake-web-release || exit
rm -rf tazar-web
mkdir tazar-web
cp bin/tazar-bot.js tazar-web/
cp bin/tazar-bot.wasm tazar-web/
cp bin/tazar-bot.html tazar-web/index.html
zip -r tazar-web.zip tazar-web
popd || exit