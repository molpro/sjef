if(WIN32)
    set(CXX_STANDARD_OPTION "/std:c++17")
    set(EXTRA_LIBRARY_DIRS "'${CMAKE_INSTALL_PREFIX}/Library/lib','${CMAKE_INSTALL_PREFIX}/bin'")
    set(EXTRA_LIBRARIES "'shell32'")
else()
    set(CXX_STANDARD_OPTION "-std=c++17")
endif()
message("CXX_STANDARD_OPTION=${CXX_STANDARD_OPTION}")

configure_file(pyproject.toml.in ${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml
)