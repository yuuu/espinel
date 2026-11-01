require "gpio"
require "machine"

led = GPIO.new(2, GPIO::OUT)
3.times do
  led.high
  puts "LED on"
  Machine.sleep(0.5)
  led.low
  puts "LED off"
  Machine.sleep(0.5)
end
