/* lwIP has no UNIX-domain sockets; socket(AF_UNIX, ...) fails there. */
#ifndef SP_ESP_SYS_SOCKET_H
#define SP_ESP_SYS_SOCKET_H
#include_next <sys/socket.h>
#ifndef PF_UNIX
#define PF_UNIX 1
#endif
#ifndef AF_UNIX
#define AF_UNIX PF_UNIX
#endif
#endif
