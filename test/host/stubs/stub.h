/* Shared by the host stubs: the esp_err_t values they answer, as ESP-IDF
   defines them (esp_err.h), and the bytes a stub records. */
#ifndef ESPINEL_HOST_STUB_H
#define ESPINEL_HOST_STUB_H

#define ESP_OK                0
#define ESP_FAIL              -1
#define ESP_ERR_INVALID_ARG   0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND     0x105
#define ESP_ERR_TIMEOUT       0x107

/* the most bytes a stub keeps of one call */
#define STUB_BYTES 256

#endif
