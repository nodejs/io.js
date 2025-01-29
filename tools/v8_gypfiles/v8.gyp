# Copyright 2012 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'variables': {
    'V8_ROOT': '../../deps/v8',
    'v8_code': 1,
    'v8_random_seed%': 314159265,
    'v8_vector_stores%': 0,
    'v8_embed_script%': "",
    'mksnapshot_exec': '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)mksnapshot<(EXECUTABLE_SUFFIX)',
    'v8_os_page_size%': 0,
    'generate_bytecode_output_root': '<(SHARED_INTERMEDIATE_DIR)/generate-bytecode-output-root',
    'generate_bytecode_builtins_list_output': '<(generate_bytecode_output_root)/builtins-generated/bytecodes-builtins-list.h',
    'torque_files': ['<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "torque_files = ")'],
    # Torque and V8 expect the files to be named relative to V8's root.
    'torque_files_without_v8_root': ['<!@pymod_do_main(ForEachReplace "<@(V8_ROOT)/" "" <@(torque_files))'],
    'torque_files_replaced': ['<!@pymod_do_main(ForEachReplace ".tq" "-tq" <@(torque_files_without_v8_root))'],
    'torque_outputs_csa_cc': ['<!@pymod_do_main(ForEachFormat "<(SHARED_INTERMEDIATE_DIR)/torque-generated/%s-csa.cc" <@(torque_files_replaced))'],
    'torque_outputs_csa_h': ['<!@pymod_do_main(ForEachFormat "<(SHARED_INTERMEDIATE_DIR)/torque-generated/%s-csa.h" <@(torque_files_replaced))'],
    'torque_outputs_inl_inc': ['<!@pymod_do_main(ForEachFormat "<(SHARED_INTERMEDIATE_DIR)/torque-generated/%s-inl.inc" <@(torque_files_replaced))'],
    'torque_outputs_cc': ['<!@pymod_do_main(ForEachFormat "<(SHARED_INTERMEDIATE_DIR)/torque-generated/%s.cc" <@(torque_files_replaced))'],
    'torque_outputs_inc': ['<!@pymod_do_main(ForEachFormat "<(SHARED_INTERMEDIATE_DIR)/torque-generated/%s.inc" <@(torque_files_replaced))'],
    'v8_compiler_sources': ['<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_compiler_sources = ")'],
    'conditions': [
      ['v8_enable_i18n_support==1', {
        'torque_files': [
          '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "torque_files =.*?v8_enable_i18n_support.*?torque_files \\+= ")',
        ],
      }],
      ['v8_enable_webassembly==1', {
        'torque_files': [
          '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "torque_files =.*?v8_enable_webassembly.*?torque_files \\+= ")',
        ],
        'v8_compiler_sources': [
          '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_compiler_sources =.*?v8_enable_webassembly.*?v8_compiler_sources \\+= ")',
        ],
      }],
    ],
  },
  'includes': ['toolchain.gypi', 'features.gypi'],
  'target_defaults': {
    'msvs_settings': {
      'VCCLCompilerTool': {
        'AdditionalOptions': ['/utf-8']
      }
    },
  },
  'targets': [
    {
      'target_name': 'run_torque',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'conditions': [
        ['want_separate_host_toolset', {
          'dependencies': ['torque#host'],
        }, {
          'dependencies': ['torque#target'],
        }],
      ],
      'hard_dependency': 1,
      'direct_dependent_settings': {
        'include_dirs': [
          '<(SHARED_INTERMEDIATE_DIR)',
        ],
      },
      'actions': [
        {
          'action_name': 'run_torque_action',
          'inputs': [  # Order matters.
            '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)torque<(EXECUTABLE_SUFFIX)',
            '<@(torque_files)',
          ],
          'outputs': [
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/bit-fields.h",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/builtin-definitions.h",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/class-debug-readers.cc",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/class-debug-readers.h",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/class-forward-declarations.h",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/class-verifiers.cc",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/class-verifiers.h",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/csa-types.h",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/debug-macros.cc",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/debug-macros.h",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/enum-verifiers.cc",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/exported-macros-assembler.cc",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/exported-macros-assembler.h",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/factory.cc",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/factory.inc",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/instance-types.h",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/interface-descriptors.inc",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/objects-body-descriptors-inl.inc",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/objects-printer.cc",
            "<(SHARED_INTERMEDIATE_DIR)/torque-generated/visitor-lists.h",
            '<@(torque_outputs_csa_cc)',
            '<@(torque_outputs_csa_h)',
            '<@(torque_outputs_inl_inc)',
            '<@(torque_outputs_cc)',
            '<@(torque_outputs_inc)',
          ],
          'action': [
            '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)torque<(EXECUTABLE_SUFFIX)',
            '-o', '<(SHARED_INTERMEDIATE_DIR)/torque-generated',
            '-v8-root', '<(V8_ROOT)',
            '<@(torque_files_without_v8_root)',
          ],
        },
      ],
    },  # run_torque
    {
      'target_name': 'v8_maybe_icu',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'hard_dependency': 1,
      'conditions': [
        ['v8_enable_i18n_support==1', {
          'dependencies': [
            '<(icu_gyp_path):icui18n',
            '<(icu_gyp_path):icuuc',
          ],
          'export_dependent_settings': [
            '<(icu_gyp_path):icui18n',
            '<(icu_gyp_path):icuuc',
          ],
        }],
      ],
    },  # v8_maybe_icu
    {
      'target_name': 'torque_runtime_support',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'direct_dependent_settings': {
        'sources': [
          '<(V8_ROOT)/src/torque/runtime-support.h',
        ],
      },
    },  # torque_runtime_support
    {
      'target_name': 'torque_generated_initializers',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'hard_dependency': 1,
      'dependencies': [
        'generate_bytecode_builtins_list',
        'run_torque',
        'v8_base_without_compiler',
        'torque_runtime_support',
        'v8_maybe_icu',
      ],
      'direct_dependent_settings': {
        'sources': [
          '<(SHARED_INTERMEDIATE_DIR)/torque-generated/csa-types.h',
          '<(SHARED_INTERMEDIATE_DIR)/torque-generated/enum-verifiers.cc',
          '<(SHARED_INTERMEDIATE_DIR)/torque-generated/exported-macros-assembler.cc',
          '<(SHARED_INTERMEDIATE_DIR)/torque-generated/exported-macros-assembler.h',
          '<@(torque_outputs_csa_cc)',
          '<@(torque_outputs_csa_h)',
        ],
      }
    },  # torque_generated_initializers
    {
      'target_name': 'torque_generated_definitions',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'hard_dependency': 1,
      'dependencies': [
        'generate_bytecode_builtins_list',
        'run_torque',
        'v8_internal_headers',
        'v8_libbase',
        'v8_maybe_icu',
      ],
      'direct_dependent_settings': {
        'sources': [
          '<(SHARED_INTERMEDIATE_DIR)/torque-generated/class-forward-declarations.h',
          '<(SHARED_INTERMEDIATE_DIR)/torque-generated/class-verifiers.cc',
          '<(SHARED_INTERMEDIATE_DIR)/torque-generated/class-verifiers.h',
          '<(SHARED_INTERMEDIATE_DIR)/torque-generated/factory.cc',
          '<(SHARED_INTERMEDIATE_DIR)/torque-generated/objects-printer.cc',
          '<@(torque_outputs_inl_inc)',
          '<@(torque_outputs_cc)',
          '<@(torque_outputs_inc)',
        ],
        'include_dirs': [
          '<(SHARED_INTERMEDIATE_DIR)',
        ],
      },
    },  # torque_generated_definitions
    {
      'target_name': 'generate_bytecode_builtins_list',
      'type': 'none',
      'hard_dependency': 1,
      'toolsets': ['host', 'target'],
      'conditions': [
        ['want_separate_host_toolset', {
          'dependencies': ['bytecode_builtins_list_generator#host'],
        }, {
          'dependencies': ['bytecode_builtins_list_generator#target'],
        }],
      ],
      'direct_dependent_settings': {
        'sources': [
          '<(generate_bytecode_builtins_list_output)',
        ],
        'include_dirs': [
          '<(generate_bytecode_output_root)',
          '<(SHARED_INTERMEDIATE_DIR)',
        ],
      },
      'actions': [
        {
          'action_name': 'generate_bytecode_builtins_list_action',
          'inputs': [
            '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)bytecode_builtins_list_generator<(EXECUTABLE_SUFFIX)',
          ],
          'outputs': [
            '<(generate_bytecode_builtins_list_output)',
          ],
          'action': [
            '<(python)',
            '<(V8_ROOT)/tools/run.py',
            '<@(_inputs)',
            '<@(_outputs)',
          ],
        },
      ],
    },  # generate_bytecode_builtins_list
    {
      'target_name': 'v8_init',
      'type': 'static_library',
      'toolsets': ['host', 'target'],
      'dependencies': [
        'generate_bytecode_builtins_list',
        'run_torque',
        'v8_base_without_compiler',
        'v8_initializers',
        'v8_maybe_icu',
      ],
      'sources': [
        '<(V8_ROOT)/src/init/setup-isolate-full.cc',
      ],
    },  # v8_init
    {
      'target_name': 'v8_initializers',
      'type': 'static_library',
      'toolsets': ['host', 'target'],
      'dependencies': [
        'torque_generated_initializers',
        'v8_base_without_compiler',
        'v8_shared_internal_headers',
      ],
      'include_dirs': [
        '<(SHARED_INTERMEDIATE_DIR)',
        '<(generate_bytecode_output_root)',
      ],
      'sources': [
        '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_initializers.*?sources = ")',
      ],
      'conditions': [
        ['v8_enable_webassembly==1', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_initializers.*?v8_enable_webassembly.*?sources \\+= ")',
          ],
        }],
        ['v8_target_arch=="ia32"', {
          'sources': [
            '<(V8_ROOT)/src/builtins/ia32/builtins-ia32.cc',
          ],
        }],
        ['v8_target_arch=="x64"', {
          'sources': [
            '<(V8_ROOT)/src/builtins/x64/builtins-x64.cc',
          ],
        }],
        ['v8_target_arch=="arm"', {
          'sources': [
            '<(V8_ROOT)/src/builtins/arm/builtins-arm.cc',
          ],
        }],
        ['v8_target_arch=="arm64"', {
          'sources': [
            '<(V8_ROOT)/src/builtins/arm64/builtins-arm64.cc',
          ],
        }],
        ['v8_target_arch=="mips" or v8_target_arch=="mipsel"', {
          'sources': [
            '<(V8_ROOT)/src/builtins/mips/builtins-mips.cc',
          ],
        }],
        ['v8_target_arch=="riscv64" or v8_target_arch=="riscv64"', {
          'sources': [
            '<(V8_ROOT)/src/builtins/riscv64/builtins-riscv64.cc',
          ],
        }],        
        ['v8_target_arch=="loong64" or v8_target_arch=="loong64"', {
          'sources': [
            '<(V8_ROOT)/src/builtins/loong64/builtins-loong64.cc',
          ],
        }],        
        ['v8_target_arch=="mips64" or v8_target_arch=="mips64el"', {
          'sources': [
            '<(V8_ROOT)/src/builtins/mips64/builtins-mips64.cc',
          ],
        }],
        ['v8_target_arch=="ppc"', {
          'sources': [
            '<(V8_ROOT)/src/builtins/ppc/builtins-ppc.cc',
          ],
        }],
        ['v8_target_arch=="ppc64"', {
          'sources': [
            '<(V8_ROOT)/src/builtins/ppc/builtins-ppc.cc',
          ],
        }],
        ['v8_target_arch=="s390x"', {
          'sources': [
            '<(V8_ROOT)/src/builtins/s390/builtins-s390.cc',
          ],
        }],
        ['v8_enable_i18n_support==1', {
          'dependencies': [
            '<(icu_gyp_path):icui18n',
            '<(icu_gyp_path):icuuc',
          ],
        }, {
           'sources!': [
             '<(V8_ROOT)/src/builtins/builtins-intl-gen.cc',
           ],
         }],
        ['OS=="win"', {
          'msvs_precompiled_header': '<(V8_ROOT)/../../tools/msvs/pch/v8_pch.h',
          'msvs_precompiled_source': '<(V8_ROOT)/../../tools/msvs/pch/v8_pch.cc',
          'sources': [
            '<(_msvs_precompiled_header)',
            '<(_msvs_precompiled_source)',
          ],
        }],
      ],
    },  # v8_initializers
    {
      'target_name': 'v8_snapshot',
      'type': 'static_library',
      'toolsets': ['target'],
      'conditions': [
        ['want_separate_host_toolset', {
          'conditions': [
            ['v8_target_arch=="arm64"', {
              'msvs_enable_marmasm': 1,
            }]
          ],
          'dependencies': [
            'generate_bytecode_builtins_list',
            'run_torque',
            'mksnapshot#host',
            'v8_maybe_icu',
            # [GYP] added explicitly, instead of inheriting from the other deps
            'v8_base_without_compiler',
            'v8_compiler_for_mksnapshot',
            'v8_initializers',
            'v8_libplatform',
          ]
        }, {
          'dependencies': [
            'generate_bytecode_builtins_list',
            'run_torque',
            'mksnapshot',
            'v8_maybe_icu',
            # [GYP] added explicitly, instead of inheriting from the other deps
            'v8_base_without_compiler',
            'v8_compiler_for_mksnapshot',
            'v8_initializers',
            'v8_libplatform',
          ]
        }],
      ],
      'sources': [
        '<(V8_ROOT)/src/init/setup-isolate-deserialize.cc',
      ],
      'xcode_settings': {
        # V8 7.4 over macOS10.11 compatibility
        # Refs: https://github.com/nodejs/node/pull/26685
        'GCC_GENERATE_DEBUGGING_SYMBOLS': 'NO',
      },
      'actions': [
        {
          'action_name': 'run_mksnapshot',
          'message': 'generating: >@(_outputs)',
          'variables': {
            'mksnapshot_flags': [
              '--turbo_instruction_scheduling',
              # In cross builds, the snapshot may be generated for both the host and
              # target toolchains.  The same host binary is used to generate both, so
              # mksnapshot needs to know which target OS to use at runtime.  It's weird,
              # but the target OS is really <(OS).
              '--target_os=<(OS)',
              '--target_arch=<(v8_target_arch)',
              '--startup_src', '<(INTERMEDIATE_DIR)/snapshot.cc',
              '--embedded_variant', 'Default',
              '--embedded_src', '<(INTERMEDIATE_DIR)/embedded.S',
            ],
          },
          'inputs': [
            '<(mksnapshot_exec)',
          ],
          'outputs': [
            '<(INTERMEDIATE_DIR)/snapshot.cc',
            '<(INTERMEDIATE_DIR)/embedded.S',
          ],
          'process_outputs_as_sources': 1,
          'conditions': [
            ['v8_random_seed', {
              'variables': {
                'mksnapshot_flags': ['--random-seed', '<(v8_random_seed)'],
              },
            }],
            ['v8_os_page_size', {
              'variables': {
                'mksnapshot_flags': ['--v8_os_page_size', '<(v8_os_page_size)'],
              },
            }],
            ['v8_embed_script != ""', {
              'inputs': ['<(v8_embed_script)'],
              'variables': {
                'mksnapshot_flags': ['<(v8_embed_script)'],
              },
            }],
            ['v8_enable_snapshot_code_comments', {
              'variables': {
                'mksnapshot_flags': ['--code-comments'],
              },
            }],
            ['v8_enable_snapshot_native_code_counters', {
              'variables': {
                'mksnapshot_flags': ['--native-code-counters'],
              },
            }, {
               # --native-code-counters is the default in debug mode so make sure we can
               # unset it.
               'variables': {
                 'mksnapshot_flags': ['--no-native-code-counters'],
               },
             }],
          ],
          'action': [
            '>@(_inputs)',
            '>@(mksnapshot_flags)',
          ],
        },
      ],
    },  # v8_snapshot
    {
      'target_name': 'v8_version',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'direct_dependent_settings': {
        'sources': [
          '<(V8_ROOT)/include/v8-value-serializer-version.h',
          '<(V8_ROOT)/include/v8-version-string.h',
          '<(V8_ROOT)/include/v8-version.h',
        ],
      },
    },  # v8_version
    {
      'target_name': 'v8_config_headers',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'direct_dependent_settings': {
        'sources': [
          '<(V8_ROOT)/include/v8-platform.h',
          '<(V8_ROOT)/include/v8config.h',
        ],
      },
    },  # v8_config_headers
    {
      'target_name': 'v8_headers',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'dependencies': [
        'v8_config_headers',
        'v8_heap_base_headers',
        'v8_version',
      ],
      'direct_dependent_settings': {
        'sources': [
          '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_headers\\".*?sources = ")',

          '<(V8_ROOT)/include/v8-wasm-trap-handler-posix.h',
          '<(V8_ROOT)/include/v8-wasm-trap-handler-win.h',
        ],
      },
    },  # v8_headers
    {
      'target_name': 'v8_shared_internal_headers',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'dependencies': [
        'v8_headers',
        'v8_libbase',
      ],
      'direct_dependent_settings': {
        'sources': [
          '<(V8_ROOT)/src/common/globals.h',
          '<(V8_ROOT)/src/wasm/wasm-constants.h',
          '<(V8_ROOT)/src/wasm/wasm-limits.h',
        ],
      },
    },  # v8_shared_internal_headers
    {
      'target_name': 'v8_flags',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'dependencies': [
        'v8_shared_internal_headers',
      ],
      'direct_dependent_settings': {
        'sources': [
          '<(V8_ROOT)/src/flags/flag-definitions.h',
          '<(V8_ROOT)/src/flags/flags.h',
        ],
      },
    },  # v8_flags
    {
      'target_name': 'v8_internal_headers',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'dependencies': [
        'torque_runtime_support',
        'v8_flags',
        'v8_headers',
        'v8_maybe_icu',
        'v8_shared_internal_headers',
        'v8_heap_base_headers',
        'generate_bytecode_builtins_list',
        'run_torque',
        'v8_libbase',
      ],
      'direct_dependent_settings': {
        'sources': [
          '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?sources = ")',
        ],
        'conditions': [
          ['v8_enable_maglev==1', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_enable_maglev.*?sources \\+= ")',
            ],
          }],
          ['v8_enable_webassembly==1', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_enable_webassembly.*?sources \\+= ")',
            ],
          }],
          ['v8_enable_i18n_support==1', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_enable_i18n_support.*?sources \\+= ")',
            ],
          }],
          ['v8_control_flow_integrity==0', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?!v8_control_flow_integrity.*?sources \\+= ")',
            ],
          }],
          ['v8_enable_heap_snapshot_verify==1', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_enable_heap_snapshot_verify.*?sources \\+= ")',
            ],
          }],
          ['v8_target_arch=="ia32"', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"x86\\".*?sources \\+= ")',
            ],
          }],
          ['v8_target_arch=="x64"', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"x64\\".*?sources \\+= ")',
            ],
            'conditions': [
              ['OS=="win"', {
                'sources': [
                  '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"x64\\".*?is_win.*?sources \\+= ")',
                ],
              }],
              ['v8_enable_webassembly==1', {
                'conditions': [
                  ['OS=="linux" or OS=="mac" or OS=="ios" or OS=="freebsd"', {
                    'sources': [
                      '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"x64\\".*?v8_enable_webassembly.*?is_linux.*?sources \\+= ")',
                    ],
                  }],
                  ['OS=="win"', {
                    'sources': [
                      '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"x64\\".*?v8_enable_webassembly.*?is_win.*?sources \\+= ")',
                    ],
                  }],
                ],
              }],
            ],
          }],
          ['v8_target_arch=="arm"', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"arm\\".*?sources \\+= ")',
            ],
          }],
          ['v8_target_arch=="arm64"', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"arm64\\".*?sources \\+= ")',
            ],
            'conditions': [
              ['v8_control_flow_integrity==1', {
                'sources': [
                  '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"arm64\\".*?v8_control_flow_integrity.*?sources \\+= ")',
                ],
              }],
              ['v8_enable_webassembly==1', {
                'conditions': [
                  ['OS=="mac" or (_toolset=="host" and host_arch=="x64" and OS=="linux")', {
                    'sources': [
                      '<(V8_ROOT)/src/trap-handler/handler-inside-posix.h',
                    ],
                  }],
                  # TODO(targos): Replace False with OS=="win" if handler-outside-simulator.cc becomes compatible with MSVC.
                  ['_toolset=="host" and host_arch=="x64" and (OS=="linux" or OS=="mac" or False)', {
                    'sources': [
                      '<(V8_ROOT)/src/trap-handler/trap-handler-simulator.h',
                    ],
                  }],
                ],
              }],
              ['OS=="win"', {
                'sources': [
                  '<(V8_ROOT)/src/diagnostics/unwinding-info-win64.h',
                ],
              }],
            ],
          }],
          ['v8_target_arch=="mips" or v8_target_arch=="mipsel"', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"mips\\".*?sources \\+= ")',
            ],
          }],
          ['v8_target_arch=="mips64" or v8_target_arch=="mips64el"', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"mips64\\".*?sources \\+= ")',
            ],
          }],
          ['v8_target_arch=="ppc"', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"ppc\\".*?sources \\+= ")',
            ],
          }],
          ['v8_target_arch=="ppc64"', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"ppc64\\".*?sources \\+= ")',
            ],
          }],
          ['v8_target_arch=="s390x"', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"s390\\".*?sources \\+= ")',
            ],
          }],
          ['v8_target_arch=="riscv64"', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"riscv64\\".*?sources \\+= ")',
            ],
          }],
          ['v8_target_arch=="loong64"', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_header_set.\\"v8_internal_headers\\".*?v8_current_cpu == \\"loong64\\".*?sources \\+= ")',
            ],
          }],
        ],
      },
    },  # v8_internal_headers
    {
      'target_name': 'v8_compiler_opt',
      'type': 'static_library',
      'toolsets': ['host', 'target'],
      'dependencies': [
        'generate_bytecode_builtins_list',
        'run_torque',
        'v8_maybe_icu',
        'v8_base_without_compiler',
        'v8_internal_headers',
        'v8_libbase',
        'v8_shared_internal_headers',
      ],
      'sources': ['<@(v8_compiler_sources)'],
      'conditions': [
        ['OS=="win"', {
          'msvs_precompiled_header': '<(V8_ROOT)/../../tools/msvs/pch/v8_pch.h',
          'msvs_precompiled_source': '<(V8_ROOT)/../../tools/msvs/pch/v8_pch.cc',
          'sources': [
            '<(_msvs_precompiled_header)',
            '<(_msvs_precompiled_source)',
          ],
        }],
      ],
    },  # v8_compiler_opt
    {
      'target_name': 'v8_compiler',
      'type': 'static_library',
      'toolsets': ['host', 'target'],
      'dependencies': [
        'generate_bytecode_builtins_list',
        'run_torque',
        'v8_internal_headers',
        'v8_maybe_icu',
        'v8_base_without_compiler',
        'v8_libbase',
        'v8_shared_internal_headers',
      ],
      'sources': ['<@(v8_compiler_sources)'],
      'conditions': [
        ['OS=="win"', {
          'msvs_precompiled_header': '<(V8_ROOT)/../../tools/msvs/pch/v8_pch.h',
          'msvs_precompiled_source': '<(V8_ROOT)/../../tools/msvs/pch/v8_pch.cc',
          'sources': [
            '<(_msvs_precompiled_header)',
            '<(_msvs_precompiled_source)',
          ],
        }],
      ],
    },  # v8_compiler
    {
      'target_name': 'v8_compiler_for_mksnapshot',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'hard_dependency': 1,
      'dependencies': [
        'generate_bytecode_builtins_list',
        'run_torque',
        'v8_maybe_icu',
      ],
      'conditions': [
        ['is_component_build and not v8_optimized_debug and v8_enable_fast_mksnapshot', {
          'dependencies': [
            'v8_compiler_opt',
          ],
          'export_dependent_settings': [
            'v8_compiler_opt',
          ],
        }, {
           'dependencies': [
             'v8_compiler',
           ],
           'export_dependent_settings': [
             'v8_compiler',
           ],
         }],
      ],
    },  # v8_compiler_for_mksnapshot
    {
      'target_name': 'v8_base_without_compiler',
      'type': 'static_library',
      'toolsets': ['host', 'target'],
      'dependencies': [
        'torque_generated_definitions',
        'v8_bigint',
        'v8_headers',
        'v8_heap_base',
        'v8_libbase',
        'v8_shared_internal_headers',
        'v8_version',
        'cppgc_base',
        'generate_bytecode_builtins_list',
        'run_torque',
        'v8_internal_headers',
        'v8_maybe_icu',
        'v8_zlib',
      ],
      'includes': ['inspector.gypi'],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(generate_bytecode_output_root)',
          '<(SHARED_INTERMEDIATE_DIR)',
        ],
      },
      'sources': [
        '<(generate_bytecode_builtins_list_output)',

        '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?sources = ")',

        '<@(inspector_all_sources)',
      ],
      'conditions': [
        ['v8_enable_third_party_heap==1', {
          # TODO(targos): add values from v8_third_party_heap_files to sources
        }, {
          'sources': [
            '<(V8_ROOT)/src/heap/third-party/heap-api-stub.cc',
          ],
        }],
        ['v8_enable_maglev==1', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_enable_maglev.*?sources \\+= ")',
          ],
        }],
        ['v8_enable_webassembly==1', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_enable_webassembly.*?sources \\+= ")',
          ],
        }],
        ['v8_enable_heap_snapshot_verify==1', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_enable_heap_snapshot_verify.*?sources \\+= ")',
          ],
        }],
        ['v8_target_arch=="ia32"', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"x86.*?sources \\+= ")',
          ],
        }],
        ['v8_target_arch=="x64"', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"x64\\".*?sources \\+= ")',
          ],
          'conditions': [
            ['OS=="win"', {
              'sources': [
                '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"x64\\".*?is_win.*?sources \\+= ")',
              ],
            }],
            ['v8_enable_webassembly==1', {
              'conditions': [
                ['OS=="linux" or OS=="mac" or OS=="ios" or OS=="freebsd"', {
                  'sources': [
                    '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"x64\\".*?v8_enable_webassembly.*?is_linux.*?sources \\+= ")',
                  ],
                }],
                ['OS=="win"', {
                  'sources': [
                    '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"x64\\".*?v8_enable_webassembly.*?is_win.*?sources \\+= ")',
                  ],
                }],
              ],
            }],
          ],
        }],
        ['v8_target_arch=="arm"', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"arm\\".*?sources \\+= ")',
          ],
        }],
        ['v8_target_arch=="arm64"', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"arm64\\".*?sources \\+= ")',
          ],
          'conditions': [
            ['v8_enable_webassembly==1', {
              'conditions': [
                ['OS=="mac" or OS=="ios" or '
                 '(_toolset=="host" and host_arch=="x64" and (host_os=="linux" or host_os=="mac"))', {
                  'sources': [
                    '<(V8_ROOT)/src/trap-handler/handler-inside-posix.cc',
                    '<(V8_ROOT)/src/trap-handler/handler-outside-posix.cc',
                  ],
                }],
                # TODO(targos): Replace False with OS=="win" if handler-outside-simulator.cc becomes compatible with MSVC.
                ['_toolset=="host" and host_arch=="x64" and False', {
                  'sources': [
                    '<(V8_ROOT)/src/trap-handler/handler-inside-win.cc',
                    '<(V8_ROOT)/src/trap-handler/handler-outside-win.cc',
                  ],
                }],
                # TODO(targos): Replace False with OS=="win" if handler-outside-simulator.cc becomes compatible with MSVC.
                ['_toolset=="host" and host_arch=="x64" and '
                 '(host_os=="linux" or host_os=="mac" or False)', {
                  'sources': [
                    '<(V8_ROOT)/src/trap-handler/handler-outside-simulator.cc',
                  ],
                }],
              ],
            }],
            ['OS=="win"', {
              'sources': [
                '<(V8_ROOT)/src/diagnostics/unwinding-info-win64.cc',
              ],
            }],
          ],
        }],
        ['v8_target_arch=="mips" or v8_target_arch=="mipsel"', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"mips\\".*?sources \\+= ")',
          ],
        }],
        ['v8_target_arch=="mips64" or v8_target_arch=="mips64el"', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"mips64\\".*?sources \\+= ")',
          ],
        }],
        ['v8_target_arch=="ppc"', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"ppc\\".*?sources \\+= ")',
          ],
        }],
        ['v8_target_arch=="ppc64"', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"ppc64\\".*?sources \\+= ")',
          ],
        }],
        ['v8_target_arch=="s390x"', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"s390\\".*?sources \\+= ")',
          ],
        }],
        ['v8_target_arch=="riscv64"', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"riscv64\\".*?sources \\+= ")',
          ],
        }],        
        ['v8_target_arch=="loong64"', {
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_current_cpu == \\"loong64\\".*?sources \\+= ")',
          ],
        }],        
        ['OS=="win"', {
          'msvs_precompiled_header': '<(V8_ROOT)/../../tools/msvs/pch/v8_pch.h',
          'msvs_precompiled_source': '<(V8_ROOT)/../../tools/msvs/pch/v8_pch.cc',
          'sources': [
            '<(_msvs_precompiled_header)',
            '<(_msvs_precompiled_source)',
          ],
          # This will prevent V8's .cc files conflicting with the inspector's
          # .cpp files in the same shard.
          'msvs_settings': {
            'VCCLCompilerTool': {
              'ObjectFile': '$(IntDir)%(Extension)\\',
            },
          },
          'conditions': [
            ['v8_enable_system_instrumentation==1', {
              'sources': [
                '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?is_win.*?v8_enable_system_instrumentation.*?sources \\+= ")',
              ],
            }],
          ],
        }],
        ['component=="shared_library"', {
          'defines': [
            'BUILDING_V8_SHARED',
          ],
        }],
        ['v8_enable_i18n_support==1', {
          'dependencies': [
            'run_gen-regexp-special-case',
          ],
          'sources': [
            '<(SHARED_INTERMEDIATE_DIR)/src/regexp/special-case.cc',
          ],
          'conditions': [
            ['icu_use_data_file_flag', {
              'defines': ['ICU_UTIL_DATA_IMPL=ICU_UTIL_DATA_FILE'],
            }, {
               'conditions': [
                 ['OS=="win"', {
                   'defines': ['ICU_UTIL_DATA_IMPL=ICU_UTIL_DATA_SHARED'],
                 }, {
                    'defines': ['ICU_UTIL_DATA_IMPL=ICU_UTIL_DATA_STATIC'],
                  }],
               ],
             }],
            ['OS=="win"', {
              'dependencies': [
                '<(icu_gyp_path):icudata#target',
              ],
            }],
          ],
        }, {  # v8_enable_i18n_support==0
           'sources!': [
             '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_base_without_compiler.*?v8_enable_i18n_support.*?sources -= ")',
           ],
         }],
        ['v8_postmortem_support', {
          'dependencies': ['postmortem-metadata#target'],
        }],
        ['v8_enable_third_party_heap', {
          # TODO(targos): add values from v8_third_party_heap_libs to link_settings.libraries
        }],
        # Platforms that don't have Compare-And-Swap (CAS) support need to link atomic library
        # to implement atomic memory access
        ['v8_current_cpu in ["mips", "mipsel", "mips64", "mips64el", "ppc", "arm", "riscv64", "loong64"]', {
          'link_settings': {
            'libraries': ['-latomic', ],
          },
        }],
      ],
    },  # v8_base_without_compiler
    {
      'target_name': 'v8_base',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'dependencies': [
        'v8_base_without_compiler',
        'v8_compiler',
      ],
    },  # v8_base
    {
      'target_name': 'torque_base',
      'type': 'static_library',
      'toolsets': ['host', 'target'],
      'sources': [
        '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"torque_base.*?sources = ")',
      ],
      'dependencies': [
        'v8_shared_internal_headers',
        'v8_libbase',
      ],
      'defines!': [
        '_HAS_EXCEPTIONS=0',
        'BUILDING_V8_SHARED=1',
      ],
      'cflags_cc!': ['-fno-exceptions'],
      'cflags_cc': ['-fexceptions'],
      'xcode_settings': {
        'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',  # -fexceptions
      },
      'msvs_settings': {
        'VCCLCompilerTool': {
          'RuntimeTypeInfo': 'true',
          'ExceptionHandling': 1,
        },
      },
    },  # torque_base
    {
      'target_name': 'torque_ls_base',
      'type': 'static_library',
      'toolsets': ['host', 'target'],
      'sources': [
        '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"torque_ls_base.*?sources = ")',
      ],
      'dependencies': [
        'torque_base',
      ],
      'defines!': [
        '_HAS_EXCEPTIONS=0',
        'BUILDING_V8_SHARED=1',
      ],
      'cflags_cc!': ['-fno-exceptions'],
      'cflags_cc': ['-fexceptions'],
      'xcode_settings': {
        'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',  # -fexceptions
      },
      'msvs_settings': {
        'VCCLCompilerTool': {
          'RuntimeTypeInfo': 'true',
          'ExceptionHandling': 1,
        },
      },
    },  # torque_ls_base
    {
      'target_name': 'v8_libbase',
      'type': 'static_library',
      'toolsets': ['host', 'target'],
      'sources': [
        '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"v8_libbase.*?sources = ")',
      ],

      'dependencies': [
        'v8_headers',
      ],

      'conditions': [
        ['is_component_build', {
          'defines': ["BUILDING_V8_BASE_SHARED"],
        }],
        ['is_posix or is_fuchsia', {
          'sources': [
            '<(V8_ROOT)/src/base/platform/platform-posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-posix.h',
          ],
          'conditions': [
            ['OS != "aix" and OS != "os400" and OS != "solaris"', {
              'sources': [
                '<(V8_ROOT)/src/base/platform/platform-posix-time.cc',
                '<(V8_ROOT)/src/base/platform/platform-posix-time.h',
              ],
            }],
          ],
        }],
        ['OS=="linux"', {
          'sources': [
            '<(V8_ROOT)/src/base/debug/stack_trace_posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-linux.cc',
          ],
          'link_settings': {
            'libraries': [
              '-ldl',
              '-lrt'
            ],
          },
        }],
        ['OS in "aix os400"', {
          'variables': {
            # Used to differentiate `AIX` and `OS400`(IBM i).
            'aix_variant_name': '<!(uname -s)',
          },
          'sources': [
            '<(V8_ROOT)/src/base/debug/stack_trace_posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-aix.cc',
          ],
          'conditions': [
            [ '"<(aix_variant_name)"=="AIX"', { # It is `AIX`
              'link_settings': {
                'libraries': [
                  '-ldl',
                  '-lrt'
                ],
              },
            }],
          ],
        }],
        ['is_android', {
          'sources': [
            '<(V8_ROOT)/src/base/platform/platform-posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-posix.h',
            '<(V8_ROOT)/src/base/platform/platform-posix-time.cc',
            '<(V8_ROOT)/src/base/platform/platform-posix-time.h',
          ],
          'link_settings': {
            'target_conditions': [
              ['_toolset=="host" and host_os=="linux"', {
                'libraries': [
                  '-ldl',
                  '-lrt'
                ],
              }],
            ],
          },
          'target_conditions': [
            ['_toolset=="host"', {
              'sources': [
                '<(V8_ROOT)/src/base/debug/stack_trace_posix.cc',
                '<(V8_ROOT)/src/base/platform/platform-linux.cc',
              ],
            }, {
              'sources': [
                '<(V8_ROOT)/src/base/debug/stack_trace_android.cc',
                '<(V8_ROOT)/src/base/platform/platform-linux.cc',
              ],
            }],
          ],
        }],
        ['is_fuchsia', {
          'sources': [
            '<(V8_ROOT)/src/base/debug/stack_trace_fuchsia.cc',
            '<(V8_ROOT)/src/base/platform/platform-fuchsia.cc',
          ]
        }],
        ['OS == "mac" or (_toolset=="host" and host_os=="mac")', {
          'sources': [
            '<(V8_ROOT)/src/base/debug/stack_trace_posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-darwin.cc',
            '<(V8_ROOT)/src/base/platform/platform-macos.cc',
          ]
        }],
        ['OS == "ios"', {
          'sources': [
            '<(V8_ROOT)/src/base/debug/stack_trace_posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-darwin.cc',
          ]
        }],
        ['is_win', {
          'sources': [
            '<(V8_ROOT)/src/base/debug/stack_trace_win.cc',
            '<(V8_ROOT)/src/base/platform/platform-win32.cc',
            '<(V8_ROOT)/src/base/win32-headers.h',
          ],
          'defines': ['_CRT_RAND_S'], # for rand_s()
          'direct_dependent_settings': {
            'msvs_settings': {
              'VCLinkerTool': {
                'AdditionalDependencies': [
                  'dbghelp.lib',
                  'winmm.lib',
                  'ws2_32.lib'
                ]
              }
            },
            'conditions': [
              ['v8_enable_system_instrumentation==1', {
                'msvs_settings': {
                  'VCLinkerTool': {
                    'AdditionalDependencies': [
                      'advapi32.lib',
                    ],
                  },
                },
              }],
            ],
          },
        }],
        ['target_arch == "mips" or OS == "mips64"', {
          # here just for 'BUILD.gn' sync
          # 'data': [
          #   '<(V8_ROOT)/tools/mips_toolchain/sysroot/usr/lib/',
          #   '<(V8_ROOT)/tools/mips_toolchain/sysroot/usr/lib/',
          # ],
        }],
        # end of conditions from 'BUILD.gn'

        # Node.js validated
        ['OS=="solaris"', {
          'link_settings': {
            'libraries': [
              '-lnsl',
              '-lrt',
            ]
          },
          'sources': [
            '<(V8_ROOT)/src/base/debug/stack_trace_posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-solaris.cc',
          ],
        }],

        # YMMV with the following conditions
        ['OS=="qnx"', {
          'link_settings': {
            'target_conditions': [
              ['_toolset=="host" and host_os=="linux"', {
                'libraries': [
                  '-lrt'
                ],
              }],
              ['_toolset=="target"', {
                'libraries': [
                  '-lbacktrace'
                ],
              }],
            ],
          },
          'sources': [
            '<(V8_ROOT)/src/base/debug/stack_trace_posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-posix.h',
            '<(V8_ROOT)/src/base/platform/platform-posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-posix-time.h',
            '<(V8_ROOT)/src/base/platform/platform-posix-time.cc',
            '<(V8_ROOT)/src/base/qnx-math.h'
          ],
          'target_conditions': [
            ['_toolset=="host" and host_os=="linux"', {
              'sources': [
                '<(V8_ROOT)/src/base/platform/platform-linux.cc'
              ],
            }],
            ['_toolset=="host" and host_os=="mac"', {
              'sources': [
                '<(V8_ROOT)/src/base/platform/platform-macos.cc'
              ],
            }],
            ['_toolset=="target"', {
              'sources': [
                '<(V8_ROOT)/src/base/platform/platform-qnx.cc'
              ],
            }],
          ],
        },
         ],
        ['OS=="freebsd"', {
          'link_settings': {
            'libraries': [
              '-L/usr/local/lib -lexecinfo',
            ]
          },
          'sources': [
            '<(V8_ROOT)/src/base/debug/stack_trace_posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-freebsd.cc',
            '<(V8_ROOT)/src/base/platform/platform-posix.h',
            '<(V8_ROOT)/src/base/platform/platform-posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-posix-time.h',
            '<(V8_ROOT)/src/base/platform/platform-posix-time.cc',
          ],
        }
         ],
        ['OS=="openbsd"', {
          'link_settings': {
            'libraries': [
              '-L/usr/local/lib -lexecinfo',
            ]
          },
          'sources': [
            '<(V8_ROOT)/src/base/debug/stack_trace_posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-openbsd.cc',
            '<(V8_ROOT)/src/base/platform/platform-posix.h',
            '<(V8_ROOT)/src/base/platform/platform-posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-posix-time.h',
            '<(V8_ROOT)/src/base/platform/platform-posix-time.cc',
          ],
        }
         ],
        ['OS=="netbsd"', {
          'link_settings': {
            'libraries': [
              '-L/usr/pkg/lib -Wl,-R/usr/pkg/lib -lexecinfo',
            ]
          },
          'sources': [
            '<(V8_ROOT)/src/base/debug/stack_trace_posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-openbsd.cc',
            '<(V8_ROOT)/src/base/platform/platform-posix.h',
            '<(V8_ROOT)/src/base/platform/platform-posix.cc',
            '<(V8_ROOT)/src/base/platform/platform-posix-time.h',
            '<(V8_ROOT)/src/base/platform/platform-posix-time.cc',
          ],
        }
         ],
      ],
    },  # v8_libbase
    {
      'target_name': 'v8_libplatform',
      'type': 'static_library',
      'toolsets': ['host', 'target'],
      'dependencies': [
        'v8_libbase',
      ],
      'sources': [
        '<(V8_ROOT)/base/trace_event/common/trace_event_common.h',
        '<(V8_ROOT)/include/libplatform/libplatform-export.h',
        '<(V8_ROOT)/include/libplatform/libplatform.h',
        '<(V8_ROOT)/include/libplatform/v8-tracing.h',
        '<(V8_ROOT)/src/libplatform/default-foreground-task-runner.cc',
        '<(V8_ROOT)/src/libplatform/default-foreground-task-runner.h',
        '<(V8_ROOT)/src/libplatform/default-job.cc',
        '<(V8_ROOT)/src/libplatform/default-job.h',
        '<(V8_ROOT)/src/libplatform/default-platform.cc',
        '<(V8_ROOT)/src/libplatform/default-platform.h',
        '<(V8_ROOT)/src/libplatform/default-worker-threads-task-runner.cc',
        '<(V8_ROOT)/src/libplatform/default-worker-threads-task-runner.h',
        '<(V8_ROOT)/src/libplatform/delayed-task-queue.cc',
        '<(V8_ROOT)/src/libplatform/delayed-task-queue.h',
        '<(V8_ROOT)/src/libplatform/task-queue.cc',
        '<(V8_ROOT)/src/libplatform/task-queue.h',
        '<(V8_ROOT)/src/libplatform/tracing/trace-buffer.cc',
        '<(V8_ROOT)/src/libplatform/tracing/trace-buffer.h',
        '<(V8_ROOT)/src/libplatform/tracing/trace-config.cc',
        '<(V8_ROOT)/src/libplatform/tracing/trace-object.cc',
        '<(V8_ROOT)/src/libplatform/tracing/trace-writer.cc',
        '<(V8_ROOT)/src/libplatform/tracing/trace-writer.h',
        '<(V8_ROOT)/src/libplatform/tracing/tracing-controller.cc',
        '<(V8_ROOT)/src/libplatform/worker-thread.cc',
        '<(V8_ROOT)/src/libplatform/worker-thread.h',
      ],
      'conditions': [
        ['component=="shared_library"', {
          'direct_dependent_settings': {
            'defines': ['USING_V8_PLATFORM_SHARED'],
          },
          'defines': ['BUILDING_V8_PLATFORM_SHARED'],
        }],
        ['v8_use_perfetto==1', {
          'sources!': [
            '<(V8_ROOT)/base/trace_event/common/trace_event_common.h',
            '<(V8_ROOT)/src/libplatform/tracing/trace-buffer.cc',
            '<(V8_ROOT)/src/libplatform/tracing/trace-buffer.h',
            '<(V8_ROOT)/src/libplatform/tracing/trace-object.cc',
            '<(V8_ROOT)/src/libplatform/tracing/trace-writer.cc',
            '<(V8_ROOT)/src/libplatform/tracing/trace-writer.h',
          ],
          'sources': [
            '<(V8_ROOT)/src/libplatform/tracing/trace-event-listener.cc',
            '<(V8_ROOT)/src/libplatform/tracing/trace-event-listener.h',
          ],
          'dependencies': [
            '<(V8_ROOT)/third_party/perfetto:libperfetto',
            '<(V8_ROOT)/third_party/perfetto/protos/perfetto/trace:lite',
          ],
        }],
        ['v8_enable_system_instrumentation==1 and is_win', {
          'sources': [
            '<(V8_ROOT)/src/libplatform/tracing/recorder.h',
            '<(V8_ROOT)/src/libplatform/tracing/recorder-win.cc',
          ],
        }],
        ['v8_enable_system_instrumentation==1 and OS=="mac"', {
          'sources': [
            '<(V8_ROOT)/src/libplatform/tracing/recorder.h',
            '<(V8_ROOT)/src/libplatform/tracing/recorder-mac.cc',
          ],
        }],
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(V8_ROOT)/include',
        ],
      },
    },  # v8_libplatform
    {
      'target_name': 'v8_libsampler',
      'type': 'static_library',
      'toolsets': ['host', 'target'],
      'dependencies': [
        'v8_libbase',
      ],
      'sources': [
        '<(V8_ROOT)/src/libsampler/sampler.cc',
        '<(V8_ROOT)/src/libsampler/sampler.h'
      ],
    },  # v8_libsampler
    {
      'target_name': 'bytecode_builtins_list_generator',
      'type': 'executable',
      'conditions': [
        ['want_separate_host_toolset', {
          'toolsets': ['host'],
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
      'defines!': [
        'BUILDING_V8_SHARED=1',
      ],
      'dependencies': [
        "v8_libbase",
        # "build/win:default_exe_manifest",
      ],
      'sources': [
        "<(V8_ROOT)/src/builtins/generate-bytecodes-builtins-list.cc",
        "<(V8_ROOT)/src/interpreter/bytecode-operands.cc",
        "<(V8_ROOT)/src/interpreter/bytecode-operands.h",
        "<(V8_ROOT)/src/interpreter/bytecode-traits.h",
        "<(V8_ROOT)/src/interpreter/bytecodes.cc",
        "<(V8_ROOT)/src/interpreter/bytecodes.h",
      ],
    },  # bytecode_builtins_list_generator
    {
      'target_name': 'mksnapshot',
      'type': 'executable',
      'dependencies': [
        'v8_base_without_compiler',
        'v8_compiler_for_mksnapshot',
        'v8_init',
        'v8_libbase',
        'v8_libplatform',
        'v8_maybe_icu',
        # "build/win:default_exe_manifest",
      ],
      'sources': [
        '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "\\"mksnapshot.*?sources = ")',
      ],
      'conditions': [
        ['want_separate_host_toolset', {
          'toolsets': ['host'],
        }],
        ['OS=="win"', {
          'msvs_precompiled_header': '<(V8_ROOT)/../../tools/msvs/pch/v8_pch.h',
          'msvs_precompiled_source': '<(V8_ROOT)/../../tools/msvs/pch/v8_pch.cc',
          'sources': [
            '<(_msvs_precompiled_header)',
            '<(_msvs_precompiled_source)',
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
    },  # mksnapshot
    {
      'target_name': 'torque',
      'type': 'executable',
      'dependencies': [
        'torque_base',
        # "build/win:default_exe_manifest",
      ],
      'conditions': [
        ['want_separate_host_toolset', {
          'toolsets': ['host'],
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
      'defines!': [
        '_HAS_EXCEPTIONS=0',
        'BUILDING_V8_SHARED=1',
      ],
      'cflags_cc!': ['-fno-exceptions'],
      'cflags_cc': ['-fexceptions'],
      'xcode_settings': {
        'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',  # -fexceptions
      },
      'msvs_settings': {
        'VCCLCompilerTool': {
          'RuntimeTypeInfo': 'true',
          'ExceptionHandling': 1,
        },
        'VCLinkerTool': {
          'AdditionalDependencies': [
            'dbghelp.lib',
            'winmm.lib',
            'ws2_32.lib'
          ]
        }
      },
      'sources': [
        "<(V8_ROOT)/src/torque/torque.cc",
      ],
    },  # torque
    {
      'target_name': 'torque-language-server',
      'type': 'executable',
      'conditions': [
        ['want_separate_host_toolset', {
          'toolsets': ['host'],
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
      'dependencies': [
        'torque_base',
        'torque_ls_base',
        # "build/win:default_exe_manifest",
      ],
      'defines!': [
        '_HAS_EXCEPTIONS=0',
        'BUILDING_V8_SHARED=1',
      ],
      'msvs_settings': {
        'VCCLCompilerTool': {
          'RuntimeTypeInfo': 'true',
          'ExceptionHandling': 1,
        },
      },
      'sources': [
        "<(V8_ROOT)/src/torque/ls/torque-language-server.cc",
      ],
    },  # torque-language-server
    {
      'target_name': 'gen-regexp-special-case',
      'type': 'executable',
      'dependencies': [
        'v8_libbase',
        # "build/win:default_exe_manifest",
        'v8_maybe_icu',
      ],
      'conditions': [
        ['want_separate_host_toolset', {
          'toolsets': ['host'],
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
      'sources': [
        "<(V8_ROOT)/src/regexp/gen-regexp-special-case.cc",
        "<(V8_ROOT)/src/regexp/special-case.h",
      ],
    },  # gen-regexp-special-case
    {
      'target_name': 'run_gen-regexp-special-case',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'conditions': [
        ['want_separate_host_toolset', {
          'dependencies': ['gen-regexp-special-case#host'],
        }, {
          'dependencies': ['gen-regexp-special-case#target'],
        }],
      ],
      'actions': [
        {
          'action_name': 'run_gen-regexp-special-case_action',
          'inputs': [
            '<(PRODUCT_DIR)/<(EXECUTABLE_PREFIX)gen-regexp-special-case<(EXECUTABLE_SUFFIX)',
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/src/regexp/special-case.cc',
          ],
          'action': [
            '<(python)',
            '<(V8_ROOT)/tools/run.py',
            '<@(_inputs)',
            '<@(_outputs)',
          ],
        },
      ],
    },  # run_gen-regexp-special-case
    {
      'target_name': 'v8_heap_base_headers',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'direct_dependent_settings': {
        'sources': [
          '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_source_set.\\"v8_heap_base_headers.*?sources = ")',
        ],
      },
    },  # v8_heap_base_headers
    {
      'target_name': 'cppgc_base',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'direct_dependent_settings': {
        'sources': [
          '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_source_set.\\"cppgc_base.*?sources = ")',
        ],
      },
    },  # cppgc_base
    {
      'target_name': 'v8_bigint',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'direct_dependent_settings': {
        'sources': [
          '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_source_set.\\"v8_bigint.*?sources = ")',
        ],
        'conditions': [
          ['v8_advanced_bigint_algorithms==1', {
            'sources': [
              '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_source_set.\\"v8_bigint.*?v8_advanced_bigint_algorithms.*?sources \\+= ")',
            ],
          }],
        ],
      },
    },  # v8_bigint
    {
      'target_name': 'v8_heap_base',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'direct_dependent_settings': {
        'sources': [
          '<(V8_ROOT)/src/heap/base/active-system-pages.cc',
          '<(V8_ROOT)/src/heap/base/stack.cc',
          '<(V8_ROOT)/src/heap/base/worklist.cc',
        ],
        'conditions': [
          ['enable_lto=="true"', {
            'cflags_cc': [ '-fno-lto' ],
          }],
          ['clang or OS!="win"', {
            'conditions': [
              ['_toolset == "host" and host_arch == "x64" or _toolset == "target" and target_arch=="x64"', {
                'sources': [
                  '<(V8_ROOT)/src/heap/base/asm/x64/push_registers_asm.cc',
                ],
              }],
              ['_toolset == "host" and host_arch == "ia32" or _toolset == "target" and target_arch=="ia32"', {
                'sources': [
                  '<(V8_ROOT)/src/heap/base/asm/ia32/push_registers_asm.cc',
                ],
              }],
              ['_toolset == "host" and host_arch == "arm" or _toolset == "target" and target_arch=="arm"', {
                'sources': [
                  '<(V8_ROOT)/src/heap/base/asm/arm/push_registers_asm.cc',
                ],
              }],
              ['_toolset == "host" and host_arch == "arm64" or _toolset == "target" and target_arch=="arm64"', {
                'sources': [
                  '<(V8_ROOT)/src/heap/base/asm/arm64/push_registers_asm.cc',
                ],
              }],
              ['_toolset == "host" and host_arch == "ppc64" or _toolset == "target" and target_arch=="ppc64"', {
                'sources': [
                  '<(V8_ROOT)/src/heap/base/asm/ppc/push_registers_asm.cc',
                ],
              }],
              ['_toolset == "host" and host_arch == "s390x" or _toolset == "target" and target_arch=="s390x"', {
                'sources': [
                  '<(V8_ROOT)/src/heap/base/asm/s390/push_registers_asm.cc',
                ],
              }],
              ['_toolset == "host" and host_arch == "mips" or _toolset == "target" and target_arch=="mips" or _toolset == "host" and host_arch == "mipsel" or _toolset == "target" and target_arch=="mipsel"', {
                'sources': [
                  '<(V8_ROOT)/src/heap/base/asm/mips/push_registers_asm.cc',
                ],
              }],
              ['_toolset == "host" and host_arch == "mips64" or _toolset == "target" and target_arch=="mips64" or _toolset == "host" and host_arch == "mips64el" or _toolset == "target" and target_arch=="mips64el"', {
                'sources': [
                  '<(V8_ROOT)/src/heap/base/asm/mips64/push_registers_asm.cc',
                ],
              }],
              ['_toolset == "host" and host_arch == "riscv64" or _toolset == "target" and target_arch=="riscv64"', {
                'sources': [
                  '<(V8_ROOT)/src/heap/base/asm/riscv64/push_registers_asm.cc',
                ],
              }],
              ['_toolset == "host" and host_arch == "loong64" or _toolset == "target" and target_arch=="loong64"', {
                'sources': [
                  '<(V8_ROOT)/src/heap/base/asm/loong64/push_registers_asm.cc',
                ],
              }],
            ]
          }],
          ['OS=="win"', {
            'conditions': [
              ['_toolset == "host" and host_arch == "x64" or _toolset == "target" and target_arch=="x64"', {
                'sources': [
                  '<(V8_ROOT)/src/heap/base/asm/x64/push_registers_masm.S',
                ],
              }],
              ['_toolset == "host" and host_arch == "ia32" or _toolset == "target" and target_arch=="ia32"', {
                'sources': [
                  '<(V8_ROOT)/src/heap/base/asm/ia32/push_registers_masm.S',
                ],
              }],
              ['_toolset == "host" and host_arch == "arm64" or _toolset == "target" and target_arch=="arm64"', {
                'sources': [
                  '<(V8_ROOT)/src/heap/base/asm/arm64/push_registers_masm.S',
                ],
              }],
            ],
          }],
        ],
      },
    },  # v8_heap_base

    ###############################################################################
    # Public targets
    #

    {
      'target_name': 'v8',
      'hard_dependency': 1,
      'toolsets': ['target'],
      'dependencies': [
        'v8_snapshot',
      ],
      'conditions': [
        ['component=="shared_library"', {
          'type': '<(component)',
          'sources': [
            # Note: on non-Windows we still build this file so that gyp
            # has some sources to link into the component.
            '<(V8_ROOT)/src/utils/v8dll-main.cc',
          ],
          'defines': [
            'BUILDING_V8_SHARED',
          ],
          'direct_dependent_settings': {
            'defines': [
              'USING_V8_SHARED',
            ],
          },
          'conditions': [
            ['OS=="mac"', {
              'xcode_settings': {
                'OTHER_LDFLAGS': ['-dynamiclib', '-all_load']
              },
            }],
            ['soname_version!=""', {
              'product_extension': 'so.<(soname_version)',
            }],
          ],
        },
         {
           'type': 'static_library',
         }],
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(V8_ROOT)/include',
        ],
      },
      'actions': [
        {
          'action_name': 'v8_dump_build_config',
          'inputs': [
            '<(V8_ROOT)/tools/testrunner/utils/dump_build_config_gyp.py',
          ],
          'outputs': [
            '<(PRODUCT_DIR)/v8_build_config.json',
          ],
          'variables': {
            'v8_dump_build_config_args': [
              '<(PRODUCT_DIR)/v8_build_config.json',
              'current_cpu=<(v8_current_cpu)',
              'dcheck_always_on=<(dcheck_always_on)',
              'is_android=<(is_android)',
              'is_asan=<(asan)',
              'is_cfi=<(cfi_vptr)',
              'is_clang=<(clang)',
              'is_component_build=<(component)',
              'is_debug=<(CONFIGURATION_NAME)',
              # Not available in gyp.
              'is_full_debug=0',
              # Not available in gyp.
              'is_gcov_coverage=0',
              'is_msan=<(msan)',
              'is_tsan=<(tsan)',
              # Not available in gyp.
              'is_ubsan_vptr=0',
              'target_cpu=<(target_arch)',
              'v8_current_cpu=<(v8_current_cpu)',
              'v8_enable_atomic_object_field_writes=<(v8_enable_atomic_object_field_writes)',
              'v8_enable_concurrent_marking=<(v8_enable_concurrent_marking)',
              'v8_enable_i18n_support=<(v8_enable_i18n_support)',
              'v8_enable_verify_predictable=<(v8_enable_verify_predictable)',
              'v8_enable_verify_csa=<(v8_enable_verify_csa)',
              'v8_enable_lite_mode=<(v8_enable_lite_mode)',
              'v8_enable_pointer_compression=<(v8_enable_pointer_compression)',
              'v8_enable_shared_ro_heap=<(v8_enable_shared_ro_heap)',
              'v8_enable_webassembly=<(v8_enable_webassembly)',
              # Not available in gyp.
              'v8_control_flow_integrity=0',
              'v8_target_cpu=<(v8_target_arch)',
            ]
          },
          'conditions': [
            ['v8_target_arch=="mips" or v8_target_arch=="mipsel" \
              or v8_target_arch=="mips64" or v8_target_arch=="mips64el"', {
              'v8_dump_build_config_args': [
                'mips_arch_variant=<(mips_arch_variant)',
                'mips_use_msa=<(mips_use_msa)',
              ],
            }],
          ],
          'action': [
            '<(python)', '<(V8_ROOT)/tools/testrunner/utils/dump_build_config_gyp.py',
            '<@(v8_dump_build_config_args)',
          ],
        },
      ],
    },  # v8
    # missing a bunch of fuzzer targets

    ###############################################################################
    # Protobuf targets, used only when building outside of chromium.
    #

    {
      'target_name': 'postmortem-metadata',
      'type': 'none',
      'toolsets': ['host', 'target'],
      'dependencies': ['run_torque'],
      'variables': {
        'heapobject_files': [
          '<(SHARED_INTERMEDIATE_DIR)/torque-generated/instance-types.h',
          '<(V8_ROOT)/src/objects/allocation-site.h',
          '<(V8_ROOT)/src/objects/allocation-site-inl.h',
          '<(V8_ROOT)/src/objects/cell.h',
          '<(V8_ROOT)/src/objects/cell-inl.h',
          '<(V8_ROOT)/src/objects/code.h',
          '<(V8_ROOT)/src/objects/code-inl.h',
          '<(V8_ROOT)/src/objects/data-handler.h',
          '<(V8_ROOT)/src/objects/data-handler-inl.h',
          '<(V8_ROOT)/src/objects/descriptor-array.h',
          '<(V8_ROOT)/src/objects/descriptor-array-inl.h',
          '<(V8_ROOT)/src/objects/feedback-cell.h',
          '<(V8_ROOT)/src/objects/feedback-cell-inl.h',
          '<(V8_ROOT)/src/objects/fixed-array.h',
          '<(V8_ROOT)/src/objects/fixed-array-inl.h',
          '<(V8_ROOT)/src/objects/heap-number.h',
          '<(V8_ROOT)/src/objects/heap-number-inl.h',
          '<(V8_ROOT)/src/objects/heap-object.h',
          '<(V8_ROOT)/src/objects/heap-object-inl.h',
          '<(V8_ROOT)/src/objects/instance-type.h',
          '<(V8_ROOT)/src/objects/js-array-buffer.h',
          '<(V8_ROOT)/src/objects/js-array-buffer-inl.h',
          '<(V8_ROOT)/src/objects/js-array.h',
          '<(V8_ROOT)/src/objects/js-array-inl.h',
          '<(V8_ROOT)/src/objects/js-function-inl.h',
          '<(V8_ROOT)/src/objects/js-function.cc',
          '<(V8_ROOT)/src/objects/js-function.h',
          '<(V8_ROOT)/src/objects/js-objects.cc',
          '<(V8_ROOT)/src/objects/js-objects.h',
          '<(V8_ROOT)/src/objects/js-objects-inl.h',
          '<(V8_ROOT)/src/objects/js-promise.h',
          '<(V8_ROOT)/src/objects/js-promise-inl.h',
          '<(V8_ROOT)/src/objects/js-regexp.cc',
          '<(V8_ROOT)/src/objects/js-regexp.h',
          '<(V8_ROOT)/src/objects/js-regexp-inl.h',
          '<(V8_ROOT)/src/objects/js-regexp-string-iterator.h',
          '<(V8_ROOT)/src/objects/js-regexp-string-iterator-inl.h',
          '<(V8_ROOT)/src/objects/map.cc',
          '<(V8_ROOT)/src/objects/map.h',
          '<(V8_ROOT)/src/objects/map-inl.h',
          '<(V8_ROOT)/src/objects/name.h',
          '<(V8_ROOT)/src/objects/name-inl.h',
          '<(V8_ROOT)/src/objects/objects.h',
          '<(V8_ROOT)/src/objects/objects-inl.h',
          '<(V8_ROOT)/src/objects/oddball.h',
          '<(V8_ROOT)/src/objects/oddball-inl.h',
          '<(V8_ROOT)/src/objects/primitive-heap-object.h',
          '<(V8_ROOT)/src/objects/primitive-heap-object-inl.h',
          '<(V8_ROOT)/src/objects/scope-info.h',
          '<(V8_ROOT)/src/objects/scope-info-inl.h',
          '<(V8_ROOT)/src/objects/script.h',
          '<(V8_ROOT)/src/objects/script-inl.h',
          '<(V8_ROOT)/src/objects/shared-function-info.cc',
          '<(V8_ROOT)/src/objects/shared-function-info.h',
          '<(V8_ROOT)/src/objects/shared-function-info-inl.h',
          '<(V8_ROOT)/src/objects/string.cc',
          '<(V8_ROOT)/src/objects/string-comparator.cc',
          '<(V8_ROOT)/src/objects/string-comparator.h',
          '<(V8_ROOT)/src/objects/string.h',
          '<(V8_ROOT)/src/objects/string-inl.h',
          '<(V8_ROOT)/src/objects/struct.h',
          '<(V8_ROOT)/src/objects/struct-inl.h',
        ],
      },
      'actions': [
        {
          'action_name': 'gen-postmortem-metadata',
          'inputs': [
            '<(V8_ROOT)/tools/gen-postmortem-metadata.py',
            '<@(heapobject_files)',
          ],
          'outputs': [
            '<(SHARED_INTERMEDIATE_DIR)/debug-support.cc',
          ],
          'action': [
            '<(python)',
            '<(V8_ROOT)/tools/gen-postmortem-metadata.py',
            '<@(_outputs)',
            '<@(heapobject_files)'
          ],
        },
      ],
      'direct_dependent_settings': {
        'sources': ['<(SHARED_INTERMEDIATE_DIR)/debug-support.cc', ],
      },
    },  # postmortem-metadata

    {
      'target_name': 'v8_zlib',
      'type': 'static_library',
      'toolsets': ['host', 'target'],
      'conditions': [
        ['OS=="win"', {
          'conditions': [
            ['"<(target_arch)"=="arm64" and _toolset=="target"', {
              'defines': ['CPU_NO_SIMD']
            }, {
              'defines': ['X86_WINDOWS']
            }]
          ]
        }],
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(V8_ROOT)/third_party/zlib',
          '<(V8_ROOT)/third_party/zlib/google',
        ],
      },
      'defines': [ 'ZLIB_IMPLEMENTATION' ],
      'include_dirs': [
        '<(V8_ROOT)/third_party/zlib',
        '<(V8_ROOT)/third_party/zlib/google',
      ],
      'sources': [
        '<(V8_ROOT)/third_party/zlib/adler32.c',
        '<(V8_ROOT)/third_party/zlib/chromeconf.h',
        '<(V8_ROOT)/third_party/zlib/compress.c',
        '<(V8_ROOT)/third_party/zlib/contrib/optimizations/insert_string.h',
        '<(V8_ROOT)/third_party/zlib/contrib/optimizations/insert_string.h',
        '<(V8_ROOT)/third_party/zlib/cpu_features.c',
        '<(V8_ROOT)/third_party/zlib/cpu_features.h',
        '<(V8_ROOT)/third_party/zlib/crc32.c',
        '<(V8_ROOT)/third_party/zlib/crc32.h',
        '<(V8_ROOT)/third_party/zlib/deflate.c',
        '<(V8_ROOT)/third_party/zlib/deflate.h',
        '<(V8_ROOT)/third_party/zlib/gzclose.c',
        '<(V8_ROOT)/third_party/zlib/gzguts.h',
        '<(V8_ROOT)/third_party/zlib/gzlib.c',
        '<(V8_ROOT)/third_party/zlib/gzread.c',
        '<(V8_ROOT)/third_party/zlib/gzwrite.c',
        '<(V8_ROOT)/third_party/zlib/infback.c',
        '<(V8_ROOT)/third_party/zlib/inffast.c',
        '<(V8_ROOT)/third_party/zlib/inffast.h',
        '<(V8_ROOT)/third_party/zlib/inffixed.h',
        '<(V8_ROOT)/third_party/zlib/inflate.c',
        '<(V8_ROOT)/third_party/zlib/inflate.h',
        '<(V8_ROOT)/third_party/zlib/inftrees.c',
        '<(V8_ROOT)/third_party/zlib/inftrees.h',
        '<(V8_ROOT)/third_party/zlib/trees.c',
        '<(V8_ROOT)/third_party/zlib/trees.h',
        '<(V8_ROOT)/third_party/zlib/uncompr.c',
        '<(V8_ROOT)/third_party/zlib/zconf.h',
        '<(V8_ROOT)/third_party/zlib/zlib.h',
        '<(V8_ROOT)/third_party/zlib/zutil.c',
        '<(V8_ROOT)/third_party/zlib/zutil.h',
        '<(V8_ROOT)/third_party/zlib/google/compression_utils_portable.cc',
        '<(V8_ROOT)/third_party/zlib/google/compression_utils_portable.h',
      ],
    },  # v8_zlib
  ],
}
