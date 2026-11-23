/* The heap, the Ruby task's stack and Spinel's GC as numbers, for
   memory.rb. Every function answers one figure in bytes (or a count), so
   the Ruby side binds them with plain ffi_func. */
#include <stddef.h>
#include <stdint.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* main/main.c and main/stack_check.c */
extern const unsigned espinel_ruby_stack_size;
uintptr_t sp_esp_stack_limit(void);

/* Spinel's collector (lib/sp_gc.h) */
extern size_t sp_gc_bytes, sp_gc_threshold;
extern unsigned long long sp_gc_stat_collections, sp_gc_stat_fulls;

/* ---- the heap ---- */

/* region: 0 every byte-addressable heap, 1 internal RAM, 2 PSRAM */
static uint32_t caps_of(int region) {
  switch (region) {
  case 1: return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  case 2: return MALLOC_CAP_SPIRAM;
  default: return MALLOC_CAP_8BIT;
  }
}

/* what: 0 total, 1 free, 2 allocated, 3 largest free block, 4 the least
   free since boot */
size_t espinel_mem_heap(int region, int what) {
  multi_heap_info_t info;
  uint32_t caps = caps_of(region);
  heap_caps_get_info(&info, caps);
  switch (what) {
  case 0: return heap_caps_get_total_size(caps);
  case 1: return info.total_free_bytes;
  case 2: return info.total_allocated_bytes;
  case 3: return info.largest_free_block;
  default: return info.minimum_free_bytes;
  }
}

/* ---- the Ruby task's stack (it grows down from start + size) ---- */

size_t espinel_mem_stack_size(void) { return espinel_ruby_stack_size; }

/* bytes in use at the caller: from the top of the stack to this frame */
size_t espinel_mem_stack_used(void) {
  uintptr_t top = (uintptr_t)pxTaskGetStackStart(NULL) + espinel_ruby_stack_size;
  return top - (uintptr_t)__builtin_frame_address(0);
}

/* the most ever in use, from FreeRTOS's high-water mark (bytes on ESP-IDF) */
size_t espinel_mem_stack_peak(void) { return espinel_ruby_stack_size - uxTaskGetStackHighWaterMark(NULL); }

/* the depth at which a Ruby method raises SystemStackError; the whole
   stack when the check is off (CONFIG_ESPINEL_STACK_CHECK) */
size_t espinel_mem_stack_limit(void) {
  uintptr_t limit = sp_esp_stack_limit();
  uintptr_t start = (uintptr_t)pxTaskGetStackStart(NULL);
  if (!limit) return espinel_ruby_stack_size;
  return start + espinel_ruby_stack_size - limit;
}

/* ---- Spinel's GC ---- */

size_t espinel_mem_gc_bytes(void) { return sp_gc_bytes; }
size_t espinel_mem_gc_threshold(void) { return sp_gc_threshold; }
size_t espinel_mem_gc_count(void) { return (size_t)sp_gc_stat_collections; }
size_t espinel_mem_gc_full_count(void) { return (size_t)sp_gc_stat_fulls; }
