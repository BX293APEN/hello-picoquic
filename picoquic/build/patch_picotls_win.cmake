
# patch_picotls_win.cmake
# picotls の CMakeLists.txt に Windows 向け3点パッチを適用する。
# FetchContent の PATCH_COMMAND から呼ばれる。
# CMAKE_CURRENT_SOURCE_DIR = <picotls_SOURCE_DIR>

set(_f "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")
if(NOT EXISTS "${_f}")
    message(FATAL_ERROR "[patch] Not found: ${_f}")
endif()

file(READ "${_f}" _src)

# 冪等チェック
if(_src MATCHES "__PATCHED_FOR_WINDOWS__")
    message(STATUS "[patch] Already patched. Skipping.")
    return()
endif()

# --- パッチ1: PkgConfig REQUIRED を外す ---
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

# --- パッチ3: INCLUDE_DIRECTORIES に picoquic/picoquic を追加 ---
# picotls.h が #ifdef _WINDOWS で wincompat.h を include するが、
# wincompat.h は picotls 自身に含まれず picoquic/picoquic/ にある。
# INCLUDE_DIRECTORIES ブロックの末尾 (${CMAKE_CURRENT_BINARY_DIR}) の後に追加する。
string(REPLACE
    "    ${CMAKE_CURRENT_BINARY_DIR})"
    "    ${CMAKE_CURRENT_BINARY_DIR}\n    D:/WorkSpace/GitHub/hello-picoquic/picoquic/picoquic) # __PATCHED_FOR_WINDOWS__"
    _src "${_src}")

file(WRITE "${_f}" "${_src}")
message(STATUS "[patch] picotls CMakeLists.txt: applied 3 Windows patches.")
