require "pulse_counter"

# Counts a quadrature encoder (a geared motor's Hall encoder, a rotary
# knob) and prints the count, its change and the speed ten times a second.
# Turn it one way and the count goes up; turn it the other way and it goes
# down. Swap the pins to reverse the sign.
#
# Wiring (Seeed Studio XIAO ESP32S3):
#   phase A to D4 (GPIO5), phase B to D5 (GPIO6), the encoder's VCC to
#   3V3 and GND to GND; the internal pull-ups let open-collector outputs
#   be read
#
#   bundle exec rake flash monitor PROJECT=examples/encoder
PERIOD = 0.1

encoder = PulseCounter.new(5, 6)
last = encoder.count
loop do
  sleep PERIOD
  count = encoder.count
  delta = count - last
  last = count
  puts "count #{count}  #{delta >= 0 ? "+" : ""}#{delta}  #{(delta / PERIOD).round} counts/s"
end
