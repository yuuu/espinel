# spinel-esp32

[Spinel](https://github.com/yuuu/spinel)（RubyのAOTコンパイラ）でコンパイルしたRubyプログラムを、ESP-IDF上のESP32-S3で動かす検証用プロジェクトです。現在は `ruby/hello.rb`（`puts "Hello, world!"`）がQEMU上で動きます。

## 必要なもの

- Docker
- このディレクトリと同じ階層に置いたSpinelのチェックアウト（`../spinel`。`make deps` 済みのもの）

## 使い方

```sh
# 1. 開発用イメージを作る（ESP-IDF v5.5.4 + Spinelのホスト向けビルド）
docker build --build-context spinel=../spinel -f docker/Dockerfile -t spinel-esp32-idf:v5.5.4 .

# 2. ビルドしてQEMUで実行（15秒で終了）
scripts/run-qemu

# 任意のコマンドをコンテナ内で実行
scripts/idf idf.py build
scripts/idf idf.py size
```

実行すると、起動ログのあとに次のように表示されます。

```
I (76) main_task: Calling app_main()
Hello, world!
spinel_main returned 0 (stack left 31580 bytes, free heap 320408 bytes)
```

Spinelのソースを変えたときは、1.のイメージを作り直してください。

## 仕組み

```
ruby/hello.rb ──(ホストのspinel -c)──> build/esp-idf/main/hello_rb.c ─┐
                                                                       ├─> ESP-IDFでリンク
$SPINEL_DIR/lib/*.c ──(xtensa-esp32s3-elf-gcc)──> libspinel.a ──────────┘
```

- **`main/`**: ビルド時に `spinel ruby/hello.rb -c` でCを生成します。生成された `main` は `-Dmain=spinel_main` で名前を変え、`main.c` が32KBのスタックを持つFreeRTOSタスクから呼び出します（ESP-IDFのメインタスクのスタックは3.5KBしかありません）。
- **`components/spinel/`**: Spinelのランタイム（Makefileの `RT_MEMBERS` と `RE_SRC`）を、チェックアウトから**変更せずに**そのままコンパイルします。ESP-IDF（newlib / lwIP）に無いものは `port/` で補います。
  - `port/*.h`, `port/sys/*.h`: ツールチェーンのヘッダより先に読まれる置き換えヘッダです（`sys/mman.h`, `poll.h`, `ucontext.h`, `signal.h` の足りない定義など）。
  - `port/sp_esp.c`: `mmap`・`sigaction`・`*context` などを「失敗を返す」関数として定義します。ランタイムは失敗時の代替経路を持っているので、そちらで動きます（スラブを使わず `malloc`、メインスタックを確保せずタスクのスタックで実行、Fiberは使えない）。`poll` は `select` の上に実装しています。
  - `port/sp_esp_config.h`: すべてのソースの先頭で読み込まれる（`-include`）設定です。`SP_NO_SIGNALS` と、64bitホスト向けに約1.6MBある静的テーブルの縮小を指定しています。この縮小で、Spinel分の `.bss` は約33KBになりました。

## 分かっていること・制限

- **`--ext-init` は使っていません**: 小さなプログラムで `--ext-init` を付けて出力したCは、`spinel_rt.h` の `SP_TU_NO_POLY_RENDER` 用の代替定義と、ext用の `extern` 宣言がぶつかってコンパイルできません。x86-64でも起きるので、Spinel本体の問題です。
- **内部RAMだけではすぐメモリが尽きます**: 内部RAMだけ（空きヒープ約320KB）だと、数千個のオブジェクトと文字列を作るプログラムで `out of memory` になりました。失敗するのは `sp_pl_realloc` で、`malloc` / `realloc` が失敗したときにGCを試さず、その場で `sp_oom_die()` します。GCを起動するしきい値は64bitホスト向けで、オブジェクト用・文字列用が256KBずつ、古い文字列用が1MBあり、ESP32の内部RAMより大きい値です。また、しきい値は `main` より前に環境変数から読まれます。PSRAMを有効にする（`CONFIG_SPIRAM=y`、`CONFIG_SPIRAM_USE_MALLOC=y`、`main` の `PRIV_REQUIRES` に `esp_psram`）と、同じプログラムが最後まで動きました。
- **Integerは32bit**: `2**40` は `RangeError` になります（`--int-overflow=promote` で多倍長整数になります）。
- **未捕捉の例外で再起動します**: 未捕捉の例外で `exit()` が呼ばれると、ESP-IDFでは `abort()` になり再起動します。
- **未対応**: Fiber / Thread / プロセス / シグナルは使えません。ソケット・ファイル・時刻は未検証です。
