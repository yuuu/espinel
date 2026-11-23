/* The heap, the Ruby task's stack and Spinel's GC as numbers, for
   memory.rb. Every function answers one figure in bytes (or a count), so
   the Ruby side binds them with plain ffi_func. The stack and the GC are
   the calling program's VM's (packages/task). */
#include <stddef.h>
#include <stdint.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "espinel_task.h"

static const espinel_vm none;

/* the calling program's VM; all zeros outside one */
static const espinel_vm *vm(void) {
  const espinel_vm *v = espinel_vm_current();
  return v ? v : &none;
}

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

size_t espinel_mem_stack_size(void) { return vm()->stack_size; }

/* bytes in use at the caller: from the top of the stack to this frame */
size_t espinel_mem_stack_used(void) {
  uintptr_t top = (uintptr_t)pxTaskGetStackStart(NULL) + vm()->stack_size;
  return top - (uintptr_t)__builtin_frame_address(0);
}

/* the most ever in use, from FreeRTOS's high-water mark (bytes on ESP-IDF) */
size_t espinel_mem_stack_peak(void) {
  return vm()->stack_size - uxTaskGetStackHighWaterMark(NULL);
}

/* the depth at which a Ruby method raises SystemStackError; the whole
   stack when the check is off (CONFIG_ESPINEL_STACK_CHECK) */
size_t espinel_mem_stack_limit(void) {
  const espinel_vm *v = vm();
  uintptr_t limit = v->stack_limit ? v->stack_limit() : 0;
  uintptr_t start = (uintptr_t)pxTaskGetStackStart(NULL);
  if (!limit) return v->stack_size;
  return start + v->stack_size - limit;
}

/* ---- Spinel's GC ---- */

size_t espinel_mem_gc_bytes(void) { return vm()->gc_bytes ? *vm()->gc_bytes : 0; }
size_t espinel_mem_gc_threshold(void) { return vm()->gc_threshold ? *vm()->gc_threshold : 0; }
size_t espinel_mem_gc_count(void) { return vm()->gc_collections ? (size_t)*vm()->gc_collections : 0; }
size_t espinel_mem_gc_full_count(void) { return vm()->gc_fulls ? (size_t)*vm()->gc_fulls : 0; }
