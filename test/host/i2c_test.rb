# The Ruby side of the i2c package over test/host/stubs/i2c_stub.c: what it
# hands the C functions, and how their errors surface.
require "i2c"
require_relative "support/assert"

module I2CStub
  ffi_func :stub_i2c_attach, [:int], :void
  ffi_func :stub_i2c_fail_open, [:int], :void
  ffi_func :stub_i2c_fail_write, [:int], :void
  ffi_func :stub_i2c_fail_read, [:int], :void
  ffi_func :stub_i2c_fail_probe, [:int], :void
  ffi_func :stub_i2c_open_arg, [:int], :int
  ffi_func :stub_i2c_last_addr, [], :int
  ffi_func :stub_i2c_last_timeout, [], :int
  ffi_func :stub_i2c_sent_len, [], :int
  ffi_func :stub_i2c_sent_byte, [:int], :int
  ffi_func :stub_i2c_probes, [], :int
end

ESP_FAIL = -1
ESP_ERR_INVALID_ARG = 0x102
ESP_ERR_TIMEOUT = 0x107

def open_args = (0..3).map { |i| I2CStub.stub_i2c_open_arg(i) }
def sent = (0...I2CStub.stub_i2c_sent_len).map { |i| I2CStub.stub_i2c_sent_byte(i) }

def i2c = I2C.new(scl_pin: 6, sda_pin: 5)

test("defaults: unit 0 at 100 kHz") do
  bus = i2c
  assert_equal 0, bus.unit
  assert_equal 100_000, bus.frequency
  assert_equal [0, 6, 5, 100_000], open_args
end

test("unit, frequency and its alias freq") do
  I2C.new(1, frequency: 400_000, scl_pin: 2, sda_pin: 3)
  assert_equal [1, 2, 3, 400_000], open_args
  assert_equal 1_000_000, I2C.new(unit: 1, freq: 1_000_000, scl_pin: 2, sda_pin: 3).frequency
end

test("a unit the chip lacks, or a missing pin, is refused") do
  assert_raises(ArgumentError) { I2C.new(2, scl_pin: 6, sda_pin: 5) }
  assert_raises(ArgumentError) { I2C.new(-1, scl_pin: 6, sda_pin: 5) }
  message = assert_raises(ArgumentError) { I2C.new(scl_pin: 6) }
  assert_equal "I2C0: scl_pin and sda_pin are required", message
end

test("a setup error raises ArgumentError with the error's name") do
  I2CStub.stub_i2c_fail_open(ESP_ERR_INVALID_ARG)
  message = assert_raises(ArgumentError) { i2c }
  assert_equal "I2C0: setup failed (ESP_ERR_INVALID_ARG)", message
end

test("write sends the packed outputs and answers their count") do
  bus = i2c
  assert_equal 4, bus.write(0x44, 0x24, [0x00, 0x1ff], "A")
  assert_equal 0x44, I2CStub.stub_i2c_last_addr
  assert_equal [0x24, 0x00, 0xff, 0x41], sent
  assert_equal I2C::TIMEOUT_MS, I2CStub.stub_i2c_last_timeout
end

test("write with nothing to send probes the address") do
  bus = i2c
  I2CStub.stub_i2c_attach(0x50)
  before = I2CStub.stub_i2c_probes
  assert_equal 0, bus.write(0x50)
  assert_equal before + 1, I2CStub.stub_i2c_probes
  assert_raises(IOError) { bus.write(0x51) }
end

test("an address beyond 7 bits is refused") do
  bus = i2c
  assert_raises(ArgumentError) { bus.write(0x80, 1) }
  assert_raises(ArgumentError) { bus.write(-1, 1) }
  assert_raises(ArgumentError) { bus.read(0x80, 1) }
end

test("read writes the outputs first, then answers the bytes read") do
  bus = i2c
  assert_equal "\x45\x46\x47".b, bus.read(0x45, 3, 0xf3, 0x2d)
  assert_equal 0x45, I2CStub.stub_i2c_last_addr
  assert_equal [0xf3, 0x2d], sent
  assert_equal "\x45".b, bus.read(0x45, 1)
  assert_equal [], sent
end

test("read of 0 bytes does nothing; a negative count is refused") do
  bus = i2c
  bus.write(0x10, 1)
  assert_equal "".b, bus.read(0x45, 0)
  assert_equal 0x10, I2CStub.stub_i2c_last_addr
  assert_raises(ArgumentError) { bus.read(0x45, -1) }
end

test("a failed transfer raises IOError naming the address and the error") do
  bus = i2c
  I2CStub.stub_i2c_fail_write(ESP_FAIL)
  message = assert_raises(IOError) { bus.write(0x3c, 0) }
  assert_equal "I2C0: write to 0x3c failed (ESP_FAIL)", message
  I2CStub.stub_i2c_fail_read(ESP_ERR_TIMEOUT)
  message = assert_raises(IOError) { bus.read(0x3c, 2) }
  assert_equal "I2C0: read from 0x3c failed (ESP_ERR_TIMEOUT)", message
end

test("scan probes 0x08 to 0x77 and lists the addresses that answered") do
  bus = i2c
  I2CStub.stub_i2c_attach(0x3c)
  I2CStub.stub_i2c_attach(0x44)
  I2CStub.stub_i2c_attach(0x50)
  I2CStub.stub_i2c_attach(0x05) # reserved: never probed
  before = I2CStub.stub_i2c_probes
  assert_equal [0x3c, 0x44, 0x50], bus.scan
  assert_equal 0x78 - 0x08, I2CStub.stub_i2c_probes - before
  assert_equal I2C::SCAN_TIMEOUT_MS, I2CStub.stub_i2c_last_timeout
end

test("a scan error other than no answer raises IOError") do
  bus = i2c
  I2CStub.stub_i2c_fail_probe(ESP_ERR_TIMEOUT)
  message = assert_raises(IOError) { bus.scan }
  assert_equal "I2C0: scan failed at 0x8 (ESP_ERR_TIMEOUT)", message
end

finish
