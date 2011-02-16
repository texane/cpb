#!/usr/bin/env sh

# NUMA_CFLAGS=-DCONFIG_USE_NUMA=1
# NUMA_LDFLAGS=-lnuma

NUMA_CFLAGS=-DCONFIG_USE_NUMA=0
NUMA_LDFLAGS=

gcc -Wall -O3 -march=native -Wall \
$NUMA_CFLAGS \
../src/main.c \
-lpthread \
$NUMA_LDFLAGS \