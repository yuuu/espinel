# The basics: output, strings, arrays, hashes and blocks.
puts "hello from espinel"
print "no newline", "\n"
p([1, 2, 3].map { |x| x * 2 })
h = { "a" => 1, "b" => 2 }
puts h.map { |k, v| "#{k}=#{v}" }.join(",")
puts (1..10).select(&:even?).sum
puts "%05.2f|%-4s|%x" % [3.14159, "ab", 255]
$stderr.puts "to stderr"
