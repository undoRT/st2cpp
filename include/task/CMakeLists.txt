# Task Configuration module library
add_library(st2cpp_task STATIC
    ${CMAKE_SOURCE_DIR}/src/task/TaskConfigLoader.cpp
)

target_include_directories(st2cpp_task PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(st2cpp_task PUBLIC cxx_std_17)

target_link_libraries(st2cpp_task PUBLIC st2cpp_json)