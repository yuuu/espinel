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
bundle exec rake image   # build the development image (ESP-IDF v6.1 plus Spinel)
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

## Examples

`examples/<name>/` are programs ready to run on a Seeed Studio XIAO
ESP32S3. `PROJECT=<dir>` builds `<dir>/main.rb` in place of `lib/main.rb`,
for `rake build`, `qemu`, `flash` and `c`:

```sh
bundle exec rake set-target TARGET=esp32s3   # once
bundle exec rake flash monitor PROJECT=examples/blink
```

The directory can be one of your own, anywhere (relative to where rake
runs): a `main.rb` and the files it `require_relative`s. One outside this
checkout is mounted into the container. `require "<package>"` still finds
`packages/`.

| Example | |
|---|---|
| `blink` | blinks the board's own LED (GPIO21); nothing to wire |
| `hello` | plain Ruby (a class, blocks, Hashes, an exception) and `Memory.report`; also runs under QEMU |
| `wifi_https` | joins Wi-Fi and fetches https://example.com/ with the certificate checked; copy `config.example.rb` to `config.rb` (ignored by git) and set the SSID and password first |
| `aws_iot` | connects to AWS IoT Core over MQTT with TLS and the thing's certificate, publishes telemetry every 10 seconds and prints what arrives on `<thing>/cmd`; set Wi-Fi, the endpoint, the thing's name, certificate and key in `config.rb` (from `config.example.rb`) |
| `tasks` | two programs at once, one on each core: `main.rb` blinks the board's LED at a steady 5 Hz while `tasks/primes.rb` counts primes flat out on the other core and reports through a `Task::Queue`; also runs under QEMU |
| `oled` | "Spinel on ESP32" animated on a 128x64 SSD1306 OLED at 0x3C (SDA to D4, SCL to D5): a scan line draws the title, the subtitle types itself, a glint sweeps across; the driver and a 5x7 font are plain Ruby beside it |
| `lidar` | a radar of what a YDLIDAR X2 sees, on the `oled` example's SSD1306 (same wiring): one rotation as a map seen from above with a ring per metre, the scan frequency, the point count and the nearest point; the X2's TX to D0, VCC to 5V. The packet parser is plain Ruby beside it |

A build without `PROJECT` goes back to `lib/main.rb`.

## Packages

`require "<name>"` loads `packages/<name>/<name>.rb`, laid out as a spin
package; the C a package carries (`packages/<name>/*.c`) is built into the
firmware.

