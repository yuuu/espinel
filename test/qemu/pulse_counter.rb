# PulseCounter on the emulated ESP32-S3's PCNT: four units can be set up,
# a fifth is refused, and a setup that fails (a bad pin, both phases on
# one pin) raises and gives its unit back. QEMU drives no pin, so the
# counts stay 0.
require "pulse_counter"

counters = [[1, 2], [3, 4], [5, 6]].map { |a, b| PulseCounter.new(a, b) }
p counters.map(&:count)
p counters[0].clear

[-> { PulseCounter.new(99, 8) },
 -> { PulseCounter.new(7, 7) },
 -> { PulseCounter.new(7, 8, glitch_ns: 100_000) }].each do |f|
  f.call
  puts "no error"
rescue ArgumentError => e
  puts "ArgumentError: #{e.message}"
end

counters << PulseCounter.new(7, 8, glitch_ns: 0, pull_up: false)
p counters.size
begin
  PulseCounter.new(9, 10)
rescue ArgumentError => e
  puts "ArgumentError: #{e.message}"
end
