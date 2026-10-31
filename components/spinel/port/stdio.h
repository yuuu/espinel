/* newlib has getline/getdelim only as __getline/__getdelim and leaves them
   out of <stdio.h>; sp_esp.c forwards the POSIX names to them. */
#ifndef SP_ESP_STDIO_H
#define SP_ESP_STDIO_H
#include_next <stdio.h>
#include <sys/types.h>
ssize_t getline(char **, size_t *, FILE *);
ssize_t getdelim(char **, size_t *, int, FILE *);
#endif
