#!/usr/bin/env sh
gcc -Wall -O3 -march=native -Wall \
../src/main.c \
-lnuma -lpthread