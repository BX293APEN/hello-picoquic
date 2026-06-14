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
    set(CMAKE_C_FLAGS_RELEASE "/O2 /DNDEBUG /W3 /wd4996"
        CACHE STRING "C flags for Release (MSVC)" FORCE)
else()
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG"
        CACHE STRING "C flags for Release" FORCE)
endif()

# ---------------------------------------------------------------------------
# Windows 向け: picotls の CMakeLists.txt パッチ
#
# picotls は Linux 専用に書かれており Windows ではそのままビルドできない。
# FetchContent の「最初の Declare が優先される」仕様を利用して、
# add_subdirectory(picoquic) より前に PATCH_COMMAND 付きで先行宣言し、
# ダウンロード直後に CMakeLists.txt を書き換える。
#
# パッチ一覧:
#   1. FIND_PACKAGE(PkgConfig REQUIRED) → REQUIRED 削除
#   2. PKG_CHECK_MODULES(BROTLI_*) → IF(PkgConfig_FOUND) でガード
#   3. INCLUDE_DIRECTORIES に picoquic/picoquic/ を追加
#      (picotls.h が _WINDOWS 時に wincompat.h を要求するが
#       picotls 自身には存在せず picoquic/picoquic/ にある)
#   4. test-minicrypto.t を削除
#      (テストバイナリが sockaddr_in6 未定義等 Windows 非対応エラーを起こす)
#   5. GCC 専用の CMAKE_C_FLAGS 行を MSVC では /utf-8 に差し替え
#      (t/picotls.c が UTF-8 で書かれており MSVC がコードページ 932 として
#       誤解釈するため。また -std=c99 は MSVC では使えない)
#   6. include/wincompat.h を再生成して ws2tcpip.h を追加
#      (picotls の lib/picotls.c が sockaddr_in6 を使うが、
#       元の wincompat.h は Winsock2.h だけで ws2tcpip.h が抜けている)
# ---------------------------------------------------------------------------
if(WIN32)
    include(FetchContent)

    set(_picoquic_inc "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/picoquic")
    set(_ptls_patch   "${CMAKE_BINARY_DIR}/patch_picotls_win.cmake")

    file(WRITE "${_ptls_patch}" [=[
set(_f "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
if(NOT EXISTS "${_f}")
    message(FATAL_ERROR "[patch] Not found: ${_f}")
endif()
file(READ "${_f}" _src)
if(_src MATCHES "__PATCHED_FOR_WINDOWS__")
    message(STATUS "[patch] Already patched. Skipping.")
    return()
endif()

# --- パッチ1: PkgConfig REQUIRED を削除 ---
string(REPLACE
    "FIND_PACKAGE(PkgConfig REQUIRED)"
    "FIND_PACKAGE(PkgConfig) # __PATCHED_FOR_WINDOWS__"
    _src "${_src}")

# --- パッチ2: PKG_CHECK_MODULES を IF(PkgConfig_FOUND) でガード ---
string(REPLACE
    "PKG_CHECK_MODULES(BROTLI_DEC libbrotlidec)"
    "IF(PkgConfig_FOUND) # __PATCHED_FOR_WINDOWS__\nPKG_CHECK_MODULES(BROTLI_DEC libbrotlidec)"
    _src "${_src}")
string(REPLACE
    "PKG_CHECK_MODULES(BROTLI_ENC libbrotlienc)"
    "PKG_CHECK_MODULES(BROTLI_ENC libbrotlienc)\nENDIF() # __PATCHED_FOR_WINDOWS__"
    _src "${_src}")

# --- パッチ3: INCLUDE_DIRECTORIES に wincompat.h のあるパスを追加 ---
string(REPLACE
    "    ${CMAKE_CURRENT_BINARY_DIR})"
    "    ${CMAKE_CURRENT_BINARY_DIR}\n    @PICOQUIC_INC@) # __PATCHED_FOR_WINDOWS__"
    _src "${_src}")

# --- パッチ4: test-minicrypto.t ターゲット削除 ---
# Windows 非対応のテストバイナリ (sockaddr_in6 未定義・UTF-8 誤解釈等)
string(REPLACE
    "ADD_EXECUTABLE(test-minicrypto.t
    \${MINICRYPTO_LIBRARY_FILES}
    \${MINICRYPTO_AEGIS_FILES}
    deps/picotest/picotest.c
    \${CORE_TEST_FILES}
    t/minicrypto.c
    lib/asn1.c
    lib/pembase64.c
    lib/ffx.c
    lib/cifra/x25519.c
    lib/cifra/chacha20.c
    lib/cifra/aes128.c
    lib/cifra/aes256.c
    lib/cifra/random.c)
SET(TEST_EXES test-minicrypto.t)"
    "# test-minicrypto.t removed by __PATCHED_FOR_WINDOWS__\nSET(TEST_EXES)"
    _src "${_src}")

# --- パッチ5: GCC 専用 CMAKE_C_FLAGS を MSVC では /utf-8 に差し替え ---
# -std=c99 / -Wall / -O2 は MSVC では無効。
# /utf-8 を追加しないと t/picotls.c (UTF-8) が Shift-JIS として解釈されてエラーになる。
string(REPLACE
    "SET(CMAKE_C_FLAGS \"-std=c99 -Wall -O2 -g \${CC_WARNING_FLAGS} \${CMAKE_C_FLAGS}\")"
    "IF(MSVC) # __PATCHED_FOR_WINDOWS__\n  SET(CMAKE_C_FLAGS \"/utf-8 \${CMAKE_C_FLAGS}\")\nELSE()\n  SET(CMAKE_C_FLAGS \"-std=c99 -Wall -O2 -g \${CC_WARNING_FLAGS} \${CMAKE_C_FLAGS}\")\nENDIF()"
    _src "${_src}")

file(WRITE "${_f}" "${_src}")
message(STATUS "[patch] picotls CMakeLists.txt: 5 patches applied.")

# --- パッチ6: include/wincompat.h を ws2tcpip.h 追加版で上書き ---
# picotls の lib/picotls.c が sockaddr_in6 を使うが、
# 元の wincompat.h は Winsock2.h のみで ws2tcpip.h (sockaddr_in6 の定義元) が抜けている。
set(_wc "${CMAKE_CURRENT_SOURCE_DIR}/include/wincompat.h")
file(WRITE "${_wc}" "/*
 * wincompat.h - patched for Windows build (__PATCHED_FOR_WINDOWS__)
 * ws2tcpip.h added for sockaddr_in6 / inet_pton etc.
 */
#ifndef WINCOMPAT_H
#define WINCOMPAT_H

#include <stdint.h>
#define ssize_t int

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>   /* sockaddr_in6, inet_pton, InetNtop */
#include <windows.h>

#ifndef gettimeofday
#define gettimeofday wintimeofday

#ifndef __attribute__
#define __attribute__(X)
#endif

#pragma warning(disable : 4214)
#pragma warning(disable : 4996)

#ifdef __cplusplus
extern \"C\" {
#endif

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int wintimeofday(struct timeval *tv, struct timezone *tz);

#ifdef __cplusplus
} /* extern \"C\" */
#endif

#endif /* gettimeofday */

#endif /* WINCOMPAT_H */
")
message(STATUS "[patch] include/wincompat.h: ws2tcpip.h added.")
]=])

    # @PICOQUIC_INC@ を実際のパスに展開してからスクリプトを書き直す
    file(READ "${_ptls_patch}" _patch_content)
    string(REPLACE "@PICOQUIC_INC@" "${_picoquic_inc}" _patch_content "${_patch_content}")
    file(WRITE "${_ptls_patch}" "${_patch_content}")

    FetchContent_Declare(picotls
        GIT_REPOSITORY https://github.com/h2o/picotls.git
        PATCH_COMMAND  ${CMAKE_COMMAND}
                       "-DCMAKE_CURRENT_SOURCE_DIR=<SOURCE_DIR>"
                       -P "${_ptls_patch}"
    )
    message(STATUS "[win32] picotls FetchContent pre-declared with 6-point Windows patch.")
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
# 共通設定ヘルパー
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
    if(WIN32)
        # _WINDOWS マクロ: picosocks.h 等の #ifdef _WINDOWS 分岐を有効化
        target_compile_definitions(${TARGET_NAME} PRIVATE _WINDOWS)
        # WinSock2 / BCrypt は明示リンクが必要
        target_link_libraries(${TARGET_NAME} PRIVATE ws2_32 bcrypt)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# ターゲット定義 (glob をやめ明示指定)
# ---------------------------------------------------------------------------
add_executable(hello_server      src/hello_server.c)
add_executable(hello_client      src/hello_client.c)
add_executable(hello_server_loop src/hello_server_loop.c)

setup_quic_target(hello_server)
setup_quic_target(hello_client)
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
