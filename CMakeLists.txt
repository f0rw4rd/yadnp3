cmake_minimum_required (VERSION 3.11)

# Check if we are the root project
set(is_root OFF)
if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_SOURCE_DIR})
    set(is_root ON)
endif()

# Project declaration
set(OPENDNP3_MAJOR_VERSION 3)
set(OPENDNP3_MINOR_VERSION 1)
set(OPENDNP3_MICRO_VERSION 2)
set(OPENDNP3_VERSION ${OPENDNP3_MAJOR_VERSION}.${OPENDNP3_MINOR_VERSION}.${OPENDNP3_MICRO_VERSION})
project(yadnp3 VERSION ${OPENDNP3_VERSION})

# Packing variable
set(CPACK_PACKAGE_NAME yadnp3)
set(CPACK_PACKAGE_VENDOR "f0rw4rd")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "DNP3 (IEEE-1815) protocol stack - Yet Another opendnp3 fork")
set(CPACK_PACKAGE_DESCRIPTION "yadnp3 is an experimental fork of opendnp3, the de facto reference implementation of IEEE-1815 (DNP3).")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/f0rw4rd/opendnp3")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_LIST_DIR}/LICENSE)
set(CPACK_RESOURCE_FILE_README ${CMAKE_CURRENT_LIST_DIR}/README.md)
set(CPACK_PACKAGE_VERSION_MAJOR ${OPENDNP3_MAJOR_VERSION})
set(CPACK_PACKAGE_VERSION_MINOR ${OPENDNP3_MINOR_VERSION})
set(CPACK_PACKAGE_VERSION_PATCH ${OPENDNP3_MICRO_VERSION})

# Minimum compiler version
if(MSVC_VERSION LESS 1900)
    message(FATAL_ERROR "Visual Studio earlier than 2015 does not implement std::chrono::steady_clock properly. Use a modern compiler.")
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Clang utilities
include(./cmake/ClangFormat.cmake)
include(./cmake/ClangTidy.cmake)

if(WIN32)
    set(DNP3_STATIC_LIBS_BY_DEFAULT ON)
else()
    set(DNP3_STATIC_LIBS_BY_DEFAULT OFF)
endif()

# Compilation options
option(DNP3_TLS "Build TLS client/server support (requires OpenSSL)" ON)
option(DNP3_TESTS "Build unit and integration tests" OFF)
option(DNP3_EXAMPLES "Build example applications" OFF)
option(DNP3_FUZZING "Build Google OSS-Fuzz targets" OFF)
option(DNP3_LIBFUZZER "Link the fuzz targets with libFuzzer (clang); needs DNP3_FUZZING" OFF)
option(DNP3_COVERAGE "Enable code coverage target" OFF)
option(DNP3_JAVA "Building the native Java bindings" OFF)
if(WIN32)
    option(DNP3_DOTNET "Build the .NET bindings" OFF)
    set(DNP3_DOTNET_FRAMEWORK_VERSION "v4.5" CACHE STRING "The target .NET framework version for the .NET components")
endif()
option(DNP3_PYTHON "Build Python bindings (requires Python and pybind11)" OFF)
option(DNP3_EVERYTHING "Build all optional targets" OFF)
option(DNP3_STATIC_LIBS "Build static libraries instead of shared libraries" ${DNP3_STATIC_LIBS_BY_DEFAULT})
option(DNP3_LTO "Enable Link-Time Optimization for release builds" OFF)
option(DNP3_INTEROP_TESTS "Build interop tests against stepfunc/dnp3 (requires prebuilt FFI library)" OFF)
option(DNP3_FREYR_INTEROP_TESTS "Build interop tests against FreyrSCADA DNP3 (proprietary, for testing only)" OFF)

if(DNP3_EVERYTHING)
    set(DNP3_TLS ON)
    set(DNP3_TESTS ON)
    set(DNP3_EXAMPLES ON)
    set(DNP3_FUZZING ON)
    set(DNP3_PYTHON ON)
endif()

