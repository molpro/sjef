add_executable(${PROGRAM_NAME} sjef-program.cpp)
set_target_properties(${PROGRAM_NAME} PROPERTIES OUTPUT_NAME "${PROGRAM_FILENAME}")
target_compile_definitions(${PROGRAM_NAME} PRIVATE "-DSJEF_VERSION=\"${SJEF_VERSION}\"")
target_compile_features(${PROGRAM_NAME} PUBLIC cxx_std_17)
target_link_libraries(${PROGRAM_NAME} PUBLIC ${LIB_NAME})

target_link_libraries(${PROGRAM_NAME} INTERFACE PkgConfig::tclap)
include(FetchContent)
FetchContent_Declare(tclap GIT_REPOSITORY https://gitlab+deploy-token-103300:HKsBK8FZcZZshqAa_yyx@gitlab.com/molpro/tclap.git )
FetchContent_GetProperties(tclap)
if (NOT tclap_POPULATED)
    FetchContent_Populate(tclap)
endif ()
target_include_directories(${PROGRAM_NAME} PRIVATE ${tclap_SOURCE_DIR}/include)
set_target_properties(${PROGRAM_NAME} PROPERTIES INSTALL_RPATH_USE_LINK_PATH TRUE)
set_target_properties(${PROGRAM_NAME} PROPERTIES INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
install(TARGETS ${PROGRAM_NAME} RUNTIME DESTINATION bin)

set(package_name "sjef-${SJEF_VERSION}.${CMAKE_SYSTEM_NAME}_${CMAKE_SYSTEM_PROCESSOR}")

set(deploy ${CMAKE_CURRENT_BINARY_DIR}/deploy)
set(prefix "/usr/local")
set(deployp "${deploy}${prefix}")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/uninstall.in ${deployp}/share/${PROGRAM_FILENAME}/sbin/uninstall)
add_custom_command(OUTPUT ${deployp}/bin/${PROGRAM_FILENAME}${CMAKE_EXECUTABLE_SUFFIX}
        DEPENDS ${PROGRAM_FILENAME} ${deployp}/share/${PROGRAM_FILENAME}/sbin/uninstall
        COMMAND ${CMAKE_COMMAND} -E make_directory ${deployp}/bin
        COMMAND chmod +x ${deployp}/share/${PROGRAM_FILENAME}/sbin/uninstall
        COMMAND ls -ltraR ${deployp}
        COMMAND ls -ltraR ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND find ${CMAKE_CURRENT_BINARY_DIR} -name 'sjef*'
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${PROGRAM_FILENAME}${CMAKE_EXECUTABLE_SUFFIX} ${deployp}/bin
        )
add_custom_target(${LIB_FILENAME}-deploy ALL DEPENDS ${deployp}/bin/${PROGRAM_FILENAME}${CMAKE_EXECUTABLE_SUFFIX})

if (APPLE)
    add_custom_command(OUTPUT ${package_name}.pkg
            DEPENDS ${deployp}/bin/${PROGRAM_FILENAME}${CMAKE_EXECUTABLE_SUFFIX}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${deployp}/share/${PROGRAM_FILENAME}/lib
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/osx-localize-binaries ${deployp}/share/${PROGRAM_FILENAME}/lib ${deployp}/bin/${PROGRAM_FILENAME}${CMAKE_EXECUTABLE_SUFFIX}
            COMMAND pkgbuild --root ${deploy} --identifier net.molpro.sjef --version ${SJEF_VERSION} ${package_name}.pkg
            )
    add_custom_target(sjef-pkg DEPENDS ${package_name}.pkg)
else ()
    add_custom_command(OUTPUT ${package_name}${CMAKE_EXECUTABLE_SUFFIX}
            DEPENDS ${deployp}/bin/${PROGRAM_FILENAME}${CMAKE_EXECUTABLE_SUFFIX}
            COMMAND ${CMAKE_COMMAND} -E copy ${deployp}/bin/${PROGRAM_FILENAME}${CMAKE_EXECUTABLE_SUFFIX} ${package_name}${CMAKE_EXECUTABLE_SUFFIX}
            )
    add_custom_target(binary ALL DEPENDS ${package_name}${CMAKE_EXECUTABLE_SUFFIX})
endif ()
