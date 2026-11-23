# LEDC sets up without an error under QEMU, so the channel and timer
# bookkeeping can be checked, though not the output.
require "pwm"

a = PWM.new(1, frequency: 1000)
a.duty(25)
a.pulse_width_us(100)
b = PWM.new(2, frequency: 1000, duty: 75)
c = PWM.new(3, frequency: 2000)
d = PWM.new(4, frequency: 3000)
e = PWM.new(5, frequency: 4000)
puts "four frequencies at once"
begin
  PWM.new(6, frequency: 5000)
rescue ArgumentError => e
  puts "ArgumentError: #{e.message}"
end
a.frequency(4000)
a.period_us(250)
puts "a moved to 4000 Hz"
begin
  a.frequency(-1)
rescue ArgumentError => e
  puts "ArgumentError: #{e.message}"
end
idle = PWM.new(7)
begin
  idle.pulse_width_us(10)
rescue ArgumentError => e
  puts "ArgumentError: #{e.message}"
end
