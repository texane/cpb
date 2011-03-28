#include <pthread.h>
#include <sys/errno.h>
#include "kaapi_workqueue.h"

int kaapi_workqueue_set
( kaapi_workqueue_t* kwq, kaapi_workqueue_index_t beg, kaapi_workqueue_index_t end)
{
  pthread_mutex_lock(&kwq->lock);
  kwq->beg = beg;
  kwq->end = end;
  pthread_mutex_unlock(&kwq->lock);

  return 0;
}

/** */
int kaapi_workqueue_slowpop(
  kaapi_workqueue_t* kwq, 
  kaapi_workqueue_index_t* beg,
  kaapi_workqueue_index_t* end,
  kaapi_workqueue_index_t max_size
)
{
  kaapi_workqueue_index_t size;
  kaapi_workqueue_index_t loc_beg;

  pthread_mutex_lock(&kwq->lock);

  loc_beg = kwq->beg;
  size = kwq->end - loc_beg;
  if (size ==0) 
    goto empty_case;
  if (size > max_size)
    size = max_size;
  loc_beg += size;
  kwq->beg = loc_beg;

  pthread_mutex_unlock(&kwq->lock);

  *end = loc_beg;
  *beg = *end - size;
  return 0;

empty_case:
  pthread_mutex_unlock(&kwq->lock);
  return EBUSY;
}
