/* ESP-IDF's I2C master driver (driver/i2c_master.h) behind plain-int
   functions, so the Ruby side binds them with ffi_func. Each port has one
   bus; the driver wants a device handle per address, made the first time
   an address is used and kept until the bus is opened again. */
#include <stddef.h>
#include "driver/i2c_master.h"

#define ESPINEL_I2C_ADDRS 128

static i2c_master_bus_handle_t buses[SOC_I2C_NUM];
static i2c_master_dev_handle_t devices[SOC_I2C_NUM][ESPINEL_I2C_ADDRS];
static uint32_t speeds[SOC_I2C_NUM];

static int valid_port(int port) { return port >= 0 && port < SOC_I2C_NUM; }

int espinel_i2c_port_count(void) { return SOC_I2C_NUM; }

static void close_bus(int port) {
  for (int a = 0; a < ESPINEL_I2C_ADDRS; a++) {
    if (devices[port][a]) {
      i2c_master_bus_rm_device(devices[port][a]);
      devices[port][a] = NULL;
    }
  }
  if (buses[port]) {
    i2c_del_master_bus(buses[port]);
    buses[port] = NULL;
  }
}

/* A bus on SCL and SDA at frequency Hz, with the internal pull-ups on
   (weak: a bus faster than 100 kHz wants external ones). A bus the port
   had already is taken down first. */
int espinel_i2c_open(int port, int scl, int sda, int frequency) {
  if (!valid_port(port) || frequency <= 0) return ESP_ERR_INVALID_ARG;
  close_bus(port);
  i2c_master_bus_config_t cfg = {
    .i2c_port = port,
    .sda_io_num = sda,
    .scl_io_num = scl,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,
    .flags.enable_internal_pullup = 1,
  };
  esp_err_t err = i2c_new_master_bus(&cfg, &buses[port]);
  if (err != ESP_OK) {
    buses[port] = NULL;
    return err;
  }
  speeds[port] = (uint32_t)frequency;
  return ESP_OK;
}

static esp_err_t device(int port, int addr, i2c_master_dev_handle_t *ret) {
  if (!valid_port(port) || !buses[port] || addr < 0 || addr >= ESPINEL_I2C_ADDRS)
    return ESP_ERR_INVALID_ARG;
  if (!devices[port][addr]) {
    i2c_device_config_t cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = (uint16_t)addr,
      .scl_speed_hz = speeds[port],
    };
    esp_err_t err = i2c_master_bus_add_device(buses[port], &cfg, &devices[port][addr]);
    if (err != ESP_OK) {
      devices[port][addr] = NULL;
      return err;
    }
  }
  *ret = devices[port][addr];
  return ESP_OK;
}

/* S, addr W, the len bytes of data, P */
int espinel_i2c_write(int port, int addr, const char *data, int len, int timeout_ms) {
  i2c_master_dev_handle_t dev;
  esp_err_t err = device(port, addr, &dev);
  if (err != ESP_OK) return err;
  return i2c_master_transmit(dev, (const uint8_t *)data, (size_t)len, timeout_ms);
}

/* With out_len 0: S, addr R, len bytes into buf, P. Otherwise the out_len
   bytes of out are written first and a repeated start comes before the
   read: S, addr W, out, Sr, addr R, len bytes, P. */
int espinel_i2c_read(int port, int addr, const char *out, int out_len, void *buf,
                     int len, int timeout_ms) {
  i2c_master_dev_handle_t dev;
  esp_err_t err = device(port, addr, &dev);
  if (err != ESP_OK) return err;
  if (out_len == 0)
    return i2c_master_receive(dev, (uint8_t *)buf, (size_t)len, timeout_ms);
  return i2c_master_transmit_receive(dev, (const uint8_t *)out, (size_t)out_len,
                                     (uint8_t *)buf, (size_t)len, timeout_ms);
}

/* ESP_OK when a device answers addr with an ACK, ESP_ERR_NOT_FOUND on a
   NACK */
int espinel_i2c_probe(int port, int addr, int timeout_ms) {
  if (!valid_port(port) || !buses[port] || addr < 0 || addr >= ESPINEL_I2C_ADDRS)
    return ESP_ERR_INVALID_ARG;
  return i2c_master_probe(buses[port], (uint16_t)addr, timeout_ms);
}

int espinel_i2c_not_found(void) { return ESP_ERR_NOT_FOUND; }
