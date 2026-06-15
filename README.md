# hello-picoquic

[picoquic](https://github.com/private-octopus/picoquic) と [picotls](https://github.com/h2o/picotls) のソースを取り込み、外部ライブラリに依存せず単独でビルドできるようにしたサンプルプロジェクトです。

- OpenSSL / mbedTLS は使用せず、picotls 内蔵の **minicrypto**（cifra + micro-ecc による純Cの暗号実装）のみを TLS バックエンドとして使用します。
- Windows / Linux のどちらでも、追加の暗号ライブラリをインストールせずにビルドできます。Raspberry Pi などの ARM 系 Linux でも同じ手順でビルド可能です。
- 実行ファイルは可能な範囲で静的リンクされます（Windows: `/MT`、Linux: `-static-libgcc -static-libstdc++`、任意で完全静的リンクも可能）。

## ディレクトリ構成

```
.
├── CMakeLists.txt      # トップレベルのビルド定義（ここだけで全体をビルドします）
├── src/                 # サンプルアプリ
├── picoquic/
│   ├── picoquic/        # picoquic 本体のソース (picoquic-core)
│   ├── loglib/          # qlog などのログ関連 (picoquic-log)
│   └── picohttp/        # HTTP/3, WebTransport など (picohttp-core)
└── picotls/
    ├── include/, lib/   # picotls 本体 (picotls-core)
    └── deps/            # cifra, micro-ecc (picotls-minicrypto)
```

## 必要環境

- CMake 3.13 以降
- C/C++ コンパイラ
  - Linux: GCC または Clang
  - Windows: Visual Studio (MSVC)
- スレッドライブラリ（Linux は pthread。CMake が自動検出します）

外部ライブラリ（OpenSSL, mbedTLS など）のインストールは不要です。

## ビルド方法 (Windows)

Visual Studio のコマンドプロンプト（または "Developer Command Prompt for VS"）から実行してください。

```bat
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

実行ファイルは `build\Release\` 以下に生成されます。MSVC のランタイムは静的 (`/MT`) にリンクされるため、配布時に VC++ 再頒布可能パッケージは不要です。

## ビルド方法 (Linux / Raspberry Pi 等)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

実行ファイル（`hello`, `hello_client`, `hello_server`, `hello_server_loop`, `main`）が `build/` 直下に生成されます。

完全に静的なバイナリ（libc も含めて静的リンク）を作りたい場合は、CMake の構成時に以下を追加してください。

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_FULLY_STATIC=ON
```

Raspberry Pi 上でビルドする場合も、クロスコンパイル用のツールチェインファイルを指定する以外は同じ手順です。

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=<your-arm-toolchain>.cmake
make -j$(nproc)
```

## 補足

- `hello_server` / `hello_server_loop` はデフォルトで `certs/cert.pem`, `certs/key.pem` を読み込みます。サーバを起動する場合は、minicrypto で読み込み可能な証明書・秘密鍵ファイルを別途用意し、コマンドライン引数で指定してください。
- TLS バックエンドは minicrypto 固定です（`CMakeLists.txt` 内で `PTLS_WITHOUT_OPENSSL` / `PTLS_WITHOUT_FUSION` を定義し、OpenSSL・mbedTLS・x86 専用の Fusion AES-GCM 実装を無効化しています）。
