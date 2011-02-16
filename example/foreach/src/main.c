#define _GNU_SOURCE 1
#include <sched.h>

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <numaif.h>
#include <sys/types.h>
#include <sys/mman.h>


/* static configuration
 */

#define CONFIG_USE_IDKOIFF 1
#define CONFIG_USE_IDFREEZE 0

#define CONFIG_PAGE_SIZE 0x1000

#define CONFIG_PROC_COUNT 128


/* atomics
 */

typedef struct kaapi_atomic
{
  volatile unsigned long value;
} kaapi_atomic_t;

static inline unsigned long kaapi_atomic_read(kaapi_atomic_t* a)
{
  return a->value;
}

static inline void kaapi_atomic_write(kaapi_atomic_t* a, unsigned long n)
{
  a->value = n;
}

static inline void kaapi_atomic_or(kaapi_atomic_t* a, unsigned long n)
{
  __sync_or_and_fetch(&a->value, n);
}

static inline void kaapi_atomic_sub(kaapi_atomic_t* a, unsigned long n)
{
  __sync_sub_and_fetch(&a->value, n);
}

static inline void kaapi_atomic_inc(kaapi_atomic_t* a)
{
  __sync_add_and_fetch(&a->value, 1);
}

static inline void kaapi_atomic_dec(kaapi_atomic_t* a)
{
  __sync_sub_and_fetch(&a->value, 1);
}


/* memory ordering
 */

static inline void kaapi_mem_write_barrier(void)
{
  /* todo_optimize */
  __sync_synchronize();
}

static inline void kaapi_mem_read_barrier(void)
{
  /* todo_optimize */
  __sync_synchronize();
}


/* slow the cpu down
 */

static void inline kaapi_cpu_slowdown(void)
{
  __asm__ __volatile__ ("pause\n\t");
}


/* locks
 */

typedef kaapi_atomic_t kaapi_lock_t;

static inline void kaapi_lock_init(kaapi_lock_t* l)
{
  kaapi_atomic_write(l, 0);
}

static inline unsigned int kaapi_lock_try_acquire(kaapi_lock_t* l)
{
  /* try acquiring the lock. return true on success. */

  /* put in cache, then try lock */
  if (kaapi_atomic_read(l)) return 0;
  return __sync_bool_compare_and_swap(&l->value, 0, 1) - 1;
} 

static inline void kaapi_lock_acquire(kaapi_lock_t* l)
{
  while (kaapi_lock_try_acquire(l) == 0)
    kaapi_cpu_slowdown();
}

static void kaapi_lock_release(kaapi_lock_t* l)
{
  kaapi_atomic_write(l, 0);
}


/* bitmaps (128 bits max)
 */

typedef struct kaapi_bitmap
{
  unsigned long bits[2];
} kaapi_bitmap_t;

static inline void kaapi_bitmap_zero
(kaapi_bitmap_t* bitmap)
{
  bitmap->bits[0] = 0;
  bitmap->bits[1] = 0;
}

static inline size_t kaapi_bitmap_count
(const kaapi_bitmap_t* bitmap)
{
  return
    __builtin_popcountl(bitmap->bits[0]) +
    __builtin_popcountl(bitmap->bits[1]);
}

static inline void kaapi_bitmap_set
(kaapi_bitmap_t* bitmap, size_t i)
{
  bitmap->bits[0] |= (unsigned long)i & ((1UL << 6) - 1UL);
  bitmap->bits[1] |= (unsigned long)i >> 6;
}

static inline void kaapi_bitmap_dup
(kaapi_bitmap_t* dest, const kaapi_bitmap_t* src)
{
  dest->bits[0] = src->bits[0];
  dest->bits[1] = src->bits[1];
}

static inline unsigned int kaapi_bitmap_is_empty
(const kaapi_bitmap_t* bitmap)
{
  return (bitmap->bits[0] | bitmap->bits[1]) == 0;
}

static inline size_t kaapi_bitmap_scan
(const kaapi_bitmap_t* bitmap, size_t i)
{
  /* scan for the first bit set, from pos i (included).
     assume the bitmap is not empty.
   */
  return __builtin_ffsl(bitmap->bits[(unsigned long)i >> 6]) - 1;
}

