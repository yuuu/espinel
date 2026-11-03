/* newlib has no <poll.h>. ESP-IDF has a poll (<sys/poll.h>, built on the
   VFS's select in newlib/src/poll.c), but with other POLL* values; this
   header and the poll in sp_esp.c, which the link takes instead of
   ESP-IDF's, stand in for both. */
#ifndef SP_ESP_POLL_H
#define SP_ESP_POLL_H
#define POLLIN   0x001
#define POLLPRI  0x002
#define POLLOUT  0x004
#define POLLERR  0x008
#define POLLHUP  0x010
#define POLLNVAL 0x020
#define POLLRDNORM POLLIN
#define POLLWRNORM POLLOUT
typedef unsigned int nfds_t;
struct pollfd { int fd; short events; short revents; };
int poll(struct pollfd *, nfds_t, int);
#endif
