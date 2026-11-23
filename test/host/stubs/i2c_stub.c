/* Host stand-in for packages/i2c/i2c.c: two ports, and a bus on which the
   addresses given to stub_i2c_attach answer. A read answers addr, addr + 1,
   and so on. It records the last write's or read's address, bytes and
   timeout, and stub_i2c_fail_* makes the next call of that kind answer an
   error. */
#include <string.h>
#include "stub.h"

static int open_args[4];
static int fail_open, fail_write, fail_read, fail_probe;
static int last_addr, last_timeout, sent_len, probes;
static unsigned char sent[STUB_BYTES];
static unsigned char attached[128];

static int take(int *fail) {
  int err = *fail;
  *fail = ESP_OK;
  return err;
}

static void record(int addr, const char *data, int len, int timeout_ms) {
  last_addr = addr;
  last_timeout = timeout_ms;
  sent_len = len;
  memcpy(sent, data, len < STUB_BYTES ? len : STUB_BYTES);
}

int espinel_i2c_port_count(void) { return 2; }
int espinel_i2c_not_found(void) { return ESP_ERR_NOT_FOUND; }

int espinel_i2c_open(int port, int scl, int sda, int frequency) {
  int a[4] = { port, scl, sda, frequency };
  memcpy(open_args, a, sizeof a);
  return take(&fail_open);
}

int espinel_i2c_write(int port, int addr, const char *data, int len, int timeout_ms) {
  (void)port;
  record(addr, data, len, timeout_ms);
  return take(&fail_write);
}

int espinel_i2c_read(int port, int addr, const char *out, int out_len, void *buf, int len, int timeout_ms) {
  (void)port;
  record(addr, out, out_len, timeout_ms);
  int err = take(&fail_read);
  if (err == ESP_OK)
    for (int i = 0; i < len; i++) ((unsigned char *)buf)[i] = (unsigned char)(addr + i);
  return err;
}

int espinel_i2c_probe(int port, int addr, int timeout_ms) {
  (void)port;
  probes++;
  record(addr, "", 0, timeout_ms);
  int err = take(&fail_probe);
  if (err != ESP_OK) return err;
  return attached[addr & 0x7f] ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/* test-only */
void stub_i2c_attach(int addr) { attached[addr & 0x7f] = 1; }
void stub_i2c_fail_open(int err) { fail_open = err; }
void stub_i2c_fail_write(int err) { fail_write = err; }
void stub_i2c_fail_read(int err) { fail_read = err; }
void stub_i2c_fail_probe(int err) { fail_probe = err; }
int stub_i2c_open_arg(int i) { return open_args[i]; }
int stub_i2c_last_addr(void) { return last_addr; }
int stub_i2c_last_timeout(void) { return last_timeout; }
int stub_i2c_sent_len(void) { return sent_len; }
int stub_i2c_sent_byte(int i) { return sent[i]; }
int stub_i2c_probes(void) { return probes; }
