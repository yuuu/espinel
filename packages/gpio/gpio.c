/* ESP-IDF's GPIO behind plain-int functions, so the Ruby side binds them
   with ffi_func without copying enum values. */
#include "driver/gpio.h"

int espinel_gpio_mode_input(void) { return GPIO_MODE_INPUT; }
int espinel_gpio_mode_output(void) { return GPIO_MODE_OUTPUT; }
int espinel_gpio_mode_input_output(void) { return GPIO_MODE_INPUT_OUTPUT; }

int espinel_gpio_pull_none(void) { return GPIO_FLOATING; }
int espinel_gpio_pull_up(void) { return GPIO_PULLUP_ONLY; }
int espinel_gpio_pull_down(void) { return GPIO_PULLDOWN_ONLY; }

/* gpio_reset_pin leaves the pull-up on, so the pull mode is always set:
   a pin given no pull floats. */
int espinel_gpio_setup(int pin, int mode, int pull) {
  esp_err_t err = gpio_reset_pin((gpio_num_t)pin);
  if (err != ESP_OK) return err;
  err = gpio_set_direction((gpio_num_t)pin, (gpio_mode_t)mode);
  if (err != ESP_OK) return err;
  return gpio_set_pull_mode((gpio_num_t)pin, (gpio_pull_mode_t)pull);
}

int espinel_gpio_write(int pin, int level) {
  return gpio_set_level((gpio_num_t)pin, (uint32_t)level);
}

int espinel_gpio_read(int pin) { return gpio_get_level((gpio_num_t)pin); }
