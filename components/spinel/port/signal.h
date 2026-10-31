/* newlib declares struct sigaction and sigprocmask but leaves out the
   flags, siginfo_t and sigaltstack; sigaction itself answers ENOSYS in
   sp_esp.c, so no handler is ever installed and the runtime's fallbacks
   apply (no fault report, no guard-page overflow raise). */
#ifndef SP_ESP_SIGNAL_H
#define SP_ESP_SIGNAL_H
/* newlib keeps sa_sigaction and siginfo_t behind the realtime-signals
   option; ask for them (there is no si_addr: it is the value's pointer) */
#ifndef _POSIX_REALTIME_SIGNALS
#define _POSIX_REALTIME_SIGNALS 1
#endif
#include_next <signal.h>
#ifndef si_addr
#define si_addr si_value.sival_ptr
#endif
#include <sys/types.h>
#ifndef SA_SIGINFO
#define SA_SIGINFO   0x00000002
#endif
#ifndef SA_RESTART
#define SA_RESTART   0x10000000
#endif
#ifndef SA_ONSTACK
#define SA_ONSTACK   0x08000000
#endif
#ifndef SA_NODEFER
#define SA_NODEFER   0x40000000
#endif
#ifndef SA_RESETHAND
#define SA_RESETHAND 0x80000000
#endif
#ifndef SS_DISABLE
#define SS_DISABLE 2
typedef struct sigaltstack { void *ss_sp; int ss_flags; size_t ss_size; } stack_t;
#endif
int sigaltstack(const stack_t *, stack_t *);
#endif
