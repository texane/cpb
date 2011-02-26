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

typedef void (*kaapi_ws_splitfn_t)(const kaapi_bitmap_t*, size_t, void*);

static int kaapi_ws_steal_work(kaapi_ws_work_t*);

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
#if 0
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

  kaapi_proc_args_t args[KAAPI_CONFIG_MAX_PROC];

  pthread_attr_init(&attr);

  /* make a bitmap that is the union of every members */
  kaapi_bitmap_zero(&all_members);
  for (i = 0; i < group_count; ++i)
  {
    kaapi_ws_group_t* const group = &groups[i];

    /* add group members to the member set */
    kaapi_bitmap_or(&all_members, &group->members);
  }

  kaapi_mt_spawn_threads(&all_members);
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
