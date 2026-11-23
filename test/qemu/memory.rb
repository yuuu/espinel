# The memory package reads the heap, the Ruby task's stack and Spinel's
# GC counters; the values vary, so only their shape and sense are checked.
require "memory"

heap = Memory.heap
puts heap.keys.inspect
puts heap.values.all? { |v| v.is_a?(Integer) && v >= 0 }
stack = Memory.stack
puts stack.keys.inspect
puts stack.values.all? { |v| v.is_a?(Integer) && v > 0 }
gc = Memory.gc
puts gc.keys.inspect
begin
  Memory.heap(:flash)
rescue ArgumentError => e
  puts "ArgumentError: #{e.message}"
end
