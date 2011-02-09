/* hierarchical workstealing implementation */

#include "kaapi_lock.h"


/* hierarchical structures
 */

typedef struct kaapi_ws_topo
{
  
} kaapi_ws_topo_t;

typedef struct kaapi_ws_group
{
  /* workstealing group */
  kaapi_lock_t lock;
  struct kaapi_ws_group* top;
  struct kaapi_ws_group* down;
  size_t member_count;
  kaapi_proc_t* members[1];
} kaapi_ws_group_t;


/* workstealing context
 */

typedef struct kaapi_ws_context
{
  kaapi_ws_splitter_t splitter;

} kaapi_ws_context_t;


/* workstealing entry routine
 */

static void kaapi_ws_schedule(kaapi_ws_context_t* kwc)
{
  /* todo: should the splitter be in the group instead of
     the thread? in that case specialise workstealing routines.
   */

  /* todo: find a way to split amongst all the group members
     and submembers. either this is done by walking the
     hierarchy down or ...
   */

  kaapi_topo_t* topo;

  kaapi_ws_group_t* group = kaapi_ws_get_bottom_group(kwc);

  /* emit the stealing request */
  unsigned int vid = select_victim(group);
  kpb->ws_request_word = KAAPI_WS_REQUEST_POST | vid;

  while (1)
  {
  redo_split:
    if (kaapi_lock_trylock(&group->lock) == 0)
    {
      /* success, split amongst the group members */

      if (kw->splitter(group->members) == KAAPI_ERROR_WS_EMPTY)
      {
	/* topmost group, failure */
	if (group->top == NULL) break ;

	/* no more work, try in the next level */
	group = group->top;
	goto redo_split;
      }

      /* success, next level */

      group = ;
    }
    else
    {
      /* check if no one replied the request */
    }
  }

  kaapi_topo_get_group();
}
