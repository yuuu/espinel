# Network interfaces, with PicoRuby's interface (picoruby-network,
# picoruby-esp32): Network::WiFi joins an access point, and once it has an
# address, Spinel's `require "socket"` (TCPSocket, TCPServer, UDPSocket,
# as in CRuby) talks over it. Network::Ethernet is QEMU's OpenCores
# Ethernet, for running the same programs without a board. The C half is
# network.c in this package.
module ESPNetwork
  ffi_func :espinel_net_err_name, [:int], :str
  ffi_func :espinel_net_ipv4, [:int, :int], :str
  ffi_func :espinel_net_up, [:int], :int
  ffi_func :espinel_net_dhcp_supplied, [:int], :int
  ffi_func :espinel_wifi_auth_open, [], :int
  ffi_func :espinel_wifi_auth_wpa_psk, [], :int
  ffi_func :espinel_wifi_auth_wpa2_psk, [], :int
  ffi_func :espinel_wifi_auth_wpa_wpa2_psk, [], :int
  ffi_func :espinel_wifi_auth_wpa3_psk, [], :int
  ffi_func :espinel_wifi_init, [:str], :int
  ffi_func :espinel_wifi_initialized, [], :int
  ffi_func :espinel_wifi_connect, [:str, :str, :int, :int], :int
  ffi_func :espinel_wifi_disconnect, [], :int
  ffi_func :espinel_wifi_link_status, [], :int
  ffi_func :espinel_eth_connect, [:int], :int

  # network.c's interface numbers
  WIFI = 0
  ETHERNET = 1

  def self.address(ifc, which)
    addr = ESPNetwork.espinel_net_ipv4(ifc, which)
    addr.empty? ? nil : addr
  end

  # connect's result: true with an address, false when the network refused
  # us, Network::ConnectTimeout when the time ran out
  def self.connected(rc, what)
    return true if rc == 0
    return false if rc == -1
    raise Network::ConnectTimeout, "#{what}: connection timeout" if rc == -2
    raise IOError, "#{what}: #{ESPNetwork.espinel_net_err_name(rc)}"
  end
end

module Network
  class ConnectTimeout < RuntimeError
  end

  class WiFi
    # the weakest security the access point may offer
    class Auth
      OPEN = ESPNetwork.espinel_wifi_auth_open
      WPA_TKIP_PSK = ESPNetwork.espinel_wifi_auth_wpa_psk
      WPA2_AES_PSK = ESPNetwork.espinel_wifi_auth_wpa2_psk
      WPA2_MIXED_PSK = ESPNetwork.espinel_wifi_auth_wpa_wpa2_psk
      WPA3_PSK = ESPNetwork.espinel_wifi_auth_wpa3_psk
    end

    LINK_DOWN = 0
    LINK_NOIP = 2
    LINK_UP = 3
    LINK_NONET = 5

    # country: a two-letter code for the channels allowed; force: accepted
    # for PicoRuby's sake, as its ESP32 port does, and ignored
    def self.init(country = nil, force: false)
      return true if initialized?
      err = ESPNetwork.espinel_wifi_init(country)
      return true if err == 0
      puts "Network::WiFi.init failed: #{ESPNetwork.espinel_net_err_name(err)}"
      false
    end

    def self.initialized?
      ESPNetwork.espinel_wifi_initialized != 0
    end

    # Joins ssid and waits up to timeout seconds for DHCP's address
    def self.connect_timeout(ssid, password, auth, timeout = 60)
      init unless initialized?
      rc = ESPNetwork.espinel_wifi_connect(ssid, password, auth, (timeout * 1000).to_i)
      ESPNetwork.connected(rc, "Wi-Fi #{ssid}")
    end

    def self.disconnect
      ESPNetwork.espinel_wifi_disconnect == 0
    end

    # The station is the only mode; init sets it.
    def self.enable_sta_mode
      true
    end

    def self.tcpip_link_status
      ESPNetwork.espinel_wifi_link_status
    end

    def self.tcpip_link_status_name
      case tcpip_link_status
      when LINK_DOWN then "LINK_DOWN"
      when LINK_NOIP then "LINK_NOIP"
      when LINK_UP then "LINK_UP"
      when LINK_NONET then "LINK_NONET"
      else "UNKNOWN_STATUS"
      end
    end

    def self.link_connected?
      tcpip_link_status == LINK_UP
    end

    def self.ipv4_address = ESPNetwork.address(ESPNetwork::WIFI, 0)
    def self.ipv4_netmask = ESPNetwork.address(ESPNetwork::WIFI, 1)
    def self.ipv4_gateway = ESPNetwork.address(ESPNetwork::WIFI, 2)

    def self.dhcp_supplied?
      ESPNetwork.espinel_net_dhcp_supplied(ESPNetwork::WIFI) != 0
    end
  end

  # QEMU's OpenCores Ethernet (CONFIG_ETH_USE_OPENETH): `idf.py qemu`
  # attaches it to QEMU's user-mode network, whose DHCP hands out 10.0.2.15.
  # There is no such device on a chip.
  class Ethernet
    # Starts the interface and waits up to timeout seconds for an address
    def self.connect_timeout(timeout = 60)
      ESPNetwork.connected(ESPNetwork.espinel_eth_connect((timeout * 1000).to_i), "Ethernet")
    end

    def self.link_connected?
      ESPNetwork.espinel_net_up(ESPNetwork::ETHERNET) != 0
    end

    def self.ipv4_address = ESPNetwork.address(ESPNetwork::ETHERNET, 0)
    def self.ipv4_netmask = ESPNetwork.address(ESPNetwork::ETHERNET, 1)
    def self.ipv4_gateway = ESPNetwork.address(ESPNetwork::ETHERNET, 2)

    def self.dhcp_supplied?
      ESPNetwork.espinel_net_dhcp_supplied(ESPNetwork::ETHERNET) != 0
    end
  end
end
