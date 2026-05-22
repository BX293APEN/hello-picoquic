# hello_quic

picoquic + picotls (minicrypto) を使った最小の QUIC "Hello, World!" サンプル。

- **OpenSSL 不使用** (picotls の minicrypto バックエンドのみ)
- **H3 / QPACK 不使用** (生 QUIC ストリームで直接テキストを送受信)
- **ビルドシステム**: CMake (依存ライブラリは FetchContent で自動取得)

---

## ディレクトリ構成

```
hello_quic_project/
├── CMakeLists.txt          # トップレベル CMake
├── README.md
├── certs/
│   ├── cert.pem            # テスト用自己署名証明書
│   └── key.pem             # テスト用秘密鍵
├── picoquic/               # picoquic ソース (add_subdirectory)
│   └── ...                 # picotls は picoquic が FetchContent で取得
└── src/
    ├── hello_server.c      # サーバー
    └── hello_client.c      # クライアント
```

---

## 依存パッケージ (Ubuntu / Debian)

```bash
sudo apt update
sudo apt install -y \
    cmake git build-essential \
    pkg-config
# OpenSSL は不要です
```

> picotls および picotls の依存ライブラリ (cifra, micro-ecc) は
> CMake の FetchContent / add_subdirectory で自動的に取得・ビルドされます。

---

## ビルド

```bash
cd hello_quic_project
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

ビルド成果物:

| ファイル | 説明 |
|---|---|
| `build/hello_server` | QUIC サーバー |
| `build/hello_client` | QUIC クライアント |

---

## 実行

### 1. サーバーを起動

```bash
cd hello_quic_project
./build/hello_server 4433 certs/cert.pem certs/key.pem
```

または引数省略 (デフォルト: ポート 4433、`certs/cert.pem`, `certs/key.pem`):

```bash
./build/hello_server
```

### 2. 別ターミナルでクライアントを実行

```bash
./build/hello_client 127.0.0.1 4433
```

### 期待されるサーバー出力

```
Starting Hello-QUIC server on port 4433
[server] connection almost ready.
[server] connection ready.
[server] received 4 bytes on stream 0: Hi!
[server] sent "Hello, World!\n"
[server] connection closed.
```

### 期待されるクライアント出力

```
Connecting to 127.0.0.1:4433
[client] packet loop ready.
[client] connection almost ready.
[client] connection ready. Opening stream...
[client] sent "Hi!\n"
[client] received 14 bytes (fin): Hello, World!
[client] stream fin received. Closing connection.
Client exiting, ret=0
```

---

## 仕組み

1. **クライアント**が QUIC 接続を確立する。
2. `picoquic_callback_ready` イベントで双方向ストリームを開き、`"Hi!\n"` を送信 (FIN 付き)。
3. **サーバー**がストリームの FIN を受信し、`"Hello, World!\n"` を返す (FIN 付き)。
4. **クライアント**がサーバーの FIN を受け取り、接続を閉じる。

TLS ハンドシェイクには picotls の minicrypto バックエンドを使用しており、
AES-GCM/ChaCha20-Poly1305 および P-256/X25519 鍵交換を OpenSSL なしで実現しています。

---

## カスタム証明書の生成

付属の `certs/` にはテスト用の自己署名証明書が同梱されています。
新たに生成したい場合は次のようにします:

```bash
openssl req -x509 -newkey rsa:2048 -keyout certs/key.pem \
    -out certs/cert.pem -days 3650 -nodes \
    -subj "/CN=localhost"
```

> クライアントは `picoquic_set_null_verifier()` で証明書検証を無効化しているため、
> 自己署名証明書でそのまま動作します。本番環境では検証を有効にしてください。
