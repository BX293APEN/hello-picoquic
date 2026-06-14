cmake_minimum_required(VERSION 3.16)
project(hello_quic C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# デフォルトビルドタイプを Release に設定
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not specified. Defaulting to Release.")
endif()

# ---------------------------------------------------------------------------
# プラットフォーム別コンパイルフラグ
# ---------------------------------------------------------------------------
if(MSVC)
    # /O2 最適化、/wd4996 非推奨関数警告を抑制
    set(CMAKE_C_FLAGS_RELEASE "/O2 /DNDEBUG /W3 /wd4996"
        CACHE STRING "C flags for Release (MSVC)" FORCE)
else()
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG"
        CACHE STRING "C flags for Release" FORCE)
endif()

# ---------------------------------------------------------------------------
# Windows 向け: picotls の CMakeLists.txt パッチ
#
# picotls は Linux 前提で書かれており Windows ではそのままビルドできない。
# FetchContent の「最初の Declare が優先される」仕様を利用して、
# add_subdirectory(picoquic) より前に PATCH_COMMAND 付きで先行宣言し、
# ダウンロード直後に CMakeLists.txt を書き換える。
#
# パッチ内容:
#   1. FIND_PACKAGE(PkgConfig REQUIRED) → REQUIRED を削除
#   2. PKG_CHECK_MODULES(BROTLI_*) → IF(PkgConfig_FOUND) でガード
#   3. INCLUDE_DIRECTORIES に picoquic/picoquic/ を追加
#      (picotls.h が _WINDOWS 時に #include "wincompat.h" するが、
#       wincompat.h は picotls 自身になく picoquic/picoquic/ にある)
# ---------------------------------------------------------------------------
if(WIN32)
    include(FetchContent)

    set(_picoquic_inc "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/picoquic")
    set(_ptls_patch   "${CMAKE_BINARY_DIR}/patch_picotls_win.cmake")

    file(WRITE "${_ptls_patch}" "
set(_f \"\${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt\")
if(NOT EXISTS \"\${_f}\")
    message(FATAL_ERROR \"[patch] Not found: \${_f}\")
endif()
file(READ \"\${_f}\" _src)
if(_src MATCHES \"__PATCHED_FOR_WINDOWS__\")
    message(STATUS \"[patch] Already patched.\")
    return()
endif()
# パッチ1: REQUIRED を削除
string(REPLACE
    \"FIND_PACKAGE(PkgConfig REQUIRED)\"
    \"FIND_PACKAGE(PkgConfig) # __PATCHED_FOR_WINDOWS__\"
    _src \"\${_src}\")
# パッチ2: PKG_CHECK_MODULES を IF(PkgConfig_FOUND) でガード
string(REPLACE
    \"PKG_CHECK_MODULES(BROTLI_DEC libbrotlidec)\"
    \"IF(PkgConfig_FOUND) # __PATCHED_FOR_WINDOWS__\\nPKG_CHECK_MODULES(BROTLI_DEC libbrotlidec)\"
    _src \"\${_src}\")
string(REPLACE
    \"PKG_CHECK_MODULES(BROTLI_ENC libbrotlienc)\"
    \"PKG_CHECK_MODULES(BROTLI_ENC libbrotlienc)\\nENDIF() # __PATCHED_FOR_WINDOWS__\"
    _src \"\${_src}\")
# パッチ3: INCLUDE_DIRECTORIES に wincompat.h のあるディレクトリを追加
string(REPLACE
    \"    \\\${CMAKE_CURRENT_BINARY_DIR})\"
    \"    \\\${CMAKE_CURRENT_BINARY_DIR}\\n    ${_picoquic_inc}) # __PATCHED_FOR_WINDOWS__\"
    _src \"\${_src}\")
file(WRITE \"\${_f}\" \"\${_src}\")
message(STATUS \"[patch] picotls CMakeLists.txt: 3 patches applied.\")
")

    FetchContent_Declare(picotls
        GIT_REPOSITORY https://github.com/h2o/picotls.git
        PATCH_COMMAND  ${CMAKE_COMMAND}
                       "-DCMAKE_CURRENT_SOURCE_DIR=<SOURCE_DIR>"
                       -P "${_ptls_patch}"
    )
    message(STATUS "[win32] picotls FetchContent pre-declared with Windows patches.")
endif()

# ---------------------------------------------------------------------------
# picotls / picoquic オプション
# ---------------------------------------------------------------------------
set(WITH_OPENSSL         OFF CACHE BOOL   "" FORCE)
set(PICOQUIC_FETCH_PTLS  ON  CACHE BOOL   "" FORCE)
set(OPENSSL_INCLUDE_DIR  ""  CACHE PATH   "" FORCE)
set(OPENSSL_LIBRARIES    ""  CACHE STRING "" FORCE)
set(WITH_FUSION          OFF CACHE BOOL   "" FORCE)
set(PTLS_WITH_FUSION     OFF CACHE BOOL   "" FORCE)
set(BUILD_DEMO           OFF CACHE BOOL   "" FORCE)
set(BUILD_HTTP           OFF CACHE BOOL   "" FORCE)
set(BUILD_PQBENCH        OFF CACHE BOOL   "" FORCE)
set(BUILD_LOGLIB         ON  CACHE BOOL   "" FORCE)
set(BUILD_LOGREADER      OFF CACHE BOOL   "" FORCE)
set(picoquic_BUILD_TESTS OFF CACHE BOOL   "" FORCE)
set(BUILD_PICO_SIM       OFF CACHE BOOL   "" FORCE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/cmake")
add_subdirectory(picoquic)

# ---------------------------------------------------------------------------
# 共通インクルード / リンク設定のヘルパー関数
# ---------------------------------------------------------------------------
function(setup_quic_target TARGET_NAME)
    target_include_directories(${TARGET_NAME} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/picoquic"
        "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/loglib"
    )
    target_link_libraries(${TARGET_NAME} PRIVATE
        picoquic-core
        picoquic-log
    )
    # Windows: _WINDOWS マクロを定義 (picosocks.h 等の #ifdef _WINDOWS 分岐に必要)
    #          WinSock2 / BCrypt は明示リンクが必要
    if(WIN32)
        target_compile_definitions(${TARGET_NAME} PRIVATE _WINDOWS)
        target_link_libraries(${TARGET_NAME} PRIVATE ws2_32 bcrypt)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# ターゲット定義
#
# src/*.c を glob で一括ビルドする代わりにターゲットを明示する。
# これにより不要なファイルを混入させず、各ターゲットの意図が明確になる。
# ---------------------------------------------------------------------------

# メインサンプル: Hello, World サーバー / クライアント
add_executable(hello_server src/hello_server.c)
setup_quic_target(hello_server)

add_executable(hello_client src/hello_client.c)
setup_quic_target(hello_client)

# 追加サンプル: エコーサーバー (複数ストリーム対応、ALPN="echo", port=7777)
add_executable(hello_server_loop src/hello_server_loop.c)
setup_quic_target(hello_server_loop)

# ---------------------------------------------------------------------------
# 便利情報
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== hello_quic ===")
message(STATUS "  Platform   : ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler   : ${CMAKE_C_COMPILER_ID}")
message(STATUS "  Build type : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C flags    : ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}")
message(STATUS "  Targets    : hello_server  hello_client  hello_server_loop")
if(WIN32)
    message(STATUS "  Run server : build\\Release\\hello_server.exe [port] [cert.pem] [key.pem]")
    message(STATUS "  Run client : build\\Release\\hello_client.exe [host] [port]")
    message(STATUS "  Run echo   : build\\Release\\hello_server_loop.exe [port] [cert.pem] [key.pem]")
else()
    message(STATUS "  Run server : ./hello_server [port] [cert.pem] [key.pem]")
    message(STATUS "  Run client : ./hello_client [host] [port]")
    message(STATUS "  Run echo   : ./hello_server_loop [port] [cert.pem] [key.pem]")
endif()
message(STATUS "")
