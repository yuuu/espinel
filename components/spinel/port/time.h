/* ESP-IDF's clock_gettime knows CLOCK_REALTIME and CLOCK_MONOTONIC only
   (picolibc names the CPU clocks, newlib does not); Process.clock_gettime
   of either CPU clock reads the monotonic clock, as Spinel's wasm32-wasi
   port does (lib/wasi/time.h). One task runs Ruby, so it is that task's
   time too, with what other tasks ran in between counted in. */
#ifndef SP_ESP_TIME_H
#define SP_ESP_TIME_H
#include_next <time.h>
#undef CLOCK_PROCESS_CPUTIME_ID
#define CLOCK_PROCESS_CPUTIME_ID CLOCK_MONOTONIC
#undef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID CLOCK_MONOTONIC
#endif
