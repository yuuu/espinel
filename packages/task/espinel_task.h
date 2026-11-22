/* A message on a Task::Queue (task.c, task_vm.c) */
#ifndef ESPINEL_TASK_H
#define ESPINEL_TASK_H
#include <stddef.h>

typedef struct {
  size_t len;
  int utf8;       /* the sender's String was UTF-8, not binary */
  char data[];
} espinel_msg;

espinel_msg *espinel_queue_receive_msg(int id, int timeout_ms);

#endif
