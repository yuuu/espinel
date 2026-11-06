/* SystemStackError on the Ruby task's stack.

   Spinel catches a C stack overflow with a SIGSEGV handler on a guard
   page; here there are neither signals nor guard pages (SP_NO_SIGNALS),
   and FreeRTOS checks its stack canary only at a context switch, so a deep
   recursion ran past the 32 KB stack into the heap without a word and
   crashed later somewhere else. The generated C is compiled with
   -finstrument-functions (CMakeLists.txt), so every Ruby method's C
   function calls __cyg_profile_func_enter first; once the frame is within
   SP_ESP_STACK_MARGIN of the stack's end, it raises SystemStackError
   through the hook Spinel's fault handler uses, which longjmps to the
   nearest rescue. With none armed it reports the error and exits, as
   Spinel's handler does.

   The margin is what the runtime's own C may still use below the last
   Ruby frame, uninstrumented: sp_sprintf alone puts a 4 KB buffer on the
   stack. */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SP_ESP_STACK_MARGIN (6 * 1024)

extern void (*sp_stack_overflow_raise_fn)(void);

static uintptr_t stack_limit;   /* 0: not armed */

__attribute__((no_instrument_function)) void sp_esp_stack_check_init(void) {
  stack_limit = (uintptr_t)pxTaskGetStackStart(NULL) + SP_ESP_STACK_MARGIN;
}

__attribute__((no_instrument_function)) void __cyg_profile_func_enter(void *fn, void *site) {
  (void)fn; (void)site;
  if (!stack_limit || (uintptr_t)__builtin_frame_address(0) >= stack_limit) return;
  if (sp_stack_overflow_raise_fn) sp_stack_overflow_raise_fn();   /* returns only if nothing rescues */
  stack_limit = 0;
  fflush(stdout);
  fputs("stack level too deep (SystemStackError)\n", stderr);
  _exit(1);
}

__attribute__((no_instrument_function)) void __cyg_profile_func_exit(void *fn, void *site) {
  (void)fn; (void)site;
}
