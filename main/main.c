#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "sdkconfig.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* the Ruby program's main, renamed (see CMakeLists.txt) */
int spinel_main(int argc, char **argv);
/* the GC's budget for this heap (components/spinel/port/sp_esp_gc.c) */
void sp_esp_gc_init(void);
/* SystemStackError near the end of this task's stack (stack_check.c) */
void sp_esp_stack_check_init(void);

/* exit() ends the Ruby program, not the chip. ESP-IDF's _exit is abort(),
   which panics and reboots, so every exit, exit!, abort and uncaught
   exception restarted the board and the program with it, over and over.
   This exit does what newlib's does short of _exit (the atexit handlers,
   Ruby's at_exit among them, then flushing) and returns to ruby_task as if
   spinel_main had returned the status. Spinel's state is not reset, so
   the program is not run again in the same boot. Called from any other
   task, it ends as before. */
static TaskHandle_t ruby_task_handle;
static jmp_buf ruby_exit;
static int ruby_exit_status;

void __call_exitprocs(int, void *);

void exit(int status) {
  __call_exitprocs(status, NULL);
  fflush(NULL);
  if (ruby_task_handle && xTaskGetCurrentTaskHandle() == ruby_task_handle) {
    ruby_exit_status = status;
    longjmp(ruby_exit, 1);
  }
  _exit(status);
}

static void ruby_task(void *arg) {
  (void)arg;
  sp_esp_gc_init();
#if CONFIG_ESPINEL_STACK_CHECK
  sp_esp_stack_check_init();
#endif
  char *argv[] = { "main.rb", NULL };
  int rc;
  ruby_task_handle = xTaskGetCurrentTaskHandle();
  if (setjmp(ruby_exit) == 0) rc = spinel_main(1, argv);
  else rc = ruby_exit_status;
  ruby_task_handle = NULL;
  fflush(NULL);
  printf("spinel_main returned %d (stack left %u bytes, free heap %u bytes)\n",
         rc, (unsigned)uxTaskGetStackHighWaterMark(NULL),
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
  vTaskDelete(NULL);
}

/* stdin on the UART console. Without the UART driver the console's reads
   do not block: gets answered nil at once, as at end of file, and the next
   read never returned. With it a read blocks the task (other tasks run)
   until a line comes, and a CR LF from a terminal reads as "\n". */
static void console_stdin(void) {
#if CONFIG_ESP_CONSOLE_UART
  if (uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0) != ESP_OK) return;
  uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
  uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);
#endif
}

void app_main(void) {
  console_stdin();
  /* the main task's 3.5 KB stack is too small for generated code */
  xTaskCreate(ruby_task, "ruby", 32 * 1024, NULL, 5, NULL);
}
