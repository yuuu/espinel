# I2C controller on ESP-IDF, after the I2C class of the mruby / mruby/c
# peripheral interface guide
# (github.com/mruby/microcontroller-peripheral-interface-guide): 7-bit
# addresses, write, read and write-then-read, plus a bus scan. The guide's
# low-level methods (send_start, raw_read and the rest) are left out: the
# driver runs whole transactions. The C half is i2c.c in this package.
module ESPI2C
  ffi_func :espinel_i2c_port_count, [], :int
  ffi_func :espinel_i2c_open, [:int, :int, :int, :int], :int
  ffi_func :espinel_i2c_write, [:int, :int, :str, :int, :int], :int
  ffi_func :espinel_i2c_read, [:int, :int, :str, :int, :ptr, :int, :int], :int
  ffi_func :espinel_i2c_probe, [:int, :int, :int], :int
  ffi_func :espinel_i2c_not_found, [], :int
  ffi_func :esp_err_to_name, [:int], :str
end

class I2C
  # how long a transaction may take before it fails, and how long the scan
  # waits for each address
  TIMEOUT_MS = 1000
  SCAN_TIMEOUT_MS = 50

  attr_reader :unit, :frequency

  # I2C.new(scl_pin: 6, sda_pin: 5) on unit 0 at 100 kHz, or
  # I2C.new(1, frequency: 400_000, scl_pin: ..., sda_pin: ...). The pins
  # have no default: ESP-IDF routes I2C to whichever pins it is given.
  def initialize(id = nil, unit: nil, frequency: 100_000, freq: nil,
                 scl_pin: nil, sda_pin: nil)
    @unit = unit || id || 0
    if @unit < 0 || @unit >= ESPI2C.espinel_i2c_port_count
      raise ArgumentError, "I2C#{@unit}: no such unit"
    end
    raise ArgumentError, "I2C#{@unit}: scl_pin and sda_pin are required" if scl_pin.nil? || sda_pin.nil?
    @frequency = freq || frequency
    err = ESPI2C.espinel_i2c_open(@unit, scl_pin, sda_pin, @frequency)
    raise ArgumentError, "I2C#{@unit}: setup failed (#{ESPI2C.esp_err_to_name(err)})" if err != 0
  end

  # Reads read_bytes bytes from the device at i2c_adrs_7. Bytes given after
  # them (as for write) are written first, then a repeated start begins the
  # read: i2c.read(0x45, 3, 0xf3, 0x2d).
  def read(i2c_adrs_7, read_bytes, *param)
    check_address(i2c_adrs_7)
    raise ArgumentError, "negative length #{read_bytes} given" if read_bytes < 0
    return "".b if read_bytes == 0
    out = pack(param)
    buf = IO::Buffer.new(read_bytes)
    err = ESPI2C.espinel_i2c_read(@unit, i2c_adrs_7, out, out.bytesize, buf, read_bytes, TIMEOUT_MS)
    check(err, "read from", i2c_adrs_7)
    buf.get_string(0, read_bytes)
  end

  # Writes the outputs, each an Integer (a byte), an Array of them or a
  # String, to the device at i2c_adrs_7; answers the bytes written.
  #   i2c.write(0x50, 0x00, 0x80, data_string)
  def write(i2c_adrs_7, *outputs)
    check_address(i2c_adrs_7)
    data = pack(outputs)
    err = if data.bytesize == 0
            ESPI2C.espinel_i2c_probe(@unit, i2c_adrs_7, TIMEOUT_MS)
          else
            ESPI2C.espinel_i2c_write(@unit, i2c_adrs_7, data, data.bytesize, TIMEOUT_MS)
          end
    check(err, "write to", i2c_adrs_7)
    data.bytesize
  end

  # The addresses from 0x08 to 0x77 a device answers with an ACK.
  def scan
    not_found = ESPI2C.espinel_i2c_not_found
    found = []
    (0x08..0x77).each do |addr|
      err = ESPI2C.espinel_i2c_probe(@unit, addr, SCAN_TIMEOUT_MS)
      if err == 0
        found << addr
      elsif err != not_found
        raise IOError, "I2C#{@unit}: scan failed at 0x#{addr.to_s(16)} (#{ESPI2C.esp_err_to_name(err)})"
      end
    end
    found
  end

  private

  def check_address(addr)
    raise ArgumentError, "I2C#{@unit}: address #{addr} is not 7-bit" if addr < 0 || addr > 0x7f
  end

  def check(err, what, addr)
    return if err == 0
    raise IOError, "I2C#{@unit}: #{what} 0x#{addr.to_s(16)} failed (#{ESPI2C.esp_err_to_name(err)})"
  end

  def pack(outputs)
    s = "".b
    outputs.each do |o|
      case o
      when Integer then s << (o & 0xff).chr
      when String then s << o.b
      when Array then o.each { |b| s << (b & 0xff).chr }
      else raise ArgumentError, "I2C: cannot send #{o.inspect}"
      end
    end
    s
  end
end
