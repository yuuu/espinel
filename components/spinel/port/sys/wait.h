/* newlib's sys/wait.h has no WCOREDUMP; no process ever dumps core here.
   (Process::Status#coredump? reads it; with the runtime's -w an undeclared
   macro would become a call to a function nothing defines.) */
#ifndef SP_ESP_SYS_WAIT_H
#define SP_ESP_SYS_WAIT_H
#include_next <sys/wait.h>
#ifndef WCOREDUMP
#define WCOREDUMP(s) 0
#endif
#endif
