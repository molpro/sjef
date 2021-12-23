include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG "release-1.11.0"
)
FetchContent_GetProperties(googletest)
if (NOT googletest_POPULATED)
    FetchContent_Populate(googletest)
    add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR} EXCLUDE_FROM_ALL)
endif ()

file(COPY He.molpro DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
find_program(MOLPRO molpro)

include(GoogleTest)
foreach (t test-sjef test-sjef-c test-FileLock test-sjef-molpro)
    if (NOT (MOLPRO STREQUAL MOLPRO-NOTFOUND AND t STREQUAL test-sjef-molpro))
        add_executable(${t} ${t}.cpp test-sjef.h)
        target_link_libraries(${t} PUBLIC gmock_main ${LIB_NAME})
        if (NOT CMAKE_CROSSCOMPILING)
            gtest_discover_tests(${t})
            add_test(${t} ${t})
        endif ()
    endif ()
endforeach ()

foreach (t test-c-sjef-backend)
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PUBLIC ${LIB_NAME})
endforeach ()
