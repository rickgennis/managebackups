#ifndef DEBUG_H
#define DEBUG_H

/*
 Borrowed from Philip Hazel's Exim Mail Transport Agent
 */


#include <sys/types.h>

typedef unsigned char uschar;
#define US   (unsigned char *)
#define CUSS (const unsigned char **)
#define CS   (char *)
#define CCS   (const char *)
#define Uskip_whitespace(sp) skip_whitespace(CUSS sp)
#define Ustrncmp(s,t,n)    strncmp(CCS(s),CCS(t),n)
#define nelem(arr) (sizeof(arr) / sizeof(*arr))

/* Assume words are 32 bits wide. Tiny waste of space on 64 bit
platforms, but this ensures bit vectors always work the same way. */
#define BITWORDSIZE 32

/* This macro is for single-word bit vectors: the debug selector,
and the first word of the log selector. */
#define BIT(n) (1UL << (n))

/* And these are for multi-word vectors. */
#define BITWORD(n) (      (n) / BITWORDSIZE)
#define BITMASK(n) (1U << (n) % BITWORDSIZE)

#define BIT_CLEAR(s,z,n) ((s)[BITWORD(n)] &= ~BITMASK(n))
#define BIT_SET(s,z,n)   ((s)[BITWORD(n)] |=  BITMASK(n))
#define BIT_TEST(s,z,n) (((s)[BITWORD(n)] &   BITMASK(n)) != 0)

#define BIT_TABLE(T,name) { US #name, T##i_##name }

/* IOTA allows us to keep an implicit sequential count, like a simple enum,
but we can have sequentially numbered identifiers which are not declared
sequentially. We use this for more compact declarations of bit indexes and
masks, alternating between sequential bit index and corresponding mask. */

#define IOTA(iota)      (__LINE__ - iota)
#define IOTA_INIT(zero) (__LINE__ - zero + 1)

/* Options bits for debugging. DEBUG_BIT() declares both a bit index and the
corresponding mask. Di_all is a special value recognized by decode_bits().
These must match the debug_options table in globals.c .

Exim's code assumes in a number of places that the debug_selector is one
word, and this is exposed in the local_scan ABI. The D_v and D_local_scan bit
masks are part of the local_scan API so are #defined in local_scan.h */

#define DEBUG_BIT(name) Di_##name = IOTA(Di_iota), D_##name = (int)BIT(Di_##name)

enum {
  Di_all        = -1,
  Di_v          = 0,

  Di_iota = IOTA_INIT(1),
  DEBUG_BIT(backup),               /* 2 */
  DEBUG_BIT(cache),
  DEBUG_BIT(config),
  DEBUG_BIT(exec),
  DEBUG_BIT(faub),
  DEBUG_BIT(link),
  DEBUG_BIT(netproto),
  DEBUG_BIT(notify),
  DEBUG_BIT(prune),
  DEBUG_BIT(scan),
  DEBUG_BIT(transfer),
  DEBUG_BIT(tripwire),
};

#define D_all                        0xffffffff

#define D_any                        (D_all)
                                     //  & \
                                     //  ~(D_dns))

#define D_default                    (D_all & \
                                       ~(D_transfer         | \
                                         D_config           | \
                                         D_scan             | \
                                         D_cache            | \
                                         D_exec             | \
                                         D_faub             | \
                                         D_netproto         | \
                                         D_notify           | \
                                         D_tripwire))


#define DEBUG(x)      if (GLOBALS.debugSelector & (x))

typedef struct bit_table {
  uschar *name;
  int bit;
} bit_table;


extern bit_table debug_options[];
extern int debug_notall[];
extern int ndebug_options;


void decode_bits(unsigned int *selector, size_t selsize, int *notall,
  uschar *parsestring, bit_table *options, int count);


#endif

