# SPDX-License-Identifier: LGPL-2.1-or-later
install(TARGETS pdconvert RUNTIME DESTINATION bin)
if(TARGET PlaneDraft)
    install(FILES "${pd_qtbase_ru}" DESTINATION bin/translations)
    install(TARGETS PlaneDraft RUNTIME DESTINATION bin BUNDLE DESTINATION .)
endif()
install(TARGETS sample_line frame_generator LIBRARY DESTINATION plugins RUNTIME DESTINATION plugins)
install(DIRECTORY examples/scripts examples/drawings DESTINATION examples
        FILES_MATCHING PATTERN "*.pdl" PATTERN "*.dxf")
# Standalone v2 header, plus the namespaced include paths used by existing v1/v2 clients.
install(FILES sdk/include/planedraft/pd_plugin_api.h DESTINATION include)
install(DIRECTORY sdk/include/planedraft DESTINATION include FILES_MATCHING PATTERN "*.h")
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
install(DIRECTORY LICENSES DESTINATION .)
install(FILES docs/DRAWINGS.md docs/LICENSING.md DESTINATION docs)
install(FILES resources/aliases.ini DESTINATION examples)

option(PLANEDRAFT_PACKAGE_WINDOWS "Stage the portable MSVC/vcpkg desktop payload with CPack" OFF)
if(PLANEDRAFT_PACKAGE_WINDOWS)
    include("${CMAKE_CURRENT_LIST_DIR}/windows/package.cmake")
endif()
