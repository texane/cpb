#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include "kaapi_config.h"
#include "kaapi_atomic.h"
#include "kaapi_proc.h"
#include "kaapi_numa.h"

#if KAAPI_CONFIG_PERF
# include "kaapi_perf.h"
#endif

#if KAAPI_CONFIG_WS
# include "kaapi_ws.h"
#endif


int kaapi_initialize(void)
{
  srand(getpid() * time(0));

  kaapi_proc_init_once();

  kaapi_numa_initialize();

  if (pthread_key_create(&kaapi_proc_key, NULL)) return -1;

  memset((void*)kaapi_all_procs, 0, sizeof(kaapi_all_procs));

#if KAAPI_CONFIG_PERF
  if (kaapi_perf_initialize() == -1)
    return -1;
#endif

#if KAAPI_CONFIG_WS
  if (kaapi_create_all_ws_groups() == -1)
    return -1;
#endif

  return 0;
}

void kaapi_finalize(void)
{
  pthread_key_delete(kaapi_proc_key);

#if KAAPI_CONFIG_WS
  kaapi_destroy_all_ws_groups();
#endif

#if KAAPI_CONFIG_PERF
  kaapi_perf_finalize();
#endif
}
