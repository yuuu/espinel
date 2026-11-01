# The board itself. Machine.sleep(seconds) waits as Kernel#sleep does:
# seconds may be fractional, and the seconds slept come back rounded.
module ESPMachine
  ffi_func :espinel_machine_sleep, [:double], :void
end

module Machine
  def self.sleep(seconds)
    raise ArgumentError, "time interval must not be negative" if seconds < 0
    ESPMachine.espinel_machine_sleep(seconds.to_f)
    seconds.round
  end
end