static inline void kaapi_bitmap_or
(kaapi_bitmap_t* a, const kaapi_bitmap_t* b)
{
  a->bits[0] |= b->bits[0];
  a->bits[1] |= b->bits[1];
}

static size_t kaapi_bitmap_pos
(const kaapi_bitmap_t* bitmap, size_t i)
{
  /* return the position of ith bit set */

  size_t pos = kaapi_bitmap_scan(bitmap, 0);

  for (i -= 1; i; --i)
    pos = kaapi_bitmap_scan(bitmap, pos + 1);

  return pos;
}


/* global processing unit id
 */

typedef unsigned long kaapi_procid_t;


/* numa routines
 */

static int kaapi_numa_bind
(void* addr, size_t size, kaapi_procid_t procid)
{
  const int mode = MPOL_BIND;
  const unsigned int flags = MPOL_MF_STRICT | MPOL_MF_MOVE;
  const unsigned long maxnode = CONFIG_PROC_COUNT;
  
  unsigned long nodemask[CONFIG_PROC_COUNT / (8 * sizeof(unsigned long))];

  memset(nodemask, 0, sizeof(nodemask));

  nodemask[procid / (8 * sizeof(unsigned long))] |=
    1UL << (procid % (8 * sizeof(unsigned long)));

  if (mbind(addr, size, mode, nodemask, maxnode, flags))
    return -1;

  return 0;
}


/* workstealing request outcome
 */

typedef void (*kaapi_ws_execfn_t)(void*);

typedef struct kaapi_ws_work
{
  kaapi_ws_execfn_t exec_fn;
  void* exec_data;
} kaapi_ws_work_t;

static inline void kaapi_ws_work_exec(kaapi_ws_work_t* w)
{
  w->exec_fn(w->exec_data);
}


/* workstealing request
 */

typedef struct kaapi_ws_request
{
  volatile kaapi_procid_t victim_id;

#define KAAPI_WS_REQUEST_UNDEF 0UL
#define KAAPI_WS_REQUEST_POSTED (1UL << 0)
#define KAAPI_WS_REQUEST_REPLIED (1UL << 1)
  kaapi_atomic_t status;

  /* replied data */
  volatile kaapi_ws_execfn_t exec_fn;
  volatile size_t data_size;
  volatile unsigned char data[256];

} kaapi_ws_request_t;

static void kaapi_ws_request_post
(kaapi_ws_request_t* req, kaapi_procid_t victim_id)
{
  req->victim_id = victim_id;
  kaapi_mem_write_barrier();
  kaapi_atomic_or(&req->status, KAAPI_WS_REQUEST_POSTED);
}

static inline void kaapi_ws_request_reply
(kaapi_ws_request_t* req, kaapi_ws_execfn_t exec_fn)
{
  req->exec_fn = exec_fn;
  kaapi_mem_write_barrier();
  kaapi_atomic_or(&req->status, KAAPI_WS_REQUEST_REPLIED);
}

static inline unsigned int kaapi_ws_request_test_ack
(kaapi_ws_request_t* req)
{
  /* return a boolean value */

  const unsigned long ored_value =
    KAAPI_WS_REQUEST_REPLIED | KAAPI_WS_REQUEST_POSTED;

  unsigned int is_replied = __sync_bool_compare_and_swap
    (&req->status.value, ored_value, KAAPI_WS_REQUEST_UNDEF);

  return is_replied;
}

static inline void* kaapi_ws_request_alloc_data
(kaapi_ws_request_t* req, size_t size)
{
  /* assume size < sizeof(req->data) */
  req->data_size = size;
  return (void*)req->data;
}


/* kaapi processors
 */

typedef void (*kaapi_ws_splitfn_t)(const kaapi_bitmap_t*, void*);

struct kaapi_ws_group;

