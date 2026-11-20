# SPDX-License-Identifier: LGPL-2.1-or-later
# Build the external LibreDWG converter programs from a pinned upstream release.
#
# The library is linked ONLY into dwg2dxf/dxf2dwg. PlaneDraft itself never links or loads
# libredwg: the C++ core stays LGPL and talks to the GPLv3 tools through separate processes.
# A static library keeps the payload free of an extra shared object; only the two programs
# are installed under tools/libredwg/.
set(PD_LIBREDWG_VERSION "0.14" CACHE STRING "Pinned LibreDWG release built for DWG exchange")
set(PD_LIBREDWG_URL
    "https://codeload.github.com/LibreDWG/libredwg/tar.gz/refs/tags/0.14"
    CACHE STRING "Pinned LibreDWG source archive (no branch or moving tag)")
set(PD_LIBREDWG_SHA256 "711061f5ce860cd6c1e1a2ef1d3125c29aacc7b55bdc80e1a7e015579917462b"
    CACHE STRING "SHA-256 of PD_LIBREDWG_URL")
set(PD_LIBREDWG_SOURCE_DIR "" CACHE PATH
    "Use an already extracted LibreDWG source tree instead of downloading the archive")

include(FetchContent)
if(PD_LIBREDWG_SOURCE_DIR)
    set(FETCHCONTENT_SOURCE_DIR_LIBREDWG "${PD_LIBREDWG_SOURCE_DIR}" CACHE PATH "" FORCE)
endif()
FetchContent_Declare(libredwg URL "${PD_LIBREDWG_URL}" URL_HASH "SHA256=${PD_LIBREDWG_SHA256}"
                     DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

# Static library: the GPLv3 dependency must not become a shipped shared object.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "LibreDWG static build" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "LibreDWG tests are not part of this build" FORCE)
set(LIBREDWG_LIBONLY OFF CACHE BOOL "Build the converter programs" FORCE)
set(LIBREDWG_DISABLE_WRITE OFF CACHE BOOL "dxf2dwg needs write support" FORCE)
set(LIBREDWG_DISABLE_JSON ON CACHE BOOL "JSON export is not used by the bridge" FORCE)
set(DISABLE_WERROR ON CACHE BOOL "Do not fail on upstream compiler warnings" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "Keep the converter build reproducible" FORCE)
FetchContent_MakeAvailable(libredwg)

if(NOT TARGET dwg2dxf OR NOT TARGET dxf2dwg)
    message(FATAL_ERROR "LibreDWG did not provide the expected dwg2dxf/dxf2dwg targets")
endif()
set_property(TARGET dwg2dxf PROPERTY EXCLUDE_FROM_ALL FALSE)
set_property(TARGET dxf2dwg PROPERTY EXCLUDE_FROM_ALL FALSE)

# Runtime discovery and the smoke tests both look beside the executable, exactly like the
# installed layout (bin/PlaneDraft.exe + tools/libredwg/). No environment variable needed.
add_custom_target(pd_dwg_tools ALL
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tools/libredwg"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:dwg2dxf>" "$<TARGET_FILE:dxf2dwg>"
            "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tools/libredwg"
    COMMENT "Stage pinned LibreDWG converters beside the build products"
    VERBATIM)
add_dependencies(pd_dwg_tools dwg2dxf dxf2dwg)

install(TARGETS dwg2dxf dxf2dwg RUNTIME DESTINATION tools/libredwg)
install(FILES "${libredwg_SOURCE_DIR}/COPYING" RENAME "LibreDWG-${PD_LIBREDWG_VERSION}-GPL-3.0.txt"
        DESTINATION LICENSES)
