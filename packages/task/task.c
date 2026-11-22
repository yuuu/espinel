/* The C half of task.rb, linked once for the whole firmware.

   Task: the Ruby face of the table of VMs (main/main.c, espinel_vm.h).

   Task::Queue: named queues between the VMs, the process's one table,
   which every VM reaches by name. A message is a copy of a String's
   bytes, malloc'd by the sender and freed by the receiver (task_vm.c), so
   no VM ever holds another's object. FreeRTOS queues of message pointers
   carry them. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "espinel_vm.h"
#include "espinel_task.h"

/* Task */

/* the VM called name, or -1 */
int espinel_task_find(const char *name) {
  int n = espinel_vm_count();
  for (int i = 0; i < n; i++)
    if (strcmp(espinel_vm_name(i), name) == 0) return i;
  return -1;
}

/* see espinel_vm.h for these */
int espinel_task_start(int id, int core, int priority, int stack_size) {
  return espinel_vm_start(id, core, priority, stack_size);
}

int espinel_task_wait(int id, int timeout_ms) { return espinel_vm_wait(id, timeout_ms); }

int espinel_task_state(int id) { return espinel_vm_state(id); }

int espinel_task_status(int id) { return espinel_vm_status(id); }

int espinel_task_self(void) { return espinel_vm_self(); }

int espinel_task_count(void) { return espinel_vm_count(); }

const char *espinel_task_name(int id) { return espinel_vm_name(id); }

/* the core the calling task is on now (an unpinned task can move) */
int espinel_task_core(void) { return (int)xPortGetCoreID(); }

/* Task::Queue */

#define QUEUE_MAX 8
#define QUEUE_NAME_MAX 24

static struct {
  char name[QUEUE_NAME_MAX];
  QueueHandle_t q;
} queues[QUEUE_MAX];
static portMUX_TYPE queues_lock = portMUX_INITIALIZER_UNLOCKED;

static TickType_t ticks_of(int timeout_ms) {
  return timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
}

/* The queue called name, made with room for capacity messages if there is
   none yet. Answers its id, or -1 when the name is too long, -2 when
   every slot is taken, -3 out of memory. */
int espinel_queue_open(const char *name, int capacity) {
  if (strlen(name) >= QUEUE_NAME_MAX) return -1;
  for (int i = 0; i < QUEUE_MAX; i++)
    if (queues[i].q && strcmp(queues[i].name, name) == 0) return i;
  if (capacity < 1) capacity = 1;
  /* made outside the lock, which must not wrap an allocation; a VM that
     made the same queue at the same moment keeps its own and this one
     goes */
  QueueHandle_t q = xQueueCreate((UBaseType_t)capacity, sizeof(espinel_msg *));
  if (!q) return -3;
  int id = -2;
  taskENTER_CRITICAL(&queues_lock);
  for (int i = 0; i < QUEUE_MAX && id < 0; i++)
    if (queues[i].q && strcmp(queues[i].name, name) == 0) id = i;
  int made = id < 0;
  for (int i = 0; i < QUEUE_MAX && id < 0; i++)
    if (!queues[i].q) {
      strcpy(queues[i].name, name);
      queues[i].q = q;
      id = i;
    }
  taskEXIT_CRITICAL(&queues_lock);
  if (!made || id < 0) vQueueDelete(q);
  return id;
}

/* Copies n bytes (a UTF-8 String when utf8) onto queue id, waiting
   timeout_ms at most while it is full (-1: forever). Answers 1, 0 when the
   time ran out, -1 no such queue, -3 out of memory. */
int espinel_queue_send(int id, const char *bytes, size_t n, int utf8, int timeout_ms) {
  if (id < 0 || id >= QUEUE_MAX || !queues[id].q) return -1;
  espinel_msg *m = malloc(sizeof *m + n);
  if (!m) return -3;
  m->len = n;
  m->utf8 = utf8 != 0;
  memcpy(m->data, bytes, n);
  if (xQueueSend(queues[id].q, &m, ticks_of(timeout_ms)) != pdTRUE) {
    free(m);
    return 0;
  }
  return 1;
}

/* The next message on queue id, waiting timeout_ms at most (-1: forever),
   or NULL when the time ran out. The caller frees it. */
espinel_msg *espinel_queue_receive_msg(int id, int timeout_ms) {
  if (id < 0 || id >= QUEUE_MAX || !queues[id].q) return NULL;
  espinel_msg *m;
  return xQueueReceive(queues[id].q, &m, ticks_of(timeout_ms)) == pdTRUE ? m : NULL;
}

/* how many messages are waiting */
int espinel_queue_size(int id) {
  if (id < 0 || id >= QUEUE_MAX || !queues[id].q) return 0;
  return (int)uxQueueMessagesWaiting(queues[id].q);
}