typedef struct kaapi_proc
{
  /* registers */
  kaapi_procid_t id_word;

#define KAAPI_PROC_CONTROL_UNDEF 0
#define KAAPI_PROC_CONTROL_TERM 1
#define KAAPI_PROC_CONTROL_STEAL 2
#define KAAPI_PROC_CONTROL_SYNC 3
  kaapi_atomic_t control_word;

#define KAAPI_PROC_STATUS_UNDEF 0
#define KAAPI_PROC_STATUS_SYNC 1
#define KAAPI_PROC_STATUS_TERM 2
  kaapi_atomic_t status_word;

  /* current adaptive task */
  kaapi_atomic_t ws_split_refn;
  volatile kaapi_ws_splitfn_t ws_split_fn;
  void* volatile ws_split_data;

  /* workstealing */
  struct kaapi_ws_group* ws_group;
  kaapi_ws_request_t ws_request;

} kaapi_proc_t;

static kaapi_proc_t* volatile kaapi_all_procs[CONFIG_PROC_COUNT];

static pthread_key_t kaapi_proc_key;

static inline kaapi_proc_t* kaapi_proc_get_self(void)
{
  return pthread_getspecific(kaapi_proc_key);
}

static kaapi_proc_t* kaapi_proc_alloc(kaapi_procid_t id)
{
  /* allocate a bound page containing the proc */

  kaapi_proc_t* proc;

  if (posix_memalign((void**)&proc, CONFIG_PAGE_SIZE, sizeof(kaapi_proc_t)))
    return NULL;

  kaapi_numa_bind(proc, sizeof(kaapi_proc_t), id);

  return proc;
}

static inline void kaapi_proc_free(kaapi_proc_t* proc)
{
  free(proc);
}

static void kaapi_proc_init(kaapi_proc_t* proc, kaapi_procid_t id)
{
  proc->id_word = id;

  kaapi_atomic_write(&proc->control_word, KAAPI_PROC_CONTROL_UNDEF);
  kaapi_atomic_write(&proc->status_word, KAAPI_PROC_STATUS_UNDEF);

  kaapi_atomic_write(&proc->ws_split_refn, 0);
  proc->ws_split_fn = NULL;

  proc->ws_group = NULL;

  kaapi_atomic_write(&proc->ws_request.status, KAAPI_WS_REQUEST_UNDEF);
}


static kaapi_ws_request_t* kaapi_proc_get_ws_request(kaapi_procid_t id)
{
  return &kaapi_all_procs[id]->ws_request;
}


/* processor thread entry
 */

typedef struct kaapi_proc_args
{
  kaapi_procid_t id;
  pthread_barrier_t* barrier;
} kaapi_proc_args_t;

static int kaapi_ws_steal_work(kaapi_ws_work_t*);

static void* kaapi_proc_thread_entry(void* p)
{
  const kaapi_proc_args_t* const args = (const kaapi_proc_args_t*)p;

  kaapi_proc_t* const self_proc = kaapi_proc_alloc(args->id);

  kaapi_proc_init(self_proc, args->id);
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
	break ;
      }

    case KAAPI_PROC_CONTROL_STEAL:
      {
	kaapi_ws_work_t work;
	if (kaapi_ws_steal_work(&work) != -1)
	  kaapi_ws_work_exec(&work);
	break ;
      }

    case KAAPI_PROC_CONTROL_UNDEF:
    default:
      break ;
    }
  }

  return NULL;
}


/* memory topology interface
 */

#define KAAPI_MEMTOPO_LEVEL_L1 0
#define KAAPI_MEMTOPO_LEVEL_L2 1
#define KAAPI_MEMTOPO_LEVEL_L3 2
#define KAAPI_MEMTOPO_LEVEL_NUMA 3
#define KAAPI_MEMTOPO_LEVEL_SOCKET 4
#define KAAPI_MEMTOPO_LEVEL_MACHINE 5

static const size_t mem_size_bylevel[] =
{
  /* l1, l2, l3, numa, socket, machine */
#if CONFIG_USE_IDFREEZE
#elif CONFIG_USE_IDKOIFF
#else
# error "missing CONFIG_USE_HOSTNAME"
#endif
};

static const size_t group_count_bylevel[] =
{
  /* l1, l2, l3, numa, socket, machine */
#if CONFIG_USE_IDFREEZE
  48, 48, 8, 8, 4, 1
#elif CONFIG_USE_IDKOIFF
  16, 16, 8, 8, 8, 1
#else
# error "missing CONFIG_USE_HOSTNAME"
#endif
};

