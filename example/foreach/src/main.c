#define _GNU_SOURCE 1
#include <sched.h>

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


/* static runtime configuration
 */

#define CONFIG_PAGE_SIZE 0x1000
#define CONFIG_PROC_COUNT 128


/* atomics
 */

typedef struct kaapi_atomic
{
  volatile unsigned long value;
} kaapi_atomic_t;

static inline unsigned long kaapi_atomic_read
(const kaapi_atomic_t* a)
{
  return a->value;
}

static inline void kaapi_atomic_write
(kaapi_atomic_t* a, unsigned long n)
{
  a->value = n;
}

static inline void kaapi_atomic_or
(kaapi_atomic_t* a, unsigned long n)
{
  __sync_fetch_and_or(&a->value, n);
}

static inline void kaapi_atomic_sub
(kaapi_atomic_t* a, unsigned long n)
{
  __sync_fetch_and_sub(&a->value, n);
}

static inline void kaapi_atomic_add
(kaapi_atomic_t* a, unsigned long n)
{
  __sync_fetch_and_add(&a->value, n);
}

static inline void kaapi_atomic_inc
(kaapi_atomic_t* a)
{
  kaapi_atomic_add(a, 1);
}

static inline void kaapi_atomic_dec
(kaapi_atomic_t* a)
{
  kaapi_atomic_sub(a, 1);
}


/* slow the cpu down
 */

static void inline kaapi_cpu_slowdown(void)
{
  __asm__ __volatile__ ("pause\n\t");
}


/* ref couting
 */

typedef kaapi_atomic_t kaapi_refn_t;

static inline void kaapi_refn_init(kaapi_refn_t* refn)
{
  kaapi_atomic_write(refn, 0);
}

static int kaapi_refn_get(kaapi_refn_t* refn)
{
  unsigned long value;
  unsigned long prev_value;

  value = kaapi_atomic_read(refn);
  while (value)
  {
    prev_value = __sync_val_compare_and_swap
      (&refn->value, value, value + 1);

    /* success */
    if (prev_value == value) return 0;

    value = prev_value;
  }

  return -1;
}

static inline void kaapi_refn_put(kaapi_refn_t* refn)
{
  kaapi_atomic_dec(refn);
}

static inline void kaapi_refn_set(kaapi_refn_t* refn)
{
  /* force the initial value */
  kaapi_atomic_write(refn, 1);
}

