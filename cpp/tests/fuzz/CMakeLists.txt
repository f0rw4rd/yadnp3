# With DNP3_LIBFUZZER (clang), the targets link libFuzzer and fuzz; otherwise onefile.cpp gives them a main that runs
# one input, for OSS-Fuzz style replay
function(add_fuzzer name)
    if(DNP3_LIBFUZZER)
        add_executable(${name} ${ARGN})
        target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
        target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    else()
        add_executable(${name} ./onefile.cpp ${ARGN})
    endif()
    target_include_directories(${name} PRIVATE ./)
    set_target_properties(${name} PROPERTIES FOLDER cpp/tests/fuzzing)
    clang_format(${name})
endfunction()

add_fuzzer(fuzzdecoder ./fuzzdecoder.cpp)
target_link_libraries(fuzzdecoder PRIVATE opendnp3)
clang_tidy(fuzzdecoder)

add_fuzzer(fuzzoutstation ./fuzzoutstation.cpp)
target_link_libraries(fuzzoutstation PRIVATE catch dnp3mocks)
clang_tidy(fuzzoutstation)

add_fuzzer(fuzzmaster ./fuzzmaster.cpp)
target_link_libraries(fuzzmaster PRIVATE catch dnp3mocks)
clang_tidy(fuzzmaster)

# the session fuzzers drive a whole outstation or master through the unit test fixtures
set(fuzz_test_utils
    ../unit/utils/BufferHelpers.cpp
    ../unit/utils/CopyableBuffer.cpp
    ../unit/utils/MasterTestFixture.cpp
    ../unit/utils/OutstationTestObject.cpp
)

add_fuzzer(fuzzdecoder_logging ./fuzzdecoder_logging.cpp)
target_link_libraries(fuzzdecoder_logging PRIVATE opendnp3)

add_fuzzer(fuzzoutstation_session ./fuzzoutstation_session.cpp ${fuzz_test_utils})
target_include_directories(fuzzoutstation_session PRIVATE ../unit ../../lib/src)
target_link_libraries(fuzzoutstation_session PRIVATE dnp3mocks)

add_fuzzer(fuzzmaster_session ./fuzzmaster_session.cpp ${fuzz_test_utils})
target_include_directories(fuzzmaster_session PRIVATE ../unit ../../lib/src)
target_link_libraries(fuzzmaster_session PRIVATE dnp3mocks)

add_fuzzer(fuzzroundtrip ./fuzzroundtrip.cpp ${fuzz_test_utils})
target_include_directories(fuzzroundtrip PRIVATE ../unit ../../lib/src)
target_link_libraries(fuzzroundtrip PRIVATE dnp3mocks)

add_fuzzer(fuzzroundtrip_events ./fuzzroundtrip_events.cpp ${fuzz_test_utils})
target_include_directories(fuzzroundtrip_events PRIVATE ../unit ../../lib/src)
target_link_libraries(fuzzroundtrip_events PRIVATE dnp3mocks)

add_fuzzer(fuzzroundtrip_eventbuffer ./fuzzroundtrip_eventbuffer.cpp ${fuzz_test_utils})
target_include_directories(fuzzroundtrip_eventbuffer PRIVATE ../unit ../../lib/src)
target_link_libraries(fuzzroundtrip_eventbuffer PRIVATE dnp3mocks)

add_fuzzer(fuzzroundtrip_commands ./fuzzroundtrip_commands.cpp ${fuzz_test_utils})
target_include_directories(fuzzroundtrip_commands PRIVATE ../unit ../../lib/src)
target_link_libraries(fuzzroundtrip_commands PRIVATE dnp3mocks)
