add_subdirectory(includes)

include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/includes"
  "${CMAKE_CURRENT_BINARY_DIR}/includes"
)

add_definitions(-DBUILDING_NGHTTP2)

set(NGHTTP2_SOURCES
  nghttp2_pq.c nghttp2_map.c nghttp2_queue.c
  nghttp2_frame.c
  nghttp2_buf.c
  nghttp2_stream.c nghttp2_outbound_item.c
  nghttp2_session.c nghttp2_submit.c
  nghttp2_helper.c
  nghttp2_alpn.c
  nghttp2_hd.c nghttp2_hd_huffman.c nghttp2_hd_huffman_data.c
  nghttp2_version.c
  nghttp2_priority_spec.c
  nghttp2_option.c
  nghttp2_callbacks.c
  nghttp2_mem.c
  nghttp2_http.c
  nghttp2_rcbuf.c
  nghttp2_extpri.c
  nghttp2_ratelim.c
  nghttp2_time.c
  nghttp2_debug.c
  sfparse.c
)

set(NGHTTP2_RES "")
set(STATIC_LIB "nghttp2_static")
set(SHARED_LIB "nghttp2")

if(BUILD_SHARED_LIBS AND BUILD_STATIC_LIBS AND MSVC AND NOT STATIC_LIB_SUFFIX)
  set(STATIC_LIB_SUFFIX "_static")
endif()

if(WIN32)
  configure_file(
    version.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/version.rc
    @ONLY)

  set(NGHTTP2_RES ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
endif()

set(EXPORT_SET "${PROJECT_NAME}-targets")

# Public shared library
if(BUILD_SHARED_LIBS)
  add_library(${SHARED_LIB} SHARED ${NGHTTP2_SOURCES} ${NGHTTP2_RES})

  set_target_properties(${SHARED_LIB} PROPERTIES
    COMPILE_FLAGS "${WARNCFLAGS}"
    VERSION ${LT_VERSION} SOVERSION ${LT_SOVERSION}
    C_VISIBILITY_PRESET hidden
  )

  target_include_directories(${SHARED_LIB} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/includes>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/includes>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  install(TARGETS ${SHARED_LIB} EXPORT ${EXPORT_SET})
  list(APPEND nghttp2_exports ${SHARED_LIB})
endif()

# Static library (for unittests because of symbol visibility)
if(BUILD_STATIC_LIBS)
  add_library(${STATIC_LIB} STATIC ${NGHTTP2_SOURCES})

  set_target_properties(${STATIC_LIB} PROPERTIES
    COMPILE_FLAGS "${WARNCFLAGS}"
    VERSION ${LT_VERSION} SOVERSION ${LT_SOVERSION}
    ARCHIVE_OUTPUT_NAME nghttp2${STATIC_LIB_SUFFIX}
  )

  target_include_directories(${STATIC_LIB} INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/includes>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/includes>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  target_compile_definitions(${STATIC_LIB} PUBLIC "-DNGHTTP2_STATICLIB")

  install(TARGETS ${STATIC_LIB} EXPORT ${EXPORT_SET})
  list(APPEND nghttp2_exports ${STATIC_LIB})
endif()

if(BUILD_SHARED_LIBS)
  set(LIB_SELECTED ${SHARED_LIB})
else()
  set(LIB_SELECTED ${STATIC_LIB})
endif()

add_library(${PROJECT_NAME}::nghttp2 ALIAS ${LIB_SELECTED})

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libnghttp2.pc"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")

install(EXPORT ${EXPORT_SET}
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
  NAMESPACE ${PROJECT_NAME}::)
