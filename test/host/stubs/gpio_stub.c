/* Host stand-in for packages/gpio/gpio.c: ESP-IDF's mode and pull values,
   and pins whose level is what was last written (an output wired back to
   its input). It records the last setup, and stub_gpio_fail_* makes the
   next call of that kind answer an error. */
#include "stub.h"

static int setup_args[3];
static int fail_setup, fail_write;
static int levels[64];

static int take(int *fail) {
  int err = *fail;
  *fail = ESP_OK;
  return err;
}

/* gpio_mode_t and gpio_pull_mode_t */
int espinel_gpio_mode_input(void) { return 1; }
int espinel_gpio_mode_output(void) { return 2; }
int espinel_gpio_mode_input_output(void) { return 3; }
int espinel_gpio_pull_none(void) { return 3; }
int espinel_gpio_pull_up(void) { return 0; }
int espinel_gpio_pull_down(void) { return 1; }

int espinel_gpio_setup(int pin, int mode, int pull) {
  setup_args[0] = pin;
  setup_args[1] = mode;
  setup_args[2] = pull;
  return take(&fail_setup);
}

int espinel_gpio_write(int pin, int level) {
  int err = take(&fail_write);
  if (err == ESP_OK) levels[pin & 63] = level != 0;
  return err;
}

int espinel_gpio_read(int pin) { return levels[pin & 63]; }

/* test-only */
void stub_gpio_fail_setup(int err) { fail_setup = err; }
void stub_gpio_fail_write(int err) { fail_write = err; }
int stub_gpio_setup_arg(int i) { return setup_args[i]; }
