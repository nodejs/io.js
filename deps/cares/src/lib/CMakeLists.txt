# Copyright (C) The c-ares project and its contributors
# SPDX-License-Identifier: MIT

# Transform Makefile.inc
transform_makefile_inc("Makefile.inc" "${PROJECT_BINARY_DIR}/src/lib/Makefile.inc.cmake")
include(${PROJECT_BINARY_DIR}/src/lib/Makefile.inc.cmake)

# Write ares_config.h configuration file.  This is used only for the build.
CONFIGURE_FILE (ares_config.h.cmake ${PROJECT_BINARY_DIR}/ares_config.h)

# Build the dynamic/shared library
IF (CARES_SHARED)
	ADD_LIBRARY (${PROJECT_NAME} SHARED ${CSOURCES})

	# Include resource file in windows builds for versioned DLLs
	IF (WIN32)
		TARGET_SOURCES (${PROJECT_NAME} PRIVATE cares.rc)
	ENDIF()

	# Convert CARES_LIB_VERSIONINFO libtool version format into VERSION and SOVERSION
	# Convert from ":" separated into CMake list format using ";"
	STRING (REPLACE ":" ";" CARES_LIB_VERSIONINFO ${CARES_LIB_VERSIONINFO})
	LIST (GET CARES_LIB_VERSIONINFO 0 CARES_LIB_VERSION_CURRENT)
	LIST (GET CARES_LIB_VERSIONINFO 1 CARES_LIB_VERSION_REVISION)
	LIST (GET CARES_LIB_VERSIONINFO 2 CARES_LIB_VERSION_AGE)
	MATH (EXPR CARES_LIB_VERSION_MAJOR "${CARES_LIB_VERSION_CURRENT} - ${CARES_LIB_VERSION_AGE}")
	SET  (CARES_LIB_VERSION_MINOR "${CARES_LIB_VERSION_AGE}")
	SET  (CARES_LIB_VERSION_RELEASE "${CARES_LIB_VERSION_REVISION}")

	SET_TARGET_PROPERTIES (${PROJECT_NAME} PROPERTIES
		EXPORT_NAME                  cares
		OUTPUT_NAME                  cares
		COMPILE_PDB_NAME             cares
		SOVERSION                    ${CARES_LIB_VERSION_MAJOR}
		VERSION                      "${CARES_LIB_VERSION_MAJOR}.${CARES_LIB_VERSION_MINOR}.${CARES_LIB_VERSION_RELEASE}"
		C_STANDARD                   90
	)

	IF (ANDROID)
		SET_TARGET_PROPERTIES (${PROJECT_NAME} PROPERTIES C_STANDARD 99)
	ENDIF ()

	IF (CARES_SYMBOL_HIDING)
		SET_TARGET_PROPERTIES (${PROJECT_NAME} PROPERTIES
			C_VISIBILITY_PRESET hidden
			VISIBILITY_INLINES_HIDDEN YES
		)
	ENDIF ()

	TARGET_INCLUDE_DIRECTORIES (${PROJECT_NAME}
		PUBLIC "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>"
		       "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>"
		       "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
		       "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
		PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
		        "${CMAKE_CURRENT_SOURCE_DIR}/include"
	)

	TARGET_COMPILE_DEFINITIONS (${PROJECT_NAME} PRIVATE HAVE_CONFIG_H=1 CARES_BUILDING_LIBRARY)

	TARGET_LINK_LIBRARIES (${PROJECT_NAME}
		PUBLIC ${CARES_DEPENDENT_LIBS}
		PRIVATE ${CMAKE_THREAD_LIBS_INIT}
	)

	IF (CARES_INSTALL)
		INSTALL (TARGETS ${PROJECT_NAME}
			EXPORT ${PROJECT_NAME}-targets
			COMPONENT Library
			${TARGETS_INST_DEST}
		)
		IF (MSVC)
			INSTALL(FILES $<TARGET_PDB_FILE:${PROJECT_NAME}>
			        DESTINATION ${CMAKE_INSTALL_BINDIR}
				COMPONENT Library
				OPTIONAL
			)
		ENDIF ()
	ENDIF ()
	SET (STATIC_SUFFIX "_static")

	# For chain building: add alias targets that look like import libs that would be returned by find_package(c-ares).
	ADD_LIBRARY (${PROJECT_NAME}::cares_shared ALIAS ${PROJECT_NAME})
	ADD_LIBRARY (${PROJECT_NAME}::cares        ALIAS ${PROJECT_NAME})
ENDIF ()

# Build the static library
IF (CARES_STATIC)
	SET (LIBNAME ${PROJECT_NAME}${STATIC_SUFFIX})

	ADD_LIBRARY (${LIBNAME} STATIC ${CSOURCES})

	SET_TARGET_PROPERTIES (${LIBNAME} PROPERTIES
		EXPORT_NAME                  cares${STATIC_SUFFIX}
		COMPILE_PDB_NAME             cares${STATIC_SUFFIX}
		C_STANDARD                   90
	)

	# On Windows, the output name should have a static suffix since otherwise
	# we would have conflicting output names (libcares.lib) for the link
	# library.
	# However on Unix-like systems, we typically have something like
	# libcares.so  for shared libraries  and libcares.a for static
	# libraries, so these don't conflict.
	# This behavior better emulates what happens with autotools builds
	IF (WIN32)
		SET_TARGET_PROPERTIES(${LIBNAME} PROPERTIES OUTPUT_NAME cares${STATIC_SUFFIX})
	ELSE ()
		SET_TARGET_PROPERTIES(${LIBNAME} PROPERTIES OUTPUT_NAME cares)
	ENDIF()

	IF (ANDROID)
		SET_TARGET_PROPERTIES (${LIBNAME} PROPERTIES C_STANDARD 99)
	ENDIF ()

	IF (CARES_STATIC_PIC)
		SET_TARGET_PROPERTIES (${LIBNAME} PROPERTIES POSITION_INDEPENDENT_CODE True)
	ENDIF ()

	TARGET_INCLUDE_DIRECTORIES (${LIBNAME}
		PUBLIC "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>"
		       "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>"
		       "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
		       "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
		PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
		        "${CMAKE_CURRENT_SOURCE_DIR}/include"
	)

	TARGET_COMPILE_DEFINITIONS (${LIBNAME} PRIVATE HAVE_CONFIG_H=1 CARES_BUILDING_LIBRARY)

	# Only matters on Windows
	IF (WIN32 OR CYGWIN)
		TARGET_COMPILE_DEFINITIONS (${LIBNAME} PUBLIC CARES_STATICLIB)
	ENDIF()

	TARGET_LINK_LIBRARIES (${LIBNAME} PUBLIC ${CARES_DEPENDENT_LIBS})
	IF (CARES_INSTALL)
		INSTALL (TARGETS ${LIBNAME} EXPORT ${PROJECT_NAME}-targets COMPONENT Devel
			${TARGETS_INST_DEST}
		)
	ENDIF ()

	# For chain building: add alias targets that look like import libs that would be returned by find_package(c-ares).
	ADD_LIBRARY (${PROJECT_NAME}::cares_static ALIAS ${LIBNAME})
	IF (NOT TARGET ${PROJECT_NAME}::cares)
		# Only use static for the generic alias if shared lib wasn't built.
		ADD_LIBRARY (${PROJECT_NAME}::cares ALIAS ${LIBNAME})
	ENDIF ()
ENDIF ()




