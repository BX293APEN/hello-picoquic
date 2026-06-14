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
# 【問題1】 L12: FIND_PACKAGE(PkgConfig REQUIRED)
#   Windows に pkg-config がないためエラー → REQUIRED を削除
#
# 【問題2】 L74-75: PKG_CHECK_MODULES(BROTLI_*)
#   PkgConfig 未ロード時は未定義コマンド → IF(PkgConfig_FOUND) でガード
#
# 【問題3】 picotls.h の L30: #ifdef _WINDOWS → #include "wincompat.h"
#   wincompat.h は picotls 自身には存在せず picoquic/picoquic/ にある。
#   picotls の INCLUDE_DIRECTORIES に picoquic/picoquic/ を追加する必要がある。
#   → INCLUDE_DIRECTORIES ブロックの末尾に追記するパッチを適用。
#
# 【解決策】
#   FetchContent は「最初の FetchContent_Declare が優先される」仕様を利用し、
#   add_subdirectory(picoquic) より前に PATCH_COMMAND 付きで先行宣言する。
#   picoquic 内部の FetchContent_Declare(picotls) は後から来るため無視される。
# ---------------------------------------------------------------------------
if(WIN32)
    include(FetchContent)

    # picoquic/picoquic/ (wincompat.h の場所) への絶対パスを計算
    set(_picoquic_src_dir "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/picoquic")

    # パッチスクリプトをビルドディレクトリに生成
    set(_ptls_patch_script "${CMAKE_BINARY_DIR}/patch_picotls_win.cmake")
    file(WRITE "${_ptls_patch_script}" "
# patch_picotls_win.cmake
# picotls の CMakeLists.txt に Windows 向け3点パッチを適用する。
# FetchContent の PATCH_COMMAND から呼ばれる。
# CMAKE_CURRENT_SOURCE_DIR = <picotls_SOURCE_DIR>

set(_f \"\${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt\")
if(NOT EXISTS \"\${_f}\")
    message(FATAL_ERROR \"[patch] Not found: \${_f}\")
endif()

file(READ \"\${_f}\" _src)

# 冪等チェック
if(_src MATCHES \"__PATCHED_FOR_WINDOWS__\")
    message(STATUS \"[patch] Already patched. Skipping.\")
    return()
endif()

# --- パッチ1: PkgConfig REQUIRED を外す ---
string(REPLACE
    \"FIND_PACKAGE(PkgConfig REQUIRED)\"
    \"FIND_PACKAGE(PkgConfig) # __PATCHED_FOR_WINDOWS__\"
    _src \"\${_src}\")

# --- パッチ2: PKG_CHECK_MODULES を IF(PkgConfig_FOUND) でガード ---
string(REPLACE
    \"PKG_CHECK_MODULES(BROTLI_DEC libbrotlidec)\"
    \"IF(PkgConfig_FOUND) # __PATCHED_FOR_WINDOWS__\\nPKG_CHECK_MODULES(BROTLI_DEC libbrotlidec)\"
    _src \"\${_src}\")
string(REPLACE
    \"PKG_CHECK_MODULES(BROTLI_ENC libbrotlienc)\"
    \"PKG_CHECK_MODULES(BROTLI_ENC libbrotlienc)\\nENDIF() # __PATCHED_FOR_WINDOWS__\"
    _src \"\${_src}\")

# --- パッチ3: INCLUDE_DIRECTORIES に picoquic/picoquic を追加 ---
# picotls.h が #ifdef _WINDOWS で wincompat.h を include するが、
# wincompat.h は picotls 自身に含まれず picoquic/picoquic/ にある。
# INCLUDE_DIRECTORIES ブロックの末尾 (\${CMAKE_CURRENT_BINARY_DIR}) の後に追加する。
string(REPLACE
    \"    \${CMAKE_CURRENT_BINARY_DIR})\"
    \"    \${CMAKE_CURRENT_BINARY_DIR}\\n    ${_picoquic_src_dir}) # __PATCHED_FOR_WINDOWS__\"
    _src \"\${_src}\")

file(WRITE \"\${_f}\" \"\${_src}\")
message(STATUS \"[patch] picotls CMakeLists.txt: applied 3 Windows patches.\")
")

    # picoquic が使うタグと同じリポジトリを先行宣言 (first-declaration-wins)
    FetchContent_Declare(picotls
        GIT_REPOSITORY https://github.com/h2o/picotls.git
        PATCH_COMMAND  ${CMAKE_COMMAND}
                       "-DCMAKE_CURRENT_SOURCE_DIR=<SOURCE_DIR>"
                       -P "${_ptls_patch_script}"
    )
    message(STATUS "[win32] picotls FetchContent registered with 3-point Windows patch.")
endif()

# ---------------------------------------------------------------------------
# picotls / picoquic オプション
# ---------------------------------------------------------------------------
set(WITH_OPENSSL         OFF CACHE BOOL   "" FORCE)
set(PICOQUIC_FETCH_PTLS  ON  CACHE BOOL   "" FORCE)

set(OPENSSL_INCLUDE_DIR  ""  CACHE PATH   "" FORCE)
set(OPENSSL_LIBRARIES    ""  CACHE STRING "" FORCE)

set(WITH_FUSION          OFF CACHE BOOL "" FORCE)
set(PTLS_WITH_FUSION     OFF CACHE BOOL "" FORCE)

set(BUILD_DEMO           OFF CACHE BOOL "" FORCE)
set(BUILD_HTTP           OFF CACHE BOOL "" FORCE)
set(BUILD_PQBENCH        OFF CACHE BOOL "" FORCE)
set(BUILD_LOGLIB         ON  CACHE BOOL "" FORCE)
set(BUILD_LOGREADER      OFF CACHE BOOL "" FORCE)
set(picoquic_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PICO_SIM       OFF CACHE BOOL "" FORCE)

list(APPEND CMAKE_MODULE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/picoquic/cmake")

add_subdirectory(picoquic)

# ---------------------------------------------------------------------------
# src/ 内の .c ファイルをすべてビルド
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

    if(WIN32)
        target_link_libraries(${TARGET_NAME} PRIVATE
            ws2_32
            bcrypt
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
message(STATUS "  C flags    : ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_RELEASE}")
message(STATUS "  Targets    : all .c files in src/ (one executable each)")
if(WIN32)
    message(STATUS "  Run server : build\\Release\\hello_server.exe [port] [cert.pem] [key.pem]")
    message(STATUS "  Run client : build\\Release\\hello_client.exe [host] [port]")
else()
    message(STATUS "  Run server : ./hello_server [port] [cert.pem] [key.pem]")
    message(STATUS "  Run client : ./hello_client [host] [port]")
endif()
message(STATUS "")
