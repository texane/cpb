#ifndef KAAPI_BITMAP_H_INCLUDED
# define KAAPI_BITMAP_H_INCLUDED

/* bitmaps (128 bits max on 64 bits arch)
 */

#include <sys/types.h>

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

static inline unsigned int kaapi_bitmap_is_set
(const kaapi_bitmap_t* bitmap, size_t i)
{
  const size_t j = i / (8 * sizeof(unsigned long));
  return bitmap->bits[j] & (1UL << (i % (8 * sizeof(unsigned long))));
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

  const unsigned long mask =
    ~((1UL << (i % (8 * sizeof(unsigned long)))) - 1UL);

  /* mask the lower bits and scan */
  return (j * 8 * sizeof(unsigned long)) +
    __builtin_ffsl(bitmap->bits[j] & mask) - 1;
}

static inline void kaapi_bitmap_or
(kaapi_bitmap_t* a, const kaapi_bitmap_t* b)
{
  a->bits[0] |= b->bits[0];
  a->bits[1] |= b->bits[1];
}

static inline size_t kaapi_bitmap_pos
(const kaapi_bitmap_t* bitmap, size_t i)
{
  /* return the position of ith bit set
     assume there is a ith bit set
   */

  size_t j, pos;

  for (j = 0, pos = 0; j <= i; ++j, ++pos)
    pos = kaapi_bitmap_scan(bitmap, pos);

  return pos;
}

#include <stdio.h>
static inline void kaapi_bitmap_print
(const kaapi_bitmap_t* bitmap)
{
  printf("%08lx%08lx\n", bitmap->bits[1], bitmap->bits[0]);
}


#endif /* ! KAAPI_BITMAP_H_INCLUDED */
