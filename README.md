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
| `spi` | `SPI.new(unit = 2, frequency: 1_000_000, mode: 0, first_bit: SPI::MSB_FIRST, sck_pin:, copi_pin:, cipo_pin:, cs_pin:)`, a controller on SPI2 or SPI3; `cs_pin:` (optional) is driven low for each call. `write(*outputs)` (Integers, Arrays of them, Strings), `read(bytes)` (sends zeros), `transfer(outputs, additional_read_bytes = 0)` answers the bytes clocked in, `setmode(...)` |

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

```ruby
require "spi"

# XIAO ESP32S3's D8, D10, D9, and D3 as the chip select
spi = SPI.new(sck_pin: 7, copi_pin: 9, cipo_pin: 8, cs_pin: 4)
id = spi.transfer(0x9f, 3).byteslice(1, 3)  # a flash chip's JEDEC ID
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
