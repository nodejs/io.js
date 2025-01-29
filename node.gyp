{
  'variables': {
    'v8_use_siphash%': 0,
    'v8_trace_maps%': 0,
    'v8_enable_pointer_compression%': 0,
    'v8_enable_31bit_smis_on_64bit_arch%': 0,
    'node_use_dtrace%': 'false',
    'node_use_etw%': 'false',
    'node_no_browser_globals%': 'false',
    'node_snapshot_main%': '',
    'node_use_node_snapshot%': 'false',
    'node_use_v8_platform%': 'true',
    'node_use_bundled_v8%': 'true',
    'node_shared%': 'false',
    'force_dynamic_crt%': 0,
    'ossfuzz' : 'false',
    'node_module_version%': '',
    'node_shared_brotli%': 'false',
    'node_shared_zlib%': 'false',
    'node_shared_http_parser%': 'false',
    'node_shared_cares%': 'false',
    'node_shared_libuv%': 'false',
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
    # We list the deps/ files out instead of globbing them in js2c.py since we
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
    'node_mksnapshot_exec': '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)node_mksnapshot<(EXECUTABLE_SUFFIX)',
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

    # Relevant only for x86.
    # Refs: https://github.com/nodejs/node/pull/25852
    # Refs: https://docs.microsoft.com/en-us/cpp/build/reference/safeseh-image-has-safe-exception-handlers
    'msvs_settings': {
      'VCLinkerTool': {
        'ImageHasSafeExceptionHandlers': 'false',
      },
    },

    'conditions': [
      ['OS in "aix os400"', {
        'ldflags': [
          '-Wl,-bnoerrmsg',
        ],
      }],
      ['OS == "linux" and llvm_version != "0.0"', {
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
        'deps/uvwasi/uvwasi.gyp:uvwasi',
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
                '/WHOLEARCHIVE:<(node_lib_target_name)<(STATIC_LIB_SUFFIX)',
                '/WHOLEARCHIVE:<(STATIC_LIB_PREFIX)v8_base_without_compiler<(STATIC_LIB_SUFFIX)',
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
              'conditions': [
                [ 'node_use_etw=="true"', {
                  'sources': [
                    'tools/msvs/genfiles/node_etw_provider.rc'
                  ],
                }],
              ],
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
                'python', 'tools/copyfile.py',
                '<(fipsmodule_internal)',
                '<(fipsmodule)',
              ],
            },
            {
              'action_name': 'copy_openssl_cnf_and_include_fips_cnf',
              'inputs': [ '<(opensslconfig)', ],
              'outputs': [ '<(opensslconfig_internal)', ],
              'action': [
                'python', 'tools/enable_fips_include.py',
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
        'deps/base64/base64.gyp:base64',
        'deps/googletest/googletest.gyp:gtest_prod',
        'deps/histogram/histogram.gyp:histogram',
        'deps/uvwasi/uvwasi.gyp:uvwasi',
        'deps/simdutf/simdutf.gyp:simdutf',
        'deps/ada/ada.gyp:ada',
      ],

      'sources': [
        'src/api/async_resource.cc',
        'src/api/callback.cc',
        'src/api/embed_helpers.cc',
        'src/api/encoding.cc',
        'src/api/environment.cc',
        'src/api/exceptions.cc',
        'src/api/hooks.cc',
        'src/api/utils.cc',
        'src/async_wrap.cc',
        'src/base_object.cc',
        'src/cares_wrap.cc',
        'src/cleanup_queue.cc',
        'src/connect_wrap.cc',
        'src/connection_wrap.cc',
        'src/debug_utils.cc',
        'src/env.cc',
        'src/fs_event_wrap.cc',
        'src/handle_wrap.cc',
        'src/heap_utils.cc',
        'src/histogram.cc',
        'src/js_native_api.h',
        'src/js_native_api_types.h',
        'src/js_native_api_v8.cc',
        'src/js_native_api_v8.h',
        'src/js_native_api_v8_internals.h',
        'src/js_stream.cc',
        'src/json_utils.cc',
        'src/js_udp_wrap.cc',
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
        'src/node_dir.cc',
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
        'src/node_stat_watcher.cc',
        'src/node_symbols.cc',
        'src/node_task_queue.cc',
        'src/node_trace_events.cc',
        'src/node_types.cc',
        'src/node_url.cc',
        'src/node_util.cc',
        'src/node_v8.cc',
        'src/node_wasi.cc',
        'src/node_wasm_web_api.cc',
        'src/node_watchdog.cc',
        'src/node_worker.cc',
        'src/node_zlib.cc',
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
        'src/async_wrap.h',
        'src/async_wrap-inl.h',
        'src/base_object.h',
        'src/base_object-inl.h',
        'src/base_object_types.h',
        'src/base64.h',
        'src/base64-inl.h',
        'src/callback_queue.h',
        'src/callback_queue-inl.h',
        'src/cleanup_queue.h',
        'src/cleanup_queue-inl.h',
        'src/connect_wrap.h',
        'src/connection_wrap.h',
        'src/debug_utils.h',
        'src/debug_utils-inl.h',
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
        'src/node_dir.h',
        'src/node_errors.h',
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
        'src/node_stat_watcher.h',
        'src/node_union_bytes.h',
        'src/node_url.h',
        'src/node_util.h',
        'src/node_version.h',
        'src/node_v8.h',
        'src/node_v8_platform-inl.h',
        'src/node_wasi.h',
        'src/node_watchdog.h',
        'src/node_worker.h',
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
        'src/string_search.h',
        'src/tcp_wrap.h',
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
        # Dependency headers
        'deps/v8/include/v8.h',
        'deps/postject/postject-api.h'
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
        [ 'node_use_etw=="true"', {
          'defines': [ 'HAVE_ETW=1' ],
          'dependencies': [ 'node_etw' ],
          'include_dirs': [
            'src',
            'tools/msvs/genfiles',
            '<(SHARED_INTERMEDIATE_DIR)' # for node_natives.h
          ],
          'sources': [
            'src/node_win32_etw_provider.h',
            'src/node_win32_etw_provider-inl.h',
            'src/node_win32_etw_provider.cc',
            'src/node_dtrace.h',
            'src/node_dtrace.cc',
            'tools/msvs/genfiles/node_etw_provider.h',
          ],
          'conditions': [
            ['node_intermediate_lib_type != "static_library"', {
              'sources': [
                'tools/msvs/genfiles/node_etw_provider.rc',
              ],
            }],
          ],
        }],
        [ 'node_use_dtrace=="true"', {
          'defines': [ 'HAVE_DTRACE=1' ],
          'dependencies': [
            'node_dtrace_header',
            'specialize_node_d',
          ],
          'include_dirs': [ '<(SHARED_INTERMEDIATE_DIR)' ],
          #
          # DTrace is supported on linux, solaris, mac, and bsd.  There are
          # three object files associated with DTrace support, but they're
          # not all used all the time:
          #
          #   node_dtrace.o           all configurations
          #   node_dtrace_ustack.o    not supported on mac and linux
          #   node_dtrace_provider.o  All except OS X.  "dtrace -G" is not
          #                           used on OS X.
          #
          # Note that node_dtrace_provider.cc and node_dtrace_ustack.cc do not
          # actually exist.  They're listed here to trick GYP into linking the
          # corresponding object files into the final "node" executable.  These
          # object files are generated by "dtrace -G" using custom actions
          # below, and the GYP-generated Makefiles will properly build them when
          # needed.
          #
          'sources': [
            'src/node_dtrace.h',
            'src/node_dtrace.cc',
          ],
          'conditions': [
            [ 'OS=="linux"', {
              'sources': [
                '<(SHARED_INTERMEDIATE_DIR)/node_dtrace_provider.o'
              ],
            }],
            [ 'OS!="mac" and OS!="linux"', {
              'sources': [
                'src/node_dtrace_ustack.cc',
                'src/node_dtrace_provider.cc',
              ]
            }
          ] ]
        } ],
        [ 'node_use_openssl=="true"', {
          'sources': [
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
            'src/crypto/crypto_aes.cc',
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
            'src/node_crypto.h'
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
            # Put the code first so it's a dependency and can be used for invocation.
            'tools/js2c.py',
            '<@(library_files)',
            '<@(deps_files)',
            'config.gypi'
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/node_javascript.cc',
          ],
          'action': [
            '<(python)',
            'tools/js2c.py',
            '--directory',
            'lib',
            '--target',
            '<@(_outputs)',
            'config.gypi',
            '<@(deps_files)',
            '<@(linked_module_files)',
          ],
        },
      ],
    }, # node_lib_target_name
    {
       # generate ETW header and resource files
      'target_name': 'node_etw',
      'type': 'none',
      'conditions': [
        [ 'node_use_etw=="true"', {
          'actions': [
            {
              'action_name': 'node_etw',
              'inputs': [ 'src/res/node_etw_provider.man' ],
              'outputs': [
                'tools/msvs/genfiles/node_etw_provider.rc',
                'tools/msvs/genfiles/node_etw_provider.h',
                'tools/msvs/genfiles/node_etw_providerTEMP.BIN',
              ],
              'action': [ 'mc <@(_inputs) -h tools/msvs/genfiles -r tools/msvs/genfiles' ]
            }
          ]
        } ]
      ]
    }, # node_etw
    {
      'target_name': 'node_dtrace_header',
      'type': 'none',
      'conditions': [
        [ 'node_use_dtrace=="true" and OS!="linux"', {
          'actions': [
            {
              'action_name': 'node_dtrace_header',
              'inputs': [ 'src/node_provider.d' ],
              'outputs': [ '<(SHARED_INTERMEDIATE_DIR)/node_provider.h' ],
              'action': [ 'dtrace', '-h', '-xnolibs', '-s', '<@(_inputs)',
                '-o', '<@(_outputs)' ]
            }
          ]
        } ],
        [ 'node_use_dtrace=="true" and OS=="linux"', {
          'actions': [
            {
              'action_name': 'node_dtrace_header',
              'inputs': [ 'src/node_provider.d' ],
              'outputs': [ '<(SHARED_INTERMEDIATE_DIR)/node_provider.h' ],
              'action': [ 'dtrace', '-h', '-s', '<@(_inputs)',
                '-o', '<@(_outputs)' ]
            }
          ]
        } ],
      ]
    }, # node_dtrace_header
    {
      'target_name': 'node_dtrace_provider',
      'type': 'none',
      'conditions': [
        [ 'node_use_dtrace=="true" and OS!="mac" and OS!="linux"', {
          'actions': [
            {
              'action_name': 'node_dtrace_provider_o',
              'inputs': [
                '<(obj_dir)/<(node_lib_target_name)/src/node_dtrace.o',
              ],
              'outputs': [
                '<(obj_dir)/<(node_lib_target_name)/src/node_dtrace_provider.o'
              ],
              'action': [ 'dtrace', '-G', '-xnolibs', '-s', 'src/node_provider.d',
                '<@(_inputs)', '-o', '<@(_outputs)' ]
            }
          ]
        }],
        [ 'node_use_dtrace=="true" and OS=="linux"', {
          'actions': [
            {
              'action_name': 'node_dtrace_provider_o',
              'inputs': [ 'src/node_provider.d' ],
              'outputs': [
                '<(SHARED_INTERMEDIATE_DIR)/node_dtrace_provider.o'
              ],
              'action': [
                'dtrace', '-C', '-G', '-s', '<@(_inputs)', '-o', '<@(_outputs)'
              ],
            }
          ],
        }],
      ]
    }, # node_dtrace_provider
    {
      'target_name': 'node_dtrace_ustack',
      'type': 'none',
      'conditions': [
        [ 'node_use_dtrace=="true" and OS!="mac" and OS!="linux"', {
          'actions': [
            {
              'action_name': 'node_dtrace_ustack_constants',
              'inputs': [
                '<(obj_dir)/tools/v8_gypfiles/<(STATIC_LIB_PREFIX)v8_base_without_compiler<(STATIC_LIB_SUFFIX)'
              ],
              'outputs': [
                '<(SHARED_INTERMEDIATE_DIR)/v8constants.h'
              ],
              'action': [
                'tools/genv8constants.py',
                '<@(_outputs)',
                '<@(_inputs)'
              ]
            },
            {
              'action_name': 'node_dtrace_ustack',
              'inputs': [
                'src/v8ustack.d',
                '<(SHARED_INTERMEDIATE_DIR)/v8constants.h'
              ],
              'outputs': [
                '<(obj_dir)/<(node_lib_target_name)/src/node_dtrace_ustack.o'
              ],
              'conditions': [
                [ 'target_arch=="ia32" or target_arch=="arm"', {
                  'action': [
                    'dtrace', '-32', '-I<(SHARED_INTERMEDIATE_DIR)', '-Isrc',
                    '-C', '-G', '-s', 'src/v8ustack.d', '-o', '<@(_outputs)',
                  ]
                } ],
                [ 'target_arch=="x64"', {
                  'action': [
                    'dtrace', '-64', '-I<(SHARED_INTERMEDIATE_DIR)', '-Isrc',
                    '-C', '-G', '-s', 'src/v8ustack.d', '-o', '<@(_outputs)',
                  ]
                } ],
              ]
            },
          ]
        } ],
      ]
    }, # node_dtrace_ustack
    {
      'target_name': 'specialize_node_d',
      'type': 'none',
      'conditions': [
        [ 'node_use_dtrace=="true"', {
          'actions': [
            {
              'action_name': 'specialize_node_d',
              'inputs': [
                'src/node.d'
              ],
              'outputs': [
                '<(PRODUCT_DIR)/node.d',
              ],
              'action': [
                'tools/specialize_node_d.py',
                '<@(_outputs)',
                '<@(_inputs)',
                '<@(OS)',
                '<@(target_arch)',
              ],
            },
          ],
        } ],
      ]
    }, # specialize_node_d
    { # fuzz_url
      'target_name': 'fuzz_url',
      'type': 'executable',
      'dependencies': [
        '<(node_lib_target_name)',
      ],
      'includes': [
        'node.gypi'
      ],
      'include_dirs': [
        'src',
      ],
      'defines': [
        'NODE_ARCH="<(target_arch)"',
        'NODE_PLATFORM="<(OS)"',
        'NODE_WANT_INTERNALS=1',
      ],
      'sources': [
        'src/node_snapshot_stub.cc',
        'test/fuzzers/fuzz_url.cc',
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
        # Avoid extra debug information
        ['v8_enable_handle_zapping==0', {
          'ldflags': [ '-s' ],
        }],
      ],
    }, # fuzz_url
    { # fuzz_env
      'target_name': 'fuzz_env',
      'type': 'executable',
      'dependencies': [
        '<(node_lib_target_name)',
        'deps/histogram/histogram.gyp:histogram',
        'deps/uvwasi/uvwasi.gyp:uvwasi',
        'node_dtrace_header',
        'node_dtrace_ustack',
        'node_dtrace_provider',
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
        # Avoid extra debug information
        ['v8_enable_handle_zapping==0', {
          'ldflags': [ '-s' ],
        }],
      ],
    }, # fuzz_env
    {
      'target_name': 'cctest',
      'type': 'executable',

      'dependencies': [
        '<(node_lib_target_name)',
        'deps/base64/base64.gyp:base64',
        'deps/googletest/googletest.gyp:gtest',
        'deps/googletest/googletest.gyp:gtest_main',
        'deps/histogram/histogram.gyp:histogram',
        'deps/uvwasi/uvwasi.gyp:uvwasi',
        'node_dtrace_header',
        'node_dtrace_ustack',
        'node_dtrace_provider',
        'deps/simdutf/simdutf.gyp:simdutf',
        'deps/ada/ada.gyp:ada',
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
        'test/cctest/node_test_fixture.cc',
        'test/cctest/node_test_fixture.h',
        'test/cctest/test_aliased_buffer.cc',
        'test/cctest/test_base64.cc',
        'test/cctest/test_base_object_ptr.cc',
        'test/cctest/test_node_postmortem_metadata.cc',
        'test/cctest/test_environment.cc',
        'test/cctest/test_linked_binding.cc',
        'test/cctest/test_node_api.cc',
        'test/cctest/test_per_process.cc',
        'test/cctest/test_platform.cc',
        'test/cctest/test_report.cc',
        'test/cctest/test_json_utils.cc',
        'test/cctest/test_sockaddr.cc',
        'test/cctest/test_traced_value.cc',
        'test/cctest/test_util.cc',
      ],

      'conditions': [
        [ 'node_use_openssl=="true"', {
          'defines': [
            'HAVE_OPENSSL=1',
          ],
          'sources': [
            'test/cctest/test_crypto_clienthello.cc',
            'test/cctest/test_node_crypto.cc',
            'test/cctest/test_node_crypto_env.cc',
          ]
        }],
        ['v8_enable_inspector==1', {
          'sources': [
            'test/cctest/test_inspector_socket.cc',
            'test/cctest/test_inspector_socket_server.cc'
          ],
          'defines': [
            'HAVE_INSPECTOR=1',
          ],
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
        # Avoid extra debug information
        ['v8_enable_handle_zapping==0', {
          'ldflags': [ '-s' ],
        }],
      ],
    }, # cctest

    {
      'target_name': 'embedtest',
      'type': 'executable',

      'dependencies': [
        '<(node_lib_target_name)',
        'deps/histogram/histogram.gyp:histogram',
        'deps/uvwasi/uvwasi.gyp:uvwasi',
        'node_dtrace_header',
        'node_dtrace_ustack',
        'node_dtrace_provider',
        'deps/ada/ada.gyp:ada',
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
        'deps/uvwasi/include',
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
        # Avoid extra debug information
        ['v8_enable_handle_zapping==0', {
          'ldflags': [ '-s' ],
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
        # Avoid extra debug information
        ['v8_enable_handle_zapping==0', {
          'ldflags': [ '-s' ],
        }],
      ]
    }, # overlapped-checker
    {
      'target_name': 'node_mksnapshot',
      'type': 'executable',

      'dependencies': [
        '<(node_lib_target_name)',
        'deps/histogram/histogram.gyp:histogram',
        'deps/uvwasi/uvwasi.gyp:uvwasi',
        'deps/ada/ada.gyp:ada',
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
        'deps/uvwasi/include',
      ],

      'defines': [ 'NODE_WANT_INTERNALS=1' ],

      'sources': [
        'src/node_snapshot_stub.cc',
        'tools/snapshot/node_mksnapshot.cc',
      ],

      'conditions': [
        [ 'node_use_openssl=="true"', {
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
        # Avoid extra debug information
        ['v8_enable_handle_zapping==0', {
          'ldflags': [ '-s' ],
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
