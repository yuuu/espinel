/* ESP-IDF's UART driver behind plain-int functions, so the Ruby side binds
   them with ffi_func without copying enum values. Parity and flow control
   arrive as the package's own codes (UART::NONE and the rest in uart.rb)
   and are turned into ESP-IDF's here. */
#include <stddef.h>
#include <time.h>
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"

/* the codes of uart.rb's UART::NONE, EVEN, ODD and RTSCTS */
enum { ESPINEL_UART_NONE = 0, ESPINEL_UART_EVEN = 1, ESPINEL_UART_ODD = 2,
       ESPINEL_UART_RTSCTS = 3 };

/* No TX ring buffer: a write returns once its bytes are in the hardware
   FIFO, and nothing is left queued in software. (ESP-IDF tells only a
   conservative bound of a ring buffer's free space, not what it holds.) */
#define ESPINEL_UART_RX_BUF 1024

static int valid_port(int port) { return port >= 0 && port < SOC_UART_NUM; }

int espinel_uart_port_count(void) { return SOC_UART_NUM; }

/* Installs the driver unless one is installed already, as on the console
   UART, which then keeps its buffers. */
int espinel_uart_open(int port) {
  if (!valid_port(port)) return ESP_ERR_INVALID_ARG;
  if (uart_is_driver_installed((uart_port_t)port)) return ESP_OK;
  return uart_driver_install((uart_port_t)port, ESPINEL_UART_RX_BUF, 0, 0, NULL, 0);
}

int espinel_uart_config(int port, int baudrate, int data_bits, int stop_bits,
                        int parity, int flow_control) {
  if (!valid_port(port) || baudrate <= 0) return ESP_ERR_INVALID_ARG;
  uart_config_t cfg = { .baud_rate = baudrate, .source_clk = UART_SCLK_DEFAULT };
  switch (data_bits) {
    case 5: cfg.data_bits = UART_DATA_5_BITS; break;
    case 6: cfg.data_bits = UART_DATA_6_BITS; break;
    case 7: cfg.data_bits = UART_DATA_7_BITS; break;
    case 8: cfg.data_bits = UART_DATA_8_BITS; break;
    default: return ESP_ERR_INVALID_ARG;
  }
  switch (stop_bits) {
    case 1: cfg.stop_bits = UART_STOP_BITS_1; break;
    case 2: cfg.stop_bits = UART_STOP_BITS_2; break;
    default: return ESP_ERR_INVALID_ARG;
  }
  switch (parity) {
    case ESPINEL_UART_NONE: cfg.parity = UART_PARITY_DISABLE; break;
    case ESPINEL_UART_EVEN: cfg.parity = UART_PARITY_EVEN; break;
    case ESPINEL_UART_ODD: cfg.parity = UART_PARITY_ODD; break;
    default: return ESP_ERR_INVALID_ARG;
  }
  switch (flow_control) {
    case ESPINEL_UART_NONE: cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE; break;
    case ESPINEL_UART_RTSCTS:
      cfg.flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS;
      /* RTS goes up when the RX FIFO holds this many bytes */
      cfg.rx_flow_ctrl_thresh = 100;
      break;
    default: return ESP_ERR_INVALID_ARG;
  }
  return uart_param_config((uart_port_t)port, &cfg);
}

/* A pin below 0 is left as it is. */
int espinel_uart_set_pin(int port, int txd, int rxd, int rts, int cts) {
  if (!valid_port(port)) return ESP_ERR_INVALID_ARG;
  return uart_set_pin((uart_port_t)port,
                      txd < 0 ? UART_PIN_NO_CHANGE : txd,
                      rxd < 0 ? UART_PIN_NO_CHANGE : rxd,
                      rts < 0 ? UART_PIN_NO_CHANGE : rts,
                      cts < 0 ? UART_PIN_NO_CHANGE : cts);
}

/* Waits until all len bytes are in the hardware FIFO and returns len, or
   -1. */
int espinel_uart_write(int port, const char *data, int len) {
  return uart_write_bytes((uart_port_t)port, data, (size_t)len);
}

/* Waits until len bytes have come and returns len, or -1. */
int espinel_uart_read(int port, void *buf, int len) {
  return uart_read_bytes((uart_port_t)port, buf, (uint32_t)len, portMAX_DELAY);
}

/* the bytes the RX buffer holds, or -1 */
int espinel_uart_available(int port) {
  size_t size;
  if (uart_get_buffered_data_len((uart_port_t)port, &size) != ESP_OK) return -1;
  return (int)size;
}

/* Waits until everything queued has gone out on the line. */
int espinel_uart_flush(int port) {
  return uart_wait_tx_done((uart_port_t)port, portMAX_DELAY);
}

int espinel_uart_clear_rx(int port) {
  return uart_flush_input((uart_port_t)port);
}

/* A break: once everything queued has gone out, TXD is held low (its
   idle level, inverted) for the given microseconds. ESP-IDF's
   uart_write_bytes_with_break needs data to send before the break, and
   caps its length at 255 bit times. */
int espinel_uart_send_break(int port, int us) {
  if (!valid_port(port) || us <= 0) return ESP_ERR_INVALID_ARG;
  esp_err_t err = uart_wait_tx_done((uart_port_t)port, portMAX_DELAY);
  if (err != ESP_OK) return err;
  err = uart_set_line_inverse((uart_port_t)port, UART_SIGNAL_TXD_INV);
  if (err != ESP_OK) return err;
  struct timespec ts = { .tv_sec = us / 1000000, .tv_nsec = (long)(us % 1000000) * 1000 };
  nanosleep(&ts, NULL);
  return uart_set_line_inverse((uart_port_t)port, UART_SIGNAL_INV_DISABLE);
}
