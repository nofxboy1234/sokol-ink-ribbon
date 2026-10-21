#!/bin/sh
# Build the reachability tool (reach.c) with the host C compiler.
set -e
cd "$(dirname "$0")/.."
mkdir -p .fibs/tests
cc -std=c11 -O1 -Wall -Isrc -Ilibs -o .fibs/tests/reach tools/reach.c \
    src/level.c src/grid.c src/pathfind.c src/doors.c src/interact.c src/inventory.c src/items.c \
    src/player.c src/enemy.c src/sight.c src/throw.c src/light.c src/health.c src/crafting.c src/gear.c \
    src/savegame.c -lm
