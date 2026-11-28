# SPDX-License-Identifier: LGPL-2.1-or-later
if(NOT MSVC OR NOT TARGET PlaneDraft OR NOT VCPKG_TARGET_TRIPLET STREQUAL "x64-windows" OR
   NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "Windows packaging requires the windows-msvc preset (x64, shared, Release)")
endif()
set(pd_prefix "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
find_program(pd_windeployqt windeployqt PATHS "${pd_prefix}/tools/Qt6/bin" NO_DEFAULT_PATH REQUIRED)
install(FILES "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>" DESTINATION bin/platforms)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/qt.conf" DESTINATION bin)

# Portable, app-local Release CRT. Never copy a debug CRT, Windows system DLLs, or a redist installer.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
include(InstallRequiredSystemLibraries)
if(NOT CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    message(FATAL_ERROR "MSVC redistributable CRT not found. Install the VS 2022 x64 C++ toolset.")
endif()
configure_file("${CMAKE_CURRENT_LIST_DIR}/deploy.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/pd-deploy.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/pd-deploy.cmake")

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME PlaneDraft)
set(CPACK_PACKAGE_VENDOR "PlaneDraft contributors")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "PlaneDraft-${PROJECT_VERSION}-windows-x64")
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/packages")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_PACKAGING_INSTALL_PREFIX "/")
include(CPack)
