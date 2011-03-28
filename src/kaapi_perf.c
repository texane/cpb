#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/types.h>
#include "kaapi_config.h"
#include "kaapi_proc.h"
#include "kaapi_perf.h"
#include "papi.h"


static int papi_event_codes[3];
static size_t papi_event_count;


static int get_codes_from_environ(void)
{
  char* s;
  int code;

  papi_event_count = 0;

  s = getenv("KAAPI_PERF_PAPI0");
  if (s != NULL)
  {
    if (PAPI_event_name_to_code(s, &code) != PAPI_OK)
      return -1;
    papi_event_codes[papi_event_count++] = code;
  }

  s = getenv("KAAPI_PERF_PAPI1");
  if (s != NULL)
  {
    if (PAPI_event_name_to_code(s, &code) != PAPI_OK)
      return -1;
    papi_event_codes[papi_event_count++] = code;
  }

  s = getenv("KAAPI_PERF_PAPI2");
  if (s != NULL)
  {
    if (PAPI_event_name_to_code(s, &code) != PAPI_OK)
      return -1;
    papi_event_codes[papi_event_count++] = code;
  }

  return 0;
}


int kaapi_perf_initialize(void)
{
  if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT)
    return -1;

  if (PAPI_thread_init(pthread_self) != PAPI_OK)
    return -1;

  if (get_codes_from_environ())
    return -1;

  return 0;
}

void kaapi_perf_finalize(void)
{
  PAPI_shutdown();
}

int kaapi_perf_perthread_initialize(kaapi_proc_t* proc)
{
  int err;
  PAPI_option_t opt;

  if (PAPI_register_thread() != PAPI_OK)
    return -1;

  /* mandatory for subsequent call */
  proc->papi_event_set = PAPI_NULL;

  if (PAPI_create_eventset(&proc->papi_event_set))
    return -1;

  /* cpu as the default component */
  if (PAPI_assign_eventset_component(proc->papi_event_set, 0) != PAPI_OK)
    return -1;

  /* thread granularity */
  memset(&opt, 0, sizeof(opt));
  opt.granularity.def_cidx = proc->papi_event_set;
  opt.granularity.eventset = proc->papi_event_set;
  opt.granularity.granularity = PAPI_GRN_THR;
  err = PAPI_set_opt(PAPI_GRANUL, &opt);
  if (err != PAPI_OK) return -1;

  /* user domain */
  memset(&opt, 0, sizeof(opt));
  opt.domain.eventset = proc->papi_event_set;
  opt.domain.domain = PAPI_DOM_USER;
  err = PAPI_set_opt(PAPI_DOMAIN, &opt);
  if (err != PAPI_OK)
  {
    printf("error == %d\n", err);
    /* exit(-1); */
    return -1;
  }

  err = PAPI_add_events
    (proc->papi_event_set, papi_event_codes, papi_event_count);
  if (err != PAPI_OK) return -1;

  if (PAPI_start(proc->papi_event_set) != PAPI_OK)
    return -1;

  return 0;
}

void kaapi_perf_perthread_finalize(kaapi_proc_t* proc)
{
  PAPI_stop(proc->papi_event_set, NULL);
  PAPI_cleanup_eventset(proc->papi_event_set);
  PAPI_destroy_eventset(&proc->papi_event_set);
  PAPI_unregister_thread();
}

int kaapi_perf_start_counters(void)
{
  kaapi_proc_t* const proc = kaapi_proc_get_self();

  if (PAPI_reset(proc->papi_event_set) != PAPI_OK)
    return -1;
  return 0;
}

int kaapi_perf_stop_counters(void)
{
  kaapi_proc_t* const proc = kaapi_proc_get_self();

  const int err = PAPI_accum
    (proc->papi_event_set, (long_long*)proc->perf_counters);

  if (err != PAPI_OK)
    return -1;
  return 0;
}

int kaapi_perf_accum_proc_counters
(kaapi_proc_t* proc, kaapi_perf_counter_t* counters)
{
  size_t i;
  for (i = 0; i < KAAPI_PERF_MAX_COUNTERS; ++i)
    counters[i] += proc->perf_counters[i];
  return 0;
}

static inline int accum_counters(kaapi_proc_t* proc, void* p)
{
  kaapi_perf_accum_proc_counters(proc, p);
  return 0;
}

int kaapi_perf_accum_all_counters(kaapi_perf_counter_t* values)
{
  kaapi_proc_foreach(accum_counters, values);
  return 0;
}
