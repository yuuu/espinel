require "memory"

# Plain Ruby, compiled ahead of time, on the chip: classes, blocks, Hashes,
# exceptions. Needs no wiring; runs on a board and under QEMU.
#
#   bundle exec rake flash monitor PROJECT=examples/hello
#   bundle exec rake qemu PROJECT=examples/hello
class Greeter
  def initialize(name)
    @name = name
  end

  def greet(times)
    times.times.map { |i| "#{i + 1}: Hello, #{@name}!" }
  end
end

puts Greeter.new("ESP32-S3").greet(3)

words = %w[ruby spinel esp32 ruby xiao spinel ruby]
tally = words.tally.sort_by { |word, n| [-n, word] }
tally.each { |word, n| puts "#{word.ljust(8)} #{"*" * n}" }

primes = (2..50).select { |n| (2..Math.sqrt(n)).none? { |d| (n % d).zero? } }
puts "primes below 50: #{primes.join(" ")}"

begin
  Integer("forty-two")
rescue ArgumentError => e
  puts "rescued: #{e.message}"
end

puts Memory.report