| Package | |
|---|---|
| `gpio` | `GPIO.new(pin, GPIO::OUT)` (or `GPIO::IN`, `GPIO::IN_OUT`), with `GPIO::PULL_UP` or `GPIO::PULL_DOWN` as a third argument (none by default); `high`, `low`, `write(0 or 1)`, `read`, `high?` |
| `machine` | `Machine.sleep(seconds)`: waits like `Kernel#sleep`, fractions allowed, returns the seconds rounded |
| `i2c` | `I2C.new(unit = 0, frequency: 100_000, scl_pin:, sda_pin:)`, a controller with 7-bit addresses (the internal pull-ups are on, weak); `write(addr, *outputs)` (Integers, Arrays of them, Strings) answers the bytes written, `read(addr, bytes, *outputs)` writes the outputs and reads after a repeated start, `scan` lists the addresses that ACK. A NACK or a timeout raises `IOError` |
| `mbedtls` | Mbed TLS, as in PicoRuby: `MbedTLS::Digest.new(:sha1 or :sha256)`, `MbedTLS::HMAC.new(key, "sha256")`, `MbedTLS::CMAC.new(key, "aes")`, `MbedTLS::Cipher.new("AES-128-CBC")` (and the 192/256-bit CBC and GCM suites, `Cipher.ciphers`), `MbedTLS::PKey::RSA` and `MbedTLS::PKey::EC` (`new(pem)`, `generate`, `sign(digest, data)`, `verify(digest, signature, data)`, `public_key`, `to_pem`). Results are binary Strings, as CRuby's OpenSSL gives; `free` does nothing, the GC frees everything |
| `memory` | `Memory.heap(region = :all)` (`:internal`, `:spiram`), `Memory.stack` (the Ruby task's: used here, peak, where `SystemStackError` comes) and `Memory.gc` (Spinel's counted bytes, next collection, collections) as Hashes of bytes; `Memory.report` prints them as three lines with bars, `Memory.summary` as one |
| `net/mqtt` | An MQTT 3.1.1 client, as in PicoRuby: `Net::MQTT::Client.new(host, port = 1883, client_id:, keep_alive: 60, clean_session: true, username:, password:, ssl: false, ssl_context:, will_topic:, will_payload:, will_qos:, will_retain:)` (or `Client.connect(...)` with a block); `connect`, `publish(topic, payload, retain: false, qos: 0)`, `subscribe(*topics, qos: 0)`, `unsubscribe(*topics)`, `receive(timeout: nil)` (`[topic, payload]`, or nil when the time ran out), `ping`, `disconnect`, `connected?`. QoS 0 and 1. A refused CONNECT raises `Net::MQTT::ConnectionError` |
| `network` | `Network::WiFi`, as in PicoRuby: `init(country = nil)`, `connect_timeout(ssid, password, auth, seconds = 60)` with `Network::WiFi::Auth::OPEN`, `WPA_TKIP_PSK`, `WPA2_AES_PSK`, `WPA2_MIXED_PSK` or `WPA3_PSK` (the weakest security accepted), `disconnect`, `link_connected?`, `tcpip_link_status(_name)`, `ipv4_address`, `ipv4_netmask`, `ipv4_gateway`, `dhcp_supplied?`. `Network::Ethernet.connect_timeout(seconds = 60)` and the same address readers, for QEMU |
| `pwm` | `PWM.new(pin, frequency:, duty: 50)` on the LEDC peripheral (without `frequency:` the pin is taken but not started); `frequency(hz)` (0 stops it, low), `period_us(us)`, `duty(percent)`, `pulse_width_us(us)`. One LEDC channel per PWM (8 on the ESP32-S3), at up to four frequencies at once |
| `spi` | `SPI.new(unit = 2, frequency: 1_000_000, mode: 0, first_bit: SPI::MSB_FIRST, sck_pin:, copi_pin:, cipo_pin:, cs_pin:)`, a controller on SPI2 or SPI3; `cs_pin:` (optional) is driven low for each call. `write(*outputs)` (Integers, Arrays of them, Strings), `read(bytes)` (sends zeros), `transfer(outputs, additional_read_bytes = 0)` answers the bytes clocked in, `setmode(...)` |
| `ssl_socket` | TLS clients, as in PicoRuby: `SSLSocket.open(host, port, ctx = SSLContext.new)`, or `SSLSocket.new(tcp_socket, ctx)` with `hostname=` and `connect`; `write`, `puts`, `print`, `gets`, `read`, `readpartial`, `eof?`, `close` (`sync_close` also closes the TCP socket, by default), `timeout=`, `cipher`, `ssl_version`, `verify_result`. `SSLContext`: `verify_mode=` (`VERIFY_PEER`, the default, or `VERIFY_NONE`), `set_ca_pem`, `set_cert_pem`, `set_key_pem` |
| `task` | `Task.new(:name, core: nil, priority: 5, stack_size: 32 * 1024)` starts `tasks/<name>.rb` on a FreeRTOS task of its own (see Tasks); `join(timeout = nil)`, `value` (its exit status), `alive?`; `Task.current`, `Task.core`, `Task.list`. `Task::Queue.new(:name, capacity = 8)` opens a named queue of Strings shared by every program: `push(str, timeout: nil)`, `pop(timeout: nil)` (nil when the time ran out), `size`, `empty?` |
| `uart` | `UART.new(unit, baudrate: 9600, txd_pin:, rxd_pin:)` (also `data_bits:`, `stop_bits:`, `parity: UART::EVEN or UART::ODD`, `flow_control: UART::RTSCTS` with `rts_pin:`, `cts_pin:`); `write(string)`, `puts`, `read(bytes)` and `gets` (both wait), `bytes_available`, `can_read_line`, `flush`, `clear_rx_buffer`, `send_break(seconds)`, `setmode(...)`. Unit 0 is the console |

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

