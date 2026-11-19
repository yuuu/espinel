require "network"
require "ssl_socket"
require_relative "config"

# Joins a Wi-Fi network and fetches https://example.com/ over TLS, checking
# the server's certificate against ESP-IDF's bundle of root certificates.
# A board only: QEMU has no Wi-Fi.
#
#   cp examples/wifi_https/config.example.rb examples/wifi_https/config.rb
#   # set WIFI_SSID and WIFI_PASSWORD in config.rb
#   bundle exec rake flash monitor PROJECT=examples/wifi_https
HOST = "example.com"

Network::WiFi.init("JP")
puts "connecting to #{WIFI_SSID}..."
Network::WiFi.connect_timeout(WIFI_SSID, WIFI_PASSWORD, Network::WiFi::Auth::WPA2_AES_PSK, 30) or
  abort "the access point refused (a wrong password?)"
puts "address #{Network::WiFi.ipv4_address}, gateway #{Network::WiFi.ipv4_gateway}"

ssl = SSLSocket.open(HOST, 443)
puts "#{ssl.ssl_version}, #{ssl.cipher[0]}"
# HTTP/1.0: the body comes whole, not in chunks, and ends with the connection
ssl.write("GET / HTTP/1.0\r\nHost: #{HOST}\r\nUser-Agent: espinel\r\n\r\n")

puts ssl.gets.chomp # the status line
while (line = ssl.gets) && line != "\r\n"
  name = line.split(":").first.downcase
  puts "  #{line.chomp}" if %w[content-type content-length date].include?(name)
end

body = +""
body << ssl.readpartial(1024) until ssl.eof?
ssl.close

title = body[%r{<title>(.*?)</title>}m, 1]
puts "#{body.bytesize} bytes, title: #{title}"
