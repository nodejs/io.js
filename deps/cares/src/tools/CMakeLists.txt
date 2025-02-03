# Copyright (C) The c-ares project and its contributors
# SPDX-License-Identifier: MIT
IF (CARES_BUILD_TOOLS)
	# Transform Makefile.inc
	transform_makefile_inc("Makefile.inc" "${PROJECT_BINARY_DIR}/src/tools/Makefile.inc.cmake")
	include(${PROJECT_BINARY_DIR}/src/tools/Makefile.inc.cmake)

	# Build ahost
	ADD_EXECUTABLE (ahost ahost.c ${SAMPLESOURCES})
	TARGET_INCLUDE_DIRECTORIES (ahost
		PUBLIC "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>"
		       "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>"
		       "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/lib>"
		       "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/lib/include>"
		       "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
		       "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
		PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
	)
	SET_TARGET_PROPERTIES (ahost PROPERTIES
		C_STANDARD                   90
	)

	IF (ANDROID)
		SET_TARGET_PROPERTIES (ahost PROPERTIES C_STANDARD 99)
	ENDIF ()

	TARGET_COMPILE_DEFINITIONS (ahost PRIVATE HAVE_CONFIG_H=1 CARES_NO_DEPRECATED)
	TARGET_LINK_LIBRARIES (ahost PRIVATE ${PROJECT_NAME})

	# Avoid "fatal error C1041: cannot open program database" due to multiple
	# targets trying to use the same PDB.  /FS does NOT resolve this issue.
	SET_TARGET_PROPERTIES(ahost PROPERTIES COMPILE_PDB_NAME ahost.pdb)

	IF (CARES_INSTALL)
		INSTALL (TARGETS ahost COMPONENT Tools ${TARGETS_INST_DEST})
	ENDIF ()


	# Build adig
	ADD_EXECUTABLE (adig adig.c)
	# Don't build adig and ahost in parallel.  This is to prevent a Windows MSVC
	# build error due to them both using the same source files.
	ADD_DEPENDENCIES(adig ahost)
	TARGET_INCLUDE_DIRECTORIES (adig
		PUBLIC "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>"
		       "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>"
		       "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/lib>"
		       "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/lib/include>"
		       "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
		       "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
		PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
	)
	SET_TARGET_PROPERTIES (adig PROPERTIES
		C_STANDARD                   90
	)

	IF (ANDROID)
		SET_TARGET_PROPERTIES (adig PROPERTIES C_STANDARD 99)
	ENDIF ()

	TARGET_COMPILE_DEFINITIONS (adig PRIVATE HAVE_CONFIG_H=1 CARES_NO_DEPRECATED)
	TARGET_LINK_LIBRARIES (adig PRIVATE ${PROJECT_NAME})

	# Avoid "fatal error C1041: cannot open program database" due to multiple
	# targets trying to use the same PDB.  /FS does NOT resolve this issue.
	SET_TARGET_PROPERTIES(adig PROPERTIES COMPILE_PDB_NAME adig.pdb)

	IF (CARES_INSTALL)
		INSTALL (TARGETS adig COMPONENT Tools ${TARGETS_INST_DEST})
	ENDIF ()
ENDIF ()
