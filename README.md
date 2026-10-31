# espinel

An ESP-IDF project that runs Ruby programs compiled ahead of time by
[Spinel](https://github.com/yuuu/spinel) on the ESP32-S3.

## Requirements

- Docker
- A checkout of Spinel next to this directory (`../spinel`), with its
  dependencies fetched (`make deps`)

## Usage

Build the development image (ESP-IDF v5.5.4 plus a host build of Spinel):

```sh
docker build --build-context spinel=../spinel -f docker/Dockerfile -t espinel-idf:v5.5.4 .
```

Rebuild the image whenever the Spinel checkout changes.

Build the firmware and run it on the emulated ESP32-S3 (QEMU). The console
is printed until the given number of seconds (default 15) pass:

```sh
scripts/run-qemu [SECONDS]
```

```
I (76) main_task: Calling app_main()
Hello, world!
spinel_main returned 0 (stack left 31580 bytes, free heap 320408 bytes)
```

The Ruby program is `lib/hello.rb`; edit it and run `scripts/run-qemu`
again.

Run any other command in the container with `scripts/idf`, for example:

```sh
scripts/idf idf.py build
scripts/idf idf.py size
scripts/idf idf.py menuconfig
```

The container runs as your user, so `build/` and `sdkconfig` stay yours.
