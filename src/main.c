#include <stdint.h>
#include <cuda.h>


/* thread control words */

typedef struct kaapi_proc_word
{
  volatile uintptr_t value;
} kaapi_proc_word_t;

static inline uintptr_t kaapi_proc_word_read
(const kaapi_proc_word_t* kw)
{
  return kw->value;
}

static inline void kaapi_proc_word_write
(kaapi_proc_word_t* kw, uintptr_t value)
{
  kw->value = value;
}

typedef struct kaapi_proc_block
{
  /* general words */

  kaapi_proc_word_t id_word; /* constant */

#define KAAPI_CONTROL_WORD_DONE 0x00
#define KAAPI_CONTROL_WORD_EXEC 0x01
#define KAAPI_CONTROL_WORD_STEAL 0x02
#define KAAPI_CONTROL_WORD_NOP 0x90
  kaapi_proc_word_t control_word;

#define KAAPI_STATUS_WORD_SUCCESS 0x00
#define KAAPI_STATUS_WORD_FAILURE 0x01
  kaapi_proc_word_t status_word;

  kaapi_proc_word_t config_word;

  /* workstealing registers */

  kaapi_proc_word_t ws_request_word;
  kaapi_proc_word_t ws_data_word;
  kaapi_proc_word_t ws_reply_word;

  /* static scheduling registers */

  kaapi_proc_word_t static_data_word;

  /* execution specialisation */

  void (*exec_task)(void*);

  /* architecture dependant */

} kaapi_proc_block_t;


/* cpu thread entry */

typedef struct kaapi_cputhread_data
{
  kaapi_thread_block_t kcb;

} kaapi_cputhread_data_t;

static void cpu_thread_entry(void* p)
{
  
}

static void main_thread_entry(void)
{
}


/* cuda task execution routine */

static void kaapi_cuda_exec_gpu_thread_entry(void* p)
{
}


/* thread entry */

static void* thread_entry(void* p)
{
  kaapi_processor_block_t* const kpb = p;

  while (1)
  {
    switch (kpb->control_word)
    {
    case KAAPI_CONTROL_WORD_NOP:
      break ;

    case KAAPI_CONTROL_WORD_EXEC:
      break ;

    case KAAPI_CONTROL_WORD_STEAL:
      switch (kpb->config_word)
      {
      case KAAPI_CONFIG_WORD_WS:
	kaapi_ws_main();
	break ;

      case KAAPI_CONFIG_WORD_STATIC:
	kaapi_static_main();
	break ;
      }

      break ;

    case KAAPI_CONTROL_WORD_DONE:
      goto on_done;
      break ;

    default:
      break ;
    }
  }

 on_done:
  ktb->status_word = 0;

  return NULL;
}

static void main_thread_entry(void)
{
}


/* kaapi thread pool */

typedef struct kaapi_threadgroup
{
} kaapi_threadgroup_t;

static int kaapi_threadpool_create(kaapi_threadpool_t* ktp)
{
}

static void kaapi_threadpool_destroy(kaapi_threadpool_t* ktp)
{
}

static void kaapi_threadpool_create_thread
(kaapi_threadpool_t* ktp, kaapi_thread_t** kthread)
{
}


/* kaapi configuration accessors */

static unsigned int kaapi_config_get_gpu_count(kaapi_context_t* kc)
{
}

static unsigned int kaapi_config_get_cpu_count(kaapi_context_t* kc)
{
}


/* cache routines */


/* numa routines */


/* hierarchical routines */

typedef struct kaapi_thread_group
{
  kaapi_lock ;

} kaapi_thread_group_t;

static int kaapi_thread_group_create(kaapi_thread_group_t** ktg)
{
}

static void kaapi_thread_group_destroy(kaapi_thread_group_t* ktg)
{
}

/* high level group creation.
   group creation can be seen as a way of describing the machine
   topology. the aim is to allow composability, ie. the ability
   to create an arbitrary topology.
   once described, the topo can be instanciated
 */

typedef struct kaapi_topo_level
{
  struct top
} kaapi_topo_level_t;

typedef struct kaapi_topo
{
} kaapi_topo_t;

static void kaapi_topo_create_flat(kaapi_topo_t*)
{
  kaapi_sys_foreach_cpu(create);
}

static void kaapi_topo_create_numa(kaapi_topo_t*)
{
}

/* main */

int main(int ac, char** av)
{
  kaapi_context_t kc;
  size_t i;
  kaapi_threadpool_t ktp;

  if (kaapi_initialize(&kc, ac, av) == -1)
    return -1;

  for (i = 0; i < 10; ++i) reduce(array, size);


  if (kaapi_threadpool_create(&ktp) == -1)
    goto on_error;

  kaapi_threadpool_;

 on_error:
  kaapi_threadpool_destroy(&ktp);
  kaapi_finalize(&kc);

  return 0;
}
