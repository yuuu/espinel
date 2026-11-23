# QEMU's OpenCores Ethernet with its user-mode network: DHCP hands out
# 10.0.2.15 behind the gateway 10.0.2.2.
require "network"
require "socket"

Network::Ethernet.connect_timeout(30)
puts Network::Ethernet.link_connected?
puts Network::Ethernet.ipv4_address
puts Network::Ethernet.ipv4_netmask
puts Network::Ethernet.ipv4_gateway
puts Network::Ethernet.dhcp_supplied?
