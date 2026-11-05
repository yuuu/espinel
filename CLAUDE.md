# CLAUDE.md

An ESP-IDF (v5.5.4) project that runs Ruby compiled by Spinel, a Ruby AOT
compiler, on the ESP32-S3. Usage is in README.md. Plans and findings are
kept in a separate work log, not in this repository.

## Rules

- Write everything in this repository in English: code, comments, commit
  messages, docs.
- Do not modify the Spinel checkout (`../spinel`, `/opt/spinel` in the
  container). Supply what ESP-IDF lacks from `components/spinel/port/`. When
  a problem can only be fixed in Spinel itself, work around it here and note
  the cause.
- Build and run only through the Rake tasks (`bundle exec rake -T`), which
  run ESP-IDF and Spinel in the Docker image. QEMU stands in for the board;
  when a board is attached on USB, `rake flash` and `rake monitor` reach
  it from the host (Docker on macOS cannot pass USB devices through).

## Layout

- `Rakefile`: host-side tasks. `container` runs a command in the image as
  the host user with the project mounted at `/project`; a new task that
  needs ESP-IDF or Spinel goes through it. The target SoC is whatever
  `sdkconfig` records (`rake set-target`, or `TARGET=` on `rake build`;
  ESP-IDF's default, `esp32`, when there is no `sdkconfig`).
  `sdkconfig.defaults` must not name `CONFIG_IDF_TARGET`: `idf.py
  set-target` refuses a target that differs from it. Per-target settings go in `sdkconfig.defaults.<target>`.
- `rake flash` and `rake monitor` use the esp_flash gem (yuuu/esp_flash on
  GitHub and RubyGems, a plain-Ruby ROM loader client and serial monitor).
  They read the chip, images and baud rate from `build/flasher_args.json`
  and `build/project_description.json`. Fix the gem there, not here.
- `docker/Dockerfile`: `espressif/idf:v5.5.4` (QEMU included) plus Spinel
  built from the `spinel` build context (with the prism gem, which Spinel's
  build runs Ruby generators with, and `make deps`). Sets
  `SPINEL_DIR=/opt/spinel` and puts `spinel` on `PATH`.
- `lib/main.rb`: the Ruby entry point (fixed). Other files in `lib/` are
  reached with `require_relative`, which Spinel resolves at compile time.
- `packages/<name>/`: spin-package layout (`<name>.rb`, `spin.toml`, carried
  `.c`). `require "<name>"` finds them through `spinel -I packages`; their
  `.c` files are compiled into the `main` component, whose `PRIV_REQUIRES`
  must list the ESP-IDF components they use. Keep ESP-IDF enums and macros
  on the C side behind plain `int`/`double` functions instead of copying
  values into Ruby.
- `main/`: compiles `lib/main.rb` to C at build time
  (`spinel -I packages ... -c --cc=<target gcc>`). The generated `main` is renamed with
  `-Dmain=spinel_main`, and `main.c` calls it from a FreeRTOS task with a
  32 KB stack (the main task's 3.5 KB is too small).
- `components/spinel/`: Spinel's runtime (`RT_MEMBERS` and `RE_SRC` of
  Spinel's Makefile, which the list in `CMakeLists.txt` must mirror),
  compiled from `$SPINEL_DIR/lib` as it is.
  - `port/*.h`, `port/sys/*.h`: headers found before the toolchain's. They
    either stand in for a missing header (`sys/mman.h`, `poll.h`,
    `ucontext.h`) or `#include_next` the real one and add what newlib or
    lwIP leaves out.
  - `port/sp_esp.c`: the missing POSIX calls, each failing the way an
    unsupported call fails on POSIX (`-1`/`errno`, `MAP_FAILED`), so the
    runtime takes the fallback it already has. Its `poll`, built on
    `select`, replaces ESP-IDF's (`sys/poll.h`, other `POLL*` values).
  - `port/sp_esp_config.h`: `-include`d into the runtime and the generated
    C. Holds `SP_NO_SIGNALS` and the shrunken static table sizes
    (`SP_GC_STACK_MAX` and others). Both halves of the program must see the
    same values, which is why it is a PUBLIC compile option.

The model for all of this is Spinel's own wasm32-wasi port
(`../spinel/lib/wasi/`, `docs/wasm.md`): read it before adding a stand-in.

## Things to know

- The runtime and the generated C are compiled with `-w`: ESP-IDF's
  `-Werror=all` fails on Spinel's code, which Spinel builds with
  `-Wno-all`.
- `--ext-init` (a library entry instead of `main`) does not compile for
  small programs, on any host: `spinel_rt.h`'s `SP_TU_NO_POLY_RENDER`
  fallbacks clash with the ext kernel's extern declarations. Hence the
  renamed `main`.
- `mmap` always fails here, so the slab allocator is off (every object is a
  `malloc`), `sp_main_stack_run` runs the program on the calling task's
  stack, and `Fiber` cannot get a stack. Thread is not supported.
- Integers are 32-bit (`sp_int` is the pointer width).
- `Kernel#sleep` does not link: newlib here has no `nanosleep`. Use
  `Machine.sleep`.
- A program that reaches more of the runtime can need a POSIX call not
  stubbed yet, showing up as a link error (`lstat` first did with
  `machine`, whose argument is an Integer or a Float). Add it to
  `port/sp_esp.c`.
- QEMU does not emulate the ESP32-S3's GPIO: writes are dropped and every
  GPIO register reads 0, so only that the calls succeed can be checked
  there. `esp_rom_delay_us` counts CPU cycles and runs about 4x long under
  QEMU; time with `esp_timer`, as `machine` does.
- An uncaught exception calls `exit()`, which ESP-IDF turns into `abort()`
  and a reboot, so QEMU prints the program again every few seconds.
- Memory: about 320 KB of free heap with internal RAM only. Spinel's GC
  thresholds (256 KB each for objects and strings) are sized for a desktop,
  and a failed `malloc`/`realloc` ends the program (`sp_oom_die`) without
  trying a collection first. Objects made by top-level code are not
  reclaimed by `GC.start` (on any host); work done inside methods is.
- New `sdkconfig.defaults` entries take effect only after deleting
  `sdkconfig` (and `build/`). Components that need `esp_psram` and the like
  must list it in their `REQUIRES`.
- `container` turns on ESP-IDF's ccache (`IDF_CCACHE_ENABLE`, cache in
  `/project/.ccache`) for every command, not only `idf.py build`: CMake
  caches the setting, and a call that differs makes it reconfigure.
- Debugging a crash: decode the backtrace with
  `rake addr2line ADDRS='<addrs>'`.
- The console is UART0 (what QEMU shows) with USB Serial/JTAG as a
  secondary, output-only console. `rake monitor` resets the board with the
  port held open; the USB Serial/JTAG reset does not re-enumerate, and
  reopening the port after a reset loses the program's first lines.
- Only the ESP32-S3 has been run (QEMU and a board). The ESP32-C3 builds;
  the other SoCs of issue #1 have not been tried.
- `rake qemu` prints from the ROM banner on, dropping the image's ESP-IDF
  activation output; `rake exec CMD='timeout 15 idf.py qemu </dev/null'`
  shows everything. `idf.py qemu monitor` needs a TTY.
- To inspect what Spinel generates, use `rake c`.
