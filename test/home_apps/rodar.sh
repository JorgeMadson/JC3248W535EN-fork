#!/bin/sh
# Compila e roda o teste da tela inicial no PC (no Deck: dentro do distrobox dev-gcc).
set -e
cd "$(dirname "$0")/../.."
mkdir -p .pio/teste
g++ -std=c++17 -Wall -Wextra -Iinclude src/home_apps.cpp test/home_apps/test_home_apps.cpp -o .pio/teste/home_apps
.pio/teste/home_apps