The peripheral packages follow the
[peripheral interface guide](https://github.com/mruby/microcontroller-peripheral-interface-guide)
that mruby and mruby/c share, so programs and device drivers written for it
carry over. An error from ESP-IDF raises: `ArgumentError` for a setting it
refuses, `IOError` for a transfer that fails.

```ruby
require "uart"

uart = UART.new(1, baudrate: 115200, txd_pin: 1, rxd_pin: 2)  # D0, D1
uart.puts "hello"
line = uart.gets  # waits for a line, "\n" included
```

```ruby
require "i2c"

i2c = I2C.new(scl_pin: 6, sda_pin: 5)  # XIAO ESP32S3's D5, D4
p i2c.scan                           # => [0x44], say
i2c.write(0x44, 0x24, 0x00)          # SHT31: measure
raw = i2c.read(0x44, 6)
```

```ruby
require "spi"

# XIAO ESP32S3's D8, D10, D9, and D3 as the chip select
spi = SPI.new(sck_pin: 7, copi_pin: 9, cipo_pin: 8, cs_pin: 4)
id = spi.transfer(0x9f, 3).byteslice(1, 3)  # a flash chip's JEDEC ID
```

```ruby
require "pwm"

servo = PWM.new(2, frequency: 50)  # XIAO ESP32S3's D1
servo.pulse_width_us(1500)         # the middle
led = PWM.new(1, frequency: 1000, duty: 10)
```

### Tasks

Every `tasks/<name>.rb` beside the entry point (`lib/tasks/`, or
`<PROJECT>/tasks/`) is built into the firmware as a program of its own,
which `Task.new(:name)` starts on a FreeRTOS task of its own. Each program
is linked with its own copy of Spinel's runtime, so the programs share no
object, no global and no collector: they run on both cores at once, and an
exception, `exit` or `SystemStackError` in one ends that one only. They
talk through `Task::Queue`, named queues of Strings (a message is a copy;
send anything else as text, JSON say). `require_relative` and `require`
work in a task as in `main.rb`.

```ruby
# main.rb
require "task"

results = Task::Queue.new(:results)
worker = Task.new(:worker, core: 0)   # tasks/worker.rb
puts results.pop                      # waits for the worker's first line
worker.join

# tasks/worker.rb
require "task"

Task::Queue.new(:results).push("hello from core #{Task.core}")
```

- A program runs once a boot: Spinel's state is not reset, so a second
  `Task.new` of the same name raises `ArgumentError`.
- Each program costs its stack (32 KB unless `stack_size:` says otherwise)
  and about 5 KB more, out of about 300 KB. When it ends, its objects are
  freed, but what its globals and constants hold stays.
- A stack is one block of the heap. Start the programs early: once the
  others have churned objects, the heap can have room enough in all and no
  block that large, and `Task.new` raises `NoMemoryError`.
- Each program's collector sees only its own objects, and the heap is
  shared. An allocation that fails asks every program to collect at its
  next allocation and tries again for a second before the program ends
  out of memory; a program waiting in `pop` or `sleep` collects only when
  it next allocates.
- The peripherals are the firmware's: two programs that both set up the
  same pin, bus or the network get in each other's way, as two tasks in C
  would.
- `rake c TASK=<name>` prints the C of `tasks/<name>.rb`.

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

`require "ssl_socket"` adds TLS 1.2 over those sockets. With `VERIFY_PEER`
and no CA of its own, an `SSLContext` checks the server against ESP-IDF's
bundle of public root certificates, and its name against the one given to
`SSLSocket.open` (or `hostname=`); certificate dates are not checked. Close
each `SSLSocket`: a session holds about 20 KB the GC does not free, and
four can be open at once. With about 300 KB of RAM, a large response is
read in pieces (`gets`, `readpartial`) rather than whole: `read` of a
94 KB page runs out of memory.

```ruby
require "ssl_socket"

ssl = SSLSocket.open("example.com", 443)
ssl.write("GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n")
puts ssl.gets
ssl.close
```

`require "net/mqtt"` is an MQTT 3.1.1 client over those sockets, with
PicoRuby's `Net::MQTT::Client` interface. There is no background task:
`receive` sends the keep-alive pings while it waits, so a program that does
other work between calls should call `receive(timeout: ...)` (or `ping`)
more often than `keep_alive` seconds. For TLS, give `ssl: true` (the broker
is checked as `SSLSocket.open` checks it) or an `ssl_context:`, and
`require "ssl_socket"` too: a program without it leaves Mbed TLS out, about
200 KB.

```ruby
require "net/mqtt"

client = Net::MQTT::Client.new("broker.example.com", 1883, client_id: "xiao-1",
                               will_topic: "xiao-1/status", will_payload: "offline")
client.connect
client.publish("xiao-1/status", "online", retain: true)
client.subscribe("xiao-1/led", qos: 1)
loop do
  topic, payload = client.receive(timeout: 10)
  puts "#{topic}: #{payload}" if topic
  client.publish("xiao-1/uptime", Process.clock_gettime(Process::CLOCK_MONOTONIC).to_i.to_s)
end
```

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
