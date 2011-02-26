#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "kaapi_config.h"
#include "kaapi_proc.h"
#include "kaapi_numa.h"


kaapi_proc_t* kaapi_proc_alloc(kaapi_procid_t id)
{
  /* allocate a bound page containing the proc */

  kaapi_proc_t* proc;
  const int err = kaapi_numa_alloc_with_procid
    ((void**)&proc, sizeof(kaapi_proc_t), id);
  return err ? NULL : proc;
}

void kaapi_proc_free(kaapi_proc_t* proc)
{
  kaapi_numa_free(proc, sizeof(kaapi_proc_t));

#if KAAPI_CONFIG_PERF
  kaapi_perf_perthread_finalize(proc);
#endif
}

void kaapi_proc_init(kaapi_proc_t* proc, kaapi_procid_t id)
{
  proc->id_word = id;

  kaapi_atomic_write(&proc->control_word, KAAPI_PROC_CONTROL_UNDEF);
  kaapi_atomic_write(&proc->status_word, KAAPI_PROC_STATUS_UNDEF);

#if KAAPI_CONFIG_PERF
  kaapi_perf_perthread_initialize(proc);
  memset(proc->perf_counters, 0, sizeof(proc->perf_counters));
#endif

#if KAAPI_CONFIG_WS
  kaapi_refn_init(&proc->ws_split_refn);
  proc->ws_group = NULL;
  kaapi_atomic_write(&proc->ws_request.status, KAAPI_WS_REQUEST_UNDEF);
#endif
}

void kaapi_proc_foreach(kaapi_procfn_t on_proc, void* args)
{
  size_t i;

  for (i = 0; i < KAAPI_CONFIG_MAX_PROC; ++i)
  {
    kaapi_proc_t* const proc = kaapi_proc_get_byid(i);
    if ((proc != NULL) && on_proc(proc, args)) break ;
  }
}


/* map from a procid to a physical core id. this is needed
   when more core than physically available are used.
 */

static unsigned long physical_count = 0;

void kaapi_proc_init_once(void)
{
  physical_count = sysconf(_SC_NPROCESSORS_CONF);
}

unsigned long kaapi_proc_map_physical(kaapi_procid_t id)
{
  /* warning: kaapi_proc_init_once must have been called */
  return id % physical_count;
}

void* kaapi_proc_thread_entry(void* p)
{
  const kaapi_proc_args_t* const args = (const kaapi_proc_args_t*)p;

  kaapi_proc_t* const self_proc = kaapi_proc_alloc(args->id);
  if (self_proc == NULL)
  {
    printf("kaapi_proc_alloc() == -1\n");
    return NULL;
  }

  kaapi_proc_init(self_proc, args->id);
  self_proc->thread = pthread_self();
  pthread_setspecific(kaapi_proc_key, self_proc);
  kaapi_all_procs[args->id] = self_proc;
  kaapi_mem_write_barrier();

  /* wait until everyone ready */
  pthread_barrier_wait(args->barrier);

  while (1)
  {
    switch (kaapi_atomic_read(&self_proc->control_word))
    {
    case KAAPI_PROC_CONTROL_TERM:
      {
	kaapi_atomic_write
	  (&self_proc->status_word, KAAPI_PROC_STATUS_TERM);
	goto on_term;
	break ;
      }

    case KAAPI_PROC_CONTROL_EXEC:
      {
	void* exec_arg;
	void (*exec_fn)(void*);

	kaapi_mem_read_barrier();

	exec_arg = self_proc->exec_arg;
	exec_fn = self_proc->exec_fn;

	kaapi_atomic_write
	  (&self_proc->control_word, KAAPI_PROC_CONTROL_UNDEF);

	exec_fn(exec_arg);

	break ;
      }

    case KAAPI_PROC_CONTROL_UNDEF:
    default:
      break ;
    }
  }

 on_term:
  kaapi_proc_free(self_proc);

  return NULL;
}
