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
`QEMU_SECONDS=30`) runs it longer.

```
I (76) main_task: Calling app_main()
Hello, world!
spinel_main returned 0 (stack left 31580 bytes, free heap 320408 bytes)
```

The Ruby program is `lib/hello.rb`; edit it and run `rake qemu` again.

Everything that needs ESP-IDF or Spinel runs in the container, as your user,
so `build/` and `sdkconfig` stay yours. `rake -T` lists the other tasks:

| Task | |
|---|---|
| `rake clobber` | remove `build/` and the generated `sdkconfig` |
| `rake size` | show the firmware's memory usage |
| `rake menuconfig` | open menuconfig |
| `rake c` | print the C that Spinel generates for `lib/hello.rb` |
| `rake addr2line ADDRS='0x... 0x...'` | decode a backtrace |
| `rake shell` | open a shell in the container |
| `rake exec CMD='...'` | run a command in the container |
