set(CMAKE_OSX_DEPLOYMENT_TARGET "10.12.0" CACHE STRING "Minimum OS X deployment version" FORCE)
add_library(${LIB_NAME}
        sjef-backend.cpp
        sjef.cpp
        sjef-customization.cpp sjef-c.cpp sjef-c.h FileLock.cpp FileLock.h
        sjef-program.cpp
        )
target_compile_features(${LIB_NAME} PUBLIC cxx_std_17)
target_compile_options(${LIB_NAME} PUBLIC -fPIC)
set_target_properties(${LIB_NAME} PROPERTIES PUBLIC_HEADER "sjef.h;sjef-c.h;sjef-program.h")
set_target_properties(${LIB_NAME} PROPERTIES OUTPUT_NAME "${LIB_FILENAME}")

include(FetchContent)
FetchContent_Declare(tclap GIT_REPOSITORY https://gitlab+deploy-token-103300:HKsBK8FZcZZshqAa_yyx@gitlab.com/molpro/tclap.git)
FetchContent_GetProperties(tclap)
if (NOT tclap_POPULATED)
    FetchContent_Populate(tclap)
endif ()
target_compile_definitions(${LIB_NAME} PRIVATE SJEF_VERSION=\"${SJEF_VERSION}\")
target_include_directories(${LIB_NAME} PRIVATE ${tclap_SOURCE_DIR}/include)


find_package(Boost 1.71.0 COMPONENTS filesystem REQUIRED)

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

target_link_libraries(${LIB_NAME} PUBLIC Boost::filesystem pthread)
target_link_libraries(${LIB_NAME} PRIVATE pugixml::pugixml)
configure_library(${LIB_NAME} pugixml)
