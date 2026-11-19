/* What the C library's <stdio.h> leaves out. newlib has getline/getdelim
   only as __getline/__getdelim; picolibc (ESP-IDF's default since v6.0) has
   them but no popen/pclose. sp_esp.c supplies the missing ones. */
#ifndef SP_ESP_STDIO_H
#define SP_ESP_STDIO_H
#include_next <stdio.h>
#include <sys/types.h>
#include "sdkconfig.h"
#if CONFIG_LIBC_NEWLIB
ssize_t getline(char **, size_t *, FILE *);
ssize_t getdelim(char **, size_t *, int, FILE *);
#else
FILE *popen(const char *, const char *);
int pclose(FILE *);
#endif
#endif
