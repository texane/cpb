#ifndef KAAPI_MT_H_INCLUDED
# define KAAPI_MT_H_INCLUDED


#include "kaapi_proc.h"


struct kaapi_bitmap;

int kaapi_mt_create_threads(const struct kaapi_bitmap*);
int kaapi_mt_create_self(kaapi_procid_t);
int kaapi_mt_join_threads(const struct kaapi_bitmap*);
int kaapi_mt_spawn_task(kaapi_procid_t, void (*)(void*), void*);


#endif /* ! KAAPI_MT_H_INCLUDED */
