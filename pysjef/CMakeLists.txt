if (WIN32)
    set(EXTRA_COMPILE_ARGS "'/std:c++17'")
    set(EXTRA_LIBRARY_DIRS "'${CMAKE_INSTALL_PREFIX}/Library/lib','${CMAKE_INSTALL_PREFIX}/Lib'")
    set(EXTRA_LIBRARIES "'shell32'")
elseif (APPLE)
    set(EXTRA_COMPILE_ARGS "'-std=c++17', '-mmacosx-version-min=13'")
else ()
    set(EXTRA_COMPILE_ARGS "'-std=c++17'")
endif ()

configure_file(pyproject.toml.in ${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml
)