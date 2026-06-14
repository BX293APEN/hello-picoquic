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
# Windows 向け: picotls の pkg-config 依存をパッチで除去
#
# 【問題】
#   picotls/CMakeLists.txt には以下の2つの問題がある:
#     L12: FIND_PACKAGE(PkgConfig REQUIRED)  ← REQUIRED のため pkg-config がないとエラー
#     L74: PKG_CHECK_MODULES(BROTLI_DEC ...)  ← PkgConfig が未ロードなので未定義コマンド
#
# 【解決策】
#   FetchContent は「同名の最初の FetchContent_Declare が優先される」仕様のため、
#   add_subdirectory(picoquic) より前に FetchContent_Declare(picotls) を
#   PATCH_COMMAND 付きで宣言しておく。
#   picoquic 内部の FetchContent_Declare(picotls) は後から来るため無視される。
#
#   PATCH_COMMAND では cmake -P でパッチスクリプトを実行し、
#   ダウンロード済みの picotls/CMakeLists.txt を書き換える。
#   パッチは冪等 (__PATCHED_FOR_WINDOWS__ マーカーで二重適用を防止)。
#
# 【Linux への影響】
#   WIN32 ブロック内にのみ記述するため Linux ビルドには一切影響しない。
# ---------------------------------------------------------------------------
if(WIN32)
    include(FetchContent)

    # パッチスクリプトをビルドディレクトリに生成
    set(_ptls_patch_script "${CMAKE_BINARY_DIR}/patch_picotls_pkgconfig.cmake")
    file(WRITE "${_ptls_patch_script}" [=[
# patch_picotls_pkgconfig.cmake
# picotls の CMakeLists.txt から pkg-config 必須依存を除去する。
# このスクリプトは FetchContent の PATCH_COMMAND から呼ばれる。
# 作業ディレクトリは <picotls_SOURCE_DIR> になる。

set(_f "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
if(NOT EXISTS "${_f}")
    message(FATAL_ERROR "[patch] Not found: ${_f}")
endif()

file(READ "${_f}" _src)

# 冪等チェック: すでにパッチ済みならスキップ
if(_src MATCHES "__PATCHED_FOR_WINDOWS__")
    message(STATUS "[patch] picotls CMakeLists.txt already patched. Skipping.")
    return()
endif()

# パッチ1: REQUIRED を外す
string(REPLACE
    "FIND_PACKAGE(PkgConfig REQUIRED)"
    "FIND_PACKAGE(PkgConfig) # __PATCHED_FOR_WINDOWS__"
    _src "${_src}")

# パッチ2: PKG_CHECK_MODULES を IF(PkgConfig_FOUND) で囲む
#   対象行 (連続2行):
#     PKG_CHECK_MODULES(BROTLI_DEC libbrotlidec)
#     PKG_CHECK_MODULES(BROTLI_ENC libbrotlienc)
string(REPLACE
    "PKG_CHECK_MODULES(BROTLI_DEC libbrotlidec)"
    "IF(PkgConfig_FOUND) # __PATCHED_FOR_WINDOWS__\nPKG_CHECK_MODULES(BROTLI_DEC libbrotlidec)"
    _src "${_src}")
string(REPLACE
    "PKG_CHECK_MODULES(BROTLI_ENC libbrotlienc)"
    "PKG_CHECK_MODULES(BROTLI_ENC libbrotlienc)\nENDIF() # __PATCHED_FOR_WINDOWS__"
    _src "${_src}")

file(WRITE "${_f}" "${_src}")
message(STATUS "[patch] picotls CMakeLists.txt patched successfully.")
]=])

    # picoquic が使う picotls のタグ (picoquic の cmake/picotls.cmake 参照)
    # 空文字にすると HEAD になるため、picoquic のデフォルトタグを踏襲して master を指定
    # (実際のタグは picoquic の PICOQUIC_FETCH_PTLS_DEFAULT_TAG 変数で決まる)
    # ここでは GIT_TAG を指定せず GIT_REPOSITORY だけ合わせることで
    # 「同じリポジトリへの最初の Declare」として picoquic 側を上書きする。
    FetchContent_Declare(picotls
        GIT_REPOSITORY https://github.com/h2o/picotls.git
        PATCH_COMMAND  ${CMAKE_COMMAND}
                       "-DCMAKE_CURRENT_SOURCE_DIR=<SOURCE_DIR>"
                       -P "${_ptls_patch_script}"
    )
    message(STATUS "[win32] Registered picotls FetchContent with PATCH_COMMAND.")
endif()

# ---------------------------------------------------------------------------
# picotls と picoquic を add_subdirectory で取り込む
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

    # Windows: WinSock2 / BCrypt は明示リンクが必要
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
