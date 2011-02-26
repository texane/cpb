#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <numaif.h>
#include <numa.h>
#include "kaapi_config.h"
#include "kaapi_bitmap.h"
#include "kaapi_numa.h"
#include "kaapi_proc.h"


int kaapi_numa_initialize(void)
{
  numa_set_bind_policy(1);
  numa_set_strict(1);
  return 0;
}

int kaapi_numa_bind
(const void* addr, size_t size, kaapi_numaid_t id)
{
  const int mode = MPOL_BIND;
  const unsigned int flags = MPOL_MF_STRICT | MPOL_MF_MOVE;
  const unsigned long maxnode = KAAPI_CONFIG_MAX_PROC;
  
  unsigned long nodemask
    [KAAPI_CONFIG_MAX_PROC / (8 * sizeof(unsigned long))];

  memset(nodemask, 0, sizeof(nodemask));

  nodemask[id / (8 * sizeof(unsigned long))] |=
    1UL << (id % (8 * sizeof(unsigned long)));

  if (mbind((void*)addr, size, mode, nodemask, maxnode, flags))
    return -1;

  return 0;
}

int kaapi_numa_alloc(void** addr, size_t size, kaapi_numaid_t id)
{
#define NUMA_FUBAR_FIX 1
#if NUMA_FUBAR_FIX /* bug: non reentrant??? */
  *addr = NULL;
  if (posix_memalign(addr, 0x1000, size) == 0)
  {
    if (kaapi_numa_bind(*addr, size, id))
    { free(addr); *addr = NULL; }
  }
#else
  *addr = numa_alloc_onnode(size, (int)id);
#endif
  return (*addr == NULL) ? -1 : 0;
}

void kaapi_numa_free(void* addr, size_t size)
{
#if NUMA_FUBAR_FIX
  free(addr);
#else
  numa_free(addr, size);
#endif
}

int kaapi_numa_alloc_with_procid
(void** addr, size_t size, kaapi_procid_t procid)
{
  const kaapi_numaid_t numaid = kaapi_numa_procid_to_numaid(procid);
  return kaapi_numa_alloc(addr, size, numaid);
}

kaapi_numaid_t kaapi_numa_get_page_node(uintptr_t addr)
{
  const unsigned long flags = MPOL_F_NODE | MPOL_F_ADDR;
  kaapi_bitmap_t nodemask;

  const int err = get_mempolicy
    (NULL, nodemask.bits, KAAPI_CONFIG_MAX_PROC, (void*)addr, flags);

  if (err || kaapi_bitmap_is_empty(&nodemask))
    return KAAPI_INVALID_PROCID;

  return kaapi_bitmap_scan(&nodemask, 0);
}

kaapi_numaid_t kaapi_numa_procid_to_numaid(kaapi_procid_t procid)
{
  return (kaapi_numaid_t)numa_node_of_cpu((int)procid);
}
