# GPIO on ESP-IDF: a pin set up as input, output or both, with its pull-up
# or pull-down resistor on or neither, written and read as 0/1. The C half
# is gpio.c in this package.
module ESPGPIO
  ffi_func :espinel_gpio_mode_input, [], :int
  ffi_func :espinel_gpio_mode_output, [], :int
  ffi_func :espinel_gpio_mode_input_output, [], :int
  ffi_func :espinel_gpio_pull_none, [], :int
  ffi_func :espinel_gpio_pull_up, [], :int
  ffi_func :espinel_gpio_pull_down, [], :int
  ffi_func :espinel_gpio_setup, [:int, :int, :int], :int
  ffi_func :espinel_gpio_write, [:int, :int], :int
  ffi_func :espinel_gpio_read, [:int], :int
end

class GPIO
  IN = ESPGPIO.espinel_gpio_mode_input
  OUT = ESPGPIO.espinel_gpio_mode_output
  IN_OUT = ESPGPIO.espinel_gpio_mode_input_output

  PULL_NONE = ESPGPIO.espinel_gpio_pull_none
  PULL_UP = ESPGPIO.espinel_gpio_pull_up
  PULL_DOWN = ESPGPIO.espinel_gpio_pull_down

  HIGH = 1
  LOW = 0

  attr_reader :pin

  def initialize(pin, mode, pull = PULL_NONE)
    @pin = pin
    err = ESPGPIO.espinel_gpio_setup(pin, mode, pull)
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
