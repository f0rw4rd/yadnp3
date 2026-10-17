# Patches ser4cpp's float serialization: run as the PATCH_COMMAND of its FetchContent, in its source directory.
#
# ser4cpp rebuilds the IEEE 754 bit pattern of a float or double from frexp(), and its exponent arithmetic wraps for
# subnormal values: a double of 3e-322 goes out as 1e+303, a float below 1.2e-38 as a huge number. Decoding ignores the
# sign of zero and gets subnormals wrong too. Where the compiler's float and double are IEEE 754 (is_iec559, so on
# every platform this builds for), the bit pattern is the value's own: copy it, and keep the old code for any other.

# replaces the one occurrence of old with new in the file
function(replace_once path old new)
    file(READ "${path}" content)
    string(FIND "${content}" "${old}" first)
    if(first EQUAL -1)
        message(FATAL_ERROR "ser4cpp-iec559: no '${old}' in ${path}")
    endif()
    string(REPLACE "${old}" "${new}" content "${content}")
    file(WRITE "${path}" "${content}")
endfunction()

set(dir "src/ser4cpp/serialization")

foreach(spec "DoubleFloat;double;uint64_t;to_double" "SingleFloat;float;uint32_t;to_float32")
    list(GET spec 0 file)
    list(GET spec 1 type)
    list(GET spec 2 bits)
    list(GET spec 3 decode)
    set(path "${dir}/${file}.h")

    replace_once("${path}" "#include <cmath>\n" "#include <cmath>\n#include <cstring>\n")
    replace_once("${path}"
        "    static ${type} ${decode}(const ${bits} value)\n    {\n"
        "    static ${type} ${decode}(const ${bits} value)\n    {\n        if (std::numeric_limits<${type}>::is_iec559)\n        {\n            ${type} result;\n            std::memcpy(&result, &value, sizeof(result));\n            return result;\n        }\n\n")
    replace_once("${path}"
        "        ${bits} encoded_value{0};\n"
        "        if (std::numeric_limits<${type}>::is_iec559 && !std::isnan(value))\n        {\n            ${bits} encoded;\n            std::memcpy(&encoded, &value, sizeof(encoded));\n            return encoded;\n        }\n\n        ${bits} encoded_value{0};\n")
endforeach()
