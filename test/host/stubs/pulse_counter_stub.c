/* Host stand-in for packages/pulse_counter/pulse_counter.c: four units, as
   the ESP32-S3 has, whose counts are what stub_pulse_counter_turn sets.
   It records the last setup, and stub_pulse_counter_fail_setup makes the
   next setup answer an error. */
#include "stub.h"

#define UNITS 4

static int used[UNITS], counting[UNITS], counts[UNITS];
static int setup_args[4];
static int fail_setup, closes;

int espinel_pulse_counter_new(void) {
  for (int s = 0; s < UNITS; s++) {
    if (!used[s]) {
      used[s] = 1;
      return s;
    }
  }
  return -1;
}

void espinel_pulse_counter_close(int s) {
  if (s < 0 || s >= UNITS || !used[s]) return;
  used[s] = counting[s] = counts[s] = 0;
  closes++;
}

int espinel_pulse_counter_setup(int s, int pin_a, int pin_b, int glitch_ns, int pull_up) {
  setup_args[0] = pin_a;
  setup_args[1] = pin_b;
  setup_args[2] = glitch_ns;
  setup_args[3] = pull_up;
  int err = fail_setup;
  fail_setup = ESP_OK;
  if (err == ESP_OK) counting[s] = 1;
  return err;
}

int espinel_pulse_counter_count(int s) { return counting[s] ? counts[s] : 0; }

int espinel_pulse_counter_clear(int s) {
  if (!counting[s]) return ESP_ERR_INVALID_STATE;
  counts[s] = 0;
  return ESP_OK;
}

int espinel_pulse_counter_not_found(void) { return ESP_ERR_NOT_FOUND; }
int espinel_pulse_counter_not_supported(void) { return ESP_ERR_NOT_SUPPORTED; }

/* test-only */
void stub_pulse_counter_turn(int s, int delta) { counts[s] += delta; }
void stub_pulse_counter_fail_setup(int err) { fail_setup = err; }
int stub_pulse_counter_setup_arg(int i) { return setup_args[i]; }
int stub_pulse_counter_closes(void) { return closes; }
