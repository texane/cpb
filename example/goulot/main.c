#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>
#include "kaapi_ctor.h"
#include "kaapi_mt.h"
#include "kaapi_numa.h"
#include "kaapi_proc.h"
#include "kaapi_bitmap.h"
#include "kaapi_perf.h"

typedef struct task_context
{
  pthread_barrier_t* start_barrier;
  pthread_barrier_t* stop_barrier;

  uint64_t* addr;
  size_t size;

  uint64_t sum;
  uint64_t usecs;

} task_context_t;

static void task_entry(void* args)
{
  volatile task_context_t* const tc = args;
  struct timeval now, sta, dif;
  const size_t size = tc->size / sizeof(uint64_t);
  volatile uint64_t* p = tc->addr;
  uint64_t sum = 0;
  size_t i;

  pthread_barrier_wait(tc->start_barrier);

  kaapi_perf_start_counters();

  gettimeofday(&sta, NULL);
  for (i = 0; i < size; ++i, ++p) sum += *p;
  gettimeofday(&now, NULL);

  kaapi_perf_stop_counters();

  timersub(&now, &sta, &dif);

  tc->sum += sum;
  tc->usecs += dif.tv_sec * 1000000 + dif.tv_usec;

  pthread_barrier_wait(tc->stop_barrier);
}

static void invalidate_memory
(void* addr, size_t size, uint64_t value)
{
  uint64_t* p = addr;
  size_t count = size / sizeof(uint64_t);
  for (; count; --count, ++p) *p = value;
}

static int print_proc_counters(kaapi_proc_t* proc, void* fubar)
{
  kaapi_perf_counter_t counters[KAAPI_PERF_MAX_COUNTERS] = { 0 };

  if (proc == kaapi_proc_get_self()) return 0;

  kaapi_perf_accum_proc_counters(proc, counters);
  printf("%02lu: %llu\n", kaapi_proc_get_id(proc), counters[0]);
  return 0;
}

int main(int ac, char** av)
{
  /* every command line provided ids are
     physical cpu ids. the translation to
     numa node is done by the program.
     av[1] the cpu source memory resides on
     av[2] the master physical cpu
     av[3+i] the task physical cpus
   */

#define CONFIG_MEM_SIZE (100 * 1024 * 1024)
#define CONFIG_ITER_COUNT 20

  const kaapi_procid_t source_id = atoi(av[1]);
  const kaapi_procid_t master_id = atoi(av[2]);

  void* addr;
  size_t total_size;
  size_t perthread_size;

  kaapi_bitmap_t cpu_map;
  size_t cpu_count;
  size_t i;
  size_t iter;

  uint64_t total_usecs;
  uint64_t total_sum;

  task_context_t contexts[KAAPI_CONFIG_MAX_PROC];
  pthread_barrier_t start_barrier, stop_barrier;

  kaapi_initialize();
  kaapi_mt_create_self(master_id);

  kaapi_bitmap_zero(&cpu_map);
  for (i = 3; i < ac; ++i)
    kaapi_bitmap_set(&cpu_map, atoi(av[i]));
  cpu_count = kaapi_bitmap_count(&cpu_map);
  perthread_size = CONFIG_MEM_SIZE / cpu_count;
  total_size = perthread_size * cpu_count;

  if (kaapi_numa_alloc_with_procid(&addr, total_size, source_id) == -1)
  {
    printf("kaapi_numa_alloc() == -1\n");
    return -1;
  }

  /* initialize contexts */
  for (i = 0; i < cpu_count; ++i)
  {
    task_context_t* const tc = &contexts[i];
    tc->start_barrier = &start_barrier;
    tc->stop_barrier = &stop_barrier;
    tc->addr = &((uint64_t*)addr)[i * (perthread_size / sizeof(uint64_t))];
    tc->size = perthread_size;
    tc->sum = 0;
    tc->usecs = 0;
  }

  kaapi_mt_create_threads(&cpu_map);

  for (iter = 0; iter < CONFIG_ITER_COUNT; ++iter)
  {
    kaapi_procid_t id;

    invalidate_memory(addr, total_size, iter + 1);

    pthread_barrier_init(&start_barrier, NULL, cpu_count + 1);
    pthread_barrier_init(&stop_barrier, NULL, cpu_count + 1);

    for (id = 0, i = 0; i < cpu_count; ++i, ++id)
    {
      id = kaapi_bitmap_scan(&cpu_map, id);
      kaapi_mt_spawn_task(id, task_entry, &contexts[i]);
    }

    pthread_barrier_wait(&start_barrier);
    pthread_barrier_wait(&stop_barrier);
  }

  /* reduce results */
  total_sum = 0;
  total_usecs = 0;
  for (i = 0; i < cpu_count; ++i)
  {
    volatile task_context_t* const tc = &contexts[i];
    total_usecs += tc->usecs;
    total_sum += tc->sum;
  }

#if 0
  const uint64_t wanted_sum =   
    (total_size / sizeof(uint64_t)) *
    (CONFIG_ITER_COUNT * (CONFIG_ITER_COUNT + 1) / 2);
  if (wanted_sum != total_sum)
    printf("invalid sum: %lu != %lu\n", wanted_sum, total_sum);
#endif

  const double total_secs = (double)total_usecs / 1E6;
  const double total_mb = (double)total_size / (1024. * 1024.);
  const double rate = ((double)CONFIG_ITER_COUNT * total_mb) / (total_secs / cpu_count);

  const double total_msecs = (double)total_usecs / 1E3;
  const double perthread_msecs = total_msecs / (double)cpu_count;

  printf("perthreadTime: %lf ms. memRate: %lf MB/s.\n", perthread_msecs, rate);

  kaapi_proc_foreach(print_proc_counters, kaapi_proc_get_self());

  kaapi_perf_counter_t counters[KAAPI_PERF_MAX_COUNTERS] = { 0 };
  kaapi_perf_accum_all_counters(counters);
  printf("to: %llu\n", counters[0]);

  kaapi_mt_join_threads(&cpu_map);

  kaapi_numa_free(addr, total_size);

  kaapi_finalize();

  return 0;
}
