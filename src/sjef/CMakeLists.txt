#set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15.0" CACHE STRING "Minimum OS X deployment version" FORCE)
LibraryManager_Append(${PROJECT_NAME}
        SOURCES sjef-backend.cpp sjef.cpp sjef-customization.cpp sjef-c.cpp util/Locker.cpp sjef-program.cpp util/Job.cpp util/Shell.cpp
        PUBLIC_HEADER sjef.h sjef-c.h util/Shell.h sjef-program.h util/Locker.h util/Logger.h
        PRIVATE_HEADER util/util.h
        )
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
#target_compile_options(${PROJECT_NAME} PUBLIC -fPIC)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_EXTENSIONS OFF)

target_compile_definitions(${PROJECT_NAME} PRIVATE SJEF_VERSION=\"${SJEF_VERSION}\")

include(FetchContent)
FetchContent_Declare(tclap GIT_REPOSITORY https://github.com/molpro/tclap.git GIT_TAG v1.2.5)
FetchContent_GetProperties(tclap)
if (NOT tclap_POPULATED)
    FetchContent_Populate(tclap)
endif ()
target_include_directories(${PROJECT_NAME} PRIVATE ${tclap_SOURCE_DIR}/include)


find_package(Boost 1.71.0 COMPONENTS filesystem REQUIRED)
target_link_libraries(${PROJECT_NAME} PUBLIC Boost::headers)
target_link_libraries(${PROJECT_NAME} PRIVATE pthread Boost::filesystem)

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

target_link_libraries(${PROJECT_NAME} PUBLIC pugixml::pugixml)

if (MINGW)
    target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32)
endif ()

add_executable(dummy dummy.cpp)
target_compile_features(dummy PRIVATE cxx_std_17)
