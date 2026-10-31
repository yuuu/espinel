/* ESP-IDF has no virtual memory: mmap answers MAP_FAILED (ENOSYS), so the
   runtime takes its no-mmap paths (the slab off, the program body on the
   task's own stack, no fiber stacks). The definitions are in sp_esp.c. */
#ifndef SP_ESP_SYS_MMAN_H
#define SP_ESP_SYS_MMAN_H
#include <sys/types.h>
#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000
#define MAP_STACK     0x20000
#define MAP_FAILED ((void *)-1)
#define MADV_NORMAL   0
#define MADV_DONTNEED 4
void *mmap(void *, size_t, int, int, int, off_t);
int munmap(void *, size_t);
int mprotect(void *, size_t, int);
int madvise(void *, size_t, int);
#endif
