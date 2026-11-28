/* A quadrature encoder on ESP-IDF's PCNT peripheral, behind plain-int
   functions so the Ruby side binds them with ffi_func. Each PulseCounter
   takes one PCNT unit (4 on the ESP32-S3) with two channels that count
   every edge of both phases (x4). The unit's counter is 16 bits; the driver
   adds it up each time it reaches +-ESPINEL_PCNT_LIMIT (accum_count), so
   the count keeps going past that. Chips without a PCNT (the ESP32-C2 and
   -C3) answer ESP_ERR_NOT_SUPPORTED from the setup.

   Accumulating takes an interrupt per unit, all on the one PCNT source,
   which ESP-IDF shares only among handlers on one core. The Ruby task can
   run on either core, so a unit set up after it moved would fail to get its
   interrupt (pcnt_new_unit: "install interrupt service failed"); the setup
   runs on a task pinned to core 0 instead. */
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_err.h"
#include "soc/soc_caps.h"
#if SOC_PCNT_SUPPORTED
#include "driver/pulse_cnt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

#define ESPINEL_PCNT_LIMIT 10000
/* more than any chip has units; pcnt_new_unit says when they run out */
#define ESPINEL_PCNT_SLOTS 8

typedef struct {
  bool used;
#if SOC_PCNT_SUPPORTED
  pcnt_unit_handle_t unit; /* NULL until set up */
  pcnt_channel_handle_t chan[2];
  int watch_points;        /* added so far, of the two */
  bool enabled;
#endif
} pcnt_slot_t;

/* The units are the firmware's, as the pins are, so the table is shared
   by every program rather than kept per VM. */
static pcnt_slot_t slots[ESPINEL_PCNT_SLOTS];

/* A free slot, or -1 when all are taken. */
int espinel_pulse_counter_new(void) {
  for (int s = 0; s < ESPINEL_PCNT_SLOTS; s++) {
    if (!slots[s].used) {
      slots[s] = (pcnt_slot_t){ .used = true };
      return s;
    }
  }
  return -1;
}

static bool valid(int s) { return s >= 0 && s < ESPINEL_PCNT_SLOTS && slots[s].used; }

/* Gives slot s back, its unit deleted and the pins let go. */
void espinel_pulse_counter_close(int s) {
  if (!valid(s)) return;
#if SOC_PCNT_SUPPORTED
  pcnt_slot_t *p = &slots[s];
  /* undoes only what the setup got to, so the driver logs no errors */
  if (p->unit) {
    if (p->enabled) {
      pcnt_unit_stop(p->unit);
      pcnt_unit_disable(p->unit);
    }
    if (p->watch_points > 0) pcnt_unit_remove_watch_point(p->unit, ESPINEL_PCNT_LIMIT);
    if (p->watch_points > 1) pcnt_unit_remove_watch_point(p->unit, -ESPINEL_PCNT_LIMIT);
    for (int i = 0; i < 2; i++) {
      if (p->chan[i]) pcnt_del_channel(p->chan[i]);
    }
    pcnt_del_unit(p->unit);
  }
#endif
  slots[s] = (pcnt_slot_t){ 0 };
}

#if SOC_PCNT_SUPPORTED
/* One phase's channel: counts on its edges, the direction set by the other
   phase's level. Channel A goes down on a rising edge while B is low,
   channel B up on a rising edge while A is low (the other phase high
   reverses them), so B leading A counts up: PicoRuby's direction. */
