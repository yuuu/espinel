# Mbed TLS with ESP-IDF's hardware SHA and AES, against values from CRuby's
# OpenSSL.
require "mbedtls"

def hex(s) = s.unpack1("H*")

d = MbedTLS::Digest.new(:sha256)
d.update("ab")
d.update("c")
puts hex(d.finish)
d = MbedTLS::Digest.new(:sha1)
d.update("abc")
puts hex(d.finish)

h = MbedTLS::HMAC.new("key", "sha256")
h.update("The quick brown fox jumps over the lazy dog")
puts hex(h.digest)

key = "0123456789abcdef"
iv = "fedcba9876543210"
c = MbedTLS::Cipher.new("AES-128-CBC")
c.encrypt
c.key = key
c.iv = iv
enc = c.update("espinel on the ESP32-S3") + c.finish
puts hex(enc)
c = MbedTLS::Cipher.new("AES-128-CBC")
c.decrypt
c.key = key
c.iv = iv
puts c.update(enc) + c.finish
