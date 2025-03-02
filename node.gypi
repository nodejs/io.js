{
  # 'force_load' means to include the static libs into the shared lib or
  # executable. Therefore, it is enabled when building:
  # 1. The executable and it uses static lib (cctest and node)
  # 2. The shared lib
  # Linker optimizes out functions that are not used. When force_load=true,
  # --whole-archive,force_load and /WHOLEARCHIVE are used to include
  # all obj files in static libs into the executable or shared lib.
  'variables': {
    'variables': {
      'variables': {
        'force_load%': 'true',
        'current_type%': '<(_type)',
      },
      'force_load%': '<(force_load)',
      'conditions': [
        ['current_type=="static_library"', {
          'force_load': 'false',
        }],
        [ 'current_type=="executable" and node_target_type=="shared_library"', {
          'force_load': 'false',
        }]
      ],
    },
    'force_load%': '<(force_load)',
  },

  'conditions': [
    [ 'clang==1', {
      'cflags': [
        '-Werror=undefined-inline',
        '-Werror=extra-semi',
        '-Werror=ctad-maybe-unsupported',
      ],
    }],
    [ '"<(_type)"=="executable"', {
      'msvs_settings': {
        'VCManifestTool': {
          'EmbedManifest': 'true',
          'AdditionalManifestFiles': 'src/res/node.exe.extra.manifest'
        }
      },
    }],
    [ 'node_shared=="true"', {
      'defines': [
        'NODE_SHARED_MODE',
      ],
      'conditions': [
        ['"<(_type)"=="executable"', {
          'defines': [
            'USING_UV_SHARED',
            'USING_V8_SHARED',
            'BUILDING_NODE_EXTENSION'
          ],
          'defines!': [
            'BUILDING_V8_SHARED=1',
            'BUILDING_UV_SHARED=1'
          ]
        }],
      ],
    }],
    [ 'OS=="win"', {
      'defines!': [
        'NODE_PLATFORM="win"',
      ],
      'defines': [
        'FD_SETSIZE=1024',
        # we need to use node's preferred "win32" rather than gyp's preferred "win"
        'NODE_PLATFORM="win32"',
        '_UNICODE=1',
      ],
      'conditions': [
          ['clang != 1 or use_ccache_win != 1', {
            'msvs_precompiled_header': 'tools/msvs/pch/node_pch.h',
            'msvs_precompiled_source': 'tools/msvs/pch/node_pch.cc',
            'sources': [
              '<(_msvs_precompiled_header)',
              '<(_msvs_precompiled_source)',
            ],
          }]
      ]
    }, { # POSIX
      'defines': [ '__POSIX__' ],
    }],
    [ 'node_enable_d8=="true"', {
      'dependencies': [ 'tools/v8_gypfiles/d8.gyp:d8' ],
    }],
    [ 'node_use_bundled_v8=="true"', {
      'dependencies': [
        'tools/v8_gypfiles/v8.gyp:v8_snapshot',
        'tools/v8_gypfiles/v8.gyp:v8_libplatform',
      ],
    }],
    [ 'node_use_v8_platform=="true"', {
      'defines': [
        'NODE_USE_V8_PLATFORM=1',
      ],
    }, {
      'defines': [
        'NODE_USE_V8_PLATFORM=0',
      ],
    }],
    [ 'node_tag!=""', {
      'defines': [ 'NODE_TAG="<(node_tag)"' ],
    }],
    [ 'node_v8_options!=""', {
      'defines': [ 'NODE_V8_OPTIONS="<(node_v8_options)"'],
    }],
    [ 'node_release_urlbase!=""', {
      'defines': [
        'NODE_RELEASE_URLBASE="<(node_release_urlbase)"',
      ]
    }],
    [ 'v8_enable_i18n_support==1', {
      'defines': [ 'NODE_HAVE_I18N_SUPPORT=1' ],
      'dependencies': [
        '<(icu_gyp_path):icui18n',
        '<(icu_gyp_path):icuuc',
      ],
      'conditions': [
        [ 'icu_small=="true"', {
          'defines': [ 'NODE_HAVE_SMALL_ICU=1' ],
          'conditions': [
            [ 'icu_default_data!=""', {
              'defines': [
                'NODE_ICU_DEFAULT_DATA_DIR="<(icu_default_data)"',
              ],
            }],
          ],
      }]],
    }],
    [ 'node_use_bundled_v8=="true" and \
       node_enable_v8_vtunejit=="true" and (target_arch=="x64" or \
       target_arch=="ia32" or target_arch=="x32")', {
      'defines': [ 'NODE_ENABLE_VTUNE_PROFILING' ],
      'dependencies': [
        'tools/v8_gypfiles/v8vtune.gyp:v8_vtune'
      ],
    }],
    [ 'node_no_browser_globals=="true"', {
      'defines': [ 'NODE_NO_BROWSER_GLOBALS' ],
    } ],
    [ 'node_shared_zlib=="false"', {
      'dependencies': [ 'deps/zlib/zlib.gyp:zlib' ],
      'defines': [ 'NODE_BUNDLED_ZLIB' ],
      'conditions': [
        [ 'force_load=="true"', {
          'xcode_settings': {
            'OTHER_LDFLAGS': [
              '-Wl,-force_load,<(PRODUCT_DIR)/<(STATIC_LIB_PREFIX)zlib<(STATIC_LIB_SUFFIX)',
            ],
          },
          'msvs_settings': {
            'VCLinkerTool': {
              'AdditionalOptions': [
                '/WHOLEARCHIVE:<(PRODUCT_DIR)/lib/zlib<(STATIC_LIB_SUFFIX)',
              ],
            },
          },
          'conditions': [
            ['OS!="aix" and OS!="os400" and OS!="ios" and node_shared=="false"', {
              'ldflags': [
                '-Wl,--whole-archive',
                '<(obj_dir)/deps/zlib/<(STATIC_LIB_PREFIX)zlib<(STATIC_LIB_SUFFIX)',
                '-Wl,--no-whole-archive',
              ],
            }],
          ],
        }],
      ],
    }],

    [ 'node_shared_http_parser=="false"', {
      'dependencies': [
        'deps/llhttp/llhttp.gyp:llhttp'
      ],
    } ],

    [ 'node_shared_cares=="false"', {
      'dependencies': [ 'deps/cares/cares.gyp:cares' ],
    }],

    [ 'node_shared_libuv=="false"', {
      'dependencies': [ 'deps/uv/uv.gyp:libuv' ],
      'conditions': [
        [ 'force_load=="true"', {
          'xcode_settings': {
            'OTHER_LDFLAGS': [
              '-Wl,-force_load,<(PRODUCT_DIR)/libuv<(STATIC_LIB_SUFFIX)',
            ],
          },
          'msvs_settings': {
            'VCLinkerTool': {
              'AdditionalOptions': [
                '/WHOLEARCHIVE:<(PRODUCT_DIR)/lib/libuv<(STATIC_LIB_SUFFIX)',
              ],
            },
          },
          'conditions': [
            ['OS!="aix" and OS!="os400" and OS!="ios" and node_shared=="false"', {
              'ldflags': [
                '-Wl,--whole-archive',
                '<(obj_dir)/deps/uv/<(STATIC_LIB_PREFIX)uv<(STATIC_LIB_SUFFIX)',
                '-Wl,--no-whole-archive',
              ],
            }],
          ],
        }],
      ],
    }],

    [ 'node_shared_uvwasi=="false"', {
      'dependencies': [ 'deps/uvwasi/uvwasi.gyp:uvwasi' ],
    }],

    [ 'node_shared_nghttp2=="false"', {
      'dependencies': [ 'deps/nghttp2/nghttp2.gyp:nghttp2' ],
    }],

    [ 'node_shared_ada=="false"', {
        'dependencies': [ 'deps/ada/ada.gyp:ada' ],
    }],

    [ 'node_shared_simdjson=="false"', {
        'dependencies': [ 'deps/simdjson/simdjson.gyp:simdjson' ],
    }],

    [ 'node_shared_simdutf=="false"', {
        'dependencies': [ 'tools/v8_gypfiles/v8.gyp:simdutf' ],
    }],

    [ 'node_shared_brotli=="false"', {
      'dependencies': [ 'deps/brotli/brotli.gyp:brotli' ],
    }],

    [ 'node_shared_sqlite=="false"', {
      'dependencies': [ 'deps/sqlite/sqlite.gyp:sqlite' ],
    }],

    [ 'node_shared_zstd=="false"', {
      'dependencies': [ 'deps/zstd/zstd.gyp:zstd' ],
    }],

    [ 'OS=="mac"', {
      # linking Corefoundation is needed since certain macOS debugging tools
      # like Instruments require it for some features. Security is needed for
      # --use-system-ca.
      'libraries': [ '-framework CoreFoundation -framework Security' ],
      'defines!': [
        'NODE_PLATFORM="mac"',
      ],
      'defines': [
        # we need to use node's preferred "darwin" rather than gyp's preferred "mac"
        'NODE_PLATFORM="darwin"',
      ],
    }],
    [ 'OS=="freebsd"', {
      'libraries': [
        '-lutil',
        '-lkvm',
      ],
    }],
    [ 'OS in "aix os400"', {
      'defines': [
        '_LINUX_SOURCE_COMPAT',
        '__STDC_FORMAT_MACROS',
      ],
      'conditions': [
        [ 'force_load=="true"', {
          'variables': {
            'exp_filename': '<(PRODUCT_DIR)/<(_target_name).exp',
          },
          'actions': [
            {
              'action_name': 'expfile',
              'inputs': [
                '<(obj_dir)',
              ],
              'outputs': [
                '<(exp_filename)',
              ],
              'action': [
                'sh', 'tools/create_expfile.sh',
                '<@(_inputs)',
                '<@(_outputs)',
              ],
            }
          ],
          'ldflags': [
            '-Wl,-bE:<(exp_filename)',
            '-Wl,-brtl',
          ],
        }],
      ],
    }],
    [ 'OS=="solaris"', {
      'libraries': [
        '-lkstat',
        '-lumem',
      ],
      'defines!': [
        'NODE_PLATFORM="solaris"',
      ],
      'defines': [
        # we need to use node's preferred "sunos"
        # rather than gyp's preferred "solaris"
        'NODE_PLATFORM="sunos"',
      ],
    }],
    [ '(OS=="freebsd" or OS=="linux") and node_shared=="false"'
        ' and force_load=="true"', {
      'ldflags': [
        '-Wl,-z,noexecstack',
        '-Wl,--whole-archive <(v8_base)',
        '-Wl,--no-whole-archive',
      ]
    }],
    [ 'node_use_bundled_v8=="true" and v8_postmortem_support==1 and force_load=="true"', {
      'xcode_settings': {
        'OTHER_LDFLAGS': [
          '-Wl,-force_load,<(v8_base)',
        ],
      },
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
    [ 'coverage=="true" and node_shared=="false" and OS in "mac freebsd linux"', {
      'cflags!': [ '-O3' ],
      'ldflags': [ '--coverage',
                   '-g',
                   '-O0' ],
      'cflags': [ '--coverage',
                   '-g',
                   '-O0' ],
      'xcode_settings': {
        'OTHER_CFLAGS': [
          '--coverage',
          '-g',
          '-O0'
        ],
      },
      'conditions': [
        [ '_type=="executable"', {
          'xcode_settings': {
            'OTHER_LDFLAGS': [ '--coverage', ],
          },
        }],
      ],
    }],
    [ 'coverage=="true"', {
      'defines': [
        'ALLOW_ATTACHING_DEBUGGER_IN_WATCH_MODE',
        'ALLOW_ATTACHING_DEBUGGER_IN_TEST_RUNNER',
      ],
    }],
    [ 'OS=="sunos"', {
      'ldflags': [ '-Wl,-M,/usr/lib/ld/map.noexstk' ],
    }],
    [ 'OS=="linux"', {
      'libraries!': [
        '-lrt'
      ],
    }],
    [ 'OS in "freebsd linux"', {
      'ldflags': [ '-Wl,-z,relro',
                   '-Wl,-z,now' ]
    }],
    [ 'node_use_openssl=="true"', {
      'defines': [ 'HAVE_OPENSSL=1' ],
      'conditions': [
        [ 'node_shared_openssl=="false"', {
          'defines': [ 'OPENSSL_API_COMPAT=0x10100000L', ],
          'dependencies': [
            './deps/openssl/openssl.gyp:openssl',

            # For tests
            './deps/openssl/openssl.gyp:openssl-cli',
          ],
          'conditions': [
            # -force_load or --whole-archive are not applicable for
            # the static library
            [ 'force_load=="true"', {
              'xcode_settings': {
                'OTHER_LDFLAGS': [
                  '-Wl,-force_load,<(PRODUCT_DIR)/<(openssl_product)',
                ],
              },
              'msvs_settings': {
                'VCLinkerTool': {
                  'AdditionalOptions': [
                    '/WHOLEARCHIVE:<(PRODUCT_DIR)/lib/<(openssl_product)',
                  ],
                },
              },
              'conditions': [
                ['OS in "linux freebsd" and node_shared=="false"', {
                  'ldflags': [
                    '-Wl,--whole-archive,'
                      '<(obj_dir)/deps/openssl/<(openssl_product)',
                    '-Wl,--no-whole-archive',
                  ],
                }],
                # openssl.def is based on zlib.def, zlib symbols
                # are always exported.
                ['use_openssl_def==1', {
                  'sources': ['<(SHARED_INTERMEDIATE_DIR)/openssl.def'],
                }],
                ['OS=="win" and use_openssl_def==0', {
                  'sources': ['deps/zlib/win32/zlib.def'],
                }],
              ],
            }],
          ]
        }],
        [ 'openssl_quic=="true" and node_shared_ngtcp2=="false"', {
          'dependencies': [ './deps/ngtcp2/ngtcp2.gyp:ngtcp2' ]
        }],
        [ 'openssl_quic=="true" and node_shared_nghttp3=="false"', {
          'dependencies': [ './deps/ngtcp2/ngtcp2.gyp:nghttp3' ]
        }]
      ]
    }, {
      'defines': [ 'HAVE_OPENSSL=0' ]
    }],
    [ 'node_use_amaro=="true"', {
      'defines': [ 'HAVE_AMARO=1' ],
    }, {
      'defines': [ 'HAVE_AMARO=0' ]
    }],
  ],
}