if(DNP3_STATIC_LIBS AND DNP3_PYTHON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

if(DNP3_STATIC_LIBS)
    set(BUILD_SHARED_LIBS OFF)
    # the java bindings are always explicitly built as a shared library
    # so fpic is required if building the main library statically
    if(DNP3_JAVA)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    endif()
else()
    if(WIN32)
        message(FATAL_ERROR "building shared libraries not supported on Windows")
    endif()
    set(BUILD_SHARED_LIBS ON)
endif()

# External dependencies
include(./deps/asio.cmake)
include(./deps/exe4cpp.cmake)
include(./deps/ser4cpp.cmake)

# the crypto behind Secure Authentication (cpp/lib/src/crypto) always uses OpenSSL's libcrypto; TLS needs libssl too
find_package(OpenSSL 1.1.1 REQUIRED)

if(DNP3_TESTS OR DNP3_FUZZING)
    include(./deps/catch.cmake)
endif()

# Set coverage flags if necessary
if(DNP3_COVERAGE)
    include(./cmake/CodeCoverage.cmake)
    add_coverage_flags()
endif()

# Link-Time Optimization
if(DNP3_LTO AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "Link-Time Optimization enabled")
    else()
        message(WARNING "LTO requested but not supported: ${ipo_output}")
    endif()
endif()

# Library
add_subdirectory(./cpp/lib)

# Tests
if(DNP3_TESTS)
    enable_testing()

    add_subdirectory(./cpp/tests/dnp3mocks)
    add_subdirectory(./cpp/tests/unit)
    add_subdirectory(./cpp/tests/asiotests)
    add_subdirectory(./cpp/tests/integration)
    add_subdirectory(./cpp/tests/conformance)

    if(DNP3_INTEROP_TESTS)
        message(WARNING
            "=================================================================\n"
            " stepfunc/dnp3 interop tests enabled.\n"
            "\n"
            " The stepfunc/dnp3 FFI library is licensed under a proprietary\n"
            " NON-COMMERCIAL EVALUATION license by Step Function I/O LLC.\n"
            " It may NOT be used for commercial or production purposes.\n"
            " It may NOT be redistributed or sublicensed.\n"
            " See LICENSE.txt in the downloaded stepfunc_dnp3 distribution.\n"
            "\n"
            " By enabling DNP3_INTEROP_TESTS you acknowledge these terms.\n"
            "=================================================================\n"
        )
        include(./deps/stepfunc_dnp3.cmake)
        add_subdirectory(./cpp/tests/interop)
    endif()

    if(DNP3_FREYR_INTEROP_TESTS)
        message(WARNING
            "=================================================================\n"
            " FreyrSCADA DNP3 interop tests enabled.\n"
            "\n"
            " The FreyrSCADA DNP3 library is PROPRIETARY software owned by\n"
            " FreyrSCADA Embedded Solution Pvt Ltd.\n"
            " It is NOT open source and NOT redistributed with opendnp3.\n"
            " It is downloaded at Docker build time for interop testing ONLY.\n"
            "\n"
            " By enabling DNP3_FREYR_INTEROP_TESTS you acknowledge this.\n"
            "=================================================================\n"
        )
        include(./deps/freyrscada_dnp3.cmake)
        add_subdirectory(./cpp/tests/interop-freyr)
    endif()

    if(DNP3_COVERAGE)
        define_coverage_target(
            NAME coverage
            DIRECTORY cpp/lib
            TARGETS unittests asiotests integrationtests
        )
    endif()
endif()

# Examples
if(DNP3_EXAMPLES)
    add_subdirectory(./cpp/examples/decoder)
    add_subdirectory(./cpp/examples/master)
    add_subdirectory(./cpp/examples/master-gprs)
    add_subdirectory(./cpp/examples/master-udp)
    add_subdirectory(./cpp/examples/outstation)
    add_subdirectory(./cpp/examples/outstation-udp)

    if(DNP3_TLS)
        add_subdirectory(./cpp/examples/tls/master)
        add_subdirectory(./cpp/examples/tls/master-gprs)
        add_subdirectory(./cpp/examples/tls/outstation)
    endif()
endif()

# Fuzzing
if(DNP3_FUZZING)
    if(NOT DNP3_TESTS)
        # DNP3 mocks is needed for the fuzzing
        add_subdirectory(./cpp/tests/dnp3mocks)
    endif()
    add_subdirectory(./cpp/tests/fuzz)
endif()

if(DNP3_JAVA)
    add_subdirectory(./java)
endif()

if(WIN32 AND DNP3_DOTNET)
    message("The .NET framework version is: ${DNP3_DOTNET_FRAMEWORK_VERSION}")
    add_subdirectory(./dotnet)
endif()

if(DNP3_PYTHON)
    include(./deps/pybind11.cmake)
    add_subdirectory(./python)
endif()

# Define utility targets
if(is_root)
    define_clang_format()
    define_clang_tidy()
endif()

# Packaging
include(CPack)
