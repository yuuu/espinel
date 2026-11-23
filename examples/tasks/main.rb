require "gpio"
require "task"

# Two Ruby programs at once, one on each core: this one blinks the
# Seeed Studio XIAO ESP32S3's own LED (GPIO21, lit when low) at a steady
# 5 Hz, while tasks/primes.rb counts primes flat out on core 0 and reports
# through a queue. The blinking keeps its pace, and each report shows how
# many blinks came while it was being computed. Nothing to wire; under
# QEMU the LED is not there, but the rest runs.
#
#   bundle exec rake flash monitor PROJECT=examples/tasks
#   bundle exec rake qemu PROJECT=examples/tasks
led = GPIO.new(21, GPIO::OUT)
reports = Task::Queue.new(:reports)
start = Process.clock_gettime(Process::CLOCK_MONOTONIC)

primes = Task.new(:primes, core: 0)
puts "main started #{primes.inspect}"

blinks = 0
while primes.alive? || !reports.empty?
  led.low
  sleep 0.1
  led.high
  sleep 0.1
  blinks += 1
  # whatever has come in meanwhile, without waiting
  while (line = reports.pop(timeout: 0))
    ms = ((Process.clock_gettime(Process::CLOCK_MONOTONIC) - start) * 1000).round
    puts "#{ms} ms, #{blinks} blinks on core #{Task.core}: #{line}"
  end
end
puts "primes ended with status #{primes.value}"
