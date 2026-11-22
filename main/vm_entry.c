/* The entry of one Ruby VM, compiled once for each.

   Each Ruby program (the entry point and every tasks/<name>.rb beside it)
   is linked with its own copy of Spinel's runtime into one relocatable
   object, whose symbols are then all made local but ESPINEL_VM_RUN
   (main/CMakeLists.txt). Spinel keeps its state in globals; with a copy
   each, two programs run on two FreeRTOS tasks at once, on two cores,
   without sharing a byte of it: each has its own heap of objects and its
   own collector.

   What the C library keeps for the whole process is taken over here for
   this VM, since the runtime's references to these names bind to the
   definitions in the same object:

   - atexit: Spinel registers END blocks, finalizers and its own reports
     with it. Kept per VM, a VM's exit runs its own handlers, not every
     VM's.
   - exit, _exit: ESP-IDF's _exit is abort(), a panic and a reboot. Here
     exit runs this VM's handlers, flushes, and returns from
     ESPINEL_VM_RUN as if the program had returned the status; _exit (exit!,
     and the stack check's last resort) does the same without the
     handlers. Spinel's state is not reset, so a VM runs once a boot.
   - malloc, calloc, realloc: every VM draws on the one heap, and each
     collector sees only its own objects. A VM's allocation can fail while
     another VM's garbage holds the room, and Spinel ends the program on a
     failed allocation (sp_oom_die) without collecting. Here a failure asks
     every VM, this one too, to collect at its next allocation
     (packages/task), and tries again for a while before it fails.

   When the program has ended, its objects are freed: its roots are
   dropped and the collector runs until it has swept the old generation
   too. What the globals and constants hold, and the runtime's own tables,
   stay. */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_random.h"
#include "espinel_task.h"

#ifndef ESPINEL_VM_RUN
#error "ESPINEL_VM_RUN names this VM's entry (main/CMakeLists.txt)"
#endif

/* the Ruby program's main, renamed (main/CMakeLists.txt) */
int spinel_main(int argc, char **argv);
/* the GC's budget for this heap (components/spinel/port/sp_esp_gc.c) */
void sp_esp_gc_init(void);
/* SystemStackError near the end of this task's stack (stack_check.c) */
void sp_esp_stack_check_init(void);
/* Kernel#rand's seed (Spinel's lib/sp_random.c) */
void sp_krand_srand(uint64_t seed);
/* Spinel's collector (lib/sp_gc.h, lib/sp_alloc.c) */
extern size_t sp_gc_threshold;
extern int sp_gc_nroots;
extern unsigned long long sp_gc_stat_fulls;
void sp_gc_collect(void);
/* what the C library's malloc and realloc are (ESP-IDF's heap) */
void *heap_caps_malloc_default(size_t size);
void *heap_caps_realloc_default(void *ptr, size_t size);

/* this VM's task stack, in bytes (packages/memory reports against it) */
unsigned espinel_ruby_stack_size;

#define VM_ATEXIT_MAX 32

static void (*exit_handlers[VM_ATEXIT_MAX])(void);
static int exit_handler_count;
static TaskHandle_t vm_task;
static jmp_buf vm_exit;
static int vm_exit_status;

int atexit(void (*fn)(void)) {
  if (exit_handler_count == VM_ATEXIT_MAX) return -1;
  exit_handlers[exit_handler_count++] = fn;
  return 0;
}

static void run_exit_handlers(void) {
  while (exit_handler_count > 0) exit_handlers[--exit_handler_count]();
}

void _exit(int status) {
  fflush(stdout);
  fflush(stderr);
  if (vm_task && xTaskGetCurrentTaskHandle() == vm_task) {
    vm_exit_status = status;
    longjmp(vm_exit, 1);
  }
  /* another task in this VM's code: nothing to return to */
  abort();
}

void exit(int status) {
  run_exit_handlers();
  _exit(status);
}

/* how long a failed allocation is tried again, while the other VMs
   collect, before it fails */
#define VM_ALLOC_RETRY_MS 1000
#define VM_ALLOC_RETRY_STEP_MS 10

static void *alloc_again(void *ptr, size_t n) {
  espinel_task_pressure();
  for (int waited = 0; waited < VM_ALLOC_RETRY_MS; waited += VM_ALLOC_RETRY_STEP_MS) {
    vTaskDelay(pdMS_TO_TICKS(VM_ALLOC_RETRY_STEP_MS) ? pdMS_TO_TICKS(VM_ALLOC_RETRY_STEP_MS) : 1);
    void *p = ptr ? heap_caps_realloc_default(ptr, n) : heap_caps_malloc_default(n);
    if (p) return p;
  }
  return NULL;
}

void *malloc(size_t n) {
  void *p = heap_caps_malloc_default(n);
  return p || n == 0 ? p : alloc_again(NULL, n);
}

void *realloc(void *ptr, size_t n) {
  if (!ptr) return malloc(n);
  void *p = heap_caps_realloc_default(ptr, n);
  return p || n == 0 ? p : alloc_again(ptr, n);
}

void *calloc(size_t count, size_t size) {
  size_t n;
  if (__builtin_mul_overflow(count, size, &n)) return NULL;
  void *p = malloc(n);
  if (p) memset(p, 0, n);
  return p;
}

/* Frees what the ended program's objects hold. A minor collection leaves
   the old generation; a full one comes every few (adaptively, up to 128),
   so the collector runs until one has been full. */
static void free_objects(void) {
  sp_gc_nroots = 0;
  unsigned long long fulls = sp_gc_stat_fulls;
  for (int i = 0; i < 130 && sp_gc_stat_fulls == fulls; i++) sp_gc_collect();
}

int ESPINEL_VM_RUN(const char *name, unsigned stack_size) {
  char *argv[] = { (char *)name, NULL };
  int rc;
  espinel_ruby_stack_size = stack_size;
  sp_esp_gc_init();
  sp_krand_srand(((uint64_t)esp_random() << 32) | esp_random());
#if CONFIG_ESPINEL_STACK_CHECK
  sp_esp_stack_check_init();
#endif
  vm_task = xTaskGetCurrentTaskHandle();
  espinel_task_attach(&sp_gc_threshold);
  if (setjmp(vm_exit) == 0) {
    rc = spinel_main(1, argv);
    /* a C program's return from main runs the handlers too */
    run_exit_handlers();
  } else {
    rc = vm_exit_status;
  }
  vm_task = NULL;
  fflush(stdout);
  fflush(stderr);
  free_objects();
  return rc;
}
