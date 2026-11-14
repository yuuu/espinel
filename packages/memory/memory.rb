# How much memory the program is using: the heap, the Ruby task's stack and
# Spinel's GC, as Hashes of bytes, and as text to print.
#
#   puts Memory.report
#   heap  [######--------------]  92.3K / 301.2K used, 208.9K free (largest block 180.0K, least free 150.2K)
#   stack [##------------------]   2.4K /  32.0K used, peak 7.1K, SystemStackError at 26.0K
#   gc    40.1K counted, next collection at 72.0K, 12 collections (2 full)
#
# The C half is memory.c in this package.
module ESPMemory
  ffi_func :espinel_mem_heap, [:int, :int], :size_t
  ffi_func :espinel_mem_stack_size, [], :size_t
  ffi_func :espinel_mem_stack_used, [], :size_t
  ffi_func :espinel_mem_stack_peak, [], :size_t
  ffi_func :espinel_mem_stack_limit, [], :size_t
  ffi_func :espinel_mem_gc_bytes, [], :size_t
  ffi_func :espinel_mem_gc_threshold, [], :size_t
  ffi_func :espinel_mem_gc_count, [], :size_t
  ffi_func :espinel_mem_gc_full_count, [], :size_t
end

module Memory
  # memory.c's region numbers
  REGIONS = { all: 0, internal: 1, spiram: 2 }

  # The heap malloc draws on: region :all (internal RAM and PSRAM, when
  # there is PSRAM), :internal or :spiram. minimum_free is the least there
  # has been free since boot.
  def self.heap(region = :all)
    r = REGIONS[region]
    raise ArgumentError, "unknown region: #{region.inspect} (:all, :internal or :spiram)" if r.nil?
    {
      total: ESPMemory.espinel_mem_heap(r, 0),
      used: ESPMemory.espinel_mem_heap(r, 2),
      free: ESPMemory.espinel_mem_heap(r, 1),
      largest_free_block: ESPMemory.espinel_mem_heap(r, 3),
      minimum_free: ESPMemory.espinel_mem_heap(r, 4)
    }
  end

  # The Ruby task's stack: used where this is called, the peak since the
  # program started, and the limit at which a Ruby method raises
  # SystemStackError (free is what is left before it).
  def self.stack
    used = ESPMemory.espinel_mem_stack_used
    limit = ESPMemory.espinel_mem_stack_limit
    {
      size: ESPMemory.espinel_mem_stack_size,
      used: used,
      peak: ESPMemory.espinel_mem_stack_peak,
      limit: limit,
      free: limit > used ? limit - used : 0
    }
  end

  # Spinel's collector: the bytes it counts (objects, strings and their
  # storage), the count at which it collects next, and the collections so
  # far (full ones sweep the old generation too).
  def self.gc
    {
      bytes: ESPMemory.espinel_mem_gc_bytes,
      threshold: ESPMemory.espinel_mem_gc_threshold,
      count: ESPMemory.espinel_mem_gc_count,
      full_count: ESPMemory.espinel_mem_gc_full_count
    }
  end

  # The three, one line each, with a bar for how full the heap and the
  # stack are
  def self.report
    h = heap
    s = stack
    g = gc
    heap_line = "heap  #{bar(h[:used], h[:total])} #{kb(h[:used])} / #{kb(h[:total])} used, " \
                "#{kb(h[:free])} free (largest block #{kb(h[:largest_free_block])}, " \
                "least free #{kb(h[:minimum_free])})"
    stack_line = "stack #{bar(s[:used], s[:size])} #{kb(s[:used])} / #{kb(s[:size])} used, " \
                 "peak #{kb(s[:peak])}, SystemStackError at #{kb(s[:limit])}"
    gc_line = "gc    #{kb(g[:bytes])} counted, next collection at #{kb(g[:threshold])}, " \
              "#{g[:count]} collections (#{g[:full_count]} full)"
    "#{heap_line}\n#{stack_line}\n#{gc_line}"
  end

  # One line, for printing in a loop
  def self.summary
    h = heap
    s = stack
    "heap #{kb(h[:used])}/#{kb(h[:total])} (largest free #{kb(h[:largest_free_block])}) " \
      "stack #{kb(s[:used])}/#{kb(s[:size])} (peak #{kb(s[:peak])}) gc #{ESPMemory.espinel_mem_gc_count}"
  end

  # bytes as kilobytes, one decimal, right-aligned in 6 columns
  def self.kb(n)
    format("%5.1fK", n / 1024.0)
  end

  def self.bar(used, total, width = 20)
    filled = total > 0 ? (used * width + total / 2) / total : 0
    filled = width if filled > width
    "[" + "#" * filled + "-" * (width - filled) + "]"
  end
end
