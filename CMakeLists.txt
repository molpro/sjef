cmake_minimum_required(VERSION 3.12)
option(BUILD_TESTS "Whether to build tests or not" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(libconfig)

execute_process(
        COMMAND git describe --tags --abbrev=0 --always HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE SJEF_VERSION)
string(STRIP "${SJEF_VERSION}" SJEF_VERSION)
project(sjef LANGUAGES CXX C VERSION ${SJEF_VERSION})
message("SJEF_VERSION: ${SJEF_VERSION}")

set(LIB_FILENAME "sjef")
set(LIB_NAME "${LIB_FILENAME}")
set(PROGRAM_FILENAME "sjef")
set(PROGRAM_NAME "sjef-program")

set(CMAKE_POLICY_DEFAULT_CMP0048 NEW)
add_subdirectory(dependencies)
add_subdirectory(lib)

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif ()
add_subdirectory(program)

find_package(Doxygen)
if (DOXYGEN_FOUND)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
    add_custom_target(${PROJECT_NAME}-doc ALL
            DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/html/index.html
            )
    file(GLOB headers lib/*.h */*.md *.md lib/*.cpp)
    add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/html/index.html
            COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            DEPENDS ${headers} ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen" VERBATIM
            )
endif (DOXYGEN_FOUND)
