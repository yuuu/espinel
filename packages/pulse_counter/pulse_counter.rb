# A quadrature encoder counted in hardware by ESP-IDF's PCNT peripheral,
# after PicoRuby's PulseCounter (picoruby-pulse_counter): the A/B phases
# are decoded at x4 (every edge of both), so reading the count is all that
# is left to the program. The count is signed, its sign the direction, and
# keeps going past the 16-bit hardware counter. Each PulseCounter takes one
# PCNT unit (4 on the ESP32-S3; none on the ESP32-C2 and -C3). The C half is
# pulse_counter.c in this package.
module ESPPulseCounter
  ffi_func :espinel_pulse_counter_new, [], :int
  ffi_func :espinel_pulse_counter_setup, [:int, :int, :int, :int, :int], :int
  ffi_func :espinel_pulse_counter_close, [:int], :void
  ffi_func :espinel_pulse_counter_count, [:int], :int
  ffi_func :espinel_pulse_counter_clear, [:int], :int
  ffi_func :espinel_pulse_counter_not_found, [], :int
  ffi_func :espinel_pulse_counter_not_supported, [], :int
  ffi_func :esp_err_to_name, [:int], :str
end

class PulseCounter
  attr_reader :pin_a, :pin_b

  # PulseCounter.new(5, 6) counts up while B leads A, as PicoRuby's does;
  # swap the pins to turn the sign around. Pulses shorter than glitch_ns are ignored (0 turns
  # the filter off; about 12700 at most), and pull_up: false leaves the
  # pins' pull-ups off.
  def initialize(pin_a, pin_b, glitch_ns: 1000, pull_up: true)
    @pin_a = pin_a
    @pin_b = pin_b
    slot = ESPPulseCounter.espinel_pulse_counter_new
    raise ArgumentError, "PulseCounter: too many counters" if slot < 0
    err = ESPPulseCounter.espinel_pulse_counter_setup(slot, pin_a, pin_b, glitch_ns, pull_up ? 1 : 0)
    if err != 0
      ESPPulseCounter.espinel_pulse_counter_close(slot)
      raise ArgumentError, setup_error(err)
    end
    @slot = slot
  end

  # The count since the start or the last clear.
  def count = ESPPulseCounter.espinel_pulse_counter_count(@slot)

  # Sets the count back to 0.
  def clear
    err = ESPPulseCounter.espinel_pulse_counter_clear(@slot)
    raise IOError, "#{name}: clear failed (#{ESPPulseCounter.esp_err_to_name(err)})" if err != 0
    nil
  end

  private

  def name = "PulseCounter(GPIO#{@pin_a}, GPIO#{@pin_b})"

  def setup_error(err)
    if err == ESPPulseCounter.espinel_pulse_counter_not_found
      "#{name}: every PCNT unit is taken"
    elsif err == ESPPulseCounter.espinel_pulse_counter_not_supported
      "PulseCounter: this chip has no PCNT"
    else
      "#{name}: setup failed (#{ESPPulseCounter.esp_err_to_name(err)})"
    end
  end
end
