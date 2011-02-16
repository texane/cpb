#!/usr/bin/env sh

NUMA_CFLAGS=-DCONFIG_USE_NUMA=1
NUMA_LDFLAGS=-lnuma

gcc -Wall -O3 -march=native \
$NUMA_CFLAGS \
-o foreach_release \
../src/main.c \
-lpthread \
$NUMA_LDFLAGS

gcc -ggdb \
$NUMA_CFLAGS \
-o foreach_debug \
../src/main.c \
-lpthread \
$NUMA_LDFLAGS