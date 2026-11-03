/* The GC's object budget sized to the heap there is.

   After each collection Spinel aims the next one at twice what is live,
   or twice what was there when the sweep freed little, with the initial
   threshold (256 KB) as a floor and no ceiling. Both outgrow the ESP32-S3's
   internal RAM, so allocation reached the end of the heap before the next
   collection and the program died out of memory. sp_esp_gc_init() lowers
   the floor and wraps the retune in a cap: the next collection comes before
   the heap's free space, less a reserve, is three quarters used.

   It runs from the Ruby task before spinel_main, after Spinel's
   constructors have installed the retune hook it wraps. */
#include <stddef.h>
#include "esp_heap_caps.h"

extern size_t sp_gc_threshold, sp_gc_threshold_init, sp_gc_bytes;
extern void (*sp_gc_obj_retune_hook)(size_t before);
void sp_gc_retune_object(size_t before);

#define SP_ESP_GC_FLOOR   (32 * 1024)  /* collect after this much at least */
#define SP_ESP_GC_RESERVE (32 * 1024)  /* never budgeted: payloads, drivers */
#define SP_ESP_GC_SHARE   75           /* percent of the rest */

static void sp_esp_gc_retune(size_t before) {
  sp_gc_retune_object(before);
  size_t free_now = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  size_t room = free_now > SP_ESP_GC_RESERVE
                    ? (free_now - SP_ESP_GC_RESERVE) / 100 * SP_ESP_GC_SHARE
                    : 0;
  size_t cap = sp_gc_bytes + room;
  if (sp_gc_threshold > cap) sp_gc_threshold = cap;
}

void sp_esp_gc_init(void) {
  sp_gc_threshold = sp_gc_threshold_init = SP_ESP_GC_FLOOR;
  sp_gc_obj_retune_hook = sp_esp_gc_retune;
}
