# QEMU does not emulate the ESP32-S3's I2C: the bus sets up, a transfer
# times out and the ones after it fail. This checks that those raise, and
# that bad arguments are refused before reaching ESP-IDF.
require "i2c"

i2c = I2C.new(scl_pin: 6, sda_pin: 5)
puts "set up"
begin
  i2c.write(0x44, 0x24, 0x00)
rescue IOError => e
  puts "IOError raised"
end
begin
  i2c.read(0x44, 6)
rescue IOError => e
  puts "IOError raised"
end
[-> { i2c.write(0x80, 0) }, -> { i2c.read(0x44, -1) }, -> { I2C.new(5, scl_pin: 6, sda_pin: 5) },
 -> { I2C.new(scl_pin: 6) }].each do |f|
  f.call
rescue ArgumentError => e
  puts "ArgumentError: #{e.message}"
end
