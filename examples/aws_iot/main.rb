require "network"
require "ssl_socket"
require "net/mqtt"
require "memory"
require_relative "config"

# Connects to AWS IoT Core over MQTT with TLS, authenticated by the thing's
# certificate, publishes telemetry to <thing>/telemetry every 10 seconds
# and prints what arrives on <thing>/cmd. AWS IoT's server certificate is
# checked against ESP-IDF's bundle of root certificates (Amazon Root CA 1).
# A board only: QEMU has no Wi-Fi.
#
# The thing's policy must allow iot:Connect as THING_NAME, iot:Publish to
# <thing>/telemetry, and iot:Subscribe and iot:Receive on <thing>/cmd.
# Send a message from the console's MQTT test client to <thing>/cmd to see
# it arrive.
#
#   cp examples/aws_iot/config.example.rb examples/aws_iot/config.rb
#   # fill in config.rb
#   bundle exec rake flash monitor PROJECT=examples/aws_iot
TELEMETRY = "#{THING_NAME}/telemetry"
COMMANDS = "#{THING_NAME}/cmd"

Network::WiFi.init("JP")
puts "connecting to #{WIFI_SSID}..."
Network::WiFi.connect_timeout(WIFI_SSID, WIFI_PASSWORD, Network::WiFi::Auth::WPA2_AES_PSK, 30) or
  abort "the access point refused (a wrong password?)"
puts "address #{Network::WiFi.ipv4_address}"

ctx = SSLContext.new
ctx.set_cert_pem(DEVICE_CERT)
ctx.set_key_pem(PRIVATE_KEY)

client = Net::MQTT::Client.new(AWS_IOT_ENDPOINT, 8883, client_id: THING_NAME, ssl_context: ctx)
client.connect
puts "connected to #{AWS_IOT_ENDPOINT} as #{THING_NAME}"
client.subscribe(COMMANDS, qos: 1)

# One tick: publish, then wait up to 10 seconds for commands (receive
# pings the broker as keep-alive needs). In a method, so that what a tick
# makes is garbage when it returns: the GC does not reclaim what top-level
# code makes.
def tick(client, count)
  started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  payload = %({"count":#{count},"uptime":#{started.to_i},"free_heap":#{Memory.heap[:free]}})
  client.publish(TELEMETRY, payload, qos: 1)
  puts "published #{TELEMETRY}: #{payload}"

  while (left = started + 10 - Process.clock_gettime(Process::CLOCK_MONOTONIC)) > 0
    topic, message = client.receive(timeout: left)
    puts "received #{topic}: #{message}" if topic
  end
end

count = 0
loop { tick(client, count += 1) }
