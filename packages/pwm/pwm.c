/* PWM on ESP-IDF's LEDC peripheral, behind plain-int and double functions
   so the Ruby side binds them with ffi_func. Each PWM object takes one
   LEDC channel; the channels share the timers (4) by frequency, so as
   many outputs as there are channels can run at up to four frequencies.
   The duty resolution is the finest the frequency allows (14 bits at most
   on the ESP32-S3 and -C3). */
#include <math.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "driver/ledc.h"

#define ESPINEL_PWM_MODE LEDC_LOW_SPEED_MODE
/* the clock the duty resolution is found against: APB, 80 MHz, which
   LEDC_AUTO_CLK takes for any frequency a resolution of 1 bit or more
   fits */
#define ESPINEL_PWM_SRC_HZ 80000000U

typedef struct {
  int refs;      /* channels running on it */
  uint32_t freq; /* Hz */
  int bits;      /* duty resolution */
} pwm_timer_t;

typedef struct {
  bool used;
  bool configured; /* ledc_channel_config done */
  int pin;
  int timer;       /* -1 while stopped */
} pwm_channel_t;

static pwm_timer_t timers[LEDC_TIMER_MAX];
static pwm_channel_t channels[LEDC_CHANNEL_MAX];

/* A free channel for pin, not started yet, or -1 when all are taken. */
int espinel_pwm_open(int pin) {
  /* the macro shifts 1ULL by pin, so the range comes first */
  if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_OUTPUT_GPIO(pin)) return -2;
  for (int c = 0; c < LEDC_CHANNEL_MAX; c++) {
    if (!channels[c].used) {
      channels[c] = (pwm_channel_t){ .used = true, .pin = pin, .timer = -1 };
      return c;
    }
  }
  return -1;
}

static void release_timer(int c) {
  int t = channels[c].timer;
  if (t >= 0) timers[t].refs--;
  channels[c].timer = -1;
}

/* Stops channel c, low, and gives it back. */
void espinel_pwm_close(int c) {
  if (c < 0 || c >= LEDC_CHANNEL_MAX || !channels[c].used) return;
  release_timer(c);
  if (channels[c].configured) ledc_stop(ESPINEL_PWM_MODE, (ledc_channel_t)c, 0);
  channels[c] = (pwm_channel_t){ .timer = -1 };
}

/* A timer running at freq, one already doing so or a free one set up for
   it; -1 when none is left. */
static esp_err_t take_timer(uint32_t freq, int *ret) {
  for (int t = 0; t < LEDC_TIMER_MAX; t++) {
    if (timers[t].refs > 0 && timers[t].freq == freq) {
      timers[t].refs++;
      *ret = t;
      return ESP_OK;
    }
  }
  for (int t = 0; t < LEDC_TIMER_MAX; t++) {
    if (timers[t].refs == 0) {
      uint32_t bits = ledc_find_suitable_duty_resolution(ESPINEL_PWM_SRC_HZ, freq);
      if (bits == 0) return ESP_ERR_INVALID_ARG; /* faster than the clock allows */
      if (bits > SOC_LEDC_TIMER_BIT_WIDTH) bits = SOC_LEDC_TIMER_BIT_WIDTH;
      ledc_timer_config_t cfg = {
        .speed_mode = ESPINEL_PWM_MODE,
        .duty_resolution = (ledc_timer_bit_t)bits,
        .timer_num = (ledc_timer_t)t,
        .freq_hz = freq,
        .clk_cfg = LEDC_AUTO_CLK,
      };
      esp_err_t err = ledc_timer_config(&cfg);
      if (err != ESP_OK) return err;
      timers[t] = (pwm_timer_t){ .refs = 1, .freq = freq, .bits = (int)bits };
      *ret = t;
      return ESP_OK;
    }
  }
  return ESP_ERR_NOT_FOUND;
}

static uint32_t raw_duty(int t, double fraction) {
  if (fraction < 0) fraction = 0;
  if (fraction > 1) fraction = 1;
  return (uint32_t)lround(fraction * (double)(1U << timers[t].bits));
}

/* Starts channel c at freq Hz with duty a fraction from 0 to 1, or changes
   them while it runs. freq 0 stops it, low, as does a frequency no timer
   can be had for. */
int espinel_pwm_set(int c, int freq, double duty) {
  if (c < 0 || c >= LEDC_CHANNEL_MAX || !channels[c].used || freq < 0)
    return ESP_ERR_INVALID_ARG;
  pwm_channel_t *ch = &channels[c];
  if (freq == 0) {
    release_timer(c);
    return ch->configured ? ledc_stop(ESPINEL_PWM_MODE, (ledc_channel_t)c, 0) : ESP_OK;
  }
  int old = ch->timer;
  if (old < 0 || timers[old].freq != (uint32_t)freq) {
    int t;
    /* let go of the old timer first, so a channel alone on it can have it
       set to the new frequency */
    release_timer(c);
    esp_err_t err = take_timer((uint32_t)freq, &t);
    if (err != ESP_OK) {
      /* its old timer may be another frequency's by now: stop, low */
      if (ch->configured) ledc_stop(ESPINEL_PWM_MODE, (ledc_channel_t)c, 0);
      return err;
    }
    ch->timer = t;
  }
  int t = ch->timer;
  if (!ch->configured) {
    ledc_channel_config_t cfg = {
      .gpio_num = ch->pin,
      .speed_mode = ESPINEL_PWM_MODE,
      .channel = (ledc_channel_t)c,
      .timer_sel = (ledc_timer_t)t,
      .duty = raw_duty(t, duty),
      .hpoint = 0,
    };
    esp_err_t err = ledc_channel_config(&cfg);
    if (err != ESP_OK) return err;
    ch->configured = true;
    return ESP_OK;
  }
  esp_err_t err = ledc_bind_channel_timer(ESPINEL_PWM_MODE, (ledc_channel_t)c, (ledc_timer_t)t);
  if (err != ESP_OK) return err;
  err = ledc_set_duty(ESPINEL_PWM_MODE, (ledc_channel_t)c, raw_duty(t, duty));
  if (err != ESP_OK) return err;
  /* also starts the output again after a stop */
  return ledc_update_duty(ESPINEL_PWM_MODE, (ledc_channel_t)c);
}

/* the duty resolution channel c runs at, in bits, or 0 while stopped */
int espinel_pwm_resolution(int c) {
  if (c < 0 || c >= LEDC_CHANNEL_MAX || channels[c].timer < 0) return 0;
  return timers[channels[c].timer].bits;
}

int espinel_pwm_not_found(void) { return ESP_ERR_NOT_FOUND; }
