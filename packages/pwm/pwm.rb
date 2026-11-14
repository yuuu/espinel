# PWM on ESP-IDF's LEDC peripheral, after the PWM class of the mruby /
# mruby/c peripheral interface guide
# (github.com/mruby/microcontroller-peripheral-interface-guide): an output
# pin with a frequency and a duty cycle in percent. Each PWM takes one of
# the LEDC channels (8 on the ESP32-S3, 6 on the -C3); they run at up to
# four frequencies at once. The C half is pwm.c in this package.
module ESPPWM
  ffi_func :espinel_pwm_open, [:int], :int
  ffi_func :espinel_pwm_close, [:int], :void
  ffi_func :espinel_pwm_set, [:int, :int, :double], :int
  ffi_func :espinel_pwm_resolution, [:int], :int
  ffi_func :espinel_pwm_not_found, [], :int
  ffi_func :esp_err_to_name, [:int], :str
end

class PWM
  attr_reader :pin

  # PWM.new(1) takes the pin without starting it; PWM.new(1, frequency:
  # 440) starts at 50% duty, PWM.new(1, frequency: 440, duty: 30) at 30%.
  def initialize(pin, frequency: nil, freq: nil, duty: nil)
    @pin = pin
    channel = ESPPWM.espinel_pwm_open(pin)
    raise ArgumentError, "GPIO#{pin} cannot output PWM" if channel == -2
    raise ArgumentError, "PWM: every LEDC channel is taken" if channel < 0
    @channel = channel
    @frequency = 0
    @duty = clamp_percent(duty.nil? ? 50 : duty)
    hz = freq || frequency
    return if hz.nil?
    begin
      run(hz.round)
    rescue ArgumentError => e
      ESPPWM.espinel_pwm_close(@channel)
      raise e
    end
  end

  # Starts the output at freq Hz (rounded to a whole Hz) or changes the
  # frequency, keeping the duty cycle. 0 stops it, low.
  def frequency(freq)
    raise ArgumentError, "PWM: negative frequency #{freq}" if freq < 0
    run(freq.round)
    nil
  end

  # The same as frequency, by the length of one cycle in microseconds.
  def period_us(micro_sec)
    raise ArgumentError, "PWM: negative period #{micro_sec}" if micro_sec < 0
    run(micro_sec == 0 ? 0 : (1_000_000.0 / micro_sec).round)
    nil
  end

  # The duty cycle, 0 to 100 percent; kept for a later start while the
  # output is stopped.
  def duty(percent)
    @duty = clamp_percent(percent)
    run(@frequency) if @frequency > 0
    nil
  end

  # The duty cycle by how long the output is high in each cycle; longer than
  # a cycle is 100%. Needs a frequency to measure the cycle by.
  def pulse_width_us(micro_sec)
    raise ArgumentError, "PWM: GPIO#{@pin} has no frequency yet" if @frequency == 0
    duty(micro_sec * @frequency / 10_000.0)
  end

  private

  def clamp_percent(percent)
    p = percent.to_f
    return 0.0 if p < 0
    return 100.0 if p > 100
    p
  end

  def run(hz)
    err = ESPPWM.espinel_pwm_set(@channel, hz, @duty / 100.0)
    if err != 0
      # a frequency that failed leaves the output stopped (pwm.c)
      @frequency = 0 if ESPPWM.espinel_pwm_resolution(@channel) == 0
      if err == ESPPWM.espinel_pwm_not_found
        raise ArgumentError, "PWM: no LEDC timer left for #{hz} Hz (four frequencies at once)"
      end
      raise ArgumentError, "PWM: GPIO#{@pin} at #{hz} Hz failed (#{ESPPWM.esp_err_to_name(err)})"
    end
    @frequency = hz
  end
end