static esp_err_t add_channel(pcnt_slot_t *p, int i, int edge_pin, int level_pin) {
  pcnt_chan_config_t cfg = { .edge_gpio_num = edge_pin, .level_gpio_num = level_pin };
  esp_err_t err = pcnt_new_channel(p->unit, &cfg, &p->chan[i]);
  if (err != ESP_OK) return err;
  /* the actions on a rising and a falling edge while the other phase is low */
  pcnt_channel_edge_action_t up = PCNT_CHANNEL_EDGE_ACTION_INCREASE;
  pcnt_channel_edge_action_t down = PCNT_CHANNEL_EDGE_ACTION_DECREASE;
  err = pcnt_channel_set_edge_action(p->chan[i], i == 0 ? down : up, i == 0 ? up : down);
  if (err != ESP_OK) return err;
  return pcnt_channel_set_level_action(p->chan[i], PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
}

static esp_err_t setup(pcnt_slot_t *p, int pin_a, int pin_b, int glitch_ns, int pull_up) {
  pcnt_unit_config_t unit_cfg = {
    .low_limit = -ESPINEL_PCNT_LIMIT,
    .high_limit = ESPINEL_PCNT_LIMIT,
    .flags.accum_count = true,
  };
  esp_err_t err = pcnt_new_unit(&unit_cfg, &p->unit);
  if (err != ESP_OK) {
    p->unit = NULL;
    return err;
  }
  if (glitch_ns > 0) {
    pcnt_glitch_filter_config_t filter = { .max_glitch_ns = (uint32_t)glitch_ns };
    err = pcnt_unit_set_glitch_filter(p->unit, &filter);
    if (err != ESP_OK) return err;
  }
  err = add_channel(p, 0, pin_a, pin_b);
  if (err != ESP_OK) return err;
  err = add_channel(p, 1, pin_b, pin_a);
  if (err != ESP_OK) return err;
  /* accumulating needs a watch point at each limit */
  err = pcnt_unit_add_watch_point(p->unit, ESPINEL_PCNT_LIMIT);
  if (err != ESP_OK) return err;
  p->watch_points = 1;
  err = pcnt_unit_add_watch_point(p->unit, -ESPINEL_PCNT_LIMIT);
  if (err != ESP_OK) return err;
  p->watch_points = 2;
  /* after the channels, which set the pins up as inputs; the pull-ups let
     open-collector encoder outputs be read */
  if (pull_up) {
    gpio_pullup_en((gpio_num_t)pin_a);
    gpio_pullup_en((gpio_num_t)pin_b);
  }
  err = pcnt_unit_enable(p->unit);
  if (err != ESP_OK) return err;
  p->enabled = true;
  err = pcnt_unit_clear_count(p->unit);
  if (err != ESP_OK) return err;
  return pcnt_unit_start(p->unit);
}

typedef struct {
  pcnt_slot_t *p;
  int pin_a, pin_b, glitch_ns, pull_up;
  esp_err_t err;
  SemaphoreHandle_t done;
} setup_job_t;

static void setup_task(void *arg) {
  setup_job_t *job = arg;
  job->err = setup(job->p, job->pin_a, job->pin_b, job->glitch_ns, job->pull_up);
  xSemaphoreGive(job->done);
  vTaskDelete(NULL);
}

/* setup on core 0, the caller waiting for it */
static esp_err_t setup_on_core0(pcnt_slot_t *p, int pin_a, int pin_b, int glitch_ns, int pull_up) {
  setup_job_t job = { p, pin_a, pin_b, glitch_ns, pull_up, ESP_OK, xSemaphoreCreateBinary() };
  if (!job.done) return ESP_ERR_NO_MEM;
  if (xTaskCreatePinnedToCore(setup_task, "pcnt_setup", 4096, &job, uxTaskPriorityGet(NULL), NULL, 0) != pdPASS) {
    vSemaphoreDelete(job.done);
    return ESP_ERR_NO_MEM;
  }
  xSemaphoreTake(job.done, portMAX_DELAY);
  vSemaphoreDelete(job.done);
  return job.err;
}
#endif

/* Starts slot s counting the A/B phases on pin_a and pin_b; glitch_ns 0
   turns the glitch filter off. A slot whose setup failed is left for
   espinel_pulse_counter_close to clean up. */
int espinel_pulse_counter_setup(int s, int pin_a, int pin_b, int glitch_ns, int pull_up) {
  if (!valid(s) || glitch_ns < 0) return ESP_ERR_INVALID_ARG;
  /* the macro shifts 1ULL by pin, so the range comes first */
  if (pin_a < 0 || pin_a >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(pin_a)) return ESP_ERR_INVALID_ARG;
  if (pin_b < 0 || pin_b >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(pin_b)) return ESP_ERR_INVALID_ARG;
  if (pin_a == pin_b) return ESP_ERR_INVALID_ARG;
#if SOC_PCNT_SUPPORTED
  return setup_on_core0(&slots[s], pin_a, pin_b, glitch_ns, pull_up);
#else
  (void)pull_up;
  return ESP_ERR_NOT_SUPPORTED;
#endif
}

/* The accumulated count of slot s, 0 for a slot not counting. */
int espinel_pulse_counter_count(int s) {
  int count = 0;
#if SOC_PCNT_SUPPORTED
  if (valid(s) && slots[s].unit) pcnt_unit_get_count(slots[s].unit, &count);
#endif
  return count;
}

/* Sets slot s's count, the accumulated part included, back to 0. */
int espinel_pulse_counter_clear(int s) {
#if SOC_PCNT_SUPPORTED
  if (valid(s) && slots[s].unit) return pcnt_unit_clear_count(slots[s].unit);
#endif
  return ESP_ERR_INVALID_STATE;
}

int espinel_pulse_counter_not_found(void) { return ESP_ERR_NOT_FOUND; }
int espinel_pulse_counter_not_supported(void) { return ESP_ERR_NOT_SUPPORTED; }
