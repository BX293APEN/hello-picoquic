cmake_minimum_required(VERSION 3.13)
project(hello-picoquic C CXX)

# ---------------------------------------------------------------------------
# Static build flags (Windows / MSVC and Linux / GCC-Clang)
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
    # GCC / Clang: statically link the compiler runtime libraries.
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")

    # Optionally produce a fully static executable (no shared libs at all,
    # including libc). Off by default because a fully static OpenSSL is
    # required for this to work.
    option(BUILD_FULLY_STATIC "Link everything statically with -static" OFF)
    if(BUILD_FULLY_STATIC)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    endif()
endif()

# ---------------------------------------------------------------------------
# picotls / picoquic: build from the local sources already present under
# ./picotls and ./picoquic. Nothing is downloaded during configure.
# ---------------------------------------------------------------------------

# picotls's own CMakeLists enables the AVX2 "fusion" AES-GCM engine on x86_64
# with GCC/Clang-only flags (-mavx2 -maes ...). Force it off so the project
# also configures cleanly with MSVC / on non-x86 hosts.
set(WITH_FUSION OFF CACHE BOOL "Disable picotls 'fusion' engine (GCC/Clang-only flags)" FORCE)
set(WITH_DTRACE OFF CACHE BOOL "Disable USDT probes" FORCE)

include(FetchContent)

# picoquic's CMakeLists.txt contains:
#     FetchContent_Declare(picotls GIT_REPOSITORY ... GIT_TAG ...)
#     FetchContent_MakeAvailable(picotls)
# Because FetchContent only honors the *first* declaration for a given name,
# declaring "picotls" here first - pointing at our local checkout - makes
# FetchContent_MakeAvailable() use this directory as-is (add_subdirectory),
# with no git clone / network access at configure time.
FetchContent_Declare(picotls SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/picotls)

set(PICOQUIC_FETCH_PTLS ON  CACHE BOOL "Use the locally-provided picotls via FetchContent override" FORCE)
set(WITH_OPENSSL         ON  CACHE BOOL "Build picotls/picoquic with OpenSSL support" FORCE)

# Trim picoquic's own demo/bench/test binaries (set to ON if you want them).
set(BUILD_DEMO    OFF CACHE BOOL "Build picoquicdemo" FORCE)
set(BUILD_PQBENCH OFF CACHE BOOL "Build pqbench" FORCE)

add_subdirectory(picoquic)

# ---------------------------------------------------------------------------
# src/: every .c file becomes its own executable, linked statically against
# picoquic (+ picotls + OpenSSL, pulled in transitively via picoquic-core).
# ---------------------------------------------------------------------------
file(GLOB APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

foreach(src_file ${APP_SOURCES})
    get_filename_component(exe_name ${src_file} NAME_WE)
    add_executable(${exe_name} ${src_file})
    target_link_libraries(${exe_name} PRIVATE
        picoquic-core
        picoquic-log
        picohttp-core
    )
    if(WIN32)
        target_link_libraries(${exe_name} PRIVATE ws2_32 crypt32 bcrypt)
    endif()
endforeach()
