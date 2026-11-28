# The Ruby side of the pulse_counter package over
# test/host/stubs/pulse_counter_stub.c, which has four units.
require "pulse_counter"
require_relative "support/assert"

module PulseCounterStub
  ffi_func :stub_pulse_counter_turn, [:int, :int], :void
  ffi_func :stub_pulse_counter_fail_setup, [:int], :void
  ffi_func :stub_pulse_counter_setup_arg, [:int], :int
  ffi_func :stub_pulse_counter_closes, [], :int
end

ESP_ERR_INVALID_ARG = 0x102
ESP_ERR_NOT_FOUND = 0x105
ESP_ERR_NOT_SUPPORTED = 0x106

def setup_args = (0..3).map { |i| PulseCounterStub.stub_pulse_counter_setup_arg(i) }

test("setup passes the pins, a 1000 ns glitch filter and the pull-ups by default") do
  counter = PulseCounter.new(5, 6)
  assert_equal [5, 6, 1000, 1], setup_args
  assert_equal [5, 6], [counter.pin_a, counter.pin_b]
  PulseCounter.new(7, 8, glitch_ns: 0, pull_up: false)
  assert_equal [7, 8, 0, 0], setup_args
end

test("a refused setup raises ArgumentError and gives the unit back") do
  closes = PulseCounterStub.stub_pulse_counter_closes
  PulseCounterStub.stub_pulse_counter_fail_setup(ESP_ERR_INVALID_ARG)
  message = assert_raises(ArgumentError) { PulseCounter.new(99, 6) }
  assert_equal "PulseCounter(GPIO99, GPIO6): setup failed (ESP_ERR_INVALID_ARG)", message
  assert_equal closes + 1, PulseCounterStub.stub_pulse_counter_closes
end

test("running out of PCNT units, and a chip without them, say so") do
  PulseCounterStub.stub_pulse_counter_fail_setup(ESP_ERR_NOT_FOUND)
  message = assert_raises(ArgumentError) { PulseCounter.new(1, 2) }
  assert_equal "PulseCounter(GPIO1, GPIO2): every PCNT unit is taken", message
  PulseCounterStub.stub_pulse_counter_fail_setup(ESP_ERR_NOT_SUPPORTED)
  message = assert_raises(ArgumentError) { PulseCounter.new(1, 2) }
  assert_equal "PulseCounter: this chip has no PCNT", message
end

test("count follows the unit, signed, and clear sets it back to 0") do
  counter = PulseCounter.new(1, 2)
  assert_equal 0, counter.count
  # the first test holds units 0 and 1; the refused setups gave theirs back
  PulseCounterStub.stub_pulse_counter_turn(2, 25_000)
  assert_equal 25_000, counter.count
  PulseCounterStub.stub_pulse_counter_turn(2, -30_000)
  assert_equal(-5_000, counter.count)
  assert_equal nil, counter.clear
  assert_equal 0, counter.count
end

test("a counter past the last unit raises ArgumentError") do
  PulseCounter.new(3, 4)
  message = assert_raises(ArgumentError) { PulseCounter.new(9, 10) }
  assert_equal "PulseCounter: too many counters", message
end

finish
