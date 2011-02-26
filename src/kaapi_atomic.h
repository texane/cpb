#ifndef KAAPI_ATOMIC_H_INCLUDED
# define KAAPI_ATOMIC_H_INCLUDED

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


/* memory ordering
 */

static inline void kaapi_mem_full_barrier(void)
{
  __sync_synchronize();
}

static inline void kaapi_mem_write_barrier(void)
{
  /* todo_optimize */
  kaapi_mem_full_barrier();
}

static inline void kaapi_mem_read_barrier(void)
{
  /* todo_optimize */
  kaapi_mem_full_barrier();
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
  return __sync_bool_compare_and_swap(&l->value, 0, 1);
} 

static inline void kaapi_lock_acquire(kaapi_lock_t* l)
{
  while (kaapi_lock_try_acquire(l) == 0)
    kaapi_cpu_slowdown();
}

__attribute__((unused))
static void kaapi_lock_release(kaapi_lock_t* l)
{
  kaapi_atomic_write(l, 0);
}


#endif /* KAAPI_ATOMIC_H_INCLUDED */
