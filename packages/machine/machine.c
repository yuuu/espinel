/* The board's timing on ESP-IDF. Whole FreeRTOS ticks are waited with
   vTaskDelay, which lets other tasks run; what is left of a tick (10 ms at
   the default 100 Hz) is a busy wait on esp_timer, the clock the ticks
   themselves follow. (esp_rom_delay_us counts CPU cycles, which QEMU does
   not run at the chip's rate.) */
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

void espinel_machine_sleep(double seconds) {
  int64_t us = (int64_t)(seconds * 1e6 + 0.5);
  int64_t end = esp_timer_get_time() + us;
  int64_t tick_us = (int64_t)portTICK_PERIOD_MS * 1000;
  int64_t ticks = us / tick_us;
  while (ticks > 0) {
    TickType_t n = ticks > portMAX_DELAY - 1 ? portMAX_DELAY - 1 : (TickType_t)ticks;
    vTaskDelay(n);
    ticks -= n;
  }
  while (esp_timer_get_time() < end) {
  }
}
