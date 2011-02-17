#!/usr/bin/env bash

NUMA_CFLAGS=-DCONFIG_USE_NUMA=1
NUMA_LDFLAGS=-lnuma

CAPED_HOSTNAME=`echo -n $HOSTNAME | tr [:lower:] [:upper:]`
HOST_CFLAGS=-DCONFIG_USE_$CAPED_HOSTNAME=1

gcc -Wall -O3 -march=native \
$NUMA_CFLAGS \
$HOST_CFLAGS \
-o foreach_release \
../src/main.c \
-lpthread \
$NUMA_LDFLAGS

gcc -ggdb \
$NUMA_CFLAGS \
$HOST_CFLAGS \
-o foreach_debug \
../src/main.c \
-lpthread \
$NUMA_LDFLAGS