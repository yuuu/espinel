/* The C half of task.rb, linked once for the whole firmware (task_vm.c
   is each VM's). Every firmware carries it, required or not: main.c
   starts the main program through it (espinel_task.h).

   Task: the table of VMs and the FreeRTOS tasks they run on.

   Task::Queue: named queues between the VMs, the process's one table,
   which every VM reaches by name. A message is a copy of a String's
   bytes, malloc'd by the sender and freed by the receiver (task_vm.c), so
   no VM ever holds another's object. FreeRTOS queues of message pointers
   carry them. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "espinel_task.h"

/* Task */

typedef struct {
  TaskHandle_t task;
  size_t *gc_threshold;     /* the VM's collection trigger (vm_entry.c) */
  SemaphoreHandle_t done;   /* given when the VM has returned */
  unsigned stack_size;
  int state;
  int status;
} vm_slot;

static vm_slot *slots;
static unsigned running;
static portMUX_TYPE slots_lock = portMUX_INITIALIZER_UNLOCKED;

static int valid(int id) { return id >= 0 && id < espinel_vm_def_count; }

int espinel_task_init(void) {
  slots = calloc((size_t)espinel_vm_def_count, sizeof *slots);
  if (!slots) return -1;
  for (int i = 0; i < espinel_vm_def_count; i++)
    if (!(slots[i].done = xSemaphoreCreateBinary())) return -1;
  return 0;
}

unsigned espinel_task_running(void) { return running; }

/* how many VMs the firmware carries */
int espinel_task_count(void) { return espinel_vm_def_count; }

/* VM id's name ("main", or the task's file name), "" for no such VM */
const char *espinel_task_name(int id) { return valid(id) ? espinel_vm_defs[id].name : ""; }

/* the VM called name, or -1 */
int espinel_task_find(const char *name) {
  for (int i = 0; i < espinel_vm_def_count; i++)
    if (strcmp(espinel_vm_defs[i].name, name) == 0) return i;
  return -1;
}

/* the VM running on the calling task, or -1 */
int espinel_task_self(void) {
  TaskHandle_t me = xTaskGetCurrentTaskHandle();
  for (int i = 0; i < espinel_vm_def_count; i++)
    if (slots[i].task == me && slots[i].state == ESPINEL_TASK_RUNNING) return i;
  return -1;
}

/* the core the calling task is on now (an unpinned task can move) */
int espinel_task_core(void) { return (int)xPortGetCoreID(); }

/* ESPINEL_TASK_IDLE (never started), _RUNNING or _FINISHED; -1 for no
   such VM */
int espinel_task_state(int id) { return valid(id) ? slots[id].state : -1; }

/* the exit status of a finished VM */
int espinel_task_status(int id) { return valid(id) ? slots[id].status : -1; }

void espinel_task_pressure(void) {
  for (int i = 0; i < espinel_vm_def_count; i++) {
    size_t *t = slots[i].gc_threshold;
    if (t && slots[i].state == ESPINEL_TASK_RUNNING) *t = 0;
  }
}

void espinel_task_attach(size_t *gc_threshold) {
  int id = espinel_task_self();
  espinel_task_pressure();
  if (id >= 0) slots[id].gc_threshold = gc_threshold;
}

static void vm_task(void *arg) {
  int id = (int)(intptr_t)arg;
  const espinel_vm_def *def = &espinel_vm_defs[id];
  vm_slot *s = &slots[id];
  /* set here, before any Ruby runs: on two cores the task can be running
     before xTaskCreatePinnedToCore has returned to its creator */
  s->task = xTaskGetCurrentTaskHandle();
  int rc = def->run(def->name, s->stack_size);
  /* the main VM's line as it always was; the others' carry their name */
  printf("%s%s%sspinel_main returned %d (stack left %u bytes, free heap %u bytes)\n",
         id ? "[" : "", id ? def->name : "", id ? "] " : "",
         rc, (unsigned)uxTaskGetStackHighWaterMark(NULL),
         (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
  fflush(stdout);
  taskENTER_CRITICAL(&slots_lock);
  s->status = rc;
  s->state = ESPINEL_TASK_FINISHED;
  s->task = NULL;
  s->gc_threshold = NULL;
  running--;
  taskEXIT_CRITICAL(&slots_lock);
  xSemaphoreGive(s->done);
  vTaskDelete(NULL);
}

int espinel_task_start(int id, int core, int priority, int stack_size) {
  if (!valid(id)) return -1;
  if (core < -1 || core >= portNUM_PROCESSORS) return -3;
  if (priority < 1) priority = 1;
  if (priority >= configMAX_PRIORITIES) priority = configMAX_PRIORITIES - 1;
  vm_slot *s = &slots[id];
  taskENTER_CRITICAL(&slots_lock);
  int was = s->state;
  if (was == ESPINEL_TASK_IDLE) {
    s->state = ESPINEL_TASK_RUNNING;
    running++;
  }
  taskEXIT_CRITICAL(&slots_lock);
  if (was != ESPINEL_TASK_IDLE) return -2;
  s->stack_size = (unsigned)stack_size;
  BaseType_t ok = xTaskCreatePinnedToCore(vm_task, espinel_vm_defs[id].name, (uint32_t)stack_size,
                                          (void *)(intptr_t)id, (UBaseType_t)priority, NULL,
                                          core < 0 ? tskNO_AFFINITY : (BaseType_t)core);
  if (ok != pdPASS) {
    taskENTER_CRITICAL(&slots_lock);
    s->state = ESPINEL_TASK_IDLE;
    running--;
    taskEXIT_CRITICAL(&slots_lock);
    return -4;
  }
  return 0;
}

/* Waits for VM id to finish, timeout_ms at most (-1: forever). Answers 1
   when it has finished, 0 when the time ran out, -1 when it never
   started, -2 when it is the calling VM itself. */
int espinel_task_wait(int id, int timeout_ms) {
  if (!valid(id)) return -1;
  vm_slot *s = &slots[id];
  if (s->state == ESPINEL_TASK_IDLE) return -1;
  if (s->state == ESPINEL_TASK_FINISHED) return 1;
  if (s->task == xTaskGetCurrentTaskHandle()) return -2;
  TickType_t ticks = timeout_ms < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  if (xSemaphoreTake(s->done, ticks) != pdTRUE) return 0;
  xSemaphoreGive(s->done);   /* for every other waiter */
  return 1;
}

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
