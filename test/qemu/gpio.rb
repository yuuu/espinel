# QEMU does not emulate the ESP32-S3's GPIO (writes are dropped, reads are
# 0), so this checks that the calls succeed and that a bad pin raises.
require "gpio"

out = GPIO.new(1, GPIO::OUT)
out.high
out.low
out.write(1)
inp = GPIO.new(5, GPIO::IN, GPIO::PULL_DOWN)
puts "read: #{inp.read}"
puts "high?: #{inp.high?}"
io = GPIO.new(2, GPIO::IN_OUT, GPIO::PULL_UP)
io.write(0)
puts "constants: #{[GPIO::LOW, GPIO::HIGH].inspect}"
begin
  GPIO.new(99, GPIO::OUT)
rescue ArgumentError => e
  puts "ArgumentError: #{e.message}"
end
