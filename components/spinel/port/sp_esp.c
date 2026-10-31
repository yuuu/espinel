/* ESP-IDF stand-ins for the POSIX calls the Spinel runtime makes and
   ESP-IDF does not provide. Each fails the way an unsupported call fails on
   a POSIX system (-1 with errno, MAP_FAILED, EAI_FAIL), so the runtime takes
   the fallback it already has for that answer instead of failing to link:

   - mmap: the slab allocator is off (every block is a malloc), and the
     program body runs on the calling task's own stack;
   - signals: no handler is installed; no fault report, no trap;
   - *context: Fiber.new fails at its stack (no stack switching yet).

   The headers beside this file declare what newlib and lwIP leave out. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <poll.h>
#include <ucontext.h>
#include <netdb.h>

/* memory mapping */
void *mmap(void *a, size_t n, int prot, int fl, int fd, off_t off) {
  (void)a; (void)n; (void)prot; (void)fl; (void)fd; (void)off;
  errno = ENOSYS;
  return MAP_FAILED;
}
int munmap(void *a, size_t n) { (void)a; (void)n; errno = ENOSYS; return -1; }
int mprotect(void *a, size_t n, int p) { (void)a; (void)n; (void)p; errno = ENOSYS; return -1; }
int madvise(void *a, size_t n, int adv) { (void)a; (void)n; (void)adv; return 0; }

/* signals */
_sig_func_ptr signal(int s, _sig_func_ptr h) { (void)s; (void)h; errno = ENOSYS; return SIG_ERR; }
int sigaction(int s, const struct sigaction *a, struct sigaction *o) {
  (void)s; (void)a; (void)o;
  errno = ENOSYS;
  return -1;
}
int sigaltstack(const stack_t *s, stack_t *o) { (void)s; (void)o; errno = ENOSYS; return -1; }
int sigprocmask(int h, const sigset_t *s, sigset_t *o) {
  (void)h; (void)s;
  if (o) *o = 0;
  return 0;
}

/* stack switching */
int getcontext(ucontext_t *u) { (void)u; errno = ENOSYS; return -1; }
int setcontext(const ucontext_t *u) { (void)u; errno = ENOSYS; return -1; }
void makecontext(ucontext_t *u, void (*f)(void), int n, ...) { (void)u; (void)f; (void)n; }
int swapcontext(ucontext_t *o, const ucontext_t *u) { (void)o; (void)u; errno = ENOSYS; return -1; }

/* resource limits */
int getrlimit(int r, struct rlimit *l) { (void)r; (void)l; errno = ENOSYS; return -1; }
int setrlimit(int r, const struct rlimit *l) { (void)r; (void)l; errno = ENOSYS; return -1; }

/* stdio: newlib's names for the POSIX line readers */
ssize_t __getline(char **, size_t *, FILE *);
ssize_t __getdelim(char **, size_t *, int, FILE *);
ssize_t getline(char **b, size_t *n, FILE *f) { return __getline(b, n, f); }
ssize_t getdelim(char **b, size_t *n, int d, FILE *f) { return __getdelim(b, n, d, f); }

/* name resolution: lwIP's getaddrinfo answers EAI_* codes but has no text */
const char *gai_strerror(int e) {
  switch (e) {
  case EAI_NONAME: return "Name or service not known";
  case EAI_SERVICE: return "Servname not supported for ai_socktype";
  case EAI_FAIL: return "Non-recoverable failure in name resolution";
  case EAI_MEMORY: return "Memory allocation failure";
  case EAI_FAMILY: return "ai_family not supported";
  default: return "Name resolution failed";
  }
}

/* poll on the VFS's select */
int poll(struct pollfd *fds, nfds_t n, int timeout_ms) {
  fd_set rd, wr, ex;
  FD_ZERO(&rd); FD_ZERO(&wr); FD_ZERO(&ex);
  int maxfd = -1;
  for (nfds_t i = 0; i < n; i++) {
    fds[i].revents = 0;
    int fd = fds[i].fd;
    if (fd < 0) continue;
    if (fd >= FD_SETSIZE) { fds[i].revents = POLLNVAL; continue; }
    if (fds[i].events & POLLIN) FD_SET(fd, &rd);
    if (fds[i].events & POLLOUT) FD_SET(fd, &wr);
    FD_SET(fd, &ex);
    if (fd > maxfd) maxfd = fd;
  }
  struct timeval tv, *tvp = NULL;
  if (timeout_ms >= 0) {
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    tvp = &tv;
  }
  int rc = select(maxfd + 1, &rd, &wr, &ex, tvp);
  if (rc < 0) return -1;
  int ready = 0;
  for (nfds_t i = 0; i < n; i++) {
    int fd = fds[i].fd;
    if (fd < 0 || fd >= FD_SETSIZE) { if (fds[i].revents) ready++; continue; }
    if (FD_ISSET(fd, &rd)) fds[i].revents |= POLLIN;
    if (FD_ISSET(fd, &wr)) fds[i].revents |= POLLOUT;
    if (FD_ISSET(fd, &ex)) fds[i].revents |= POLLERR;
    if (fds[i].revents) ready++;
  }
  return ready;
}
