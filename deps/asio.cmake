include(FetchContent)

FetchContent_Declare(
    asio
    URL      https://github.com/chriskohlhoff/asio/archive/asio-1-16-0.zip
    URL_HASH SHA1=6BDD33522D5B95B36445ABB2072A481F7CE15402
)

FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)

    # OpenSSL 4 made asn1_string_st opaque, but asio 1.16's deprecated
    # rfc2818_verification dereferences it, so ssl.hpp no longer compiles.
    # It is unused here (asio 1.34 dropped the include too), so strip it.
    set(ASIO_SSL_HPP ${asio_SOURCE_DIR}/asio/include/asio/ssl.hpp)
    file(READ ${ASIO_SSL_HPP} ASIO_SSL_HPP_IN)
    string(REPLACE "#include \"asio/ssl/rfc2818_verification.hpp\"" ""
           ASIO_SSL_HPP_OUT "${ASIO_SSL_HPP_IN}")
    if(NOT ASIO_SSL_HPP_OUT STREQUAL ASIO_SSL_HPP_IN)
        file(WRITE ${ASIO_SSL_HPP} "${ASIO_SSL_HPP_OUT}")
    endif()

    find_package(Threads)

    add_library(asio INTERFACE)
    target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
    target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
    target_compile_features(asio INTERFACE cxx_std_11)
    target_link_libraries(asio INTERFACE Threads::Threads)

    if(WIN32)
        target_link_libraries(asio INTERFACE ws2_32 wsock32) # Link to Winsock
        target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0601) # Windows 7 and up
    endif()
endif()
