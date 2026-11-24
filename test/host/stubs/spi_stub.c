/* Host stand-in for packages/spi/spi.c: SPI2 and SPI3, as on the ESP32-S3.
   It records the arguments of the last open and device call and the bytes
   of the last transfer, and answers each byte sent inverted, so a test can
   tell what came in from what went out. stub_spi_fail_* makes the next call
   of that kind answer an error. */
#include <string.h>
#include "stub.h"

static int open_args[4], device_args[5];
static int fail_open, fail_device, fail_transfer;
static int transfers, sent_len, rx_given;
static unsigned char sent[STUB_BYTES];

static int take(int *fail) {
  int err = *fail;
  *fail = ESP_OK;
  return err;
}

int espinel_spi_first_unit(void) { return 2; }
int espinel_spi_last_unit(void) { return 3; }

int espinel_spi_open(int unit, int sck, int copi, int cipo) {
  int a[4] = { unit, sck, copi, cipo };
  memcpy(open_args, a, sizeof a);
  return take(&fail_open);
}

int espinel_spi_device(int unit, int frequency, int mode, int lsb_first, int cs) {
  int a[5] = { unit, frequency, mode, lsb_first, cs };
  memcpy(device_args, a, sizeof a);
  return take(&fail_device);
}

int espinel_spi_transfer(int unit, const char *tx, void *rx, int len) {
  (void)unit;
  transfers++;
  sent_len = len;
  rx_given = rx != NULL;
  memcpy(sent, tx, len < STUB_BYTES ? len : STUB_BYTES);
  int err = take(&fail_transfer);
  if (err == ESP_OK && rx)
    for (int i = 0; i < len; i++) ((unsigned char *)rx)[i] = (unsigned char)~tx[i];
  return err;
}

/* test-only */
void stub_spi_fail_open(int err) { fail_open = err; }
void stub_spi_fail_device(int err) { fail_device = err; }
void stub_spi_fail_transfer(int err) { fail_transfer = err; }
int stub_spi_open_arg(int i) { return open_args[i]; }
int stub_spi_device_arg(int i) { return device_args[i]; }
int stub_spi_transfers(void) { return transfers; }
int stub_spi_sent_len(void) { return sent_len; }
int stub_spi_sent_byte(int i) { return sent[i]; }
int stub_spi_rx_given(void) { return rx_given; }
