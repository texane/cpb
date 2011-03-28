#!/usr/bin/env bash

# numa info
# node 0 cpus: 0 4 8 12 16 20
# node 1 cpus: 24 28 32 36 40 44
# node 2 cpus: 3 7 11 15 19 23
# node 3 cpus: 27 31 35 39 43 47
# node 4 cpus: 2 6 10 14 18 22
# node 5 cpus: 26 30 34 38 42 46
# node 6 cpus: 1 5 9 13 17 21
# node 7 cpus: 25 29 33 37 41 45

# helper routines

export WORKING_CPUS='';
function get_working_cpus {
    tmp=`numactl --hardware | grep "node $1 cpus" | cut -d: -f2` ;
    export WORKING_CPUS="$WORKING_CPUS $tmp";
}

export SOURCE_CPU='';
function get_source_cpu {
    SOURCE_CPU=`numactl --hardware | grep "node $1 cpus" | cut -d: -f2 | cut -d' ' -f2` ;
}

export MASTER_CPU='';
function get_master_cpu {
    MASTER_CPU=`numactl --hardware | grep "node $1 cpus" | cut -d: -f2 | cut -d' ' -f4` ;
}

# main

get_master_cpu 7;
get_source_cpu 7;

# WORKING_CPUS="0 4 8 12 16 20"
WORKING_CPUS="0 4 8"

#WORKING_CPUS="0 2 3 24 27 26 4 28"

#get_working_cpus 0;
#get_working_cpus 1;
#get_working_cpus 2;
#get_working_cpus 3;
#get_working_cpus 4;
#get_working_cpus 6;
#get_working_cpus 7;

LD_LIBRARY_PATH=/home/lementec/install/lib \
KAAPI_PERF_PAPI0=PAPI_TOT_CYC \
KAAPI_PERF_PAPI1=MEMORY_CONTROLLER_REQUESTS:READ_REQUESTS \
KAAPI_PERF_PAPI2=PAPI_RES_STL \
./a.out $SOURCE_CPU $MASTER_CPU $WORKING_CPUS;
