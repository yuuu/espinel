#include <stdio.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "espinel_task.h"

/* The main program, VM 0 (packages/task keeps the table of VMs). Its task
   gets a stack of its own: the main task's 3.5 KB is too small for
   generated code. */
#define MAIN_VM_STACK_SIZE (32 * 1024)
#define MAIN_VM_PRIORITY 5

/* /dev/urandom (components/spinel/port/sp_esp_random.c) */
void sp_esp_random_init(void);

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
  sp_esp_random_init();
  if (espinel_task_init() != 0) abort();
  /* the others start when a program asks (Task.new) */
  if (espinel_task_start(0, -1, MAIN_VM_PRIORITY, MAIN_VM_STACK_SIZE) != 0)
    printf("could not start the main VM\n");
}
