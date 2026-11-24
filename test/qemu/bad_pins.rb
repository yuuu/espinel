# A pin number past the chip's raises ArgumentError from every peripheral.
# Before ESP-IDF v6, GPIO_IS_VALID_GPIO shifted 1ULL by the pin without a
# bound, so a pin of 64 or more passed its check as a lower one and the
# drivers panicked; v6.1 bounds it by SOC_GPIO_PIN_COUNT. The error codes
# are ESP-IDF's (uart_set_pin answers ESP_FAIL).
require "gpio"
require "pwm"
require "i2c"
require "uart"
require "spi"

[-> { GPIO.new(99, GPIO::OUT) },
 -> { GPIO.new(-1, GPIO::OUT) },
 -> { PWM.new(99, frequency: 1000) },
 -> { I2C.new(scl_pin: 99, sda_pin: 5) },
 -> { I2C.new(scl_pin: 6, sda_pin: 130) },
 -> { UART.new(1, txd_pin: 99, rxd_pin: 2) },
 -> { SPI.new(sck_pin: 99, copi_pin: 9, cipo_pin: 8) },
 -> { SPI.new(sck_pin: 7, copi_pin: 9, cipo_pin: 8, cs_pin: 70) }].each do |f|
  f.call
  puts "no error"
rescue ArgumentError => e
  puts "ArgumentError: #{e.message}"
end