static size_t kaapi_memtopo_count_groups
(unsigned int level)
{
  /* number of groups available at mem level */
  return group_count_bylevel[level];
}

__attribute__((unused))
static size_t kaapi_memtopo_get_size
(unsigned int level)
{
  /* number of bytes available at mem level */
  return mem_size_bylevel[level];
}

static int kaapi_memtopo_get_group_members
(
 kaapi_bitmap_t* members, size_t* count,
 unsigned int level,
 unsigned int group_index
)
{
  /* todo_not_implemented */
  return -1;
}


/* uniform binding of pages on nodes at level
 */

static int kaapi_memtopo_bind_uniform
(void* addr, size_t size, unsigned int level)
{
#if 0 /* todo_not_implemented */

  kaapi_ws_layer_t* pos = kwl;

  const kaapi_ws_group_t* group;
  size_t count;
  size_t size_per_group;
  size_t size_per_block;

  while (pos->next != NULL)
  {
    if (!(pos->next->flags & KAAPI_WS_LAYER_ALLOCABLE))
      break ;
    else if (pos->next->mem_size < size)
      break ;

    pos = pos->next;
  }

  /* assume a layer found. for now, assume:
     mem_size = per_group * group_count
   */

  size_per_group = size / pos->group_count;
  block_size = size_per_group;
  if (size_per_group % size)
    block_size += ;

  group = pos->groups;
  count = pos->group_count
  for (; count; ++group, --count)
  {
    kaapi_block_t* block;
    kaapi_allocate_block(&block);
    bind(block->addr, block->size, group->memid);
  }

  return 0;

 on_failure:

#endif /* todo_not_implemented */

  return -1;
}


/* a workstealing group contains the workstealing
   registers for a given group (request, reply, ids)
   and the synchronization lock.
*/
typedef struct kaapi_ws_group
{
  /* todo_cacheline_algined */
  kaapi_lock_t lock;

  /* one bit per physical member */
  size_t member_count;
  kaapi_bitmap_t members;

  /* procid mapping */
#define KAAPI_WS_GROUP_CONTIGUOUS (1UL << 0)
  unsigned int flags;
  kaapi_procid_t first_procid;

} kaapi_ws_group_t;


/* group local id
 */
typedef unsigned long kaapi_ws_groupid_t;


static inline kaapi_procid_t kaapi_ws_groupid_to_procid
(kaapi_ws_group_t* group, kaapi_ws_groupid_t id)
{
  return (kaapi_procid_t)kaapi_bitmap_pos(&group->members, (size_t)id);
}


/* group allocation routines
 */

static inline void kaapi_ws_group_init(kaapi_ws_group_t* group)
{
  kaapi_lock_init(&group->lock);
  group->flags = 0;
  group->member_count = 0;
  kaapi_bitmap_zero(&group->members);
}


/* build a workstealing group set according
   to the machine memory topology and a level.
   return **groups an array of *group_count members
 */

static int kaapi_ws_create_mem_groups
(kaapi_ws_group_t** groups, size_t* group_count, unsigned int level)
{
  size_t i;

  *group_count = kaapi_memtopo_count_groups(level);
  if (*group_count == 0) return -1;

  *groups = malloc((*group_count) * sizeof(kaapi_ws_group_t));
  if (*groups == NULL) return -1;

  for (i = 0; i < *group_count; ++i)
  {
    kaapi_ws_group_t* const group = &(*groups)[i];

    kaapi_ws_group_init(group);

    kaapi_memtopo_get_group_members
      (&group->members, &group->member_count, level, i);
  }

  return 0;
}

static void kaapi_ws_destroy_mem_groups
(kaapi_ws_group_t* groups, size_t group_count)
{
  free(groups);
}


#if 0 /* todo_not_implemented */

