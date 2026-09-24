# Project Configuration module library
add_library(st2cpp_project STATIC
    ${CMAKE_SOURCE_DIR}/src/project/ProjectConfigLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/project/ProjectLoader.cpp
)

target_include_directories(st2cpp_project PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(st2cpp_project PUBLIC cxx_std_17)

target_link_libraries(st2cpp_project PUBLIC st2cpp_library)