/* The board's timing on ESP-IDF. Machine.sleep is nanosleep, which the
   port builds on an esp_timer one-shot (components/spinel/port/sp_esp.c):
   the task blocks, other tasks run, and it wakes at the microsecond. */
#include <time.h>

void espinel_machine_sleep(double seconds) {
  struct timespec ts;
  ts.tv_sec = (time_t)seconds;
  ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9 + 0.5);
  if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
  nanosleep(&ts, NULL);
}
