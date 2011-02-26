#ifndef KAAPI_PROC_H_INCLUDED
# define KAAPI_PROC_H_INCLUDED


#include <pthread.h>
#include "kaapi_config.h"
#include "kaapi_atomic.h"

#if KAAPI_CONFIG_PERF
# include "kaapi_perf.h"
#endif

#if KAAPI_CONFIG_WS
# include "kaapi_ws.h"
#endif


/* global processing unit id */
typedef unsigned long kaapi_procid_t;
#define KAAPI_INVALID_PROCID ((kaapi_procid_t)-1)


/* processor descriptor block */
typedef struct kaapi_proc
{
  /* registers */
  kaapi_procid_t id_word;

#define KAAPI_PROC_CONTROL_UNDEF 0
#define KAAPI_PROC_CONTROL_TERM 1
#define KAAPI_PROC_CONTROL_EXEC 2
#define KAAPI_PROC_CONTROL_SYNC 3
  kaapi_atomic_t control_word;

#define KAAPI_PROC_STATUS_UNDEF 0
#define KAAPI_PROC_STATUS_SYNC 1
#define KAAPI_PROC_STATUS_TERM 2
  kaapi_atomic_t status_word;

  void (* volatile exec_fn)(void*);
  void* volatile exec_arg;

  /* pthread related */
  pthread_t thread;

#if KAAPI_CONFIG_PERF
  kaapi_perf_counter_t perf_counters[KAAPI_PERF_MAX_COUNTERS];
  int papi_event_set;
#endif

#if KAAPI_CONFIG_WS

  /* current adaptive task */
  kaapi_refn_t ws_split_refn;
  kaapi_ws_splitfn_t volatile ws_split_fn;
  void* volatile ws_split_data;

  /* workstealing */
  kaapi_ws_group_t* volatile ws_group;
  kaapi_ws_request_t ws_request;

#endif /* KAAPI_CONFIG_WS */

} kaapi_proc_t;


typedef struct kaapi_proc_args
{
  /* processor thread entry */
  kaapi_procid_t id;
  pthread_barrier_t* barrier;
} kaapi_proc_args_t;


/* processor set iterator */
typedef int (*kaapi_procfn_t)(kaapi_proc_t*, void*);

pthread_key_t kaapi_proc_key;

kaapi_proc_t* volatile kaapi_all_procs[KAAPI_CONFIG_MAX_PROC];

static inline kaapi_proc_t* kaapi_proc_get_self(void)
{ return pthread_getspecific(kaapi_proc_key); }

static inline kaapi_procid_t kaapi_proc_self_id(void)
{ return kaapi_proc_get_self()->id_word; }

static inline kaapi_procid_t kaapi_proc_get_id
(const kaapi_proc_t*  proc)
{ return proc->id_word; }

static inline kaapi_proc_t* kaapi_proc_get_byid(kaapi_procid_t id)
{ return kaapi_all_procs[id]; }

unsigned long kaapi_proc_map_physical(kaapi_procid_t);
kaapi_proc_t* kaapi_proc_alloc(kaapi_procid_t);
void kaapi_proc_free(kaapi_proc_t*);
void kaapi_proc_init(kaapi_proc_t*, kaapi_procid_t);
void kaapi_proc_init_once(void);
void* kaapi_proc_thread_entry(void*);
void kaapi_proc_foreach(kaapi_procfn_t, void*);

#if KAAPI_CONFIG_WS
static inline kaapi_ws_request_t*
kaapi_proc_get_ws_request(kaapi_procid_t id)
{ return &kaapi_proc_byid(id)->ws_request; }
#endif


#endif /* ! KAAPI_PROC_H_INCLUDED */
