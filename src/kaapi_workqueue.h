#ifndef KAAPI_WORKQUEUE_H_INCLUDED
# define KAAPI_WORKQUEUE_H_INCLUDED

#include <pthread.h>
#include <sys/errno.h>

typedef long kaapi_workqueue_index_t;

typedef struct {
  volatile kaapi_workqueue_index_t beg __attribute__((aligned(64)));
  volatile kaapi_workqueue_index_t end __attribute__((aligned(64)));
  pthread_mutex_t lock;
} kaapi_workqueue_t;


/** Initialize the workqueue to be an empty (null) range workqueue.
*/
static inline int kaapi_workqueue_init( kaapi_workqueue_t* kwq, kaapi_workqueue_index_t b, kaapi_workqueue_index_t e )
{
  kwq->beg = b;
  kwq->end = e;

  pthread_mutex_init(&kwq->lock, NULL);

  return 0;
}

/** destroy 
*/
static inline int kaapi_workqueue_destroy( kaapi_workqueue_t* kwq )
{
  pthread_mutex_destroy(&kwq->lock);
  return 0;
}

/** This function set new bounds for the workqueue.
    The only garantee is that is a concurrent thread tries
    to access to the size of the queue while a thread set the workqueue, 
    then the concurrent thread will see the size of the queue before the call to set
    or it will see a nul size queue.
    \retval 0 in case of success
    \retval ESRCH if the current thread is not a kaapi thread.
*/
extern int kaapi_workqueue_set( kaapi_workqueue_t* kwq, kaapi_workqueue_index_t b, kaapi_workqueue_index_t e);

/**
*/
static inline kaapi_workqueue_index_t kaapi_workqueue_range_begin( kaapi_workqueue_t* kwq )
{
  return kwq->beg;
}

/**
*/
static inline kaapi_workqueue_index_t kaapi_workqueue_range_end( kaapi_workqueue_t* kwq )
{
  return kwq->end;
}

/**
*/
static inline kaapi_workqueue_index_t kaapi_workqueue_size( kaapi_workqueue_t* kwq )
{
  kaapi_workqueue_index_t size = kwq->end - kwq->beg;
  return (size <0 ? 0 : size);
}

/**
*/
static inline unsigned int kaapi_workqueue_isempty( kaapi_workqueue_t* kwq )
{
  kaapi_workqueue_index_t size = kwq->end - kwq->beg;
  return size <= 0;
}

static inline void kaapi_mem_barrier(void)
{
  __asm__ __volatile__ ("":::"memory");
  /* __sync_synchronize(); */
}


/** This function should be called by the current kaapi thread that own the workqueue.
    The function push work into the workqueue.
    Assuming that before the call, the workqueue is [beg,end).
    After the successful call to the function the workqueu becomes [newbeg,end).
    newbeg is assumed to be less than beg. Else it is a pop operation, see kaapi_workqueue_pop.
    Return 0 in case of success 
    Return EINVAL if invalid arguments
*/
static inline int kaapi_workqueue_push(
  kaapi_workqueue_t* kwq, 
  kaapi_workqueue_index_t newbeg
)
{
  if ( kwq->beg  > newbeg )
  {
    kaapi_mem_barrier();
    kwq->beg = newbeg;
    return 0;
  }
  return EINVAL;
}


/** Helper function called in case of conflict.
    Return EBUSY is the queue is empty.
    Return EINVAL if invalid arguments
    Return ESRCH if the current thread is not a kaapi thread.
*/
extern int kaapi_workqueue_slowpop(
  kaapi_workqueue_t* kwq, 
  kaapi_workqueue_index_t* beg,
  kaapi_workqueue_index_t* end,
  kaapi_workqueue_index_t size
);


/** This function should be called by the current kaapi thread that own the workqueue.
    Return 0 in case of success 
    Return EBUSY is the queue is empty.
    Return EINVAL if invalid arguments
    Return ESRCH if the current thread is not a kaapi thread.
*/
static inline int kaapi_workqueue_pop(
  kaapi_workqueue_t* kwq, 
  kaapi_workqueue_index_t* beg,
  kaapi_workqueue_index_t* end,
  kaapi_workqueue_index_t max_size
)
{
  kaapi_workqueue_index_t loc_beg;
  loc_beg = kwq->beg + max_size;
  kwq->beg = loc_beg;
  kaapi_mem_barrier();

  if (loc_beg < kwq->end)
  {
    /* no conflict */
    *end = loc_beg;
    *beg = *end - max_size;
    return 0;
  }

  /* conflict */
  loc_beg -= max_size;
  kwq->beg = loc_beg;
  return kaapi_workqueue_slowpop(kwq, beg, end, max_size);
}


/** This function should only be called into a splitter to ensure correctness
    the lock of the victim kprocessor is assumed to be locked to handle conflict.
    Return 0 in case of success 
    Return ERANGE if the queue is empty or less than requested size.
 */
static inline int kaapi_workqueue_steal(
  kaapi_workqueue_t* kwq, 
  kaapi_workqueue_index_t* beg,
  kaapi_workqueue_index_t* end,
  kaapi_workqueue_index_t size
)
{
  kaapi_workqueue_index_t loc_end;

  /* disable gcc warning */
  *beg = 0;
  *end = 0;

  pthread_mutex_lock(&kwq->lock);

  loc_end = kwq->end - size;
  kwq->end = loc_end;
  kaapi_mem_barrier();

  if (loc_end < kwq->beg)
  {
    kwq->end = loc_end+size;

    pthread_mutex_unlock(&kwq->lock);

    return ERANGE; /* false */
  }

  pthread_mutex_unlock(&kwq->lock);

  *beg = loc_end;
  *end = *beg + size;
  
  return 0; /* true */
}  

#endif /* ! KAAPI_WORKQUEUE_H_INCLUDED */
