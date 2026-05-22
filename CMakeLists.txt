cmake_minimum_required(VERSION 3.16)
project(hello_quic C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# デフォルトビルドタイプを Release (-O2) に設定
# cmake .. だけで呼んだ場合にも最適化が有効になる。
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not specified. Defaulting to Release.")
endif()

# Release フラグを -O2 -DNDEBUG に固定
# (CMake デフォルトは -O3。picoquic 側の SET(CMAKE_C_FLAGS "... -O2 ...") と
#  競合しないよう、CMAKE_C_FLAGS_RELEASE で明示的に上書きする)
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "C flags for Release" FORCE)

# ---------------------------------------------------------------------------
# picotls と picoquic を add_subdirectory で取り込む
#
# OpenSSL 不使用: WITH_OPENSSL=OFF
# picotls は picoquic の FETCH 機能で自動取得: PICOQUIC_FETCH_PTLS=ON
# picoquic / picotls のデモ・テスト類はビルドしない
# ---------------------------------------------------------------------------

set(WITH_OPENSSL         OFF CACHE BOOL   "" FORCE)
set(PICOQUIC_FETCH_PTLS  ON  CACHE BOOL   "" FORCE)

# WITH_OPENSSL=OFF でも picoquic の CMakeLists が OPENSSL_INCLUDE_DIR を
# target_include_directories に渡してしまうため、空にして
# "NOTFOUND" パスが cmake エラーにならないようにする。
set(OPENSSL_INCLUDE_DIR  ""  CACHE PATH   "" FORCE)
set(OPENSSL_LIBRARIES    ""  CACHE STRING "" FORCE)

# Fusion (AES-NI ベース) を無効化:
# - picotls の ptlsbench が WITH_FUSION=ON の場合に ALL ターゲットに含まれ、
#   OpenSSL ヘッダーを要求してビルドエラーになるのを防ぐ。
# - minicrypto バックエンドだけで十分。
set(WITH_FUSION          OFF CACHE BOOL "" FORCE)
set(PTLS_WITH_FUSION     OFF CACHE BOOL "" FORCE)

# picoquic 側オプション
set(BUILD_DEMO           OFF CACHE BOOL "" FORCE)
set(BUILD_HTTP           OFF CACHE BOOL "" FORCE)
set(BUILD_PQBENCH        OFF CACHE BOOL "" FORCE)
set(BUILD_LOGLIB         ON  CACHE BOOL "" FORCE)  # picoquic-log (autoqlog 等) が必要
set(BUILD_LOGREADER      OFF CACHE BOOL "" FORCE)
set(picoquic_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PICO_SIM       OFF CACHE BOOL "" FORCE)

# picoquic の cmake モジュールを先に参照できるよう MODULE PATH を設定
list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/cmake")

add_subdirectory(picoquic)

# ---------------------------------------------------------------------------
# サーバー実行ファイル
# ---------------------------------------------------------------------------
add_executable(hello_server src/hello_server.c)

target_include_directories(hello_server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/picoquic"
    "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/loglib"
)

target_link_libraries(hello_server PRIVATE
    picoquic-core
    picoquic-log
)

# ---------------------------------------------------------------------------
# クライアント実行ファイル
# ---------------------------------------------------------------------------
add_executable(hello_client src/hello_client.c)

target_include_directories(hello_client PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/picoquic"
    "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/loglib"
)

target_link_libraries(hello_client PRIVATE
    picoquic-core
    picoquic-log
)

# ---------------------------------------------------------------------------
# 便利情報
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== hello_quic ===")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C flags    : ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}")
message(STATUS "  Targets    : hello_server, hello_client")
message(STATUS "  Run server : ./hello_server [port] [cert.pem] [key.pem]")
message(STATUS "  Run client : ./hello_client [host] [port]")
message(STATUS "")
