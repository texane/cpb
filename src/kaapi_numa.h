#ifndef KAAPI_NUMA_H_INCLUDED
# define KAAPI_NUMA_H_INCLUDED


#include <stdint.h>
#include <sys/types.h>
#include "kaapi_proc.h"

typedef unsigned long kaapi_numaid_t;

int kaapi_numa_initialize(void);
int kaapi_numa_bind(const void*, size_t, kaapi_numaid_t);
int kaapi_numa_alloc(void**, size_t, kaapi_numaid_t);
int kaapi_numa_alloc_with_procid(void**, size_t, kaapi_procid_t);
void kaapi_numa_free(void*, size_t);
kaapi_numaid_t kaapi_numa_get_page_node(uintptr_t);
kaapi_numaid_t kaapi_numa_procid_to_numaid(kaapi_procid_t);


#endif /* ! KAAPI_NUMA_H_INCLUDED */
