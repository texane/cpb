#!/usr/bin/env sh

KAAPI_DIR=../../src
PAPI_DIR=$HOME/install

gcc -O3 -Wall -march=native \
    -I$KAAPI_DIR \
    -I$PAPI_DIR/include -DKAAPI_CONFIG_PERF=1 \
    -pthread \
    main.c \
    $KAAPI_DIR/kaapi_ctor.c \
    $KAAPI_DIR/kaapi_mt.c \
    $KAAPI_DIR/kaapi_proc.c \
    $KAAPI_DIR/kaapi_numa.c \
    $KAAPI_DIR/kaapi_perf.c \
    -L$PAPI_DIR/lib -lpapi \
    -lnuma -lpthread -lm
