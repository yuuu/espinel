/* ESP-IDF stand-ins for the POSIX calls the Spinel runtime makes and
   ESP-IDF does not provide. Each fails the way an unsupported call fails on
   a POSIX system (-1 with errno, MAP_FAILED, EAI_FAIL), so the runtime takes
   the fallback it already has for that answer instead of failing to link:

   - mmap: the slab allocator is off (every block is a malloc), and the
     program body runs on the calling task's own stack;
   - signals: no handler is installed; no fault report, no trap;
   - *context: Fiber.new fails at its stack (no stack switching yet).
   - processes and pipes: system answers nil, backticks "", and
     Process.wait raises Errno::ECHILD;
   - users: a single one, with every id 0.

   The headers beside this file declare what newlib and lwIP leave out. */
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <ucontext.h>
#include <netdb.h>
#include <stdint.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"

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

/* files: the VFS has no symbolic links, so lstat is stat; no file has
   an owner or a creation mask, and every file is the caller's */
int lstat(const char *path, struct stat *st) { return stat(path, st); }
int symlink(const char *to, const char *from) { (void)to; (void)from; errno = ENOSYS; return -1; }
int faccessat(int dirfd, const char *path, int mode, int flags) {
  (void)dirfd; (void)flags;
  return access(path, mode);
}
int utimensat(int dirfd, const char *path, const struct timespec t[2], int flags) {
  (void)dirfd; (void)path; (void)t; (void)flags;
  errno = ENOSYS;
  return -1;
}
mode_t umask(mode_t m) { (void)m; return 022; }
int dup(int fd) { (void)fd; errno = ENOSYS; return -1; }
int dup2(int fd, int to) { (void)fd; (void)to; errno = ENOSYS; return -1; }
int pipe(int fds[2]) { (void)fds; errno = ENOSYS; return -1; }

/* users: one, with every id 0 */
uid_t getuid(void) { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void) { return 0; }
gid_t getegid(void) { return 0; }
int getgroups(int n, gid_t *g) { (void)n; (void)g; return 0; }

/* processes: none to start or wait for (system and `` answer nil and "") */
int execl(const char *p, const char *a, ...) { (void)p; (void)a; errno = ENOSYS; return -1; }
int execvp(const char *f, char *const a[]) { (void)f; (void)a; errno = ENOSYS; return -1; }
pid_t waitpid(pid_t p, int *st, int fl) { (void)p; (void)st; (void)fl; errno = ECHILD; return -1; }
int setpgid(pid_t p, pid_t g) { (void)p; (void)g; errno = ENOSYS; return -1; }
pid_t setsid(void) { errno = ENOSYS; return -1; }

/* resource limits */
int getrlimit(int r, struct rlimit *l) { (void)r; (void)l; errno = ENOSYS; return -1; }
int setrlimit(int r, const struct rlimit *l) { (void)r; (void)l; errno = ENOSYS; return -1; }

/* stdio: newlib's names for the POSIX line readers */
ssize_t __getline(char **, size_t *, FILE *);
ssize_t __getdelim(char **, size_t *, int, FILE *);
ssize_t getline(char **b, size_t *n, FILE *f) { return __getline(b, n, f); }
ssize_t getdelim(char **b, size_t *n, int d, FILE *f) { return __getdelim(b, n, d, f); }
/* musl's count of bytes read ahead; newlib's FILE keeps it in _r, as BSD's does */
size_t __freadahead(FILE *f) { return f->_r > 0 ? (size_t)f->_r : 0; }

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

/* time: newlib has neither nanosleep nor pause. A sleep blocks the task
   on an esp_timer one-shot, so other tasks get the CPU and the wake-up
   comes at the microsecond asked for, not at a tick. (ESP-IDF's usleep
   busy-waits whatever is under a tick, with a CPU-cycle count that QEMU
   runs about 4x long.) Only the Ruby task sleeps here, so one timer and
   one semaphore serve every call. */
static void sp_esp_sleep_wake(void *sem) { xSemaphoreGive((SemaphoreHandle_t)sem); }
int nanosleep(const struct timespec *req, struct timespec *rem) {
  if (!req || req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L) {
    errno = EINVAL;
    return -1;
  }
  static SemaphoreHandle_t sem;
  static esp_timer_handle_t timer;
  if (!timer) {
    sem = xSemaphoreCreateBinary();
    esp_timer_create_args_t args = { .callback = sp_esp_sleep_wake, .arg = sem, .name = "sleep" };
    if (!sem || esp_timer_create(&args, &timer) != ESP_OK) { errno = ENOMEM; return -1; }
  }
  int64_t end = esp_timer_get_time() + (int64_t)req->tv_sec * 1000000 + (req->tv_nsec + 999) / 1000;
  for (int64_t left; (left = end - esp_timer_get_time()) > 0;) {
    esp_timer_start_once(timer, (uint64_t)left);
    xSemaphoreTake(sem, portMAX_DELAY);
  }
  if (rem) { rem->tv_sec = 0; rem->tv_nsec = 0; }
  return 0;
}
/* no signal ever comes (sleep with no argument) */
int pause(void) {
  for (;;) vTaskDelay(portMAX_DELAY);
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

/* the generated C's empty inspector table: no kind indexes it */
const char *sp_esp_insp_none(void *obj, const uint8_t *nb) {
  (void)obj; (void)nb;
  abort();
}
