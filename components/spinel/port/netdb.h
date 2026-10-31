/* lwIP's netdb.h has getaddrinfo but no gai_strerror (sp_esp.c has it). */
#ifndef SP_ESP_NETDB_H
#define SP_ESP_NETDB_H
#include_next <netdb.h>
const char *gai_strerror(int);
#endif
