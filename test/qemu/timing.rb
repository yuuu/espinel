# Kernel#sleep and Machine.sleep block the task on an esp_timer one-shot
# (port/sp_esp.c's nanosleep). QEMU's clock runs at its own pace, so only
# loose bounds are checked.
require "machine"

def elapsed
  t = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  yield
  Process.clock_gettime(Process::CLOCK_MONOTONIC) - t
end

d = elapsed { sleep 0.2 }
puts "sleep 0.2: #{d >= 0.2 && d < 1.0}"
d = elapsed { Machine.sleep(0.05) }
puts "Machine.sleep 0.05: #{d >= 0.05 && d < 0.5}"
puts "Machine.sleep answers #{Machine.sleep(0.6)}"
begin
  Machine.sleep(-1)
rescue ArgumentError => e
  puts "ArgumentError: #{e.message}"
end
