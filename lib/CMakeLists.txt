add_library(${LIB_NAME}
        sjef-backend.cpp
        sjef.cpp
        sjef-customization.cpp sjef-c.cpp sjef-c.h FileLock.cpp FileLock.h)
target_compile_features(${LIB_NAME} PUBLIC cxx_std_17)
target_compile_options(${LIB_NAME} PUBLIC -fPIC)
set_target_properties(${LIB_NAME} PROPERTIES PUBLIC_HEADER "sjef.h;sjef-c.h")
set_target_properties(${LIB_NAME} PROPERTIES OUTPUT_NAME "${LIB_FILENAME}")

find_package(Boost 1.71.0 REQUIRED)

if (NOT TARGET pugixml)
    find_package(pugixml QUIET)
    if (TARGET pugixml)
        set_target_properties(pugixml PROPERTIES IMPORTED_GLOBAL TRUE)
    else ()
        include(FetchContent)
        FetchContent_Declare(pugixml
                GIT_REPOSITORY https://github.com/zeux/pugixml
                GIT_TAG v1.10)
        FetchContent_MakeAvailable(pugixml)
    endif ()
    set_target_properties(pugixml PROPERTIES COMPILE_FLAGS "-fPIC")
    add_library(pugixml::pugixml ALIAS pugixml)
endif ()

target_link_libraries(${LIB_NAME} PUBLIC pthread)
target_link_libraries(${LIB_NAME} PRIVATE pugixml::pugixml)
configure_library(${LIB_NAME} pugixml)
