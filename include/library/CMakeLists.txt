# Library Descriptor module library
add_library(st2cpp_library STATIC
    ${CMAKE_SOURCE_DIR}/src/library/LibraryDescriptor.cpp
    ${CMAKE_SOURCE_DIR}/src/library/LibraryLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/library/LibrarySerializer.cpp
    ${CMAKE_SOURCE_DIR}/src/library/LibraryRegistry.cpp
)

target_include_directories(st2cpp_library PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(st2cpp_library PUBLIC cxx_std_17)

target_link_libraries(st2cpp_library PUBLIC st2cpp_json)