/* No stack switching on this port yet: getcontext answers -1 (ENOSYS), so
   Fiber.new fails at its stack rather than jumping into nothing. */
#ifndef SP_ESP_UCONTEXT_H
#define SP_ESP_UCONTEXT_H
#include <signal.h>
typedef struct ucontext_t {
  struct ucontext_t *uc_link;
  stack_t uc_stack;
  int uc_mcontext;
} ucontext_t;
int getcontext(ucontext_t *);
int setcontext(const ucontext_t *);
void makecontext(ucontext_t *, void (*)(void), int, ...);
int swapcontext(ucontext_t *, const ucontext_t *);
#endif
