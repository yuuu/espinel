/* ESP-IDF's SPI master driver behind plain-int functions, so the Ruby side
   binds them with ffi_func. A unit is the SPI peripheral's number in the
   reference manual, SPI2 or SPI3 (SPI1 holds the flash); each has one bus
   and one device on it, added again whenever the mode changes. */
#include <stdbool.h>
#include <stddef.h>
#include "driver/spi_master.h"

#define ESPINEL_SPI_FIRST_UNIT 2
#define ESPINEL_SPI_LAST_UNIT SOC_SPI_PERIPH_NUM
#define ESPINEL_SPI_UNITS (ESPINEL_SPI_LAST_UNIT - ESPINEL_SPI_FIRST_UNIT + 1)
/* the most one transaction carries; a longer transfer is split, with the
   chip select held between the pieces */
#define ESPINEL_SPI_CHUNK 4092

static bool opened[ESPINEL_SPI_UNITS];
static spi_device_handle_t devices[ESPINEL_SPI_UNITS];

static int valid_unit(int unit) {
  return unit >= ESPINEL_SPI_FIRST_UNIT && unit <= ESPINEL_SPI_LAST_UNIT;
}
/* SPI2 is SPI2_HOST and so on */
static spi_host_device_t host(int unit) { return (spi_host_device_t)(SPI2_HOST + unit - 2); }
static int slot(int unit) { return unit - ESPINEL_SPI_FIRST_UNIT; }

int espinel_spi_first_unit(void) { return ESPINEL_SPI_FIRST_UNIT; }
int espinel_spi_last_unit(void) { return ESPINEL_SPI_LAST_UNIT; }

static void close_unit(int unit) {
  int i = slot(unit);
  if (devices[i]) {
    spi_bus_remove_device(devices[i]);
    devices[i] = NULL;
  }
  if (opened[i]) {
    spi_bus_free(host(unit));
    opened[i] = false;
  }
}

/* The bus on SCK, COPI (MOSI) and CIPO (MISO); a pin below 0 is not used.
   A bus the unit had already is taken down first. */
int espinel_spi_open(int unit, int sck, int copi, int cipo) {
  if (!valid_unit(unit)) return ESP_ERR_INVALID_ARG;
  close_unit(unit);
  spi_bus_config_t cfg = {
    .sclk_io_num = sck,
    .mosi_io_num = copi,
    .miso_io_num = cipo,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = ESPINEL_SPI_CHUNK,
  };
  esp_err_t err = spi_bus_initialize(host(unit), &cfg, SPI_DMA_CH_AUTO);
  if (err == ESP_OK) opened[slot(unit)] = true;
  return err;
}

/* The device: clock in Hz, SPI mode 0 to 3, bit order, and the chip select
   the hardware drives low for each transfer (below 0: none, the program
   drives one itself). */
int espinel_spi_device(int unit, int frequency, int mode, int lsb_first, int cs) {
  if (!valid_unit(unit) || !opened[slot(unit)] || frequency <= 0 || mode < 0 || mode > 3)
    return ESP_ERR_INVALID_ARG;
  int i = slot(unit);
  if (devices[i]) {
    spi_bus_remove_device(devices[i]);
    devices[i] = NULL;
  }
  spi_device_interface_config_t cfg = {
    .mode = (uint8_t)mode,
    .clock_speed_hz = frequency,
    .spics_io_num = cs,
    .queue_size = 1,
    .flags = lsb_first ? SPI_DEVICE_BIT_LSBFIRST : 0,
  };
  esp_err_t err = spi_bus_add_device(host(unit), &cfg, &devices[i]);
  if (err != ESP_OK) devices[i] = NULL;
  return err;
}

/* Clocks out the len bytes of tx while clocking len bytes into rx (NULL:
   the bytes read are dropped). */
int espinel_spi_transfer(int unit, const char *tx, void *rx, int len) {
  if (!valid_unit(unit) || !devices[slot(unit)] || len < 0) return ESP_ERR_INVALID_ARG;
  spi_device_handle_t dev = devices[slot(unit)];
  bool split = len > ESPINEL_SPI_CHUNK;
  if (split) {
    esp_err_t err = spi_device_acquire_bus(dev, portMAX_DELAY);
    if (err != ESP_OK) return err;
  }
  esp_err_t err = ESP_OK;
  for (int done = 0; done < len && err == ESP_OK;) {
    int n = len - done > ESPINEL_SPI_CHUNK ? ESPINEL_SPI_CHUNK : len - done;
    spi_transaction_t t = {
      .flags = done + n < len ? SPI_TRANS_CS_KEEP_ACTIVE : 0,
      .length = (size_t)n * 8,
      .tx_buffer = tx + done,
      .rx_buffer = rx ? (uint8_t *)rx + done : NULL,
    };
    err = spi_device_polling_transmit(dev, &t);
    done += n;
  }
  if (split) spi_device_release_bus(dev);
  return err;
}
