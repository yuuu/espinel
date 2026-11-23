# The Ruby side of the spi package over test/host/stubs/spi_stub.c: what it
# hands the C functions, and how their errors surface.
require "spi"
require_relative "support/assert"

module SPIStub
  ffi_func :stub_spi_fail_open, [:int], :void
  ffi_func :stub_spi_fail_device, [:int], :void
  ffi_func :stub_spi_fail_transfer, [:int], :void
  ffi_func :stub_spi_open_arg, [:int], :int
  ffi_func :stub_spi_device_arg, [:int], :int
  ffi_func :stub_spi_transfers, [], :int
  ffi_func :stub_spi_sent_len, [], :int
  ffi_func :stub_spi_sent_byte, [:int], :int
  ffi_func :stub_spi_rx_given, [], :int
end

ESP_ERR_INVALID_ARG = 0x102
ESP_ERR_TIMEOUT = 0x107

def open_args = (0..3).map { |i| SPIStub.stub_spi_open_arg(i) }
def device_args = (0..4).map { |i| SPIStub.stub_spi_device_arg(i) }
def sent = (0...SPIStub.stub_spi_sent_len).map { |i| SPIStub.stub_spi_sent_byte(i) }

def spi = SPI.new(sck_pin: 7, copi_pin: 9, cipo_pin: 8)

test("defaults: SPI2, 1 MHz, mode 0, MSB first, no chip select, unused pins -1") do
  s = SPI.new(sck_pin: 7, copi_pin: 9)
  assert_equal 2, s.unit
  assert_equal [2, 7, 9, -1], open_args
  assert_equal [2, 1_000_000, 0, 0, -1], device_args
end

test("the settings given reach the device") do
  SPI.new(3, frequency: 8_000_000, mode: 3, first_bit: SPI::LSB_FIRST,
          sck_pin: 1, copi_pin: 2, cipo_pin: 3, cs_pin: 4)
  assert_equal [3, 1, 2, 3], open_args
  assert_equal [3, 8_000_000, 3, 1, 4], device_args
  SPI.new(unit: 3, sck_pin: 1)
  assert_equal 3, open_args[0]
end

test("a unit other than SPI2 or SPI3 is refused before ESP-IDF is called") do
  SPI.new(sck_pin: 5)
  assert_raises(ArgumentError) { SPI.new(1, sck_pin: 6) }
  assert_raises(ArgumentError) { SPI.new(4, sck_pin: 6) }
  assert_equal 5, open_args[1], "open was not called"
end

test("sck_pin is required") do
  message = assert_raises(ArgumentError) { SPI.new(copi_pin: 9) }
  assert_equal "SPI2: sck_pin is required", message
end

test("a setup error raises ArgumentError with the error's name") do
  SPIStub.stub_spi_fail_open(ESP_ERR_INVALID_ARG)
  message = assert_raises(ArgumentError) { spi }
  assert_equal "SPI2: setup failed (ESP_ERR_INVALID_ARG)", message
  SPIStub.stub_spi_fail_device(ESP_ERR_INVALID_ARG)
  assert_raises(ArgumentError) { spi }
end

test("setmode changes what is given and keeps the rest") do
  s = SPI.new(frequency: 2_000_000, mode: 1, sck_pin: 7, cs_pin: 4)
  assert_equal nil, s.setmode(first_bit: SPI::LSB_FIRST)
  assert_equal [2, 2_000_000, 1, 1, 4], device_args
  s.setmode(frequency: 500_000, mode: 2)
  assert_equal [2, 500_000, 2, 1, 4], device_args
end

test("a refused setmode keeps the settings that were in force") do
  s = SPI.new(frequency: 2_000_000, sck_pin: 7)
  SPIStub.stub_spi_fail_device(ESP_ERR_INVALID_ARG)
  assert_raises(ArgumentError) { s.setmode(frequency: 0) }
  s.setmode(mode: 1)
  assert_equal [2, 2_000_000, 1, 0, -1], device_args
end

test("write packs Integers, Arrays and Strings into one transfer, low bytes only") do
  s = spi
  assert_equal nil, s.write(0x02, [0xee, 0x1ad], "\x00\xff".b, "é")
  assert_equal [0x02, 0xee, 0xad, 0x00, 0xff, 0xc3, 0xa9], sent
  assert_equal 0, SPIStub.stub_spi_rx_given, "what comes in is dropped"
end

test("write with nothing to send transfers nothing") do
  s = spi
  before = SPIStub.stub_spi_transfers
  s.write
  s.write([], "")
  assert_equal before, SPIStub.stub_spi_transfers
end

test("read clocks out zeros and answers what came in") do
  s = spi
  assert_equal "\xff\xff\xff".b, s.read(3)
  assert_equal [0, 0, 0], sent
  assert_equal 1, SPIStub.stub_spi_rx_given
  assert_equal "".b, s.read(0)
  assert_raises(ArgumentError) { s.read(-1) }
end

test("transfer answers a byte for each sent, the additional reads included") do
  s = spi
  assert_equal "\xfa\xff".b, s.transfer(0x05, 1)
  assert_equal [0x05, 0x00], sent
  assert_equal "\xfe\xfd".b, s.transfer([1, 2])
  assert_raises(ArgumentError) { s.transfer(0x05, -1) }
end

test("a failed transfer raises IOError with the error's name") do
  s = spi
  SPIStub.stub_spi_fail_transfer(ESP_ERR_TIMEOUT)
  message = assert_raises(IOError) { s.write(1) }
  assert_equal "SPI2: transfer failed (ESP_ERR_TIMEOUT)", message
  SPIStub.stub_spi_fail_transfer(ESP_ERR_TIMEOUT)
  assert_raises(IOError) { s.read(2) }
end

finish
