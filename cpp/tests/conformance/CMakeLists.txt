add_executable(conformance-outstation ./main.cpp)
target_link_libraries(conformance-outstation PRIVATE opendnp3)
clang_format(conformance-outstation)
set_target_properties(conformance-outstation PROPERTIES FOLDER cpp/tests)
