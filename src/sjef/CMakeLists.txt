LibraryManager_Append(${PROJECT_NAME}
        SOURCES sjef-backend.cpp sjef.cpp sjef-customization.cpp sjef-c.cpp util/Locker.cpp sjef-program.cpp util/Job.cpp util/Shell.cpp backend-config.cpp
        PUBLIC_HEADER sjef.h sjef-c.h util/Shell.h sjef-program.h util/Locker.h util/Logger.h
        PRIVATE_HEADER util/util.h backend-config.h
)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
#target_compile_options(${PROJECT_NAME} PUBLIC -fPIC)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_EXTENSIONS OFF)

target_compile_definitions(${PROJECT_NAME} PRIVATE SJEF_VERSION=\"${SJEF_VERSION}\")

include(FetchContent)
FetchContent_Declare(tclap GIT_REPOSITORY https://github.com/molpro/tclap.git GIT_TAG v1.2.5)
FetchContent_GetProperties(tclap)
if (NOT tclap_POPULATED)
    FetchContent_MakeAvailable(tclap)
endif ()
target_include_directories(${PROJECT_NAME} PRIVATE ${tclap_SOURCE_DIR}/include)



target_link_libraries(sjef PUBLIC Boost::boost)

#target_include_directories(${PROJECT_NAME} PUBLIC ${Boost_INCLUDE_DIRS})
if (NOT MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif ()

include(FetchContent)
FetchContent_Declare(pugixml
        GIT_REPOSITORY https://github.com/zeux/pugixml
        GIT_TAG v1.15)
FetchContent_MakeAvailable(pugixml)

target_include_directories(${PROJECT_NAME} PUBLIC ${pugixml_SOURCE_DIR}/src)
target_sources(${PROJECT_NAME} PRIVATE ${pugixml_SOURCE_DIR}/src/pugixml.cpp)

include(FetchContent)
FetchContent_Declare(LibYAML
        GIT_REPOSITORY https://github.com/pjknowles/libyaml
        GIT_TAG master
)
FetchContent_MakeAvailable(LibYAML)
target_link_libraries(${PROJECT_NAME} PUBLIC yaml)

if (MINGW)
target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32)
endif ()

add_executable(dummy dummy.cpp)
target_compile_features(dummy PRIVATE cxx_std_17)