/* hierarchical workstealing
   one layer contains the set of groups
   for a given memory level. this structure
   is cumulative, that is:
   this_layer = layer_info + sum(next_layers)
*/
typedef struct kaapi_ws_layer
{
  struct kaapi_ws_layer* next;
  struct kaapi_ws_layer* prev;

#define KAAPI_WS_LAYER_ALLOCABLE (1UL << 0)
  unsigned long flags;

  unsigned int mem_level;

  /* total memory size for this layer
     ie. mem_size = sum(groups.mem_size)
   */
  unsigned int mem_size;

  size_t group_count;
  kaapi_ws_group_t groups[1];

} kaapi_ws_layers;

#endif /* todo_not_implemented */


/* emit a steal request
 */

static inline kaapi_ws_groupid_t select_group_victim
(kaapi_ws_group_t* group)
{
  return (kaapi_ws_groupid_t)(rand() % group->member_count);
}

static int kaapi_ws_steal_work(kaapi_ws_work_t* work)
{
  /* todo: when walking up the hierarchy, the thread
     must or the different members and pass all of them
     to the final splitter without unlockcing 
   */

  /* todo: find a way to split amongst all the group members
     and submembers. either this is done by walking the
     hierarchy down or ...
   */

  kaapi_proc_t* const self_proc = kaapi_proc_get_self();
  kaapi_ws_group_t* const group = self_proc->ws_group;
  kaapi_ws_request_t* const self_req = &self_proc->ws_request;

  kaapi_procid_t victim_id;

  /* emit the stealing request */
  victim_id = kaapi_ws_groupid_to_procid
    (group, select_group_victim(group));
  kaapi_ws_request_post(self_req, victim_id);

 redo_acquire:
  if (kaapi_lock_try_acquire(&group->lock))
  {
    kaapi_proc_t* const victim_proc = kaapi_all_procs[victim_id];

    kaapi_atomic_inc(&victim_proc->ws_split_refn);

    if (victim_proc->ws_split_fn != NULL)
    {
      /* todo_not_implemented
	 should be a local bitmap being the union of all requests
       */
      victim_proc->ws_split_fn
	(&group->members, victim_proc->ws_split_data);
    }

    kaapi_atomic_dec(&victim_proc->ws_split_refn);

    kaapi_lock_release(&group->lock);
  } /* try_acquire */

  /* test our own request */
  if (kaapi_ws_request_test_ack(&self_proc->ws_request))
  {
    work->exec_fn = self_req->exec_fn;
    work->exec_data = (void*)self_req->data;
    goto on_success;
  }

  /* try to lock again */
  goto redo_acquire;

 on_success:
  return 0;
}


/* force a split of the data amongst all group members
 */
static int kaapi_ws_group_split
(
 kaapi_ws_group_t* groups, size_t group_count,
 kaapi_ws_splitfn_t split_fn, void* split_data
)
{
  /* this function must not be called concurrently with
     another splitter since there is 2 concurrency issues:
     . a coarse one: the request data would be overwrite
     . a finer one: a group member request may have been
     replied but has not yet seen. to avoid this, we can
     try a compare_and_swap race winning scheme.
  */

  kaapi_bitmap_t reqs;
  size_t i;

  kaapi_bitmap_zero(&reqs);

  /* lock all the groups */
  for (i = 0; i < group_count; ++i)
  {
    kaapi_ws_group_t* const group = &groups[i];
    kaapi_lock_acquire(&group->lock);
    kaapi_bitmap_or(&reqs, &group->members);
  }

  /* call the splitter */
  split_fn(&reqs, split_data);

  /* unlock groups */
  for (i = 0; i < group_count; ++i)
  {
    kaapi_ws_group_t* const group = &groups[i];
    kaapi_lock_release(&group->lock);
  }

  return 0;
}


/* thread creation helper routine. not part of the
   actual runtime, but needed for prototyping
   a thread is created for every group members
   and bound to the associated resource.
 */
