# espinel

An ESP-IDF project that runs Ruby programs compiled ahead of time by
[Spinel](https://github.com/yuuu/spinel) on ESP32 SoCs. The ESP32-S3 is the
one it has been run on; the others are being brought up (see Targets).

## Requirements

- Docker
- Ruby (see `.ruby-version`) and Bundler, for the Rake tasks
- A checkout of Spinel next to this directory (`../spinel`). Set
  `SPINEL_CHECKOUT` to use another location.

## Usage

```sh
bundle install
bundle exec rake image   # build the development image (ESP-IDF v5.5.4 plus Spinel)
bundle exec rake build   # build the firmware
bundle exec rake qemu    # build and run it under QEMU
bundle exec rake flash   # build and write it to a board over USB
bundle exec rake monitor # reset the board and show its console
bundle exec rake clean   # remove build/
```

Rebuild the image (`rake image`) whenever the Spinel checkout changes.

Builds go through ccache, kept in `.ccache/`, so a build after `rake clean`
or a target switch mostly comes from the cache. `CCACHE=0` builds without
it.

`rake qemu` prints the console until 15 seconds pass; `rake qemu[30]` (or
`QEMU_SECONDS=30`) runs it longer. Press Ctrl+C to stop it earlier.

```
I (91) main_task: Calling app_main()
blinked; the LED now follows the button
button released
```

`rake flash` and `rake monitor` run on the host, since Docker on macOS
cannot pass a USB device into the container; they are plain Ruby
(the [esp_flash](https://github.com/yuuu/esp_flash) gem, installed by
`bundle install`), so the host needs no esptool or Python.
The port is `PORT=/dev/...`, or the one connected board. `rake flash monitor`
writes the firmware and then shows the output from the reset on. In the
monitor, Ctrl-] quits and Ctrl-T Ctrl-R resets the board. The console is
UART0 with USB Serial/JTAG as a secondary, output-only console, so the
board's output reaches USB but input from it does not reach the program.

## Targets

The SoC to build for is chosen with `rake set-target TARGET=esp32s3` (or
`rake build TARGET=esp32s3`); without one, ESP-IDF's default, the ESP32, is
used. Either switches the SoC through `idf.py set-target`, which regenerates `sdkconfig`
from `sdkconfig.defaults` plus `sdkconfig.defaults.<target>` where one
exists, and empties `build/`. The target then stays until it is changed
again. `rake flash`, `rake monitor` and `rake addr2line` follow it.

The Ruby program starts at `lib/main.rb`, the fixed entry point. Other files
under `lib/` are pulled in with `require_relative`. Edit them and run
`rake qemu` again.

## Packages

`require "<name>"` loads `packages/<name>/<name>.rb`, laid out as a spin
package; the C a package carries (`packages/<name>/*.c`) is built into the
firmware.

| Package | |
|---|---|
| `gpio` | `GPIO.new(pin, GPIO::OUT)` (or `GPIO::IN`, `GPIO::IN_OUT`), with `GPIO::PULL_UP` or `GPIO::PULL_DOWN` as a third argument (none by default); `high`, `low`, `write(0 or 1)`, `read`, `high?` |
| `machine` | `Machine.sleep(seconds)`: waits like `Kernel#sleep`, fractions allowed, returns the seconds rounded |
| `mbedtls` | Mbed TLS, as in PicoRuby: `MbedTLS::Digest.new(:sha1 or :sha256)`, `MbedTLS::HMAC.new(key, "sha256")`, `MbedTLS::CMAC.new(key, "aes")`, `MbedTLS::Cipher.new("AES-128-CBC")` (and the 192/256-bit CBC and GCM suites, `Cipher.ciphers`), `MbedTLS::PKey::RSA` and `MbedTLS::PKey::EC` (`new(pem)`, `generate`, `sign(digest, data)`, `verify(digest, signature, data)`, `public_key`, `to_pem`). Results are binary Strings, as CRuby's OpenSSL gives; `free` does nothing, the GC frees everything |
| `network` | `Network::WiFi`, as in PicoRuby: `init(country = nil)`, `connect_timeout(ssid, password, auth, seconds = 60)` with `Network::WiFi::Auth::OPEN`, `WPA_TKIP_PSK`, `WPA2_AES_PSK`, `WPA2_MIXED_PSK` or `WPA3_PSK` (the weakest security accepted), `disconnect`, `link_connected?`, `tcpip_link_status(_name)`, `ipv4_address`, `ipv4_netmask`, `ipv4_gateway`, `dhcp_supplied?`. `Network::Ethernet.connect_timeout(seconds = 60)` and the same address readers, for QEMU |

```ruby
require "gpio"
require "machine"

led = GPIO.new(1, GPIO::OUT)                     # XIAO ESP32S3's D0
button = GPIO.new(5, GPIO::IN, GPIO::PULL_DOWN)  # D4, a button to 3.3V
loop do
  led.write(button.read)
  Machine.sleep(0.01)
end
```

### Networking

Sockets are Spinel's own `require "socket"`: `TCPSocket`, `TCPServer`,
`UDPSocket`, `Socket` and `Addrinfo`, with CRuby's interface and
exceptions (`Socket::ResolutionError`, `Errno::ECONNREFUSED`,
`IO::TimeoutError`, ...), over ESP-IDF's lwIP. They need an interface with
an address first, which the `network` package brings up with PicoRuby's
`Network::WiFi` interface. `connect_timeout` answers true once DHCP has
given an address, false when the access point refuses (a wrong password,
no network with the security asked for), and raises
`Network::ConnectTimeout` when the time runs out.

```ruby
require "network"
require "socket"

Network::WiFi.init("JP")
Network::WiFi.connect_timeout("my-ssid", "my-password", Network::WiFi::Auth::WPA2_AES_PSK, 30) or abort "refused"
puts Network::WiFi.ipv4_address

s = TCPSocket.new("example.com", 80)
s.write("HEAD / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n")
puts s.gets
s.close
```

QEMU has no Wi-Fi (the driver stops in the RF calibration of
`connect_timeout`), but `rake qemu` attaches an OpenCores Ethernet to QEMU's
user-mode network. Call `Network::Ethernet.connect_timeout` in place of the
Wi-Fi calls and the same socket code runs there, with the address 10.0.2.15,
the host's network behind 10.0.2.2 and DNS at 10.0.2.3. Ethernet has no
device on a chip.

Everything that needs ESP-IDF or Spinel runs in the container, as your user,
so `build/` and `sdkconfig` stay yours. `rake -T` lists the other tasks:

| Task | |
|---|---|
| `rake set-target TARGET=<soc>` | switch the SoC to build for |
| `rake clobber` | remove `build/` and the generated `sdkconfig` |
| `rake ccache` | show ccache's statistics |
| `rake size` | show the firmware's memory usage |
| `rake menuconfig` | open menuconfig |
| `rake c` | print the C that Spinel generates for `lib/main.rb` |
| `rake addr2line ADDRS='0x... 0x...'` | decode a backtrace |
| `rake shell` | open a shell in the container |
| `rake exec CMD='...'` | run a command in the container |
