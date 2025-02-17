#!/bin/bash

emcmake cmake -S . -B cmake-web-release -DCMAKE_BUILD_TYPE=Release
cmake --build cmake-web-release --target tazar_ui
pushd cmake-web-release || exit
mv tazar_ui.html index.html
rm -rf tazar-web
mkdir tazar-web
cp tazar_ui.js tazar-web/
cp tazar_ui.wasm tazar-web/
cp index.html tazar-web/
zip -r tazar-web.zip tazar-web
popd || exit