# espinel

An ESP-IDF project that runs Ruby programs compiled ahead of time by
[Spinel](https://github.com/yuuu/spinel) on the ESP32-S3.

## Requirements

- Docker
- Ruby (see `.ruby-version`) and Bundler, for the Rake tasks
- A checkout of Spinel next to this directory (`../spinel`), with its
  dependencies fetched (`make deps`). Set `SPINEL_CHECKOUT` to use another
  location.

## Usage

```sh
bundle install
bundle exec rake image   # build the development image (ESP-IDF v5.5.4 plus Spinel)
bundle exec rake build   # build the firmware
bundle exec rake qemu    # build and run it on the emulated ESP32-S3
bundle exec rake clean   # remove build/
```

Rebuild the image (`rake image`) whenever the Spinel checkout changes.

`rake qemu` prints the console until 15 seconds pass; `rake qemu[30]` (or
`QEMU_SECONDS=30`) runs it longer. Press Ctrl+C to stop it earlier.

```
I (91) main_task: Calling app_main()
LED on
LED off
...
spinel_main returned 0 (stack left 30052 bytes, free heap 313928 bytes)
```

The Ruby program starts at `lib/main.rb`, the fixed entry point. Other files
under `lib/` are pulled in with `require_relative`. Edit them and run
`rake qemu` again.

## Packages

`require "<name>"` loads `packages/<name>/<name>.rb`, laid out as a spin
package; the C a package carries (`packages/<name>/*.c`) is built into the
firmware.

| Package | |
|---|---|
| `gpio` | `GPIO.new(pin, GPIO::OUT)` (or `GPIO::IN`, `GPIO::IN_OUT`); `high`, `low`, `write(0 or 1)`, `read`, `high?` |
| `machine` | `Machine.sleep(seconds)`: waits like `Kernel#sleep`, fractions allowed, returns the seconds rounded |

```ruby
require "gpio"
require "machine"

led = GPIO.new(2, GPIO::OUT)
loop do
  led.high
  Machine.sleep(0.5)
  led.low
  Machine.sleep(0.5)
end
```

Everything that needs ESP-IDF or Spinel runs in the container, as your user,
so `build/` and `sdkconfig` stay yours. `rake -T` lists the other tasks:

| Task | |
|---|---|
| `rake clobber` | remove `build/` and the generated `sdkconfig` |
| `rake size` | show the firmware's memory usage |
| `rake menuconfig` | open menuconfig |
| `rake c` | print the C that Spinel generates for `lib/main.rb` |
| `rake addr2line ADDRS='0x... 0x...'` | decode a backtrace |
| `rake shell` | open a shell in the container |
| `rake exec CMD='...'` | run a command in the container |
