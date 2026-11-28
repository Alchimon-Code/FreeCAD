# SPDX-License-Identifier: LGPL-2.1-or-later
# Copy the non-system runtime dependencies of the GPLv3 LibreDWG programs next to them.
#
# The host's bin/ directory is covered by vcpkg app-local deployment, but tools/libredwg/ is not:
# the MSVC build links zlib as a DLL, so a converter started with a clean PATH could not load it
# and stopped before printing anything. Called from the staging custom target and from install().
#
# Usage: cmake -DEXECUTABLES=a.exe|b.exe -DDESTINATION=dir -DSEARCH_DIRS=d1;d2 -P <this file>

if(NOT WIN32)
    return()
endif()
if(NOT DEFINED EXECUTABLES OR NOT DEFINED DESTINATION)
    message(FATAL_ERROR "EXECUTABLES and DESTINATION are required")
endif()
string(REPLACE "|" ";" executables "${EXECUTABLES}")
file(GET_RUNTIME_DEPENDENCIES
     EXECUTABLES ${executables}
     DIRECTORIES ${SEARCH_DIRS}
     RESOLVED_DEPENDENCIES_VAR resolved
     UNRESOLVED_DEPENDENCIES_VAR unresolved
     PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
     POST_EXCLUDE_REGEXES ".*[/\\\\](vcruntime140|vcruntime140_1|msvcp140|concrt140)\\.dll"
                         ".*[/\\\\]([Ww]indows|System32|SysWOW64)[/\\\\].*")
foreach(dependency IN LISTS resolved)
    file(INSTALL DESTINATION "${DESTINATION}" TYPE SHARED_LIBRARY FILES "${dependency}")
endforeach()
