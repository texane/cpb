#define _GNU_SOURCE 1
#include <sched.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/types.h>
#include "kaapi_config.h"
#include "kaapi_mt.h"
#include "kaapi_proc.h"
#include "kaapi_bitmap.h"
#include "kaapi_atomic.h"


int kaapi_mt_create_threads(const kaapi_bitmap_t* map)
{
  pthread_barrier_t barrier;
  pthread_attr_t attr;
  pthread_t thread;
  int error = -1;
  size_t id;
  size_t i;
  size_t all_count;
  cpu_set_t cpuset;

  kaapi_proc_args_t args[KAAPI_CONFIG_MAX_PROC];

  pthread_attr_init(&attr);

  all_count = kaapi_bitmap_count(map);
  if (all_count == 0) goto on_error;

  /* +1 since we synchronize too */
  pthread_barrier_init(&barrier, NULL, all_count + 1);

  /* create one thread per member */
  for (id = 0, i = 0; i < all_count; ++i, ++id)
  {
    id = kaapi_bitmap_scan(map, id);

    CPU_ZERO(&cpuset);
    CPU_SET(kaapi_proc_map_physical(id), &cpuset);

    /* create thread bound on the ith processor */
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);

    args[i].id = (kaapi_procid_t)id;
    args[i].barrier = &barrier;
    kaapi_mem_write_barrier();

    error = pthread_create
      (&thread, &attr, kaapi_proc_thread_entry, &args[i]);
    if (error) goto on_error;
  }

  /* wait for all the thread to be ready */
  pthread_barrier_wait(&barrier);

  /* success */
  error = 0;

 on_error:
  pthread_attr_destroy(&attr);
  return error;
}

int kaapi_mt_create_self(kaapi_procid_t id)
{
  kaapi_proc_t* const self_proc = kaapi_proc_alloc(id);
  cpu_set_t cpuset;

  CPU_ZERO(&cpuset);
  CPU_SET(kaapi_proc_map_physical(id), &cpuset);

  pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  sched_yield();
  kaapi_all_procs[id] = self_proc;
  kaapi_proc_init(self_proc, id);
  self_proc->thread = pthread_self();
  pthread_setspecific(kaapi_proc_key, self_proc);

  return 0;
}

static int join_proc(kaapi_proc_t* proc, void* fubar)
{
  kaapi_procid_t id;
  pthread_t thread;

  if (proc == fubar) return 0;

  /* get cached values since proc becomes invalid */
  id = proc->id_word;
  thread = proc->thread;
  kaapi_mem_read_barrier();

  kaapi_atomic_write(&proc->control_word, KAAPI_PROC_CONTROL_TERM);
  pthread_join(thread, NULL);
  kaapi_all_procs[id] = NULL;

  return 0;
}

int kaapi_mt_join_threads(const kaapi_bitmap_t* map)
{
  kaapi_proc_t* const self_proc = kaapi_proc_get_self();

  kaapi_proc_foreach(join_proc, self_proc);

  if (kaapi_bitmap_is_set(map, kaapi_proc_self_id()))
  {
    kaapi_all_procs[self_proc->id_word] = NULL;
    kaapi_proc_free(self_proc);
  }

  return 0;
}


int kaapi_mt_spawn_task
(kaapi_procid_t id, void (*fn)(void*), void* p)
{
  kaapi_proc_t* const proc = kaapi_proc_get_byid(id);

  proc->exec_fn = fn;
  proc->exec_arg = p;
  kaapi_mem_write_barrier();

  kaapi_atomic_write(&proc->control_word, KAAPI_PROC_CONTROL_EXEC);

  return 0;
}
