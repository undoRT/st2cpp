# JSON module library
add_library(st2cpp_json STATIC
    ${CMAKE_SOURCE_DIR}/src/json/JsonValue.cpp
)

target_include_directories(st2cpp_json PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(st2cpp_json PUBLIC cxx_std_17)