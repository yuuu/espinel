# SPI controller on ESP-IDF, after the SPI class of the mruby / mruby/c
# peripheral interface guide
# (github.com/mruby/microcontroller-peripheral-interface-guide): full-duplex
# transfers in 8-bit units. The guide leaves the chip select to GPIO; here
# cs_pin: also lets the hardware drive it low for each read, write or
# transfer. The C half is spi.c in this package.
module ESPSPI
  ffi_func :espinel_spi_first_unit, [], :int
  ffi_func :espinel_spi_last_unit, [], :int
  ffi_func :espinel_spi_open, [:int, :int, :int, :int], :int
  ffi_func :espinel_spi_device, [:int, :int, :int, :int, :int], :int
  ffi_func :espinel_spi_transfer, [:int, :str, :ptr, :int], :int
  ffi_func :esp_err_to_name, [:int], :str
end

class SPI
  MSB_FIRST = 0
  LSB_FIRST = 1

  attr_reader :unit

  # SPI.new(sck_pin: 7, copi_pin: 9, cipo_pin: 8) on SPI2 at 1 MHz, mode
  # 0, MSB first. unit: is SPI2 or SPI3, as the reference manual numbers
  # them (2 or 3; the ESP32-C3 has SPI2 only). The pins have no default; a
  # pin left out is not used (no cipo_pin for a device that is only
  # written to).
  def initialize(id = nil, unit: nil, frequency: 1_000_000, mode: 0,
                 first_bit: MSB_FIRST, sck_pin: nil, copi_pin: nil,
                 cipo_pin: nil, cs_pin: nil)
    @unit = unit || id || ESPSPI.espinel_spi_first_unit
    if @unit < ESPSPI.espinel_spi_first_unit || @unit > ESPSPI.espinel_spi_last_unit
      raise ArgumentError, "SPI#{@unit}: no such unit"
    end
    raise ArgumentError, "SPI#{@unit}: sck_pin is required" if sck_pin.nil?
    check(ESPSPI.espinel_spi_open(@unit, sck_pin, copi_pin || -1, cipo_pin || -1), "setup")
    configure(frequency, mode, first_bit, cs_pin || -1)
  end

  # Changes the clock, mode, bit order or chip select given; the others
  # stay. (The bus pins are fixed by the constructor.)
  def setmode(frequency: nil, mode: nil, first_bit: nil, cs_pin: nil)
    configure(frequency || @frequency, mode || @mode, first_bit || @first_bit,
              cs_pin || @cs_pin)
    nil
  end

  # read_bytes bytes, clocked in while 0 goes out.
  def read(read_bytes)
    raise ArgumentError, "negative length #{read_bytes} given" if read_bytes < 0
    xfer("\0".b * read_bytes, true)
  end

  # The outputs, each an Integer (a byte), an Array of them or a String,
  # go out in one transfer; what comes in is dropped.
  #   spi.write(0x02, 0xee, 0xad, 0x00, data_string)
  def write(*outputs)
    xfer(pack(outputs), false)
    nil
  end

  # Clocks outputs (an Integer, an Array of them or a String) out, then
  # additional_read_bytes more zero bytes, and answers every byte that came
  # in meanwhile: spi.transfer(0x05, 1) is two bytes long.
  def transfer(outputs, additional_read_bytes = 0)
    raise ArgumentError, "negative length #{additional_read_bytes} given" if additional_read_bytes < 0
    xfer(pack([outputs]) + "\0".b * additional_read_bytes, true)
  end

  private

  def check(err, what)
    return if err == 0
    raise ArgumentError, "SPI#{@unit}: #{what} failed (#{ESPSPI.esp_err_to_name(err)})"
  end

  # The settings are kept once ESP-IDF has taken them.
  def configure(frequency, mode, first_bit, cs_pin)
    check(ESPSPI.espinel_spi_device(@unit, frequency, mode, first_bit == LSB_FIRST ? 1 : 0, cs_pin),
          "setup")
    @frequency = frequency
    @mode = mode
    @first_bit = first_bit
    @cs_pin = cs_pin
  end

  def xfer(data, keep)
    len = data.bytesize
    return "".b if len == 0
    if keep
      buf = IO::Buffer.new(len)
      err = ESPSPI.espinel_spi_transfer(@unit, data, buf, len)
    else
      err = ESPSPI.espinel_spi_transfer(@unit, data, nil, len)
    end
    raise IOError, "SPI#{@unit}: transfer failed (#{ESPSPI.esp_err_to_name(err)})" if err != 0
    keep ? buf.get_string(0, len) : "".b
  end

  def pack(outputs)
    s = "".b
    outputs.each do |o|
      case o
      when Integer then s << (o & 0xff).chr
      when String then s << o.b
      when Array then o.each { |b| s << (b & 0xff).chr }
      else raise ArgumentError, "SPI: cannot send #{o.inspect}"
      end
    end
    s
  end
end
