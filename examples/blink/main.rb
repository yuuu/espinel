require "gpio"
require "machine"

# Blinks the Seeed Studio XIAO ESP32S3's own LED (the orange USER LED),
# which is on GPIO21 and lights when the pin is low. Nothing to wire.
#
#   bundle exec rake flash monitor PROJECT=examples/blink
led = GPIO.new(21, GPIO::OUT)

count = 0
loop do
  led.low
  Machine.sleep(0.5)
  led.high
  Machine.sleep(0.5)
  count += 1
  puts "blink #{count}"
end
