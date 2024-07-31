# Copyright (C) The c-ares project and its contributors
# SPDX-License-Identifier: MIT
# Write ares_build.h configuration file.  This is an installed file.
CONFIGURE_FILE (ares_build.h.cmake ${PROJECT_BINARY_DIR}/ares_build.h)

# Headers installation target
IF (CARES_INSTALL)
	SET (CARES_HEADERS ares.h ares_version.h "${PROJECT_BINARY_DIR}/ares_build.h" ares_dns.h ares_dns_record.h ares_nameser.h)
	INSTALL (FILES ${CARES_HEADERS} COMPONENT Devel DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
ENDIF ()
