#!/usr/bin/env sh

KAAPI_DIR=../../src
PAPI_DIR=$HOME/install

CFLAGS="-O3 -Wall -march=native -msse2 -pthread"
CFLAGS="$CFLAGS -I$KAAPI_DIR"
CFLAGS="$CFLAGS -I$PAPI_DIR/include -DKAAPI_CONFIG_PERF=1"

LFLAGS="-L$PAPI_DIR/lib -lpapi -lnuma -lpthread -lm"

SOURCES=""
SOURCES="$SOURCES main.c"
SOURCES="$SOURCES $KAAPI_DIR/kaapi_ctor.c"
SOURCES="$SOURCES $KAAPI_DIR/kaapi_mt.c"
SOURCES="$SOURCES $KAAPI_DIR/kaapi_proc.c"
SOURCES="$SOURCES $KAAPI_DIR/kaapi_numa.c"
SOURCES="$SOURCES $KAAPI_DIR/kaapi_perf.c"

for fubar in `seq 0 1 4` `seq 5 5 50`; do
    gcc -DCONFIG_COST=$fubar -o main_$fubar $CFLAGS $SOURCES $LFLAGS ;
done
