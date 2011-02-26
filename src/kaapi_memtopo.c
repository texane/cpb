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
	for (cpu_pos = 0; cpu_pos < KAAPI_CONFIG_MAX_PROC; ++cpu_pos)
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

