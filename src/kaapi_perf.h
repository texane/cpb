#ifndef KAAPI_PERF_H_INCLUDED
# define KAAPI_PERF_H_INCLUDED


#define KAAPI_PERF_MAX_COUNTERS 3

typedef unsigned long long kaapi_perf_counter_t;

struct kaapi_proc;

int kaapi_perf_initialize(void);
void kaapi_perf_finalize(void);
int kaapi_perf_perthread_initialize(struct kaapi_proc*);
void kaapi_perf_perthread_finalize(struct kaapi_proc*);
int kaapi_perf_start_counters(void);
int kaapi_perf_stop_counters(void);
int kaapi_perf_accum_proc_counters(struct kaapi_proc*, kaapi_perf_counter_t*);
int kaapi_perf_accum_all_counters(kaapi_perf_counter_t*);


#endif /* ! KAAPI_PERF_H_INCLUDED */
