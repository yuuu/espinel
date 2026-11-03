/* Randomness from the chip's random number generator.

   SecureRandom and Random.urandom read /dev/urandom (Spinel's
   sp_crypto_entropy uses getentropy only on WASI and Windows), which
   ESP-IDF does not have, so both raised "no secure random source". A
   read-only device at that path answers with esp_fill_random.

   Kernel#rand seeds itself on first use from time() and clock(), which at
   boot are the same every time; it is seeded from esp_random here instead.
   (Random.new without a seed still mixes only time() and an address, so it
   repeats from boot to boot until the clock is set.)

   esp_random is a true random source while Wi-Fi or Bluetooth runs, or
   while the bootloader's entropy source is on; otherwise it is a
   pseudo-random generator (see ESP-IDF's "Random Number Generation"). */
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "esp_random.h"
#include "esp_vfs.h"

void sp_krand_srand(uint64_t seed);

static int urandom_open(const char *path, int flags, int mode) {
  (void)path; (void)flags; (void)mode;
  return 0;
}
static ssize_t urandom_read(int fd, void *dst, size_t n) {
  (void)fd;
  esp_fill_random(dst, n);
  return (ssize_t)n;
}
static int urandom_close(int fd) { (void)fd; return 0; }
static int urandom_fstat(int fd, struct stat *st) {
  (void)fd;
  memset(st, 0, sizeof *st);
  st->st_mode = S_IFCHR | 0444;
  return 0;
}

void sp_esp_random_init(void) {
  static const esp_vfs_t urandom = {
    .flags = ESP_VFS_FLAG_DEFAULT,
    .open = urandom_open,
    .read = urandom_read,
    .close = urandom_close,
    .fstat = urandom_fstat,
  };
  esp_vfs_register("/dev/urandom", &urandom, NULL);
  sp_krand_srand(((uint64_t)esp_random() << 32) | esp_random());
}
