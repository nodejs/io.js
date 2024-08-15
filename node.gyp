{
  'variables': {
    'v8_use_siphash%': 0,
    'v8_trace_maps%': 0,
    'v8_enable_pointer_compression%': 0,
    'v8_enable_31bit_smis_on_64bit_arch%': 0,
    'node_no_browser_globals%': 'false',
    'node_snapshot_main%': '',
    'node_use_node_snapshot%': 'false',
    'node_use_v8_platform%': 'true',
    'node_use_bundled_v8%': 'true',
    'node_shared%': 'false',
    'node_write_snapshot_as_string_literals': 'true',
    'force_dynamic_crt%': 0,
    'ossfuzz' : 'false',
    'node_module_version%': '',
    'node_use_amaro%': 'true',
    'node_shared_brotli%': 'false',
    'node_shared_zlib%': 'false',
    'node_shared_http_parser%': 'false',
    'node_shared_cares%': 'false',
    'node_shared_libuv%': 'false',
    'node_shared_sqlite%': 'false',
    'node_shared_uvwasi%': 'false',
    'node_shared_nghttp2%': 'false',
    'node_use_openssl%': 'true',
    'node_shared_openssl%': 'false',
    'node_v8_options%': '',
    'node_enable_v8_vtunejit%': 'false',
    'node_core_target_name%': 'node',
    'node_lib_target_name%': 'libnode',
    'node_intermediate_lib_type%': 'static_library',
    'node_builtin_modules_path%': '',
    'linked_module_files': [
    ],
    # We list the deps/ files out instead of globbing them in js2c.cc since we
    # only include a subset of all the files under these directories.
    # The lengths of their file names combined should not exceed the
    # Windows command length limit or there would be an error.
    # See https://docs.microsoft.com/en-us/troubleshoot/windows-client/shell-experience/command-line-string-limitation
    'library_files': [
      '<@(node_library_files)',
      '<@(linked_module_files)',
    ],
    'deps_files': [
      'deps/v8/tools/splaytree.mjs',
      'deps/v8/tools/codemap.mjs',
      'deps/v8/tools/consarray.mjs',
      'deps/v8/tools/csvparser.mjs',
      'deps/v8/tools/profile.mjs',
      'deps/v8/tools/profile_view.mjs',
      'deps/v8/tools/logreader.mjs',
      'deps/v8/tools/arguments.mjs',
      'deps/v8/tools/tickprocessor.mjs',
      'deps/v8/tools/sourcemap.mjs',
      'deps/v8/tools/tickprocessor-driver.mjs',
      'deps/acorn/acorn/dist/acorn.js',
      'deps/acorn/acorn-walk/dist/walk.js',
      'deps/minimatch/index.js',
      '<@(node_builtin_shareable_builtins)',
    ],
    'node_sources': [
      'src/api/async_resource.cc',
      'src/api/callback.cc',
      'src/api/embed_helpers.cc',
      'src/api/encoding.cc',
      'src/api/environment.cc',
      'src/api/exceptions.cc',
      'src/api/hooks.cc',
      'src/api/utils.cc',
      'src/async_context_frame.cc',
      'src/async_wrap.cc',
      'src/base_object.cc',
      'src/cares_wrap.cc',
      'src/cleanup_queue.cc',
      'src/compile_cache.cc',
      'src/connect_wrap.cc',
      'src/connection_wrap.cc',
      'src/dataqueue/queue.cc',
      'src/debug_utils.cc',
      'src/embedded_data.cc',
      'src/encoding_binding.cc',
      'src/env.cc',
      'src/fs_event_wrap.cc',
      'src/handle_wrap.cc',
      'src/heap_utils.cc',
      'src/histogram.cc',
      'src/internal_only_v8.cc',
      'src/js_native_api.h',
      'src/js_native_api_types.h',
      'src/js_native_api_v8.cc',
      'src/js_native_api_v8.h',
      'src/js_native_api_v8_internals.h',
      'src/js_stream.cc',
      'src/json_utils.cc',
      'src/js_udp_wrap.cc',
      'src/json_parser.h',
      'src/json_parser.cc',
      'src/module_wrap.cc',
      'src/node.cc',
      'src/node_api.cc',
      'src/node_binding.cc',
      'src/node_blob.cc',
      'src/node_buffer.cc',
      'src/node_builtins.cc',
      'src/node_config.cc',
      'src/node_constants.cc',
      'src/node_contextify.cc',
      'src/node_credentials.cc',
      'src/node_debug.cc',
      'src/node_dir.cc',
      'src/node_dotenv.cc',
      'src/node_env_var.cc',
      'src/node_errors.cc',
      'src/node_external_reference.cc',
      'src/node_file.cc',
      'src/node_http_parser.cc',
      'src/node_http2.cc',
      'src/node_i18n.cc',
      'src/node_main_instance.cc',
      'src/node_messaging.cc',
      'src/node_metadata.cc',
      'src/node_modules.cc',
      'src/node_options.cc',
      'src/node_os.cc',
      'src/node_perf.cc',
      'src/node_platform.cc',
      'src/node_postmortem_metadata.cc',
      'src/node_process_events.cc',
      'src/node_process_methods.cc',
      'src/node_process_object.cc',
      'src/node_realm.cc',
      'src/node_report.cc',
      'src/node_report_module.cc',
      'src/node_report_utils.cc',
      'src/node_sea.cc',
      'src/node_serdes.cc',
      'src/node_shadow_realm.cc',
      'src/node_snapshotable.cc',
      'src/node_sockaddr.cc',
      'src/node_sqlite.cc',
      'src/node_stat_watcher.cc',
      'src/node_symbols.cc',
      'src/node_task_queue.cc',
      'src/node_task_runner.cc',
      'src/node_trace_events.cc',
      'src/node_types.cc',
      'src/node_url.cc',
      'src/node_util.cc',
      'src/node_v8.cc',
      'src/node_wasi.cc',
      'src/node_wasm_web_api.cc',
      'src/node_watchdog.cc',
      'src/node_webstorage.cc',
      'src/node_worker.cc',
      'src/node_zlib.cc',
      'src/path.cc',
      'src/permission/child_process_permission.cc',
      'src/permission/fs_permission.cc',
      'src/permission/inspector_permission.cc',
      'src/permission/permission.cc',
      'src/permission/wasi_permission.cc',
      'src/permission/worker_permission.cc',
      'src/pipe_wrap.cc',
      'src/process_wrap.cc',
      'src/signal_wrap.cc',
      'src/spawn_sync.cc',
      'src/stream_base.cc',
      'src/stream_pipe.cc',
      'src/stream_wrap.cc',
      'src/string_bytes.cc',
      'src/string_decoder.cc',
      'src/tcp_wrap.cc',
      'src/timers.cc',
      'src/timer_wrap.cc',
      'src/tracing/agent.cc',
      'src/tracing/node_trace_buffer.cc',
      'src/tracing/node_trace_writer.cc',
      'src/tracing/trace_event.cc',
      'src/tracing/traced_value.cc',
      'src/tty_wrap.cc',
      'src/udp_wrap.cc',
      'src/util.cc',
      'src/uv.cc',
      # headers to make for a more pleasant IDE experience
      'src/aliased_buffer.h',
      'src/aliased_buffer-inl.h',
      'src/aliased_struct.h',
      'src/aliased_struct-inl.h',
      'src/async_context_frame.h',
      'src/async_wrap.h',
      'src/async_wrap-inl.h',
      'src/base_object.h',
      'src/base_object-inl.h',
      'src/base_object_types.h',
      'src/blob_serializer_deserializer.h',
      'src/blob_serializer_deserializer-inl.h',
      'src/callback_queue.h',
      'src/callback_queue-inl.h',
      'src/cleanup_queue.h',
      'src/cleanup_queue-inl.h',
      'src/compile_cache.h',
      'src/connect_wrap.h',
      'src/connection_wrap.h',
      'src/dataqueue/queue.h',
      'src/debug_utils.h',
      'src/debug_utils-inl.h',
      'src/embedded_data.h',
      'src/encoding_binding.h',
      'src/env_properties.h',
      'src/env.h',
      'src/env-inl.h',
      'src/handle_wrap.h',
      'src/histogram.h',
      'src/histogram-inl.h',
      'src/js_stream.h',
      'src/json_utils.h',
      'src/large_pages/node_large_page.cc',
      'src/large_pages/node_large_page.h',
      'src/memory_tracker.h',
      'src/memory_tracker-inl.h',
      'src/module_wrap.h',
      'src/node.h',
      'src/node_api.h',
      'src/node_api_types.h',
      'src/node_binding.h',
      'src/node_blob.h',
      'src/node_buffer.h',
      'src/node_builtins.h',
      'src/node_constants.h',
      'src/node_context_data.h',
      'src/node_contextify.h',
      'src/node_debug.h',
      'src/node_dir.h',
      'src/node_dotenv.h',
      'src/node_errors.h',
      'src/node_exit_code.h',
      'src/node_external_reference.h',
      'src/node_file.h',
      'src/node_file-inl.h',
      'src/node_http_common.h',
      'src/node_http_common-inl.h',
      'src/node_http2.h',
      'src/node_http2_state.h',
      'src/node_i18n.h',
      'src/node_internals.h',
      'src/node_main_instance.h',
      'src/node_mem.h',
      'src/node_mem-inl.h',
      'src/node_messaging.h',
      'src/node_metadata.h',
      'src/node_mutex.h',
      'src/node_modules.h',
      'src/node_object_wrap.h',
      'src/node_options.h',
      'src/node_options-inl.h',
      'src/node_perf.h',
      'src/node_perf_common.h',
      'src/node_platform.h',
      'src/node_process.h',
      'src/node_process-inl.h',
      'src/node_realm.h',
      'src/node_realm-inl.h',
      'src/node_report.h',
      'src/node_revert.h',
      'src/node_root_certs.h',
      'src/node_sea.h',
      'src/node_shadow_realm.h',
      'src/node_snapshotable.h',
      'src/node_snapshot_builder.h',
      'src/node_sockaddr.h',
      'src/node_sockaddr-inl.h',
      'src/node_sqlite.h',
      'src/node_stat_watcher.h',
      'src/node_union_bytes.h',
      'src/node_url.h',
      'src/node_version.h',
      'src/node_v8.h',
      'src/node_v8_platform-inl.h',
      'src/node_wasi.h',
      'src/node_watchdog.h',
      'src/node_webstorage.h',
      'src/node_worker.h',
      'src/path.h',
      'src/permission/child_process_permission.h',
      'src/permission/fs_permission.h',
      'src/permission/inspector_permission.h',
      'src/permission/permission.h',
      'src/permission/wasi_permission.h',
      'src/permission/worker_permission.h',
      'src/pipe_wrap.h',
      'src/req_wrap.h',
      'src/req_wrap-inl.h',
      'src/spawn_sync.h',
      'src/stream_base.h',
      'src/stream_base-inl.h',
      'src/stream_pipe.h',
      'src/stream_wrap.h',
      'src/string_bytes.h',
      'src/string_decoder.h',
      'src/string_decoder-inl.h',
      'src/tcp_wrap.h',
      'src/timers.h',
      'src/tracing/agent.h',
      'src/tracing/node_trace_buffer.h',
      'src/tracing/node_trace_writer.h',
      'src/tracing/trace_event.h',
      'src/tracing/trace_event_common.h',
      'src/tracing/traced_value.h',
      'src/timer_wrap.h',
      'src/timer_wrap-inl.h',
      'src/tty_wrap.h',
      'src/udp_wrap.h',
      'src/util.h',
      'src/util-inl.h',
    ],
    'node_crypto_sources': [
      'src/crypto/crypto_aes.cc',
      'src/crypto/crypto_bio.cc',
      'src/crypto/crypto_common.cc',
      'src/crypto/crypto_dsa.cc',
      'src/crypto/crypto_hkdf.cc',
      'src/crypto/crypto_pbkdf2.cc',
      'src/crypto/crypto_sig.cc',
      'src/crypto/crypto_timing.cc',
      'src/crypto/crypto_cipher.cc',
      'src/crypto/crypto_context.cc',
      'src/crypto/crypto_ec.cc',
      'src/crypto/crypto_hmac.cc',
      'src/crypto/crypto_random.cc',
      'src/crypto/crypto_rsa.cc',
      'src/crypto/crypto_spkac.cc',
      'src/crypto/crypto_util.cc',
      'src/crypto/crypto_clienthello.cc',
      'src/crypto/crypto_dh.cc',
      'src/crypto/crypto_hash.cc',
      'src/crypto/crypto_keys.cc',
      'src/crypto/crypto_keygen.cc',
      'src/crypto/crypto_scrypt.cc',
      'src/crypto/crypto_tls.cc',
      'src/crypto/crypto_x509.cc',
      'src/crypto/crypto_bio.h',
      'src/crypto/crypto_clienthello-inl.h',
      'src/crypto/crypto_dh.h',
      'src/crypto/crypto_hmac.h',
      'src/crypto/crypto_rsa.h',
      'src/crypto/crypto_spkac.h',
      'src/crypto/crypto_util.h',
      'src/crypto/crypto_cipher.h',
      'src/crypto/crypto_common.h',
      'src/crypto/crypto_dsa.h',
      'src/crypto/crypto_hash.h',
      'src/crypto/crypto_keys.h',
      'src/crypto/crypto_keygen.h',
      'src/crypto/crypto_scrypt.h',
      'src/crypto/crypto_tls.h',
      'src/crypto/crypto_clienthello.h',
      'src/crypto/crypto_context.h',
      'src/crypto/crypto_ec.h',
      'src/crypto/crypto_hkdf.h',
      'src/crypto/crypto_pbkdf2.h',
      'src/crypto/crypto_sig.h',
      'src/crypto/crypto_random.h',
      'src/crypto/crypto_timing.h',
      'src/crypto/crypto_x509.h',
      'src/node_crypto.cc',
      'src/node_crypto.h',
    ],
    'node_quic_sources': [
      'src/quic/application.cc',
      'src/quic/bindingdata.cc',
      'src/quic/cid.cc',
      'src/quic/data.cc',
      'src/quic/endpoint.cc',
      'src/quic/http3.cc',
      'src/quic/logstream.cc',
      'src/quic/packet.cc',
      'src/quic/preferredaddress.cc',
      'src/quic/session.cc',
      'src/quic/sessionticket.cc',
      'src/quic/streams.cc',
      'src/quic/tlscontext.cc',
      'src/quic/tokens.cc',
      'src/quic/transportparams.cc',
      'src/quic/application.h',
      'src/quic/bindingdata.h',
      'src/quic/cid.h',
      'src/quic/data.h',
      'src/quic/endpoint.h',
      'src/quic/http3.h',
      'src/quic/logstream.h',
      'src/quic/packet.h',
      'src/quic/preferredaddress.h',
      'src/quic/session.h',
      'src/quic/sessionticket.h',
      'src/quic/streams.h',
      'src/quic/tlscontext.h',
      'src/quic/tokens.h',
      'src/quic/transportparams.h',
      'src/quic/quic.cc',
    ],
    'node_cctest_sources': [
      'src/node_snapshot_stub.cc',
      'test/cctest/node_test_fixture.cc',
      'test/cctest/node_test_fixture.h',
      'test/cctest/test_aliased_buffer.cc',
      'test/cctest/test_base64.cc',
      'test/cctest/test_base_object_ptr.cc',
      'test/cctest/test_cppgc.cc',
      'test/cctest/test_node_postmortem_metadata.cc',
      'test/cctest/test_node_task_runner.cc',
      'test/cctest/test_environment.cc',
      'test/cctest/test_linked_binding.cc',
      'test/cctest/test_node_api.cc',
      'test/cctest/test_path.cc',
      'test/cctest/test_per_process.cc',
      'test/cctest/test_platform.cc',
      'test/cctest/test_report.cc',
      'test/cctest/test_json_utils.cc',
      'test/cctest/test_sockaddr.cc',
      'test/cctest/test_traced_value.cc',
      'test/cctest/test_util.cc',
      'test/cctest/test_dataqueue.cc',
    ],
    'node_cctest_openssl_sources': [
      'test/cctest/test_crypto_clienthello.cc',
      'test/cctest/test_node_crypto.cc',
      'test/cctest/test_node_crypto_env.cc',
      'test/cctest/test_quic_cid.cc',
      'test/cctest/test_quic_error.cc',
      'test/cctest/test_quic_tokens.cc',
    ],
    'node_cctest_inspector_sources': [
      'test/cctest/test_inspector_socket.cc',
      'test/cctest/test_inspector_socket_server.cc',
    ],
    'node_mksnapshot_exec': '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)node_mksnapshot<(EXECUTABLE_SUFFIX)',
    'node_js2c_exec': '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)node_js2c<(EXECUTABLE_SUFFIX)',
    'conditions': [
      ['GENERATOR == "ninja"', {
        'node_text_start_object_path': 'src/large_pages/node_text_start.node_text_start.o'
      }, {
        'node_text_start_object_path': 'node_text_start/src/large_pages/node_text_start.o'
      }],
      [ 'node_shared=="true"', {
        'node_target_type%': 'shared_library',
        'conditions': [
          ['OS in "aix os400"', {
            # For AIX, always generate static library first,
            # It needs an extra step to generate exp and
            # then use both static lib and exp to create
            # shared lib.
            'node_intermediate_lib_type': 'static_library',
          }, {
            'node_intermediate_lib_type': 'shared_library',
          }],
        ],
      }, {
        'node_target_type%': 'executable',
      }],
      [ 'OS=="win" and '
        'node_use_openssl=="true" and '
        'node_shared_openssl=="false"', {
        'use_openssl_def%': 1,
      }, {
        'use_openssl_def%': 0,
      }],
      [ 'node_use_amaro=="true"', {
          'deps_files': [
              'deps/amaro/dist/index.js',
          ]
      } ]
    ],
  },

  'target_defaults': {
    # Putting these explicitly here so not to depend on `common.gypi`.
    # `common.gypi` need to be more general because it is used to build userland native addons.
    # Refs: https://github.com/nodejs/node-gyp/issues/1118
    'cflags': [ '-Wall', '-Wextra', '-Wno-unused-parameter', ],
    'xcode_settings': {
      'WARNING_CFLAGS': [
        '-Wall',
        '-Wendif-labels',
        '-W',
        '-Wno-unused-parameter',
        '-Werror=undefined-inline',
        '-Werror=extra-semi',
      ],
    },

    'conditions': [
      # Pointer authentication for ARM64.
      ['target_arch=="arm64"', {
          'target_conditions': [
              ['_toolset=="host"', {
                  'conditions': [
                      ['host_arch=="arm64"', {
                          'cflags': ['-mbranch-protection=standard'],
                      }],
                  ],
              }],
              ['_toolset=="target"', {
                  'cflags': ['-mbranch-protection=standard'],
              }],
          ],
      }],
      ['OS in "aix os400"', {
        'ldflags': [
          '-Wl,-bnoerrmsg',
        ],
      }],
      ['OS=="linux" and clang==1', {
        'libraries': ['-latomic'],
      }],
    ],
  },

  'targets': [
    {
      'target_name': 'node_text_start',
      'type': 'none',
      'conditions': [
        [ 'OS in "linux freebsd solaris" and '
          'target_arch=="x64"', {
          'type': 'static_library',
          'sources': [
            'src/large_pages/node_text_start.S'
          ]
        }],
      ]
    },
    {
      'target_name': '<(node_core_target_name)',
      'type': 'executable',

      'defines': [
        'NODE_ARCH="<(target_arch)"',
        'NODE_PLATFORM="<(OS)"',
        'NODE_WANT_INTERNALS=1',
      ],

      'includes': [
        'node.gypi'
      ],

      'include_dirs': [
        'src',
        'deps/v8/include',
        'deps/postject'
      ],

      'sources': [
        'src/node_main.cc'
      ],

      'dependencies': [
        'deps/histogram/histogram.gyp:histogram',
      ],

      'msvs_settings': {
        'VCLinkerTool': {
          'GenerateMapFile': 'true', # /MAP
          'MapExports': 'true', # /MAPINFO:EXPORTS
          'RandomizedBaseAddress': 2, # enable ASLR
          'DataExecutionPrevention': 2, # enable DEP
          'AllowIsolation': 'true',
          # By default, the MSVC linker only reserves 1 MiB of stack memory for
          # each thread, whereas other platforms typically allow much larger
          # stack memory sections. We raise the limit to make it more consistent
          # across platforms and to support the few use cases that require large
          # amounts of stack memory, without having to modify the node binary.
          'StackReserveSize': 0x800000,
        },
      },

      # - "C4244: conversion from 'type1' to 'type2', possible loss of data"
      #   Ususaly safe. Disable for `dep`, enable for `src`
      'msvs_disabled_warnings!': [4244],

      'conditions': [
        [ 'error_on_warn=="true"', {
          'cflags': ['-Werror'],
          'xcode_settings': {
            'WARNING_CFLAGS': [ '-Werror' ],
          },
        }],
        [ 'node_intermediate_lib_type=="static_library" and '
            'node_shared=="true" and OS in "aix os400"', {
          # For AIX, shared lib is linked by static lib and .exp. In the
          # case here, the executable needs to link to shared lib.
          # Therefore, use 'node_aix_shared' target to generate the
          # shared lib and then executable.
          'dependencies': [ 'node_aix_shared' ],
        }, {
          'dependencies': [ '<(node_lib_target_name)' ],
          'conditions': [
            ['OS=="win" and node_shared=="true"', {
              'dependencies': ['generate_node_def'],
              'msvs_settings': {
                'VCLinkerTool': {
                  'ModuleDefinitionFile': '<(PRODUCT_DIR)/<(node_core_target_name).def',
                },
              },
            }],
          ],
        }],
        [ 'node_intermediate_lib_type=="static_library" and node_shared=="false"', {
          'xcode_settings': {
            'OTHER_LDFLAGS': [
              '-Wl,-force_load,<(PRODUCT_DIR)/<(STATIC_LIB_PREFIX)<(node_core_target_name)<(STATIC_LIB_SUFFIX)',
              '-Wl,-force_load,<(PRODUCT_DIR)/<(STATIC_LIB_PREFIX)v8_base_without_compiler<(STATIC_LIB_SUFFIX)',
            ],
          },
          'msvs_settings': {
            'VCLinkerTool': {
              'AdditionalOptions': [
                '/WHOLEARCHIVE:<(PRODUCT_DIR)/lib/<(node_lib_target_name)<(STATIC_LIB_SUFFIX)',
                '/WHOLEARCHIVE:<(PRODUCT_DIR)/lib/<(STATIC_LIB_PREFIX)v8_base_without_compiler<(STATIC_LIB_SUFFIX)',
              ],
            },
          },
          'conditions': [
            ['OS != "aix" and OS != "os400" and OS != "mac" and OS != "ios"', {
              'ldflags': [
                '-Wl,--whole-archive',
                '<(obj_dir)/<(STATIC_LIB_PREFIX)<(node_core_target_name)<(STATIC_LIB_SUFFIX)',
                '<(obj_dir)/tools/v8_gypfiles/<(STATIC_LIB_PREFIX)v8_base_without_compiler<(STATIC_LIB_SUFFIX)',
                '-Wl,--no-whole-archive',
              ],
            }],
            [ 'OS=="win"', {
              'sources': [ 'src/res/node.rc' ],
            }],
          ],
        }],
        [ 'node_shared=="true"', {
          'xcode_settings': {
            'OTHER_LDFLAGS': [ '-Wl,-rpath,@loader_path', '-Wl,-rpath,@loader_path/../lib'],
          },
          'conditions': [
            ['OS=="linux"', {
               'ldflags': [
                 '-Wl,-rpath,\\$$ORIGIN/../lib'
               ],
            }],
          ],
        }],
        [ 'enable_lto=="true"', {
          'xcode_settings': {
            'OTHER_LDFLAGS': [
              # man ld -export_dynamic:
              # Preserves all global symbols in main executables during LTO.
              # Without this option, Link Time Optimization is allowed to
              # inline and remove global functions. This option is used when
              # a main executable may load a plug-in which requires certain
              # symbols from the main executable.
              '-Wl,-export_dynamic',
            ],
          },
        }],
        ['OS=="win"', {
          'libraries': [
            'Dbghelp.lib',
            'winmm.lib',
            'Ws2_32.lib',
          ],
        }],
        ['node_with_ltcg=="true"', {
          'msvs_settings': {
            'VCCLCompilerTool': {
              'WholeProgramOptimization': 'true'   # /GL, whole program optimization, needed for LTCG
            },
            'VCLibrarianTool': {
              'AdditionalOptions': [
                '/LTCG:INCREMENTAL',               # link time code generation
              ],
            },
            'VCLinkerTool': {
              'OptimizeReferences': 2,             # /OPT:REF
              'EnableCOMDATFolding': 2,            # /OPT:ICF
              'LinkIncremental': 1,                # disable incremental linking
              'AdditionalOptions': [
                '/LTCG:INCREMENTAL',               # incremental link-time code generation
              ],
            }
          }
        }, {
          'msvs_settings': {
            'VCCLCompilerTool': {
              'WholeProgramOptimization': 'false'
            },
            'VCLinkerTool': {
              'LinkIncremental': 2                 # enable incremental linking
            },
          },
         }],
         ['node_use_node_snapshot=="true"', {
          'dependencies': [
            'node_mksnapshot',
          ],
          'conditions': [
            ['node_snapshot_main!=""', {
              'actions': [
                {
                  'action_name': 'node_mksnapshot',
                  'process_outputs_as_sources': 1,
                  'inputs': [
                    '<(node_mksnapshot_exec)',
                    '<(node_snapshot_main)',
                  ],
                  'outputs': [
                    '<(SHARED_INTERMEDIATE_DIR)/node_snapshot.cc',
                  ],
                  'action': [
                    '<(node_mksnapshot_exec)',
                    '--build-snapshot',
                    '<(node_snapshot_main)',
                    '<@(_outputs)',
                  ],
                },
              ],
            }, {
              'actions': [
                {
                  'action_name': 'node_mksnapshot',
                  'process_outputs_as_sources': 1,
                  'inputs': [
                    '<(node_mksnapshot_exec)',
                  ],
                  'outputs': [
                    '<(SHARED_INTERMEDIATE_DIR)/node_snapshot.cc',
                  ],
                  'action': [
                    '<@(_inputs)',
                    '<@(_outputs)',
                  ],
                },
              ],
            }],
          ],
          }, {
          'sources': [
            'src/node_snapshot_stub.cc'
          ],
        }],
        [ 'OS in "linux freebsd" and '
          'target_arch=="x64"', {
          'dependencies': [ 'node_text_start' ],
          'ldflags+': [
            '<(obj_dir)/<(node_text_start_object_path)'
          ]
        }],

        ['node_fipsinstall=="true"', {
          'variables': {
            'openssl-cli': '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)openssl-cli<(EXECUTABLE_SUFFIX)',
            'provider_name': 'libopenssl-fipsmodule',
            'opensslconfig': './deps/openssl/nodejs-openssl.cnf',
            'conditions': [
              ['GENERATOR == "ninja"', {
	        'fipsmodule_internal': '<(PRODUCT_DIR)/lib/<(provider_name).so',
                'fipsmodule': '<(PRODUCT_DIR)/obj/lib/openssl-modules/fips.so',
                'fipsconfig': '<(PRODUCT_DIR)/obj/lib/fipsmodule.cnf',
                'opensslconfig_internal': '<(PRODUCT_DIR)/obj/lib/openssl.cnf',
             }, {
	        'fipsmodule_internal': '<(PRODUCT_DIR)/obj.target/deps/openssl/<(provider_name).so',
                'fipsmodule': '<(PRODUCT_DIR)/obj.target/deps/openssl/lib/openssl-modules/fips.so',
                'fipsconfig': '<(PRODUCT_DIR)/obj.target/deps/openssl/fipsmodule.cnf',
                'opensslconfig_internal': '<(PRODUCT_DIR)/obj.target/deps/openssl/openssl.cnf',
             }],
            ],
          },
          'actions': [
            {
              'action_name': 'fipsinstall',
              'process_outputs_as_sources': 1,
              'inputs': [
                '<(fipsmodule_internal)',
              ],
              'outputs': [
                '<(fipsconfig)',
              ],
              'action': [
                '<(openssl-cli)', 'fipsinstall',
                '-provider_name', '<(provider_name)',
                '-module', '<(fipsmodule_internal)',
                '-out', '<(fipsconfig)',
                #'-quiet',
              ],
            },
            {
              'action_name': 'copy_fips_module',
              'inputs': [
                '<(fipsmodule_internal)',
              ],
              'outputs': [
                '<(fipsmodule)',
              ],
              'action': [
                '<(python)', 'tools/copyfile.py',
                '<(fipsmodule_internal)',
                '<(fipsmodule)',
              ],
            },
            {
              'action_name': 'copy_openssl_cnf_and_include_fips_cnf',
              'inputs': [ '<(opensslconfig)', ],
              'outputs': [ '<(opensslconfig_internal)', ],
              'action': [
                '<(python)', 'tools/enable_fips_include.py',
                '<(opensslconfig)',
                '<(opensslconfig_internal)',
                '<(fipsconfig)',
              ],
            },
          ],
         }, {
           'variables': {
              'opensslconfig_internal': '<(obj_dir)/deps/openssl/openssl.cnf',
              'opensslconfig': './deps/openssl/nodejs-openssl.cnf',
           },
           'actions': [
             {
               'action_name': 'reset_openssl_cnf',
               'inputs': [ '<(opensslconfig)', ],
               'outputs': [ '<(opensslconfig_internal)', ],
               'action': [
                 '<(python)', 'tools/copyfile.py',
                 '<(opensslconfig)',
                 '<(opensslconfig_internal)',
               ],
             },
           ],
         }],
      ],
    }, # node_core_target_name
    {
      'target_name': '<(node_lib_target_name)',
      'type': '<(node_intermediate_lib_type)',
      'includes': [
        'node.gypi',
      ],

      'include_dirs': [
        'src',
        'deps/postject',
        '<(SHARED_INTERMEDIATE_DIR)' # for node_natives.h
      ],
      'dependencies': [
        'deps/googletest/googletest.gyp:gtest_prod',
        'deps/histogram/histogram.gyp:histogram',
        'deps/sqlite/sqlite.gyp:sqlite',
        'deps/simdjson/simdjson.gyp:simdjson',
        'deps/simdutf/simdutf.gyp:simdutf',
        'deps/ada/ada.gyp:ada',
        'deps/nbytes/nbytes.gyp:nbytes',
        'node_js2c#host',
      ],

      'sources': [
        '<@(node_sources)',
        # Dependency headers
        'deps/v8/include/v8.h',
        'deps/postject/postject-api.h',
        # javascript files to make for an even more pleasant IDE experience
        '<@(library_files)',
        '<@(deps_files)',
        # node.gyp is added by default, common.gypi is added for change detection
        'common.gypi',
      ],

      'variables': {
        'openssl_system_ca_path%': '',
        'openssl_default_cipher_list%': '',
      },

      'cflags': ['-Werror=unused-result'],

      'defines': [
        'NODE_ARCH="<(target_arch)"',
        'NODE_PLATFORM="<(OS)"',
        'NODE_WANT_INTERNALS=1',
        # Warn when using deprecated V8 APIs.
        'V8_DEPRECATION_WARNINGS=1',
        'NODE_OPENSSL_SYSTEM_CERT_PATH="<(openssl_system_ca_path)"',
      ],

      # - "C4244: conversion from 'type1' to 'type2', possible loss of data"
      #   Ususaly safe. Disable for `dep`, enable for `src`
      'msvs_disabled_warnings!': [4244],

      'conditions': [
        [ 'openssl_default_cipher_list!=""', {
          'defines': [
            'NODE_OPENSSL_DEFAULT_CIPHER_LIST="<(openssl_default_cipher_list)"'
           ]
        }],
        [ 'error_on_warn=="true"', {
          'cflags': ['-Werror'],
          'xcode_settings': {
            'WARNING_CFLAGS': [ '-Werror' ],
          },
        }],
        [ 'node_builtin_modules_path!=""', {
          'defines': [ 'NODE_BUILTIN_MODULES_PATH="<(node_builtin_modules_path)"' ]
        }],
        [ 'node_shared=="true"', {
          'sources': [
            'src/node_snapshot_stub.cc',
          ]
        }],
        [ 'node_shared=="true" and node_module_version!="" and OS!="win"', {
          'product_extension': '<(shlib_suffix)',
          'xcode_settings': {
            'LD_DYLIB_INSTALL_NAME':
              '@rpath/lib<(node_core_target_name).<(shlib_suffix)'
          },
        }],
        [ 'node_use_node_code_cache=="true"', {
          'defines': [
            'NODE_USE_NODE_CODE_CACHE=1',
          ],
        }],
        ['node_shared=="true" and OS in "aix os400"', {
          'product_name': 'node_base',
        }],
        [ 'v8_enable_inspector==1', {
          'includes' : [ 'src/inspector/node_inspector.gypi' ],
        }, {
          'defines': [ 'HAVE_INSPECTOR=0' ]
        }],
        [ 'OS=="win"', {
          'conditions': [
            [ 'node_intermediate_lib_type!="static_library"', {
              'sources': [
                'src/res/node.rc',
              ],
            }],
          ],
          'libraries': [
            'Dbghelp',
            'Psapi',
            'Winmm',
            'Ws2_32',
          ],
        }],
        [ 'node_use_openssl=="true"', {
          'sources': [
            '<@(node_crypto_sources)',
            '<@(node_quic_sources)',
          ],
          'dependencies': [
            'deps/ncrypto/ncrypto.gyp:ncrypto',
          ],
        }],
        [ 'OS in "linux freebsd mac solaris" and '
          'target_arch=="x64" and '
          'node_target_type=="executable"', {
          'defines': [ 'NODE_ENABLE_LARGE_CODE_PAGES=1' ],
        }],
        [ 'use_openssl_def==1', {
          # TODO(bnoordhuis) Make all platforms export the same list of symbols.
          # Teach mkssldef.py to generate linker maps that UNIX linkers understand.
          'variables': {
            'mkssldef_flags': [
              # Categories to export.
              '-CAES,BF,BIO,DES,DH,DSA,EC,ECDH,ECDSA,ENGINE,EVP,HMAC,MD4,MD5,'
              'PSK,RC2,RC4,RSA,SHA,SHA0,SHA1,SHA256,SHA512,SOCK,STDIO,TLSEXT,'
              'UI,FP_API,TLS1_METHOD,TLS1_1_METHOD,TLS1_2_METHOD,SCRYPT,OCSP,'
              'NEXTPROTONEG,RMD160,CAST,DEPRECATEDIN_1_1_0,DEPRECATEDIN_1_2_0,'
              'DEPRECATEDIN_3_0',
              # Defines.
              '-DWIN32',
              # Symbols to filter from the export list.
              '-X^DSO',
              '-X^_',
              '-X^private_',
              # Base generated DEF on zlib.def
              '-Bdeps/zlib/win32/zlib.def'
            ],
          },
          'conditions': [
            ['openssl_is_fips!=""', {
              'variables': { 'mkssldef_flags': ['-DOPENSSL_FIPS'] },
            }],
          ],
          'actions': [
            {
              'action_name': 'mkssldef',
              'inputs': [
                'deps/openssl/openssl/util/libcrypto.num',
                'deps/openssl/openssl/util/libssl.num',
              ],
              'outputs': ['<(SHARED_INTERMEDIATE_DIR)/openssl.def'],
              'process_outputs_as_sources': 1,
              'action': [
                '<(python)',
                'tools/mkssldef.py',
                '<@(mkssldef_flags)',
                '-o',
                '<@(_outputs)',
                '<@(_inputs)',
              ],
            },
          ],
        }],
        [ 'debug_nghttp2==1', {
          'defines': [ 'NODE_DEBUG_NGHTTP2=1' ]
        }],
      ],
      'actions': [
        {
          'action_name': 'node_js2c',
          'process_outputs_as_sources': 1,
          'inputs': [
            '<(node_js2c_exec)',
            '<@(library_files)',
            '<@(deps_files)',
            'config.gypi'
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/node_javascript.cc',
          ],
          'action': [
            '<(node_js2c_exec)',
            '<@(_outputs)',
            'lib',
            'config.gypi',
            '<@(deps_files)',
            '<@(linked_module_files)',
          ],
        },
      ],
    }, # node_lib_target_name
    { # fuzz_env
      'target_name': 'fuzz_env',
      'type': 'executable',
      'dependencies': [
        '<(node_lib_target_name)',
        'deps/histogram/histogram.gyp:histogram',
        'deps/sqlite/sqlite.gyp:sqlite',
      ],

      'includes': [
        'node.gypi'
      ],
      'include_dirs': [
        'src',
        'tools/msvs/genfiles',
        'deps/v8/include',
        'deps/cares/include',
        'deps/uv/include',
        'deps/sqlite',
        'test/cctest',
      ],

      'defines': [
        'NODE_ARCH="<(target_arch)"',
        'NODE_PLATFORM="<(OS)"',
        'NODE_WANT_INTERNALS=1',
      ],
      'sources': [
        'src/node_snapshot_stub.cc',
        'test/fuzzers/fuzz_env.cc',
      ],
      'conditions': [
        ['OS=="linux"', {
          'ldflags': [ '-fsanitize=fuzzer' ]
        }],
        # Ensure that ossfuzz flag has been set and that we are on Linux
        [ 'OS!="linux" or ossfuzz!="true"', {
          'type': 'none',
        }],
        # Avoid excessive LTO
        ['enable_lto=="true"', {
          'ldflags': [ '-fno-lto' ],
        }],
      ],
    }, # fuzz_env
    { # fuzz_ClientHelloParser.cc
      'target_name': 'fuzz_ClientHelloParser',
      'type': 'executable',
      'dependencies': [
        '<(node_lib_target_name)',
        'deps/histogram/histogram.gyp:histogram',
        'deps/sqlite/sqlite.gyp:sqlite',
        'deps/uvwasi/uvwasi.gyp:uvwasi',
      ],
      'includes': [
        'node.gypi'
      ],
      'include_dirs': [
        'src',
        'tools/msvs/genfiles',
        'deps/v8/include',
        'deps/cares/include',
        'deps/sqlite',
        'deps/uv/include',
        'deps/uvwasi/include',
        'test/cctest',
      ],
      'defines': [
        'NODE_ARCH="<(target_arch)"',
        'NODE_PLATFORM="<(OS)"',
        'NODE_WANT_INTERNALS=1',
      ],
      'sources': [
        'src/node_snapshot_stub.cc',
        'test/fuzzers/fuzz_ClientHelloParser.cc',
      ],
      'conditions': [
        ['OS=="linux"', {
          'ldflags': [ '-fsanitize=fuzzer' ]
        }],
        # Ensure that ossfuzz flag has been set and that we are on Linux
        [ 'OS!="linux" or ossfuzz!="true"', {
          'type': 'none',
        }],
        # Avoid excessive LTO
        ['enable_lto=="true"', {
          'ldflags': [ '-fno-lto' ],
        }],
      ],
    }, # fuzz_ClientHelloParser.cc
    { # fuzz_strings
      'target_name': 'fuzz_strings',
      'type': 'executable',
      'dependencies': [
        '<(node_lib_target_name)',
        'deps/googletest/googletest.gyp:gtest_prod',
        'deps/histogram/histogram.gyp:histogram',
        'deps/sqlite/sqlite.gyp:sqlite',
        'deps/uvwasi/uvwasi.gyp:uvwasi',
        'deps/ada/ada.gyp:ada',
        'deps/nbytes/nbytes.gyp:nbytes',
      ],
      'includes': [
        'node.gypi'
      ],
      'include_dirs': [
        'src',
        'tools/msvs/genfiles',
        'deps/v8/include',
        'deps/cares/include',
        'deps/sqlite',
        'deps/uv/include',
        'deps/uvwasi/include',
        'test/cctest',
      ],
      'defines': [
        'NODE_ARCH="<(target_arch)"',
        'NODE_PLATFORM="<(OS)"',
        'NODE_WANT_INTERNALS=1',
      ],
      'sources': [
        'src/node_snapshot_stub.cc',
        'test/fuzzers/fuzz_strings.cc',
      ],
      'conditions': [
        ['OS=="linux"', {
          'ldflags': [ '-fsanitize=fuzzer' ]
        }],
        # Ensure that ossfuzz flag has been set and that we are on Linux
        [ 'OS!="linux" or ossfuzz!="true"', {
          'type': 'none',
        }],
        # Avoid excessive LTO
        ['enable_lto=="true"', {
          'ldflags': [ '-fno-lto' ],
        }],
      ],
    }, # fuzz_strings
    {
      'target_name': 'cctest',
      'type': 'executable',

      'dependencies': [
        '<(node_lib_target_name)',
        'deps/googletest/googletest.gyp:gtest',
        'deps/googletest/googletest.gyp:gtest_main',
        'deps/histogram/histogram.gyp:histogram',
        'deps/sqlite/sqlite.gyp:sqlite',
        'deps/simdjson/simdjson.gyp:simdjson',
        'deps/simdutf/simdutf.gyp:simdutf',
        'deps/ada/ada.gyp:ada',
        'deps/nbytes/nbytes.gyp:nbytes',
      ],

      'includes': [
        'node.gypi'
      ],

      'include_dirs': [
        'src',
        'tools/msvs/genfiles',
        'deps/v8/include',
        'deps/cares/include',
        'deps/uv/include',
        'deps/sqlite',
        'test/cctest',
      ],

      'defines': [
        'NODE_ARCH="<(target_arch)"',
        'NODE_PLATFORM="<(OS)"',
        'NODE_WANT_INTERNALS=1',
      ],

      'sources': [ '<@(node_cctest_sources)' ],

      'conditions': [
        [ 'node_use_openssl=="true"', {
          'defines': [
            'HAVE_OPENSSL=1',
          ],
          'dependencies': [
            'deps/ncrypto/ncrypto.gyp:ncrypto',
          ],
          'sources': [ '<@(node_cctest_openssl_sources)' ],
        }],
        ['v8_enable_inspector==1', {
          'defines': [
            'HAVE_INSPECTOR=1',
          ],
          'sources': [ '<@(node_cctest_inspector_sources)' ],
        }, {
           'defines': [
             'HAVE_INSPECTOR=0',
           ]
        }],
        ['OS=="solaris"', {
          'ldflags': [ '-I<(SHARED_INTERMEDIATE_DIR)' ]
        }],
        # Skip cctest while building shared lib node for Windows
        [ 'OS=="win" and node_shared=="true"', {
          'type': 'none',
        }],
        [ 'node_shared=="true"', {
          'xcode_settings': {
            'OTHER_LDFLAGS': [ '-Wl,-rpath,@loader_path', ],
          },
        }],
        ['OS=="win"', {
          'libraries': [
            'Dbghelp.lib',
            'winmm.lib',
            'Ws2_32.lib',
          ],
        }],
        # Avoid excessive LTO
        ['enable_lto=="true"', {
          'ldflags': [ '-fno-lto' ],
        }],
      ],
    }, # cctest

    {
      'target_name': 'embedtest',
      'type': 'executable',

      'dependencies': [
        '<(node_lib_target_name)',
        'deps/histogram/histogram.gyp:histogram',
        'deps/sqlite/sqlite.gyp:sqlite',
        'deps/ada/ada.gyp:ada',
        'deps/nbytes/nbytes.gyp:nbytes',
      ],

      'includes': [
        'node.gypi'
      ],

      'include_dirs': [
        'src',
        'tools',
        'tools/msvs/genfiles',
        'deps/v8/include',
        'deps/cares/include',
        'deps/uv/include',
        'deps/sqlite',
        'test/embedding',
      ],

      'sources': [
        'src/node_snapshot_stub.cc',
        'test/embedding/embedtest.cc',
      ],

      'conditions': [
        ['OS=="solaris"', {
          'ldflags': [ '-I<(SHARED_INTERMEDIATE_DIR)' ]
        }],
        # Skip cctest while building shared lib node for Windows
        [ 'OS=="win" and node_shared=="true"', {
          'type': 'none',
        }],
        [ 'node_shared=="true"', {
          'xcode_settings': {
            'OTHER_LDFLAGS': [ '-Wl,-rpath,@loader_path', ],
          },
        }],
        ['OS=="win"', {
          'libraries': [
            'Dbghelp.lib',
            'winmm.lib',
            'Ws2_32.lib',
          ],
        }],
        # Avoid excessive LTO
        ['enable_lto=="true"', {
          'ldflags': [ '-fno-lto' ],
        }],
      ],
    }, # embedtest

    {
      'target_name': 'overlapped-checker',
      'type': 'executable',

      'conditions': [
        ['OS=="win"', {
          'sources': [
            'test/overlapped-checker/main_win.c'
          ],
        }],
        ['OS!="win"', {
          'sources': [
            'test/overlapped-checker/main_unix.c'
          ],
        }],
        # Avoid excessive LTO
        ['enable_lto=="true"', {
          'ldflags': [ '-fno-lto' ],
        }],
      ]
    }, # overlapped-checker
    {
      'target_name': 'node_js2c',
      'type': 'executable',
      'toolsets': ['host'],
      'dependencies': [
        'deps/simdutf/simdutf.gyp:simdutf#host',
      ],
      'include_dirs': [
        'tools',
        'src',
      ],
      'sources': [
        'tools/js2c.cc',
        'tools/executable_wrapper.h',
        'src/embedded_data.h',
        'src/embedded_data.cc',
      ],
      'conditions': [
        [ 'node_shared_libuv=="false"', {
          'dependencies': [ 'deps/uv/uv.gyp:libuv#host' ],
        }],
        [ 'OS in "linux mac"', {
          'defines': ['NODE_JS2C_USE_STRING_LITERALS'],
        }],
        [ 'debug_node=="true"', {
          'cflags!': [ '-O3' ],
          'cflags': [ '-g', '-O0' ],
          'defines': [ 'DEBUG' ],
          'xcode_settings': {
            'OTHER_CFLAGS': [
              '-g', '-O0'
            ],
          },
        }],
      ]
    },
    {
      'target_name': 'node_mksnapshot',
      'type': 'executable',

      'dependencies': [
        '<(node_lib_target_name)',
        'deps/histogram/histogram.gyp:histogram',
        'deps/sqlite/sqlite.gyp:sqlite',
        'deps/ada/ada.gyp:ada',
        'deps/nbytes/nbytes.gyp:nbytes',
        'deps/simdjson/simdjson.gyp:simdjson',
        'deps/simdutf/simdutf.gyp:simdutf',
      ],

      'includes': [
        'node.gypi'
      ],

      'include_dirs': [
        'src',
        'tools/msvs/genfiles',
        'deps/v8/include',
        'deps/cares/include',
        'deps/uv/include',
        'deps/sqlite',
      ],

      'defines': [ 'NODE_WANT_INTERNALS=1' ],

      'sources': [
        'src/node_snapshot_stub.cc',
        'tools/snapshot/node_mksnapshot.cc',
      ],

      'conditions': [
        ['node_write_snapshot_as_array_literals=="true"', {
          'defines': [ 'NODE_MKSNAPSHOT_USE_ARRAY_LITERALS=1' ],
        }],
        [ 'node_use_openssl=="true"', {
          'dependencies': [
            'deps/ncrypto/ncrypto.gyp:ncrypto',
          ],
          'defines': [
            'HAVE_OPENSSL=1',
          ],
        }],
        [ 'node_use_node_code_cache=="true"', {
          'defines': [
            'NODE_USE_NODE_CODE_CACHE=1',
          ],
        }],
        ['v8_enable_inspector==1', {
          'defines': [
            'HAVE_INSPECTOR=1',
          ],
        }],
        ['OS=="win"', {
          'libraries': [
            'Dbghelp.lib',
            'winmm.lib',
            'Ws2_32.lib',
          ],
        }],
        # Avoid excessive LTO
        ['enable_lto=="true"', {
          'ldflags': [ '-fno-lto' ],
        }],
      ],
    }, # node_mksnapshot
  ], # end targets

  'conditions': [
    ['OS in "aix os400" and node_shared=="true"', {
      'targets': [
        {
          'target_name': 'node_aix_shared',
          'type': 'shared_library',
          'product_name': '<(node_core_target_name)',
          'ldflags': ['--shared'],
          'product_extension': '<(shlib_suffix)',
          'includes': [
            'node.gypi'
          ],
          'dependencies': ['<(node_lib_target_name)'],
          'include_dirs': [
            'src',
            'deps/v8/include',
          ],
          'sources': [
            '<@(library_files)',
            '<@(deps_files)',
            'common.gypi',
          ],
          'direct_dependent_settings': {
            'ldflags': [ '-Wl,-brtl' ],
          },
        },
      ]
    }], # end aix section
    ['OS=="win" and node_shared=="true"', {
     'targets': [
       {
         'target_name': 'gen_node_def',
         'type': 'executable',
         'sources': [
           'tools/gen_node_def.cc'
         ],
       },
       {
         'target_name': 'generate_node_def',
         'dependencies': [
           'gen_node_def',
           '<(node_lib_target_name)',
         ],
         'type': 'none',
         'actions': [
           {
             'action_name': 'generate_node_def_action',
             'inputs': [
               '<(PRODUCT_DIR)/<(node_lib_target_name).dll'
             ],
             'outputs': [
               '<(PRODUCT_DIR)/<(node_core_target_name).def',
             ],
             'action': [
               '<(PRODUCT_DIR)/gen_node_def.exe',
               '<@(_inputs)',
               '<@(_outputs)',
             ],
           },
         ],
       },
     ],
   }], # end win section
  ], # end conditions block
}
