# Semantic analysis library
add_library(st2cpp_semantic STATIC
    ${CMAKE_SOURCE_DIR}/src/semantic/Diagnostics.cpp
    ${CMAKE_SOURCE_DIR}/src/semantic/TypeSystem.cpp
    ${CMAKE_SOURCE_DIR}/src/semantic/SymbolTable.cpp
    ${CMAKE_SOURCE_DIR}/src/semantic/SemanticAnalyzer.cpp
    ${CMAKE_SOURCE_DIR}/src/semantic/DeclVisitor.cpp
    ${CMAKE_SOURCE_DIR}/src/semantic/BodyVisitor.cpp
)

target_include_directories(st2cpp_semantic PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_compile_features(st2cpp_semantic PUBLIC cxx_std_17)

target_link_libraries(st2cpp_semantic PUBLIC st2cpp_lib)