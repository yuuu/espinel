/* The table of Ruby VMs this firmware carries (main.c): "main", the entry
   point, is VM 0; then one for each tasks/<name>.rb beside it, in the
   order of their names. Every VM is a Ruby program with its own copy of
   Spinel's runtime (vm_entry.c) and runs on a FreeRTOS task of its own.

   packages/task gives Ruby its Task class on top of this. */
#ifndef ESPINEL_VM_H
#define ESPINEL_VM_H
#include <stddef.h>

enum { ESPINEL_VM_IDLE, ESPINEL_VM_RUNNING, ESPINEL_VM_FINISHED };

/* how many VMs the firmware carries */
int espinel_vm_count(void);

/* VM id's name ("main", or the task's file name), "" for no such VM */
const char *espinel_vm_name(int id);

/* the VM running on the calling task, or -1 */
int espinel_vm_self(void);

/* Starts VM id on a task of its own: on core 0 or 1, or either (-1). A VM
   runs once a boot, since Spinel's state is not reset. Answers 0, or
   -1 no such VM, -2 started before, -3 no such core, -4 out of memory. */
int espinel_vm_start(int id, int core, int priority, int stack_size);

/* Waits for VM id to finish, timeout_ms at most (-1: forever). Answers 1
   when it has finished, 0 when the time ran out, -1 when it never
   started, -2 when it is the calling VM itself. */
int espinel_vm_wait(int id, int timeout_ms);

/* ESPINEL_VM_IDLE (never started), _RUNNING or _FINISHED; -1 for no such
   VM */
int espinel_vm_state(int id);

/* the exit status of a finished VM */
int espinel_vm_status(int id);

/* how many VMs are running (the GC shares the heap out among them,
   components/spinel/port/sp_esp_gc.c) */
unsigned espinel_vm_running(void);

/* Every VM collects at its next allocation: the heap is short (a VM's
   malloc failed), or the VMs running have changed and each should share
   the room out again. A VM blocked in a wait collects when it next
   allocates. */
void espinel_vm_pressure(void);

/* A VM's runtime, starting, says where its collection trigger
   (sp_gc_threshold) is; the others then share the room out among one
   more. */
void espinel_vm_attach(size_t *gc_threshold);

#endif
