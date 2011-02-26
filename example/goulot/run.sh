#!/usr/bin/env bash

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
    MASTER_CPU=`numactl --hardware | grep "node $1 cpus" | cut -d: -f2 | cut -d' ' -f2` ;
}


# main

get_master_cpu 1;

get_source_cpu 7;

#get_working_cpus 0;
#get_working_cpus 1;
get_working_cpus 2;
get_working_cpus 3;
#get_working_cpus 4;
#get_working_cpus 6;
#get_working_cpus 7;

LD_LIBRARY_PATH=$HOME/install/lib \
KAAPI_PERF_PAPI0=PAPI_RES_STL \
./a.out $SOURCE_CPU $MASTER_CPU $WORKING_CPUS;
