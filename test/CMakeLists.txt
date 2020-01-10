include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG "release-1.8.1"
)
FetchContent_GetProperties(googletest)
if (NOT googletest_POPULATED)
    FetchContent_Populate(googletest)
    add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR} EXCLUDE_FROM_ALL)
endif ()

file(COPY He.molpro DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

include(GoogleTest)
foreach (t test-sjef test-sjef-c)
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PUBLIC gmock_main ${LIB_NAME})
    if (NOT CMAKE_CROSSCOMPILING)
        gtest_discover_tests(${t})
        add_test(${t} ${t})
    endif ()
endforeach ()

foreach (t test-c-sjef-backend)
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PUBLIC ${LIB_NAME})
endforeach ()
