cmake_minimum_required(VERSION 3.16)
project(hello_quic C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# デフォルトビルドタイプを Release に設定
# cmake .. だけで呼んだ場合にも最適化が有効になる。
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not specified. Defaulting to Release.")
endif()

# ---------------------------------------------------------------------------
# プラットフォーム別コンパイルフラグ
#
# Windows (MSVC) は -O2 ではなく /O2 を使う。
# GCC/Clang 系 (Linux / MinGW) は -O2 -DNDEBUG。
# ---------------------------------------------------------------------------
if(MSVC)
    # MSVC: /O2 最適化、/W3 警告レベル、/wd4996 非推奨関数警告を抑制
    set(CMAKE_C_FLAGS_RELEASE "/O2 /DNDEBUG /W3 /wd4996"
        CACHE STRING "C flags for Release (MSVC)" FORCE)
else()
    # GCC / Clang (Linux, MinGW-w64 など)
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG"
        CACHE STRING "C flags for Release" FORCE)
endif()

# ---------------------------------------------------------------------------
# Windows 向け: pkg-config が存在しない環境でのエラーを回避
#
# picotls の CMakeLists.txt は find_package(PkgConfig) を呼ぶが、
# Windows には pkg-config がないため CMake エラーになる。
# CMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON でその検索自体をスキップさせる。
# ---------------------------------------------------------------------------
if(WIN32)
    set(CMAKE_DISABLE_FIND_PACKAGE_PkgConfig ON CACHE BOOL "" FORCE)
endif()

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
# src/ 内の .c ファイルをすべてビルド
# ファイル名（拡張子なし）をそのままターゲット名にする
# ---------------------------------------------------------------------------
file(GLOB SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

foreach(SRC_FILE ${SRC_FILES})
    get_filename_component(TARGET_NAME ${SRC_FILE} NAME_WE)

    add_executable(${TARGET_NAME} ${SRC_FILE})

    target_include_directories(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/picoquic"
        "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/loglib"
    )

    target_link_libraries(${TARGET_NAME} PRIVATE
        picoquic-core
        picoquic-log
    )

    # -----------------------------------------------------------------------
    # Windows 向け追加リンク設定
    #
    # picoquic は内部で WinSock2 / BCrypt を使用するが、
    # MSVC 環境では自動リンクされないことがある。
    # -----------------------------------------------------------------------
    if(WIN32)
        target_link_libraries(${TARGET_NAME} PRIVATE
            ws2_32      # WinSock2 (ソケット API)
            bcrypt      # BCrypt  (乱数生成 / 暗号プリミティブ)
        )
    endif()

endforeach()

# ---------------------------------------------------------------------------
# 便利情報
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== hello_quic ===")
message(STATUS "  Platform   : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler   : ${CMAKE_C_COMPILER_ID}")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
if(MSVC)
    message(STATUS "  C flags    : ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}")
else()
    message(STATUS "  C flags    : ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}")
endif()
message(STATUS "  Targets    : all .c files in src/ (one executable each)")
if(WIN32)
    message(STATUS "  Run server : hello_server.exe [port] [cert.pem] [key.pem]")
    message(STATUS "  Run client : hello_client.exe [host] [port]")
else()
    message(STATUS "  Run server : ./hello_server [port] [cert.pem] [key.pem]")
    message(STATUS "  Run client : ./hello_client [host] [port]")
endif()
message(STATUS "")
