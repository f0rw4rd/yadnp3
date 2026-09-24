include(FetchContent)

FetchContent_Declare(
    pybind11
    URL      https://github.com/pybind/pybind11/archive/refs/tags/v3.1.0.zip
    URL_HASH SHA256=affea1ada7b39fe1d835559fcb78800c7927d514bd480ed01b71b88205a5e536
)

FetchContent_GetProperties(pybind11)
if(NOT pybind11_POPULATED)
    FetchContent_Populate(pybind11)
    add_subdirectory(${pybind11_SOURCE_DIR} ${pybind11_BINARY_DIR})
endif()