static int kaapi_ws_start_groups
(kaapi_ws_group_t* groups, size_t group_count)
{
  pthread_barrier_t barrier;
  pthread_attr_t attr;
  pthread_t thread;
  kaapi_bitmap_t all_members;
  int error = -1;
  size_t i;
  size_t all_count;
  cpu_set_t cpuset;

  kaapi_proc_args_t args[CONFIG_PROC_COUNT];

  pthread_attr_init(&attr);

  /* make a bitmap that is the union of every members */
  kaapi_bitmap_zero(&all_members);
  for (i = 0; i < group_count; ++i)
  {
    kaapi_ws_group_t* const group = &groups[i];

    /* add group members to the member set */
    kaapi_bitmap_or(&all_members, &group->members);
  }

  all_count = kaapi_bitmap_count(&all_members);
  if (all_count == 0) goto on_error;

  pthread_barrier_init(&barrier, NULL, all_count);

  /* create one thread per member */
  for (i = 0; all_count; --all_count, ++i)
  {
    i = kaapi_bitmap_scan(&all_members, i);

    CPU_ZERO(&cpuset);
    CPU_SET(i, &cpuset);

    /* special case for the main thread */
    if (i == 0)
    {
      pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
      sched_yield();
      kaapi_all_procs[0] = kaapi_proc_alloc(0);
      kaapi_proc_init(kaapi_all_procs[0], 0);
      pthread_setspecific(kaapi_proc_key, kaapi_all_procs[0]);
      continue ; /* next member */
    }

    /* create thread bound on the ith processor */
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);

    args[i].id = (kaapi_procid_t)i;
    args[i].barrier = &barrier;
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


/* adaptive code section
 */
static void kaapi_ws_enter_adaptive
(kaapi_proc_t* proc, kaapi_ws_splitfn_t fn, void* data)
{
  proc->ws_split_data = data;
  kaapi_mem_write_barrier();
  proc->ws_split_fn = fn;
}

static void kaapi_ws_leave_adaptive
(kaapi_proc_t* proc)
{
  /* todo: wait for unused splitter */

  proc->ws_split_fn = NULL;

  kaapi_mem_write_barrier();

  /* wait for no one using the splitter */
  while (kaapi_atomic_read(&proc->ws_split_refn))
    kaapi_cpu_slowdown();
}


/* kaapi constructor, destructor
 */

static int kaapi_initialize(void)
{
  if (pthread_key_create(&kaapi_proc_key, NULL)) return -1;
  return 0;
}

static void kaapi_finalize(void)
{
  pthread_key_delete(kaapi_proc_key);

  if (kaapi_all_procs[0] != NULL)
  {
    kaapi_proc_free(kaapi_all_procs[0]);
    kaapi_all_procs[0] = NULL;
  }
}


/* foreach algorithm, application dependant.
 */

typedef struct foreach_work
{
  kaapi_atomic_t* counter;

  kaapi_lock_t lock;

  double* array;
  volatile size_t i;
  volatile size_t j;

} foreach_work_t;

static inline void init_foreach_work
(
 foreach_work_t* work,
 double* array, size_t size,
 kaapi_atomic_t* counter
)
{
  work->counter = counter;
  kaapi_lock_init(&work->lock);
  work->array = array;
  work->i = 0;
  work->j = size;
}

static void foreach_thief(void*);

static void splitter
(const kaapi_bitmap_t* req_map, void* arg)
{
  /* one splitter for both initial, grouped
     and task emitted requests. this is left
     to the splitter to distinguish if needed
   */

  foreach_work_t* const vw = arg;

  foreach_work_t* tw;
  kaapi_ws_request_t* req;
  size_t req_count;
  size_t unit_size;
  size_t work_size;
  size_t stolen_j;
  size_t i;

  req_count = kaapi_bitmap_count(req_map);
  if (req_count == 0) goto on_done;

  kaapi_lock_acquire(&vw->lock);

  work_size = vw->j - vw->i;
  unit_size = work_size / (req_count + 1);
  if (unit_size == 0)
  {
    req_count = work_size - 1;
    unit_size = 1;
  }

  stolen_j = vw->j;
  vw->j -= req_count * unit_size;

  kaapi_lock_release(&vw->lock);

  i = kaapi_bitmap_scan(req_map, 0);
  for (; i != (size_t)-1; stolen_j -= unit_size)
  {
    req = kaapi_proc_get_ws_request((kaapi_procid_t)i);

    tw = kaapi_ws_request_alloc_data(req, sizeof(foreach_work_t));
    tw->counter = vw->counter;
    kaapi_lock_init(&tw->lock);
    tw->array = vw->array;
    tw->i = stolen_j - unit_size;
    tw->j = stolen_j;

    kaapi_ws_request_reply(req, foreach_thief);

    /* next_request */
    i = kaapi_bitmap_scan(req_map, i + 1);
  }

 on_done:
  return ;
}

static int extract_seq
(foreach_work_t* w, size_t size, double** beg, double** end)
{
  int error = -1;

  kaapi_lock_acquire(&w->lock);

  if (w->i != w->j)
  {
    if (size > (w->j - w->i)) size = w->j - w->i;

    *beg = w->array + w->i;
    *end = w->array + w->i + size;

    w->i += size;

    error = 0;
  }

  kaapi_lock_release(&w->lock);

  return error;
}

static void foreach_common(foreach_work_t* w)
{
  static const size_t seq_size = 128;

  double* beg;
  double* end;

  while (extract_seq(w, seq_size, &beg, &end) != -1)
    for (; beg != end; ++beg) ++*beg;
}

static void foreach_thief(void* p)
{
  kaapi_proc_t* const self_proc = kaapi_proc_get_self();
  foreach_work_t* const work = p;

  kaapi_ws_enter_adaptive(self_proc, splitter, work);
  foreach_common(work);
  kaapi_ws_leave_adaptive(self_proc);
}


static void foreach_master(foreach_work_t* work)
{
  /* inplace foreach */

  kaapi_proc_t* const self_proc = kaapi_proc_get_self();

  kaapi_ws_enter_adaptive(self_proc, splitter, work);
  foreach_common(work);
  kaapi_ws_leave_adaptive(self_proc);

  /* wait for the thieves to end */
  while (kaapi_atomic_read(work->counter))
    kaapi_cpu_slowdown();
}


static int for_foreach
(double* array, size_t size, size_t iter_count)
{
  int error = -1;

  /* work descriptor */
  kaapi_atomic_t counter;
  foreach_work_t work;

  /* create the socket groups */
  const size_t total_size = size * sizeof(double);
  const unsigned int mem_level = KAAPI_MEMTOPO_LEVEL_SOCKET;

  kaapi_ws_group_t* groups = NULL;
  size_t group_count;

  if (kaapi_ws_create_mem_groups(&groups, &group_count, mem_level))
    goto on_error;

  /* start the associated threads */
  if (kaapi_ws_start_groups(groups, group_count))
    goto on_error;

  /* bind memory uniformly amongst group members */
  kaapi_memtopo_bind_uniform(array, total_size, mem_level);

  /* make the initial work */
  kaapi_atomic_write(&counter, 0);
  init_foreach_work(&work, array, size, &counter);

  /* initial split amongst the groups */
  kaapi_ws_group_split(groups, group_count, splitter, &work);

  /* run algorithm */
  for (; iter_count; --iter_count) foreach_master(&work);

  /* success */
  error = 0;

 on_error:
  if (groups != NULL)
    kaapi_ws_destroy_mem_groups(groups, group_count);

  return error;
}


/* array helpers
 */

static int allocate_array(double** addr, size_t count)
{
  const size_t total_size = sizeof(double) * count;
  if (posix_memalign((void**)addr, CONFIG_PAGE_SIZE, total_size))
    return -1;
  return 0;
}

static void free_array(double* addr, size_t count)
{
  free(addr);
}

static void fill_array(double* addr, size_t count)
{
  const size_t total_size = sizeof(double) * count;
  memset(addr, 0, total_size);
}


/* main
 */

int main(int ac, char** av)
{
#define CONFIG_ARRAY_COUNT (100 * 1024 * 1024)
  double* array = NULL;
  size_t count = CONFIG_ARRAY_COUNT;

  kaapi_initialize();

  allocate_array(&array, count * sizeof(double));

  /* so that pages are bound on the current core */
  fill_array(array, count * sizeof(double));

  for_foreach(array, count, 1000);

  free_array(array, count * sizeof(double));

  kaapi_finalize();

  return 0;
}
