/* Included first in every runtime source and the generated program
   (-include): the sizes and switches Spinel takes from the build. */
#ifndef SP_ESP_CONFIG_H
#define SP_ESP_CONFIG_H

/* no SIGSEGV handler for fiber stack overflow: there are no guard pages */
#define SP_NO_SIGNALS 1

/* The runtime's static tables, sized for a 64-bit host (about 1.6 MB of
   .bss there), shrunk to fit the ESP32-S3's internal DRAM. Each has a
   fallback past its bound: the root stack grows an overflow segment on the
   heap, a full remembered or pinned set falls back to a full collection,
   and the allocation statistics and string-shape samples are diagnostics
   that drop what does not fit. */
#define SP_GC_STACK_MAX      2048   /* GC root stack:   8 KB (64 K entries) */
#define SP_GC_REMEMBERED_MAX 1024   /* remembered set:  4 KB (64 K entries) */
#define SP_GC_PINNED_MAX     512    /* pinned objects:  2 KB (16 K entries) */
#define SP_ALLOC_STATS       16     /* allocation profiler slots (8192) */
#define SP_STR_SHAPE_MAX     64     /* string-shape samples (8192) */
#define SP_DYN_SYMS_MAX      256    /* symbols interned at run time (8192) */

/* stands in for the NULL of an empty inspector table in the generated C
   (main/CMakeLists.txt) */
#include <stdint.h>
const char *sp_esp_insp_none(void *obj, const uint8_t *nb);

#endif
