# GPIO on ESP-IDF: a pin set up as input, output or both, written and read
# as 0/1. The C half is gpio.c in this package.
module ESPGPIO
  ffi_func :espinel_gpio_mode_input, [], :int
  ffi_func :espinel_gpio_mode_output, [], :int
  ffi_func :espinel_gpio_mode_input_output, [], :int
  ffi_func :espinel_gpio_setup, [:int, :int], :int
  ffi_func :espinel_gpio_write, [:int, :int], :int
  ffi_func :espinel_gpio_read, [:int], :int
end

class GPIO
  IN = ESPGPIO.espinel_gpio_mode_input
  OUT = ESPGPIO.espinel_gpio_mode_output
  IN_OUT = ESPGPIO.espinel_gpio_mode_input_output

  HIGH = 1
  LOW = 0

  attr_reader :pin

  def initialize(pin, mode)
    @pin = pin
    err = ESPGPIO.espinel_gpio_setup(pin, mode)
    raise ArgumentError, "GPIO#{pin}: setup failed (esp_err_t #{err})" if err != 0
  end

  def write(level)
    err = ESPGPIO.espinel_gpio_write(@pin, level)
    raise ArgumentError, "GPIO#{@pin}: write failed (esp_err_t #{err})" if err != 0
    level
  end

  def high = write(HIGH)
  def low = write(LOW)
  def read = ESPGPIO.espinel_gpio_read(@pin)
  def high? = read == HIGH
end
