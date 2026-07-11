set(CAPELLA_GENERATED_HPP "${CMAKE_CURRENT_BINARY_DIR}/capella.hpp")

add_custom_command(
    OUTPUT "${CAPELLA_GENERATED_HPP}"
    COMMAND caper
            "${CMAKE_CURRENT_SOURCE_DIR}/capella.cpg"
            "${CAPELLA_GENERATED_HPP}"
    DEPENDS caper "${CMAKE_CURRENT_SOURCE_DIR}/capella.cpg"
    COMMENT "Generating capella.hpp"
    VERBATIM)

add_executable(capella
    capella.cpp
    capella_dependency.cpp
    capella_generate_boost.cpp
    capella_generate_cpp.cpp
    capella_generate_dot.cpp
    capella_generate_stub.cpp
    capella_generate_cs.cpp
    "${CAPELLA_GENERATED_HPP}")

target_include_directories(capella PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}")

target_link_libraries(capella PRIVATE Boost::boost)
