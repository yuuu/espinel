/* Task::Queue's receiving end, which each VM links a copy of
   (main/CMakeLists.txt): it hands the message's bytes to Spinel as a
   `:binstr` return, whose length it reads from the VM's own
   sp_ffi_bin_len. The message is freed by the next receive; between the
   two it is this VM's. */
#include <stdlib.h>
#include "espinel_task.h"

extern int sp_ffi_bin_len;   /* Spinel's lib/sp_alloc.h */

static espinel_msg *last;

/* the next message's bytes, or NULL when the time ran out (then
   espinel_queue_got answers 0) */
const char *espinel_queue_receive(int id, int timeout_ms) {
  free(last);
  last = espinel_queue_receive_msg(id, timeout_ms);
  sp_ffi_bin_len = last ? (int)last->len : 0;
  return last ? last->data : "";
}

/* 1 when the last receive got a message, 0 when its time ran out */
int espinel_queue_got(void) { return last != NULL; }

/* 1 when that message was sent from a UTF-8 String */
int espinel_queue_utf8(void) { return last && last->utf8; }
