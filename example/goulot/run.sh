#!/usr/bin/env sh
LD_LIBRARY_PATH=$HOME/install/lib \
KAAPI_PERF_PAPI0=PAPI_RES_STL \
./a.out 0 0 4 8 12 16 20 24
