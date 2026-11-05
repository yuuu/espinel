/* Spinel's runtime header, with what ESP-IDF needs changed after it.

   With the slab allocator off, every class keeps the objects the sweep
   frees on a free list of its own, up to SP_POOL_DEFAULT_MAX (a million)
   each, which spinel_rt.h defines without a guard. On a heap this small
   that keeps dead objects shaped for their class forever: memory that
   GC.start freed never came back. A short list keeps the reuse and gives
   the rest back to the heap. */
#ifndef SP_ESP_SPINEL_RT_H
#define SP_ESP_SPINEL_RT_H
#include_next "spinel_rt.h"
#undef SP_POOL_DEFAULT_MAX
#define SP_POOL_DEFAULT_MAX 64L
#endif
