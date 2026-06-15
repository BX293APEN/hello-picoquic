cmake_minimum_required(VERSION 3.13)
project(hello-picoquic C CXX)

# ---------------------------------------------------------------------------
# Language standard
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

# ---------------------------------------------------------------------------
# Static build flags (Windows / MSVC and Linux / GCC-Clang, incl. ARM/Raspberry Pi)
# ---------------------------------------------------------------------------
if(MSVC)
    # Switch the MSVC runtime from the DLL CRT (/MD, /MDd) to the static CRT
    # (/MT, /MTd) for every configuration.
    foreach(flag_var
            CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
            CMAKE_C_FLAGS_MINSIZEREL CMAKE_C_FLAGS_RELWITHDEBINFO
            CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
            CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
        if(${flag_var} MATCHES "/MD")
            string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
        endif()
    endforeach()
else()
    # GCC / Clang (Linux x86_64, and Linux/ARM such as Raspberry Pi):
    # statically link the compiler runtime libraries.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")

    # Optionally produce a fully static executable (no shared libs at all,
    # including libc).
    option(BUILD_FULLY_STATIC "Link everything statically with -static (incl. libc)" OFF)
    if(BUILD_FULLY_STATIC)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    endif()
endif()

# ---------------------------------------------------------------------------
# Threads (used by picoquic-core on all platforms)
# ---------------------------------------------------------------------------
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Source locations (left over from the picoquic / picotls checkouts)
# ---------------------------------------------------------------------------
set(PICOQUIC_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/picoquic/picoquic)
set(LOGLIB_SRC_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/picoquic/loglib)
set(PICOHTTP_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/picoquic/picohttp)
set(PICOTLS_DIR      ${CMAKE_CURRENT_SOURCE_DIR}/picotls)

# ---------------------------------------------------------------------------
# Crypto backend selection
#
#  * PTLS_WITHOUT_OPENSSL : compile picotls/picoquic without any OpenSSL
#                            dependency. picoquic_ptls_openssl.c collapses to
#                            a no-op stub and lib/openssl.c is never built.
#  * PTLS_WITHOUT_FUSION  : skip picotls' x86-only AVX2 "fusion" AES-GCM
#                            engine (picoquic_ptls_fusion.c collapses to a
#                            no-op stub). Required to build cleanly on ARM
#                            (Raspberry Pi) and on 32-bit Windows.
#
# picoquic_mbedtls.c is also a no-op stub as long as PICOQUIC_WITH_MBEDTLS is
# left undefined (we never define it). With all three switches set this way,
# the only active TLS crypto provider is picotls-minicrypto: a pure-C
# implementation (cifra + micro-ecc) with no external library dependency,
# which builds the same way on Windows, Linux x86_64 and Linux/ARM.
# ---------------------------------------------------------------------------
set(PROJECT_COMPILE_DEFINITIONS
    PTLS_WITHOUT_OPENSSL
    PTLS_WITHOUT_FUSION)

if(UNIX)
    # picotls' own build adds this for all its sources on Linux/Unix.
    list(APPEND PROJECT_COMPILE_DEFINITIONS _GNU_SOURCE)
endif()

function(picoquic_apply_common_settings target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS ON)
    target_compile_definitions(${target} PRIVATE ${PROJECT_COMPILE_DEFINITIONS})
    if(MSVC)
        target_compile_options(${target} PRIVATE /wd4100 /wd4204 /wd4996)
    else()
        target_compile_options(${target} PRIVATE -Wall)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# picotls-core: TLS 1.3 handshake / record layer, crypto-backend agnostic.
# ---------------------------------------------------------------------------
add_library(picotls-core STATIC
    ${PICOTLS_DIR}/lib/picotls.c
    ${PICOTLS_DIR}/lib/pembase64.c
    ${PICOTLS_DIR}/lib/hpke.c)

target_include_directories(picotls-core PUBLIC
    ${PICOTLS_DIR}/include
    # On Windows, picotls' "#ifdef _WINDOWS / #include \"wincompat.h\""
    # resolves to picoquic's wincompat.h shim.
    ${PICOQUIC_SRC_DIR})

picoquic_apply_common_settings(picotls-core)

# ---------------------------------------------------------------------------
# picotls-minicrypto: pure-C crypto backend (cifra + micro-ecc). This is the
# only TLS crypto provider linked into this build.
# ---------------------------------------------------------------------------
add_library(picotls-minicrypto STATIC
    ${PICOTLS_DIR}/deps/micro-ecc/uECC.c
    ${PICOTLS_DIR}/deps/cifra/src/aes.c
    ${PICOTLS_DIR}/deps/cifra/src/blockwise.c
    ${PICOTLS_DIR}/deps/cifra/src/chacha20.c
    ${PICOTLS_DIR}/deps/cifra/src/chash.c
    ${PICOTLS_DIR}/deps/cifra/src/curve25519.c
    ${PICOTLS_DIR}/deps/cifra/src/drbg.c
    ${PICOTLS_DIR}/deps/cifra/src/hmac.c
    ${PICOTLS_DIR}/deps/cifra/src/gcm.c
    ${PICOTLS_DIR}/deps/cifra/src/gf128.c
    ${PICOTLS_DIR}/deps/cifra/src/modes.c
    ${PICOTLS_DIR}/deps/cifra/src/poly1305.c
    ${PICOTLS_DIR}/deps/cifra/src/sha256.c
    ${PICOTLS_DIR}/deps/cifra/src/sha512.c
    ${PICOTLS_DIR}/lib/cifra.c
    ${PICOTLS_DIR}/lib/cifra/x25519.c
    ${PICOTLS_DIR}/lib/cifra/chacha20.c
    ${PICOTLS_DIR}/lib/cifra/aes128.c
    ${PICOTLS_DIR}/lib/cifra/aes256.c
    ${PICOTLS_DIR}/lib/cifra/random.c
    ${PICOTLS_DIR}/lib/minicrypto-pem.c
    ${PICOTLS_DIR}/lib/uecc.c
    ${PICOTLS_DIR}/lib/asn1.c
    ${PICOTLS_DIR}/lib/ffx.c)

# deps/cifra and deps/micro-ecc headers are referenced with bare names
# (e.g. "aes.h", "uECC.h") from within lib/cifra*, lib/uecc.c, etc.
target_include_directories(picotls-minicrypto PRIVATE
    ${PICOTLS_DIR}/deps/cifra/src/ext
    ${PICOTLS_DIR}/deps/cifra/src
    ${PICOTLS_DIR}/deps/micro-ecc)

target_link_libraries(picotls-minicrypto PUBLIC picotls-core)

if(WIN32)
    # lib/cifra/random.c uses the CNG BCrypt RNG for ptls_minicrypto_random_bytes().
    target_link_libraries(picotls-minicrypto PUBLIC bcrypt)
endif()

picoquic_apply_common_settings(picotls-minicrypto)

# ---------------------------------------------------------------------------
# picoquic-core
# ---------------------------------------------------------------------------
set(PICOQUIC_LIBRARY_FILES
    bbr.c
    bbr1.c
    bytestream.c
    cc_common.c
    config.c
    cubic.c
    c4.c
    dualq_aqm.c
    ech.c
    error_names.c
    fastcc.c
    frames.c
    frame_names.c
    intformat.c
    logger.c
    logwriter.c
    loss_recovery.c
    newreno.c
    pacing.c
    packet.c
    packet_names.c
    paths.c
    performance_log.c
    picohash.c
    picoquic_lb.c
    picoquic_ptls_fusion.c
    picoquic_ptls_minicrypto.c
    picoquic_ptls_openssl.c
    picoquic_mbedtls.c
    picosocks.c
    picosplay.c
    port_blocking.c
    prague.c
    qmux.c
    quicctx.c
    register_all_cc_algorithms.c
    sacks.c
    sender.c
    sim_link.c
    siphash.c
    sockloop.c
    spinbit.c
    streams.c
    ticket_store.c
    timing.c
    token_store.c
    tls_api.c
    tp_names.c
    transport.c
    unified_log.c
    util.c)
list(TRANSFORM PICOQUIC_LIBRARY_FILES PREPEND ${PICOQUIC_SRC_DIR}/)

add_library(picoquic-core STATIC ${PICOQUIC_LIBRARY_FILES})

target_include_directories(picoquic-core PUBLIC ${PICOQUIC_SRC_DIR})

target_link_libraries(picoquic-core PUBLIC
    picotls-core
    picotls-minicrypto
    Threads::Threads
    # picoquic-core (config.c / sockloop.c) calls picoquic_set_qlog(), which
    # is implemented in picoquic-log (loglib/qlog_fns.c). This makes the two
    # static libraries mutually dependent; CMake repeats both archives on the
    # final link line as needed to resolve the cycle.
    picoquic-log)

if(WIN32)
    # sockloop.c / picosocks.h: Winsock2 + IP Helper API + extended socket I/O.
    # crypt32 / advapi32: referenced via #pragma comment by deps/micro-ecc
    # (kept explicit here for MinGW, where #pragma comment(lib, ...) is ignored).
    target_link_libraries(picoquic-core PUBLIC ws2_32 iphlpapi crypt32 advapi32)
elseif(UNIX)
    target_link_libraries(picoquic-core PUBLIC m)
endif()

picoquic_apply_common_settings(picoquic-core)

# ---------------------------------------------------------------------------
# picoquic-log (qlog / binlog / svg helpers)
# ---------------------------------------------------------------------------
set(LOGLIB_LIBRARY_FILES
    autoqlog.c
    cidset.c
    csv.c
    logconvert.c
    logreader.c
    memory_log.c
    qlog.c
    qlog_frames.c
    qlog_fns.c
    svg.c)
list(TRANSFORM LOGLIB_LIBRARY_FILES PREPEND ${LOGLIB_SRC_DIR}/)

add_library(picoquic-log STATIC ${LOGLIB_LIBRARY_FILES})

target_include_directories(picoquic-log PUBLIC ${LOGLIB_SRC_DIR})

target_link_libraries(picoquic-log PUBLIC picoquic-core)

picoquic_apply_common_settings(picoquic-log)

# ---------------------------------------------------------------------------
# picohttp-core (HTTP/3, WebTransport, quicperf demo helpers)
# ---------------------------------------------------------------------------
set(PICOHTTP_LIBRARY_FILES
    democlient.c
    demoserver.c
    h3zero.c
    h3zero_client.c
    h3zero_common.c
    h3zero_server.c
    h3zero_uri.c
    h3zero_url_template.c
    picomask.c
    quicperf.c
    picoquic_ns.c
    webtransport.c)
list(TRANSFORM PICOHTTP_LIBRARY_FILES PREPEND ${PICOHTTP_SRC_DIR}/)

add_library(picohttp-core STATIC ${PICOHTTP_LIBRARY_FILES})

target_include_directories(picohttp-core PUBLIC ${PICOHTTP_SRC_DIR})

target_link_libraries(picohttp-core PUBLIC
    picoquic-core
    picoquic-log)

picoquic_apply_common_settings(picohttp-core)

# ---------------------------------------------------------------------------
# src/: every .c file becomes its own executable, statically linked against
# picoquic + picotls-minicrypto (no OpenSSL / mbedTLS anywhere in the tree).
# ---------------------------------------------------------------------------
file(GLOB APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

foreach(src_file ${APP_SOURCES})
    get_filename_component(exe_name ${src_file} NAME_WE)
    add_executable(${exe_name} ${src_file})
    target_link_libraries(${exe_name} PRIVATE
        picohttp-core
        picoquic-log
        picoquic-core)
    picoquic_apply_common_settings(${exe_name})
endforeach()
