#include <stdio.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* the Ruby program's main, renamed (see CMakeLists.txt) */
int spinel_main(int argc, char **argv);

static void ruby_task(void *arg) {
  (void)arg;
  char *argv[] = { "hello", NULL };
  int rc = spinel_main(1, argv);
  fflush(stdout);
  printf("spinel_main returned %d (stack left %u bytes, free heap %u bytes)\n",
         rc, (unsigned)uxTaskGetStackHighWaterMark(NULL),
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
  vTaskDelete(NULL);
}

void app_main(void) {
  /* the main task's 3.5 KB stack is too small for generated code */
  xTaskCreate(ruby_task, "ruby", 32 * 1024, NULL, 5, NULL);
}
