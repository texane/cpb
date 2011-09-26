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


/* for prefetching, 128 bit accesses  */
#include <xmmintrin.h>

#if CONFIG_USE_MEMCPY128

static void memcpy128(void* d, const void* s, size_t n)
{
  /* d assumed aligned */

  static uintptr_t const mask = 16UL - 1UL;

  /* this is not correct but ok for testing */
  s = (const void*)((uintptr_t)s & ~mask);

  __m128i* dd = (__m128i*)d;
  const __m128i* ss = (const __m128i*)s;
  size_t nn = n / 16;

  /* dd[i] = ss[i]; */
  for (; nn; --nn, ++dd, ++ss)
    _mm_store_si128(dd, _mm_load_si128(ss));

  /* finalize */
  memcpy((void*)dd, (const void*)ss, n & mask);
}

#define xxx_memcpy memcpy128

#else

#define xxx_memcpy memcpy

#endif /* CONFIG_USE_MEMCPY128 */


static inline uint64_t fu(uint64_t bar)
{
  /* the following operation gives speedups up to
     6 cores on the same numa node.
     theoritically, we could achieve optimzal speedup.
     Otherwise, suspect some bottleneck on memory controller
   */

#if (CONFIG_COST > 0)
  __asm__ __volatile__
  (

   "incl %0\n\t"
#if (CONFIG_COST > 1)
   "incl %0\n\t"
#endif
#if (CONFIG_COST > 2)
   "incl %0\n\t"
#endif
#if (CONFIG_COST > 3)
   "incl %0\n\t"
#endif
#if (CONFIG_COST > 4)
   "incl %0\n\t"
#endif

#if (CONFIG_COST > 5)
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
#endif

#if (CONFIG_COST > 10)
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
#endif

#if (CONFIG_COST > 15)
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
#endif

#if (CONFIG_COST > 20)
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
#endif

#if (CONFIG_COST > 25)
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
#endif

#if (CONFIG_COST > 30)
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
#endif

#if (CONFIG_COST > 35)
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
#endif

#if (CONFIG_COST > 40)
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
#endif

#if (CONFIG_COST > 45)
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
#endif

#if (CONFIG_COST > 50)
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
   "incl %0\n\t"
#endif

   :"=m"(bar)
  );
#endif /* CONFIG_COST > 0 */

  return bar;
}


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

#if 0 /* use local buffer */

  static const size_t buffer_size = 4 * 0x1000;
  for (i = 0; i < size; i += buffer_size)
  {
    uint64_t local_buffer[buffer_size] __attribute__((aligned(16)));
    const size_t local_size =
      buffer_size > (size - i) ? (size - i) : buffer_size;

    xxx_memcpy(local_buffer, (void*)p, local_size * sizeof(uint64_t));

    size_t j;
    for (j = 0; j < local_size; ++j, ++)
      sum += fu(*p);
  }

#elif 0 /* prefetch queue */

  while (1)
  {
    if (fifo_pop() == -1)
    {
      if (is_prefetch_done) break ;
    }
    else
    {
      /* process the buffer */
    }
  }

#elif 0 /* time only the memcpy */

  static const size_t buffer_size = 4 * 0x1000;
  for (i = 0; i < size; i += buffer_size)
  {
    uint64_t local_buffer[buffer_size] __attribute__((aligned(16)));

    size_t j, k;
    for (k = 0, j = i; k < buffer_size; ++k, ++j)
      local_buffer[k] = ((uint64_t*)p)[j];
  }

#elif 1 /* sw prefetching code */

  for (i = 0; i < size; ++i, ++p)
  {
    /* static const size_t pref_size = 128; */
    /* _mm_prefetch((void*)((uintptr_t)p + pref_size), _MM_HINT_NTA); */
    /* _mm_prefetch((void*)((uintptr_t)p + pref_size), _MM_HINT_T0); */
    sum += fu(*p);
  }

#else /* default code */

  for (i = 0; i < size; ++i, ++p)
    sum += fu(*p);

#endif

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

__attribute__((unused))
static int print_proc_counters(kaapi_proc_t* proc, void* fubar)
{
  kaapi_perf_counter_t counters[KAAPI_PERF_MAX_COUNTERS] = { 0, 0, 0 };

  /* if (proc == kaapi_proc_get_self()) return 0; */

  kaapi_perf_accum_proc_counters(proc, counters);

  printf("%02lu: %llu %llu\n",
	 kaapi_proc_get_id(proc),
	 counters[0], counters[1]);

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

#define CONFIG_MEM_SIZE (10 * 1024 * 1024)
#define CONFIG_ITER_COUNT 20

  const kaapi_procid_t source_id = atoi(av[1]);
  const kaapi_procid_t master_id = atoi(av[2]);

  struct timeval sto, sta, dif;

  void* addr;
  size_t total_size;
  size_t perthread_size;

  kaapi_bitmap_t cpu_map;
  size_t cpu_count;
  size_t i;
  size_t iter;

  uint64_t master_usecs;
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

  master_usecs = 0;
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

    /* start perf */
    kaapi_perf_start_counters();
    gettimeofday(&sta, NULL);

    pthread_barrier_wait(&stop_barrier);

    /* stop perf */
    kaapi_perf_stop_counters();
    gettimeofday(&sto, NULL);
    timersub(&sto, &sta, &dif);
    master_usecs += dif.tv_sec * 1000000 + dif.tv_usec;
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

#if 0
  const double total_secs = (double)total_usecs / 1E6;
  const double total_mb = (double)total_size / (1024. * 1024.);
  const double rate = ((double)CONFIG_ITER_COUNT * total_mb) / (total_secs / cpu_count);

  const double total_msecs = (double)total_usecs / 1E3;
  const double perthread_msecs = total_msecs / (double)cpu_count;

  printf("perthreadTime: %lf ms. memRate: %lf MB/s.\n", perthread_msecs, rate);
#endif

  /* kaapi_proc_foreach(print_proc_counters, NULL); */

#if 0
  kaapi_perf_counter_t self_counters[KAAPI_PERF_MAX_COUNTERS] = { 0, 0, 0 };
  kaapi_perf_accum_proc_counters(kaapi_proc_get_self(), self_counters);
  printf("# MasterUsecs       : %lu\n", master_usecs);

  kaapi_perf_counter_t all_counters[KAAPI_PERF_MAX_COUNTERS] = { 0, 0, 0 };
  kaapi_perf_accum_all_counters(all_counters);

  printf("# totalReadRequests : %llu\n", all_counters[1] - self_counters[1]);
  printf("# totalCyclesStalled: %llu\n", all_counters[2] - self_counters[2]);
#else
  printf("%lu", master_usecs);
#endif

  kaapi_mt_join_threads(&cpu_map);

  kaapi_numa_free(addr, total_size);

  kaapi_finalize();

  return 0;
}