static inline void kaapi_refn_wait(kaapi_refn_t* refn)
{
  /* wait for refn to drop to 0 */
  while (kaapi_atomic_read(refn))
    kaapi_cpu_slowdown();
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

static inline void kaapi_mem_full_barrier(void)
{
  __sync_synchronize();
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
  return __sync_bool_compare_and_swap(&l->value, 0, 1);
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
  const size_t j = i / (8 * sizeof(unsigned long));
  bitmap->bits[j] |= 1UL << (i % (8 * sizeof(unsigned long)));
}

static inline void kaapi_bitmap_clear
(kaapi_bitmap_t* bitmap, size_t i)
{
  const size_t j = i / (8 * sizeof(unsigned long));
  bitmap->bits[j] &= ~(1UL << (i % (8 * sizeof(unsigned long))));
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
  const size_t j = i / (8 * sizeof(unsigned long));
  const unsigned long mask = ~((1UL << (i % (8 * sizeof(unsigned long)))) - 1UL);

  /* mask the lower bits and scan */
  return (j * 8 * sizeof(unsigned long)) + __builtin_ffsl(bitmap->bits[j] & mask) - 1;
}

static inline void kaapi_bitmap_or
(kaapi_bitmap_t* a, const kaapi_bitmap_t* b)
{
  a->bits[0] |= b->bits[0];
  a->bits[1] |= b->bits[1];
}

static inline void kaapi_bitmap_and
(kaapi_bitmap_t* a, const kaapi_bitmap_t* b)
{
  a->bits[0] &= b->bits[0];
  a->bits[1] &= b->bits[1];
}

static size_t kaapi_bitmap_pos
(const kaapi_bitmap_t* bitmap, size_t i)
{
  /* return the position of ith bit set
     assume there is a ith bit set
   */

  size_t pos;
  for (pos = 0; i; --i, ++pos)
    pos = kaapi_bitmap_scan(bitmap, pos);

  return pos;
}

static void kaapi_bitmap_print(const kaapi_bitmap_t* bitmap)
{
  printf("%08lx%08lx\n", bitmap->bits[1], bitmap->bits[0]);
}


/* global processing unit id
 */

typedef unsigned long kaapi_procid_t;


/* map from a procid to a physical core id. this is needed
   when more core than physically available are used.
 */

static inline unsigned long kaapi_xxx_map_procid(kaapi_procid_t id)
{
  /* warning: non reentrant */
  static unsigned long physical_count = 0;
  if (physical_count == 0)
    physical_count = sysconf(_SC_NPROCESSORS_CONF);
  return id % physical_count;
}


#if CONFIG_USE_NUMA

#include <numaif.h>
#include <numa.h>

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

#endif /* CONFIG_USE_NUMA */


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

static inline unsigned int kaapi_ws_request_is_posted
(const kaapi_ws_request_t* req)
{
  /* return a boolean value */
  return kaapi_atomic_read(&req->status) & KAAPI_WS_REQUEST_POSTED;
}

static inline unsigned int kaapi_ws_request_is_replied
(kaapi_ws_request_t* req)
{
  /* return a boolean value */
  return kaapi_atomic_read(&req->status) & KAAPI_WS_REQUEST_REPLIED;
}

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

static inline void kaapi_ws_request_fail
(kaapi_ws_request_t* req)
{
  req->exec_fn = NULL;
  kaapi_mem_write_barrier();
  kaapi_atomic_or(&req->status, KAAPI_WS_REQUEST_REPLIED);
}

static inline unsigned int kaapi_ws_request_test_ack
(kaapi_ws_request_t* req)
{
  /* return a boolean value */

  const unsigned long ored_value =
    KAAPI_WS_REQUEST_REPLIED | KAAPI_WS_REQUEST_POSTED;

  const unsigned int is_replied = __sync_bool_compare_and_swap
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

typedef void (*kaapi_ws_splitfn_t)(const kaapi_bitmap_t*, size_t, void*);

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
  kaapi_refn_t ws_split_refn;
  kaapi_ws_splitfn_t volatile ws_split_fn;
  void* volatile ws_split_data;

  /* workstealing */
  struct kaapi_ws_group* volatile ws_group;
  kaapi_ws_request_t ws_request;

} kaapi_proc_t;

static kaapi_proc_t* volatile kaapi_all_procs[CONFIG_PROC_COUNT];

static pthread_key_t kaapi_proc_key;

static inline kaapi_proc_t* kaapi_proc_get_self(void)
{
  return pthread_getspecific(kaapi_proc_key);
}

static kaapi_proc_t* kaapi_proc_byid(kaapi_procid_t id)
{
  return kaapi_all_procs[id];
}

static kaapi_proc_t* kaapi_proc_alloc(kaapi_procid_t id)
{
  /* allocate a bound page containing the proc */

  kaapi_proc_t* proc;

  if (posix_memalign((void**)&proc, CONFIG_PAGE_SIZE, sizeof(kaapi_proc_t)))
    return NULL;

#if CONFIG_USE_NUMA
  kaapi_numa_bind(proc, sizeof(kaapi_proc_t), id);
#endif

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

  kaapi_refn_init(&proc->ws_split_refn);

  proc->ws_group = NULL;

  kaapi_atomic_write(&proc->ws_request.status, KAAPI_WS_REQUEST_UNDEF);
}

static kaapi_ws_request_t* kaapi_proc_get_ws_request(kaapi_procid_t id)
{
  return &kaapi_proc_byid(id)->ws_request;
}

static void kaapi_proc_foreach
(int (*on_proc)(kaapi_proc_t*, void*), void* args)
{
  size_t i;

  for (i = 0; i < CONFIG_PROC_COUNT; ++i)
  {
    kaapi_proc_t* const proc = kaapi_proc_byid(i);
    if ((proc != NULL) && on_proc(proc, args)) break ;
  }
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
  if (group->flags & KAAPI_WS_GROUP_CONTIGUOUS)
    return group->first_procid + id;
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


/* group member iterator and wrappers
 */

static void kaapi_ws_group_foreach
(kaapi_ws_group_t* group, int (*on_member)(kaapi_proc_t*, void*), void* p)
{
  size_t pos;
  size_t i;

  for (i = 0, pos = 0; i < group->member_count; ++i, ++pos)
  {
    pos = kaapi_bitmap_scan(&group->members, pos);
    if (on_member(kaapi_proc_byid(pos), p)) break ;
  }
}

static int write_control_steal(kaapi_proc_t* proc, void* group)
{
  proc->ws_group = group;
  kaapi_atomic_write(&proc->control_word, KAAPI_PROC_CONTROL_STEAL);

  /* next_member */
  return 0;
}

static void kaapi_ws_group_print(kaapi_ws_group_t* group);

static inline void kaapi_ws_group_start(kaapi_ws_group_t* group)
{
#if 1
  printf("starting_group{");
  kaapi_ws_group_print(group);
  printf(" }\n");
#endif

  kaapi_ws_group_foreach(group, write_control_steal, group);
}


/* print group members
 */

static int print_group_member(kaapi_proc_t* proc, void* furbar)
{
  printf(" %lu", proc->id_word);
  return 0;
}

static void kaapi_ws_group_print(kaapi_ws_group_t* group)
{
  kaapi_ws_group_foreach(group, print_group_member, NULL);
}


/* get the concurrency level
 */

static size_t kaapi_conf_get_cpucount(void)
{
  static size_t cpu_count = 0;

  if (cpu_count == 0)
  {
    const const char* s = getenv("KAAPI_CPUCOUNT");
    cpu_count = sysconf(_SC_NPROCESSORS_CONF);
    /* environ overrides physical count */
    if (s != NULL) cpu_count = atoi(s);
  }

  return cpu_count;
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

  size_t group_count;

  switch (level)
  {
  case KAAPI_MEMTOPO_LEVEL_NUMA:
    {
      group_count = (size_t)numa_max_node() + 1;
    } break ;

  default:
    {
      group_count = group_count_bylevel[level];
    } break ;
  }

  return group_count;
}

__attribute__((unused))
static size_t kaapi_memtopo_get_size
(unsigned int level)
{
  /* number of bytes available at mem level */
  return mem_size_bylevel[level];
}

static int kaapi_memtopo_get_ws_group_members
(
 kaapi_ws_group_t* group,
 unsigned int level,
 unsigned int group_index
)
{
  switch (level)
  {
  case KAAPI_MEMTOPO_LEVEL_NUMA:
    {
      struct bitmask* cpu_mask;
      size_t cpu_pos;

      kaapi_bitmap_zero(&group->members);

      cpu_mask = numa_allocate_cpumask();
      if (cpu_mask == NULL) break ;

      if (numa_node_to_cpus(group_index, cpu_mask) != -1)
      {
	for (cpu_pos = 0; cpu_pos < CONFIG_PROC_COUNT; ++cpu_pos)
	  if (numa_bitmask_isbitset(cpu_mask, (unsigned int)cpu_pos))
	  {
	    kaapi_bitmap_set(&group->members, cpu_pos);
	    ++group->member_count;
	  }
      }

      numa_free_cpumask(cpu_mask);
    }
    break ; /* KAAPI_MEMTOPO_LEVEL_NUMA */

  default:
    {
      kaapi_bitmap_zero(&group->members);
      group->member_count = 0;
    }
    break ; /* default */
  }

  return 0;
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

static int set_member_req(kaapi_proc_t* proc, void* reqs)
{
  /* dont overwrite already replied requests */
  if (kaapi_ws_request_is_replied(&proc->ws_request))
    return 0;

  if (kaapi_ws_request_is_posted(&proc->ws_request))
    kaapi_bitmap_set(reqs, proc->id_word);

  return 0;
}

static inline kaapi_ws_groupid_t select_group_victim
(kaapi_ws_group_t* group)
{
  return (kaapi_ws_groupid_t)(rand() % group->member_count);
}

static void fail_requests
(const kaapi_bitmap_t* req_map, size_t req_count)
{
  kaapi_ws_request_t* req;
  size_t i;

  if (req_count == 0) return ;

  for (i = 0; req_count; ++i, --req_count)
  {
    /* next_request */
    i = kaapi_bitmap_scan(req_map, i);
    req = kaapi_proc_get_ws_request((kaapi_procid_t)i);
    kaapi_ws_request_fail(req);
  }
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

  /* select a victim and particular cases */
 redo_select:
  victim_id = kaapi_ws_groupid_to_procid(group, select_group_victim(group));
  if (victim_id == self_proc->id_word)
  {
    /* initial split mechanism may have posted
       a reply before we post the request.
    */
    if (kaapi_ws_request_is_replied(self_req))
    {
      /* todo_optim: avoid posting the request */
      kaapi_ws_request_post(self_req, victim_id);
      goto on_reply;
    }

    /* if we are alone in the group, redoing the
       select would result in an infinite loop.
     */
    if (group->member_count == 1) goto on_failure;

    /* retry victim selection */
    goto redo_select;
  }

  /* emit the request */
  kaapi_ws_request_post(self_req, victim_id);

#if 0
  printf("req: %lu -> %lu\n", self_proc->id_word, victim_id);
#endif

 redo_acquire:
  if (kaapi_lock_try_acquire(&group->lock))
  {
    kaapi_proc_t* const victim_proc = kaapi_proc_byid(victim_id);
    kaapi_ws_splitfn_t split_fn;
    void* split_data;
    kaapi_bitmap_t reqs;
    size_t req_count;

    /* make request bitmap
       todo_optimize: store post / reply as bits. knowing
       who posted in the group is just a matter or oring
    */
    kaapi_bitmap_zero(&reqs);
    kaapi_ws_group_foreach(group, set_member_req, &reqs);
    req_count = kaapi_bitmap_count(&reqs);

    /* use a local split_fn to allow leave_adaptive to set NULL.
       otherwise, a refn_get always succeeds and leave_adaptive
       would starve.
    */
    split_data = victim_proc->ws_split_data;
    split_fn = victim_proc->ws_split_fn;
    if (split_fn != NULL)
    {
      if (kaapi_refn_get(&victim_proc->ws_split_refn) == -1)
	goto do_fail_requests;

      split_fn(&reqs, req_count, split_data);
      kaapi_refn_put(&victim_proc->ws_split_refn);
    }
    else /* fail requests */
    {
    do_fail_requests:
      fail_requests(&reqs, req_count);
    } /* split_fn == NULL || get_refn() == -1 */

    kaapi_lock_release(&group->lock);

  } /* lock_acquired */

  /* test our own request */
 on_reply:
  if (kaapi_ws_request_test_ack(&self_proc->ws_request))
  {
    if (self_req->exec_fn == NULL) goto on_failure;

    work->exec_fn = self_req->exec_fn;
    work->exec_data = (void*)self_req->data;
    goto on_success;
  }

  /* termination requested */
  if (kaapi_atomic_read(&self_proc->control_word) != KAAPI_PROC_CONTROL_STEAL)
    goto on_failure;

  /* try to lock again */
  goto redo_acquire;

 on_success:
  return 0;

 on_failure:
  return -1;
}


/* force a split of the data amongst all group members
 */

static int kaapi_ws_group_split
(
 kaapi_ws_group_t* groups, size_t group_count,
 kaapi_ws_splitfn_t split_fn, void* split_data
)
{
  /* cannot be called inside an adaptive section.
     2 splitter related concurrency issues:
     . a coarse one: the request data would be overwrite
     . a finer one: a group member request may have been
     replied but has not yet seen. avoiding this, would
     be done by try a compare_and_swap race winning scheme.
  */

  kaapi_bitmap_t reqs;
  size_t req_count;
  size_t i;

  kaapi_bitmap_zero(&reqs);

  /* lock all the groups */
  for (i = 0; i < group_count; ++i)
  {
    kaapi_ws_group_t* const group = &groups[i];
    kaapi_lock_acquire(&group->lock);
    kaapi_bitmap_or(&reqs, &group->members);
  }

  /* exclude my request, call the splitter */
  kaapi_bitmap_clear(&reqs, kaapi_proc_get_self()->id_word);
  req_count = kaapi_bitmap_count(&reqs);
  split_fn(&reqs, req_count, split_data);

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

static int kaapi_ws_spawn_groups
(kaapi_ws_group_t* groups, size_t group_count)
{
  pthread_barrier_t barrier;
  pthread_attr_t attr;
  pthread_t thread;
  kaapi_bitmap_t all_members;
  int error = -1;
  size_t i;
  size_t all_count;
  size_t saved_count;
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

  saved_count = kaapi_bitmap_count(&all_members);
  if (saved_count == 0) goto on_error;

  all_count = saved_count;

  pthread_barrier_init(&barrier, NULL, all_count);

  /* create one thread per member */
  for (i = 0; all_count; --all_count, ++i)
  {
    i = kaapi_bitmap_scan(&all_members, i);

    CPU_ZERO(&cpuset);
    CPU_SET(kaapi_xxx_map_procid(i), &cpuset);

    /* special case for the main thread */
    if (i == 0)
    {
      kaapi_proc_t* const self_proc = kaapi_proc_alloc(0);
      if (self_proc == NULL) goto on_error;

      pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
      sched_yield();
      kaapi_all_procs[0] = self_proc;
      kaapi_proc_init(self_proc, 0);
      pthread_setspecific(kaapi_proc_key, self_proc);

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
  kaapi_mem_read_barrier();

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
  proc->ws_split_fn = fn;
  kaapi_mem_write_barrier();

  kaapi_refn_set(&proc->ws_split_refn);
}

static void kaapi_ws_leave_adaptive
(kaapi_proc_t* proc)
{
  /* avoid starvation */
  proc->ws_split_fn = NULL;
  kaapi_refn_put(&proc->ws_split_refn);
  kaapi_refn_wait(&proc->ws_split_refn);
}


/* workstealing groups construction
 */

static kaapi_ws_group_t* kaapi_single_groups = NULL;
static size_t kaapi_single_group_count;

static kaapi_ws_group_t* kaapi_flat_groups = NULL;
static size_t kaapi_flat_group_count;

static kaapi_ws_group_t* kaapi_numa_groups = NULL;
static size_t kaapi_numa_group_count;

static kaapi_ws_group_t* kaapi_amun_groups = NULL;
static size_t kaapi_amun_group_count;

static int create_single_groups(void)
{
  /* create cpu_count singletons */

  const size_t cpu_count = kaapi_conf_get_cpucount();
  kaapi_ws_group_t* const groups =
    malloc(cpu_count * sizeof(kaapi_ws_group_t));
  size_t i;

  if (groups == NULL) return -1;

  for (i = 0; i < cpu_count; ++i)
  {
    kaapi_ws_group_t* const group = &groups[i];

    kaapi_ws_group_init(group);
    group->member_count = 1;
    group->flags |= KAAPI_WS_GROUP_CONTIGUOUS;
    group->first_procid = i;

    kaapi_bitmap_set(&group->members, i);
  }

  kaapi_single_groups = groups;
  kaapi_single_group_count = cpu_count;

  return 0;
}

static int create_flat_groups(void)
{
  /* create a [0, cpu_count[ flat group */

  const size_t cpu_count = kaapi_conf_get_cpucount();
  kaapi_ws_group_t* const groups = malloc(sizeof(kaapi_ws_group_t));
  kaapi_ws_group_t* group;
  size_t i;

  if (groups == NULL) return -1;

  group = &groups[0];
  kaapi_ws_group_init(group);
  group->member_count = cpu_count;
  group->flags |= KAAPI_WS_GROUP_CONTIGUOUS;
  group->first_procid = 0;

  for (i = 0; i < cpu_count; ++i)
    kaapi_bitmap_set(&group->members, i);

  kaapi_flat_groups = groups;
  kaapi_flat_group_count = 1;

  return 0;
}

static int create_numa_groups(void)
{
  static const unsigned int mem_level = KAAPI_MEMTOPO_LEVEL_NUMA;

  kaapi_ws_group_t* groups;
  size_t group_count;
  size_t i;

  group_count = kaapi_memtopo_count_groups(mem_level);

  groups = malloc(group_count * sizeof(kaapi_ws_group_t));
  if (groups == NULL) return -1;

  for (i = 0; i < group_count; ++i)
  {
    kaapi_ws_group_t* const group = &groups[i];
    kaapi_ws_group_init(group);
    kaapi_memtopo_get_ws_group_members(group, mem_level, i);
  }

  /* todo_clean: build the acutal physical cpu filter and apply */
  {
    const size_t cpu_count = kaapi_conf_get_cpucount();
    kaapi_bitmap_t all_cpus;

    kaapi_bitmap_zero(&all_cpus);
    for (i = 0; i < cpu_count; ++i)
      kaapi_bitmap_set(&all_cpus, i);

    for (i = 0; i < group_count; ++i)
    {
      kaapi_ws_group_t* const group = &groups[i];
      kaapi_bitmap_and(&group->members, &all_cpus);
      group->member_count = kaapi_bitmap_count(&group->members);
    }

  } /* todo_clean */

  kaapi_numa_groups = groups;
  kaapi_numa_group_count = group_count;

  return 0;
}

static int create_amun_groups(void)
{
  /* for testing purposes only
     can use less than cpu_count
   */

  static const unsigned int mem_level = KAAPI_MEMTOPO_LEVEL_NUMA;

  const size_t cpu_count = kaapi_conf_get_cpucount();

  kaapi_ws_group_t* groups;
  size_t group_count;
  size_t i;

  group_count = kaapi_memtopo_count_groups(mem_level);
  groups = malloc(group_count * sizeof(kaapi_ws_group_t));
  if (groups == NULL) return -1;

  for (i = 0; i < group_count; ++i)
  {
    kaapi_ws_group_t* const group = &groups[i];
    kaapi_ws_group_t tmp_group;
    kaapi_procid_t first_procid;

    kaapi_ws_group_init(&tmp_group);
    kaapi_memtopo_get_ws_group_members(&tmp_group, mem_level, i);
    if (tmp_group.member_count == 0) continue ;
    first_procid = kaapi_bitmap_scan(&tmp_group.members, 0);

    /* make the group */
    kaapi_ws_group_init(group);

    /* currently, [0, cpu_count[ cpus available */
    if (first_procid >= cpu_count) continue ;

    kaapi_bitmap_zero(&group->members);
    group->flags |= KAAPI_WS_GROUP_CONTIGUOUS;
    group->first_procid = first_procid;
    kaapi_bitmap_set(&group->members, first_procid);
    group->member_count = 1;
  }

  kaapi_amun_groups = groups;
  kaapi_amun_group_count = group_count;

  return 0;
}

static int kaapi_create_all_ws_groups(void)
{
  create_single_groups();
  create_flat_groups();
  create_numa_groups();
  create_amun_groups();

  return 0;
}

static void kaapi_destroy_all_ws_groups(void)
{
  if (kaapi_single_groups != NULL)
  {
    free(kaapi_single_groups);
    kaapi_single_groups = NULL;
  }

  if (kaapi_flat_groups != NULL)
  {
    free(kaapi_flat_groups);
    kaapi_flat_groups = NULL;
  }

  if (kaapi_numa_groups != NULL)
  {
    free(kaapi_numa_groups);
    kaapi_numa_groups = NULL;
  }

  if (kaapi_amun_groups != NULL)
  {
    free(kaapi_amun_groups);
    kaapi_amun_groups = NULL;
  }
}


/* kaapi constructor, destructor
 */

static int kaapi_initialize(void)
{
  srand(getpid() * time(0));

  if (pthread_key_create(&kaapi_proc_key, NULL)) return -1;

  memset((void*)kaapi_all_procs, 0, sizeof(kaapi_all_procs));

  if (kaapi_create_all_ws_groups() == -1)
    return -1;

  return 0;
}

static int signal_proc_term(kaapi_proc_t*  proc, void* fubar)
{
  if (proc == fubar) return 0;

  kaapi_atomic_write(&proc->control_word, KAAPI_PROC_CONTROL_TERM);
  return 0;
}

static int sync_proc_term(kaapi_proc_t* proc, void* fubar)
{
  if (proc == fubar) return 0;

  while (kaapi_atomic_read(&proc->status_word) != KAAPI_PROC_STATUS_TERM)
    kaapi_cpu_slowdown();

  return 0;
}

static int free_proc_term(kaapi_proc_t* proc, void* fubar)
{
  kaapi_all_procs[proc->id_word] = NULL;
  kaapi_proc_free(proc);
  return 0;
}

static void kaapi_finalize(void)
{
  kaapi_proc_t* const self_proc = kaapi_proc_get_self();

  /* signal termination, sync, free */
  kaapi_proc_foreach(signal_proc_term, self_proc);
  kaapi_proc_foreach(sync_proc_term, self_proc);
  kaapi_proc_foreach(free_proc_term, NULL);

  pthread_key_delete(kaapi_proc_key);

  kaapi_destroy_all_ws_groups();
}


/* foreach algorithm, application dependant.
 */

#define CONFIG_USE_WS_FLAT_GROUPS 0
#define CONFIG_USE_WS_SINGLE_GROUPS 0
#define CONFIG_USE_WS_NUMA_GROUPS 1
#define CONFIG_USE_WS_AMUN_GROUPS 0


typedef struct term_hack
{
  /* hack to detect the algorithm end */
  kaapi_atomic_t counter;
  size_t size;
} term_hack_t;

typedef struct foreach_work
{
  kaapi_lock_t lock;

  /* termination_hack */
  term_hack_t* term_hack;

  double* array;
  volatile size_t i;
  volatile size_t j;

} foreach_work_t;

static inline void init_foreach_work
(foreach_work_t* work, double* array, size_t size)
{
  kaapi_lock_init(&work->lock);
}

static inline void set_foreach_work
(foreach_work_t* work, double* array, size_t size)
{
  work->array = array;
  work->i = 0;
  work->j = size;
}

static void foreach_thief(void*);

static void splitter
(const kaapi_bitmap_t* req_map, size_t req_count, void* arg)
{
  /* one splitter for both initial, grouped
     and task emitted requests. this is left
     to the splitter to distinguish if needed
   */

  foreach_work_t* const vw = arg;

  foreach_work_t* tw;
  kaapi_ws_request_t* req;
  size_t unit_size;
  size_t work_size;
  size_t stolen_j;
  size_t i;

  if (req_count == 0) goto on_done;

  kaapi_lock_acquire(&vw->lock);

#define CONFIG_PAR_GRAIN 64

  work_size = vw->j - vw->i;

  unit_size = work_size / (req_count + 1);
  if (unit_size < CONFIG_PAR_GRAIN)
  {
    req_count = work_size / CONFIG_PAR_GRAIN;
    if (req_count <= 1)
    {
      /* split failure */
      kaapi_lock_release(&vw->lock);
      return ;
    }

    /* let a unit for the seq */
    req_count -= 1;

    unit_size = CONFIG_PAR_GRAIN;
  }

  stolen_j = vw->j;
  vw->j -= req_count * unit_size;

  kaapi_lock_release(&vw->lock);

  for (i = 0; req_count; stolen_j -= unit_size, ++i, --req_count)
  {
    /* next_request */
    i = kaapi_bitmap_scan(req_map, i);

    req = kaapi_proc_get_ws_request((kaapi_procid_t)i);

    tw = kaapi_ws_request_alloc_data(req, sizeof(foreach_work_t));
    tw->term_hack = vw->term_hack;
    kaapi_lock_init(&tw->lock);
    tw->array = vw->array;
    tw->i = stolen_j - unit_size;
    tw->j = stolen_j;

    kaapi_ws_request_reply(req, foreach_thief);
  }

 on_done:
  return ;
}

static int extract_seq
(foreach_work_t* w, size_t size, double** beg, double** end)
{
  size_t work_size;

  int error = -1;

  kaapi_lock_acquire(&w->lock);

  work_size = w->j - w->i;
  if (work_size)
  {
    if (size > work_size) size = work_size;

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
  static const size_t seq_size = 256;

  /* termination_hack */
  size_t term_size = 0;

  double* beg;
  double* end;

  while (extract_seq(w, seq_size, &beg, &end) != -1)
  {
    /* termination_hack */
    term_size += end - beg;

    for (; beg != end; ++beg) ++*beg;
  }

  /* termination_hack */
#if 0
  printf("[%lu] term_hac += %lu / %lu\n",
	 kaapi_proc_get_self()->id_word, term_size, w->term_hack->size);
#endif
  kaapi_atomic_add(&w->term_hack->counter, term_size);
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

  /* termination_hack */
  while (kaapi_atomic_read(&work->term_hack->counter) != work->term_hack->size)
    kaapi_cpu_slowdown();
  kaapi_atomic_write(&work->term_hack->counter, 0);
}


static int for_foreach
(double* array, size_t size, size_t iter_count)
{
  int error = -1;

  /* termination_hack */
  term_hack_t term_hack;

  /* work descriptor */
  foreach_work_t work;

  /* create the socket groups */
  const size_t total_size = size * sizeof(double);
  const unsigned int mem_level = KAAPI_MEMTOPO_LEVEL_SOCKET;

  kaapi_ws_group_t* groups = NULL;
  size_t group_count;
  size_t i;

  /* bind memory uniformly amongst memory levels */
  kaapi_memtopo_bind_uniform(array, total_size, mem_level);

  /* select the groups to use */
#if CONFIG_USE_WS_FLAT_GROUPS
  groups = kaapi_flat_groups;
  group_count = kaapi_flat_group_count;
#elif CONFIG_USE_WS_SINGLE_GROUPS
  groups = kaapi_single_groups;
  group_count = kaapi_single_group_count;
#elif CONFIG_USE_WS_NUMA_GROUPS
  groups = kaapi_numa_groups;
  group_count = kaapi_numa_group_count;
#elif CONFIG_USE_WS_AMUN_GROUPS
  groups = kaapi_amun_groups;
  group_count = kaapi_amun_group_count;
#else
# error "CONFIG_USE_WS_XXX_GROUPS undefined"
#endif

  /* spawn the associated threads */
  if (kaapi_ws_spawn_groups(groups, group_count))
    goto on_error;

  /* make them steal */
  for (i = 0; i < group_count; ++i)
  {
    /* todo: kaapi_ws_group_bind(&groups[i]); */
    kaapi_ws_group_start(&groups[i]);
  }

  /* initialize the work */
  init_foreach_work(&work, array, size);

  /* termination_hack */
  kaapi_atomic_write(&term_hack.counter, 0);
  term_hack.size = size;
  work.term_hack = &term_hack;

  /* run algorithm */
  for (i = 0; i < iter_count; ++i)
  {
    struct timeval beg, end, diff;

    gettimeofday(&beg, NULL);

    set_foreach_work(&work, array, size);

    if (i == 0)
    {
      /* initial split amongst the groups */
      kaapi_ws_group_split(groups, group_count, splitter, &work);
    }

    foreach_master(&work);

    gettimeofday(&end, NULL);
    timersub(&end, &beg, &diff);
    printf("%lf ms.\n", ((double)diff.tv_sec * 1E6 + diff.tv_usec) / 1000.);
  }

  /* success */
  error = 0;

 on_error:
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

static int check_array(const double* addr, size_t count)
{
#define CONFIG_ITER_COUNT 10

  const size_t saved_count = count;

  for (; count; ++addr, --count)
  {
    if (*addr != CONFIG_ITER_COUNT)
    {
      printf("INVALID_ARRAY @%lu == %lf\n", saved_count - count, *addr);
      return -1;
    }
  }

  return 0;
}


/* main
 */

int main(int ac, char** av)
{
#define CONFIG_ARRAY_COUNT (10 * 1024 * 1024)
  double* array = NULL;
  size_t count = CONFIG_ARRAY_COUNT;

  kaapi_initialize();

  allocate_array(&array, count * sizeof(double));

  /* so that pages are bound on the current core */
  fill_array(array, count * sizeof(double));

  for_foreach(array, count, CONFIG_ITER_COUNT);

  if (check_array(array, count) == -1)
    printf("INVALID_ARRAY\n");

  free_array(array, count * sizeof(double));

  kaapi_finalize();

  return 0;
}
