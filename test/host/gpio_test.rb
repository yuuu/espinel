# The Ruby side of the gpio package over test/host/stubs/gpio_stub.c.
require "gpio"
require_relative "support/assert"

module GPIOStub
  ffi_func :stub_gpio_fail_setup, [:int], :void
  ffi_func :stub_gpio_fail_write, [:int], :void
  ffi_func :stub_gpio_setup_arg, [:int], :int
end

ESP_ERR_INVALID_ARG = 0x102

def setup_args = (0..2).map { |i| GPIOStub.stub_gpio_setup_arg(i) }

test("the mode and pull constants are ESP-IDF's, from the C side") do
  assert_equal [1, 2, 3], [GPIO::IN, GPIO::OUT, GPIO::IN_OUT]
  assert_equal [3, 0, 1], [GPIO::PULL_NONE, GPIO::PULL_UP, GPIO::PULL_DOWN]
end

test("setup passes the pin, mode and pull, no pull by default") do
  GPIO.new(5, GPIO::IN, GPIO::PULL_DOWN)
  assert_equal [5, GPIO::IN, GPIO::PULL_DOWN], setup_args
  pin = GPIO.new(21, GPIO::OUT)
  assert_equal 21, pin.pin
  assert_equal [21, GPIO::OUT, GPIO::PULL_NONE], setup_args
end

test("a refused setup raises ArgumentError") do
  GPIOStub.stub_gpio_fail_setup(ESP_ERR_INVALID_ARG)
  message = assert_raises(ArgumentError) { GPIO.new(99, GPIO::OUT) }
  assert_equal "GPIO99: setup failed (esp_err_t 258)", message
end

test("write answers the level; high, low and high? go through it") do
  pin = GPIO.new(2, GPIO::IN_OUT)
  assert_equal 1, pin.write(1)
  assert_equal 1, pin.read
  assert pin.high?, "high after write(1)"
  pin.low
  assert_equal 0, pin.read
  assert !pin.high?, "low after low"
  pin.high
  assert pin.high?, "high after high"
end

test("a refused write raises") do
  pin = GPIO.new(3, GPIO::OUT)
  GPIOStub.stub_gpio_fail_write(ESP_ERR_INVALID_ARG)
  assert_raises(ArgumentError) { pin.write(1) }
end

finish
