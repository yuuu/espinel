require "gpio"
require "machine"

# Seeed Studio XIAO ESP32S3: an LED (through 330 ohms) on D0 (GPIO1), and a
# push button from D4 (GPIO5) to 3.3V, so D4 needs its pull-down.
led = GPIO.new(1, GPIO::OUT)
button = GPIO.new(5, GPIO::IN, GPIO::PULL_DOWN)

# Output alone first: blink three times.
3.times do
  led.high
  Machine.sleep(0.2)
  led.low
  Machine.sleep(0.2)
end
puts "blinked; the LED now follows the button"

# Then input: the LED is lit while the button is held.
last = -1
loop do
  level = button.read
  led.write(level)
  if level != last
    puts(level == GPIO::HIGH ? "button pressed" : "button released")
    last = level
  end
  Machine.sleep(0.01)
end
