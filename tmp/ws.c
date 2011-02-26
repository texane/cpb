/* hierarchical workstealing implementation */

#include <sys/types.h>
#include "kaapi_lock.h"


static unsigned int select_victim
(kaapi_ws_context_t* kwc, kaapi_ws_group_t* kwg)
{
  /* select a victim in the group */

  if (kwg->flag & KAAPI_WS_GROUP_CONTIGUOUS)
  {
    /* select a random id in the range */
  }
  else
  {
    /* randomly select a victim */
  }
}


/* to handle the processor control block has a
   ws_group_word. this is similar to
   and ipv4 addr.

   groups are statically build using the routines
   provided by the xkaapi interface.

   refer to the documentation of architectures
   handling dynamic communication in hardware
   (ie. topo mapping, for instance bluegene)
 */

typedef struct kaapi_processor_block
{
  /* at a given time, if workstealing is enabled,
     a processor belongs at most to 4 groups.

     in the case of hierarchical workstealing,
     ws_group_addrs[i] corresponds to the group
     address at memory level i the processor
     belongs to. the meaning of i depends on the
     group routine used, typically cache, numa...
   */

#define KAAPI_WS_GROUP_ADDR_ANY ((uintptr_t)-1)
#define KAAPI_WS_MAX_GROUP_ADDRS 4
  kaapi_word_t ws_group_addrs[KAAPI_WS_MAX_GROUP_ADDRS];

} kaapi_processor_block_t;


typedef struct kaapi_ws_group
{
  /* a workstealing group contains the workstealing
     registers for a given group (request, reply, ids)
     and the synchronization lock.
   */

  kaapi_lock_t lock;

#define KAAPI_WS_GROUP_CONTIGUOUS (1 << 0)
  unsigned int flags;

  size_t member_count;

  /* one bit per member */
  kaapi_bitmap_t member_map;

  /* one per member */
  kaapi_word_t request_words[];
  kaapi_word_t reply_words[];

} kaapi_ws_group_t;


typedef struct kaapi_ws_context_t
{
#if 0  /* todo */
  /* group lookup table... */
#endif
} kaapi_ws_context_t;


static kaapi_ws_group_t* kaapi_ws_get_group_byaddr(uintptr_t addr)
{
  return (kaapi_ws_group_t*)addr;
}


/* workstealing entry routine
 */

int kaapi_ws_emitsteal(kaapi_ws_context_t* kwc)
{
  /* todo: when walking up the hierarchy, the thread
     must or the different members and pass all of them
     to the final splitter without unlockcing 
   */

  /* todo: find a way to split amongst all the group members
     and submembers. either this is done by walking the
     hierarchy down or ...
   */

  kaapi_processor_block_t* const kpb = self_processor();
  unsigned int level = 0;
  int error = -1;

  kaapi_ws_group_t* group;

  uintptr_t group_addr; /* uint64_t */
  unsigned int member_addr; /* uint32_t */
  unsigned int victim_addr; /* uint32_t */

 next_level:

  group_addr = kpb->ws_group_addrs[level];

  /* resolve the group */
  group = kaapi_ws_get_group_byaddr(kwc, group_addr);
  member_addr = group_addr_to_member(group_addr);

  /* emit the stealing request */
  victim_addr = select_victim(group);
#if 0
  kpb->ws_request_word = KAAPI_WS_REQUEST_POST | victim_addr;
#else
  group->request_words[member_addr] = KAAPI_WS_REQUEST_POST | victim_addr;
#endif

 try_lock:
  if (kaapi_lock_trylock(&group->lock) == 0)
  {
    /* success, split amongst the group members */
    if (kwc->splitter(group->members) == KAAPI_ERROR_WS_EMPTY)
    {
      /* topmost level, steal failure */
      if (level == (KAAPI_WS_MAX_GROUP_ADDRS - 1))
	goto on_failure;

      /* next level */
      ++level;
      goto next_level;
    }

    kaapi_lock_unlock(&group->lock);
  } /* try_lock */

  /* test our own request */
  if (group->reply_words[member_addr] == REPLIED)
    goto on_succes;

  /* try to lock again */
  goto try_lock;

 on_succes:
  error = 0;

 on_failure:
  return error;
}
