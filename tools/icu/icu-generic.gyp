# Copyright (c) IBM Corporation and Others. All Rights Reserved.
# very loosely based on icu.gyp from Chromium:
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.


{
  'variables': {
    'icu_src_derb': [
      '<(icu_path)/source/tools/genrb/derb.c',
      '<(icu_path)/source/tools/genrb/derb.cpp'
    ],
  },
  'includes': [ '../../icu_config.gypi' ],
  'targets': [
    {
      # a target for additional uconfig defines, target only
      'target_name': 'icu_uconfig_target',
      'type': 'none',
      'toolsets': [ 'target' ],
      'direct_dependent_settings': {
        'defines': []
      },
    },
    {
      # a target to hold uconfig defines.
      # for now these are hard coded, but could be defined.
      'target_name': 'icu_uconfig',
      'type': 'none',
      'toolsets': [ 'host', 'target' ],
      'direct_dependent_settings': {
        'defines': [
          'UCONFIG_NO_SERVICE=1',
          'U_ENABLE_DYLOAD=0',
          'U_STATIC_IMPLEMENTATION=1',
          'U_HAVE_STD_STRING=1',
          # TODO(srl295): reenable following pending
          # https://code.google.com/p/v8/issues/detail?id=3345
          # (saves some space)
          'UCONFIG_NO_BREAK_ITERATION=0',
        ],
      }
    },
    {
      # a target to hold common settings.
      # make any target that is ICU implementation depend on this.
      'target_name': 'icu_implementation',
      'toolsets': [ 'host', 'target' ],
      'type': 'none',
      'direct_dependent_settings': {
        'conditions': [
          [ 'os_posix == 1 and OS != "mac" and OS != "ios"', {
            'cflags': [ '-Wno-deprecated-declarations', '-Wno-strict-aliasing' ],
            'cflags_cc': [ '-frtti' ],
            'cflags_cc!': [ '-fno-rtti' ],
          }],
          [ 'OS == "mac" or OS == "ios"', {
            'xcode_settings': {'GCC_ENABLE_CPP_RTTI': 'YES' },
          }],
          [ 'OS == "win"', {
            'msvs_settings': {
              'VCCLCompilerTool': {'RuntimeTypeInfo': 'true'},
            }
          }],
        ],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'RuntimeTypeInfo': 'true',
            'ExceptionHandling': '1',
            'AdditionalOptions': [ '/source-charset:utf-8' ],
          },
        },
        'configurations': {
          # TODO: why does this need to be redefined for Release and Debug?
          # Maybe this should be pushed into common.gypi with an "if v8 i18n"?
          'Release': {
            'msvs_settings': {
              'VCCLCompilerTool': {
                'RuntimeTypeInfo': 'true',
                'ExceptionHandling': '1',
              },
            },
          },
          'Debug': {
            'msvs_settings': {
              'VCCLCompilerTool': {
                'RuntimeTypeInfo': 'true',
                'ExceptionHandling': '1',
              },
            },
          },
        },
        'defines': [
          'U_ATTRIBUTE_DEPRECATED=',
          'U_STATIC_IMPLEMENTATION=1',
        ],
      },
    },
    {
      'target_name': 'icui18n',
      'toolsets': [ 'target', 'host' ],
      'conditions' : [
        ['_toolset=="target"', {
          'type': '<(library)',
          'sources': [
            '<@(icu_src_i18n)'
          ],
          'include_dirs': [
            '<(icu_path)/source/i18n',
          ],
          'defines': [
            'U_I18N_IMPLEMENTATION=1',
          ],
          'dependencies': [ 'icuucx', 'icu_implementation', 'icu_uconfig', 'icu_uconfig_target' ],
          'direct_dependent_settings': {
            'include_dirs': [
              '<(icu_path)/source/i18n',
            ],
          },
          'export_dependent_settings': [ 'icuucx', 'icu_uconfig_target' ],
        }],
        ['_toolset=="host"', {
          'type': 'none',
          'dependencies': [ 'icutools#host' ],
          'export_dependent_settings': [ 'icutools' ],
        }],
      ],
    },
    # This exports actual ICU data
    {
      'target_name': 'icudata',
      'type': '<(library)',
      'toolsets': [ 'target' ],
      'conditions': [
        [ 'OS == "win"', {
          'conditions': [
            [ 'icu_small == "false"', { # and OS=win
              # full data - just build the full data file, then we are done.
              'sources': [ '<(SHARED_INTERMEDIATE_DIR)/icudt<(icu_ver_major)<(icu_endianness)_dat.<(icu_asm_ext)' ],
              'dependencies': [ 'genccode#host' ],
              'conditions': [
                [ 'clang==1', {
                  'actions': [
                    {
                      'action_name': 'icudata',
                      'msvs_quote_cmd': 0,
                      'inputs': [ '<(icu_data_in)' ],
                      'outputs': [ '<(SHARED_INTERMEDIATE_DIR)/icudt<(icu_ver_major)<(icu_endianness)_dat.<(icu_asm_ext)' ],
                      # on Windows, we can go directly to .obj file (-o) option.
                      # for Clang use "-c <(target_arch)" option
                      'action': [ '<(PRODUCT_DIR)/genccode<(EXECUTABLE_SUFFIX)',
                                  '<@(icu_asm_opts)', # -o
                                  '-c', '<(target_arch)',
                                  '-d', '<(SHARED_INTERMEDIATE_DIR)',
                                  '-n', 'icudata',
                                  '-e', 'icudt<(icu_ver_major)',
                                  '<@(_inputs)' ],
                    },
                  ],
                }, {
                  'actions': [
                    {
                      'action_name': 'icudata',
                      'msvs_quote_cmd': 0,
                      'inputs': [ '<(icu_data_in)' ],
                      'outputs': [ '<(SHARED_INTERMEDIATE_DIR)/icudt<(icu_ver_major)<(icu_endianness)_dat.<(icu_asm_ext)' ],
                      # on Windows, we can go directly to .obj file (-o) option.
                      # for MSVC do not use "-c <(target_arch)" option
                      'action': [ '<(PRODUCT_DIR)/genccode<(EXECUTABLE_SUFFIX)',
                                  '<@(icu_asm_opts)', # -o
                                  '-d', '<(SHARED_INTERMEDIATE_DIR)',
                                  '-n', 'icudata',
                                  '-e', 'icudt<(icu_ver_major)',
                                  '<@(_inputs)' ],
                    },
                  ],
                }]
              ],
            }, { # icu_small == TRUE and OS == win
              # link against stub data primarily
              # then, use icupkg and genccode to rebuild data
              'dependencies': [ 'icustubdata', 'genccode#host', 'icupkg#host', 'genrb#host', 'iculslocs#host' ],
              'export_dependent_settings': [ 'icustubdata' ],
              'actions': [
                {
                  # trim down ICU
                  'action_name': 'icutrim',
                  'msvs_quote_cmd': 0,
                  'inputs': [ '<(icu_data_in)', 'icu_small.json' ],
                  'outputs': [ '<(SHARED_INTERMEDIATE_DIR)/icutmp/icudt<(icu_ver_major)<(icu_endianness).dat' ],
                  'action': [ '<(python)',
                              'icutrim.py',
                              '-P', '<(PRODUCT_DIR)/.', # '.' suffix is a workaround against GYP assumptions :(
                              '-D', '<(icu_data_in)',
                              '--delete-tmp',
                              '-T', '<(SHARED_INTERMEDIATE_DIR)/icutmp',
                              '-F', 'icu_small.json',
                              '-O', 'icudt<(icu_ver_major)<(icu_endianness).dat',
                              '-v',
                              '-L', '<(icu_locales)'],
                },
                {
                  # build final .dat -> .obj
                  'action_name': 'genccode',
                  'msvs_quote_cmd': 0,
                  'inputs': [ '<(SHARED_INTERMEDIATE_DIR)/icutmp/icudt<(icu_ver_major)<(icu_endianness).dat' ],
                  'outputs': [ '<(SHARED_INTERMEDIATE_DIR)/icudt<(icu_ver_major)<(icu_endianness)_dat.<(icu_asm_ext)' ],
                  'action': [ '<(PRODUCT_DIR)/genccode<(EXECUTABLE_SUFFIX)',
                              '<@(icu_asm_opts)', # -o
                              '-d', '<(SHARED_INTERMEDIATE_DIR)/',
                              '-n', 'icudata',
                              '-e', 'icusmdt<(icu_ver_major)',
                              '<@(_inputs)' ],
                },
              ],
              # This file contains the small ICU data.
              'sources': [ '<(SHARED_INTERMEDIATE_DIR)/icudt<(icu_ver_major)<(icu_endianness)_dat.<(icu_asm_ext)' ],
            } ] ], #end of OS==win and icu_small == true
        }, { # OS != win
          'conditions': [
            [ 'icu_small == "false"', {
              # full data - no trim needed
              'sources': [ '<(SHARED_INTERMEDIATE_DIR)/icudt<(icu_ver_major)_dat.<(icu_asm_ext)' ],
              'dependencies': [ 'genccode#host', 'icupkg#host', 'icu_implementation#host', 'icu_uconfig' ],
              'include_dirs': [
                '<(icu_path)/source/common',
              ],
              'actions': [
                {
                   # Copy the .dat file, swapping endianness if needed.
                   'action_name': 'icupkg',
                   'inputs': [ '<(icu_data_in)' ],
                   'outputs':[ '<(SHARED_INTERMEDIATE_DIR)/icudt<(icu_ver_major)<(icu_endianness).dat' ],
                   'action': [ '<(PRODUCT_DIR)/icupkg<(EXECUTABLE_SUFFIX)',
                               '-t<(icu_endianness)',
                               '<@(_inputs)',
                               '<@(_outputs)',
                             ],
                },
                {
                   # Rename without the endianness marker (icudt64l.dat -> icudt64.dat)
                   'action_name': 'copy',
                   'inputs': [ '<(SHARED_INTERMEDIATE_DIR)/icudt<(icu_ver_major)<(icu_endianness).dat' ],
                   'outputs':[ '<(SHARED_INTERMEDIATE_DIR)/icudt<(icu_ver_major).dat' ],
                   'action': [ 'cp',
                               '<@(_inputs)',
                               '<@(_outputs)',
                             ],
                },
                {
                  # convert full ICU data file to .c, or .S, etc.
                  'action_name': 'icudata',
                  'inputs': [ '<(SHARED_INTERMEDIATE_DIR)/icudt<(icu_ver_major).dat' ],
                  'outputs':[ '<(SHARED_INTERMEDIATE_DIR)/icudt<(icu_ver_major)_dat.<(icu_asm_ext)' ],
                  'action': [ '<(PRODUCT_DIR)/genccode<(EXECUTABLE_SUFFIX)',
                              '-e', 'icudt<(icu_ver_major)',
                              '-d', '<(SHARED_INTERMEDIATE_DIR)',
                              '<@(icu_asm_opts)',
                              '-f', 'icudt<(icu_ver_major)_dat',
                              '<@(_inputs)' ],
                },
              ], # end actions
            }, { # icu_small == true ( and OS != win )
              # link against stub data (as primary data)
              # then, use icupkg and genccode to rebuild small data
              'dependencies': [ 'icustubdata', 'genccode#host', 'icupkg#host', 'genrb#host', 'iculslocs#host',
                               'icu_implementation', 'icu_uconfig' ],
              'export_dependent_settings': [ 'icustubdata' ],
              'actions': [
                {
                  # Trim down ICU.
                  # Note that icupkg is invoked automatically, swapping endianness if needed.
                  'action_name': 'icutrim',
                  'inputs': [ '<(icu_data_in)', 'icu_small.json' ],
                  'outputs': [ '<(SHARED_INTERMEDIATE_DIR)/icutmp/icudt<(icu_ver_major)<(icu_endianness).dat' ],
                  'action': [ '<(python)',
                              'icutrim.py',
                              '-P', '<(PRODUCT_DIR)',
                              '-D', '<(icu_data_in)',
                              '--delete-tmp',
                              '-T', '<(SHARED_INTERMEDIATE_DIR)/icutmp',
                              '-F', 'icu_small.json',
                              '-O', 'icudt<(icu_ver_major)<(icu_endianness).dat',
                              '-v',
                              '-L', '<(icu_locales)'],
                }, {
                  # rename to get the final entrypoint name right (icudt64l.dat -> icusmdt64.dat)
                   'action_name': 'rename',
                   'inputs': [ '<(SHARED_INTERMEDIATE_DIR)/icutmp/icudt<(icu_ver_major)<(icu_endianness).dat' ],
                   'outputs': [ '<(SHARED_INTERMEDIATE_DIR)/icutmp/icusmdt<(icu_ver_major).dat' ],
                   'action': [ 'cp',
                               '<@(_inputs)',
                               '<@(_outputs)',
                             ],
                }, {
                  # For icu-small, always use .c, don't try to use .S, etc.
                  'action_name': 'genccode',
                  'inputs': [ '<(SHARED_INTERMEDIATE_DIR)/icutmp/icusmdt<(icu_ver_major).dat' ],
                  'outputs': [ '<(SHARED_INTERMEDIATE_DIR)/icusmdt<(icu_ver_major)_dat.<(icu_asm_ext)' ],
                  'action': [ '<(PRODUCT_DIR)/genccode<(EXECUTABLE_SUFFIX)',
                              '<@(icu_asm_opts)',
                              '-d', '<(SHARED_INTERMEDIATE_DIR)',
                              '<@(_inputs)' ],
                },
              ],
              # This file contains the small ICU data
              'sources': [ '<(SHARED_INTERMEDIATE_DIR)/icusmdt<(icu_ver_major)_dat.<(icu_asm_ext)' ],
              # for umachine.h
              'include_dirs': [
                '<(icu_path)/source/common',
              ],
            }]], # end icu_small == true
        }]], # end OS != win
    }, # end icudata
    # icustubdata is a tiny (~1k) symbol with no ICU data in it.
    # tools must link against it as they are generating the full data.
    {
      'target_name': 'icustubdata',
      'type': '<(library)',
      'toolsets': [ 'target' ],
      'dependencies': [ 'icu_implementation' ],
      'sources': [
        '<@(icu_src_stubdata)'
      ],
      'include_dirs': [
        '<(icu_path)/source/common',
      ],
    },
    # this target is for v8 consumption.
    # it is icuuc + stubdata
    # it is only built for target
    {
      'target_name': 'icuuc',
      'type': 'none',
      'toolsets': [ 'target', 'host' ],
      'conditions' : [
        ['_toolset=="host"', {
          'dependencies': [ 'icutools#host' ],
          'export_dependent_settings': [ 'icutools' ],
        }],
        ['_toolset=="target"', {
          'dependencies': [ 'icuucx', 'icudata' ],
          'export_dependent_settings': [ 'icuucx', 'icudata' ],
        }],
      ],
    },
    # This is the 'real' icuuc.
    {
      'target_name': 'icuucx',
      'type': '<(library)',
      'dependencies': [ 'icu_implementation', 'icu_uconfig', 'icu_uconfig_target' ],
      'toolsets': [ 'target' ],
      'sources': [
        '<@(icu_src_common)',
      ],
          ## if your compiler can dead-strip, this will
          ## make ZERO difference to binary size.
          ## Made ICU-specific for future-proofing.
      'conditions': [
        [ 'OS == "solaris"', { 'defines': [
          '_XOPEN_SOURCE_EXTENDED=0',
        ]}],
      ],
      'include_dirs': [
        '<(icu_path)/source/common',
      ],
      'defines': [
        'U_COMMON_IMPLEMENTATION=1',
      ],
      'cflags_c': ['-std=c99'],
      'export_dependent_settings': [ 'icu_uconfig', 'icu_uconfig_target' ],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(icu_path)/source/common',
        ],
        'conditions': [
          [ 'OS=="win"', {
            'link_settings': {
              'libraries': [ '-lAdvAPI32.lib', '-lUser32.lib' ],
            },
          }],
        ],
      },
    },
    # tools library. This builds all of ICU together.
    {
      'target_name': 'icutools',
      'type': '<(library)',
      'toolsets': [ 'host' ],
      'dependencies': [ 'icu_implementation', 'icu_uconfig' ],
      'sources': [
        '<@(icu_src_tools)',
        '<@(icu_src_common)',
        '<@(icu_src_i18n)',
        '<@(icu_src_stubdata)',
      ],
      'sources!': [
        '<(icu_path)/source/tools/toolutil/udbgutil.cpp',
        '<(icu_path)/source/tools/toolutil/udbgutil.h',
        '<(icu_path)/source/tools/toolutil/dbgutil.cpp',
        '<(icu_path)/source/tools/toolutil/dbgutil.h',
      ],
      'include_dirs': [
        '<(icu_path)/source/common',
        '<(icu_path)/source/i18n',
        '<(icu_path)/source/tools/toolutil',
      ],
      'defines': [
        'U_COMMON_IMPLEMENTATION=1',
        'U_I18N_IMPLEMENTATION=1',
        'U_IO_IMPLEMENTATION=1',
        'U_TOOLUTIL_IMPLEMENTATION=1',
        #'DEBUG=0', # http://bugs.icu-project.org/trac/ticket/10977
      ],
      'cflags_c': ['-std=c99'],
      'conditions': [
        ['OS == "solaris"', {
          'defines': [ '_XOPEN_SOURCE_EXTENDED=0' ]
        }]
      ],
      'direct_dependent_settings': {
        'include_dirs': [
          '<(icu_path)/source/common',
          '<(icu_path)/source/i18n',
          '<(icu_path)/source/tools/toolutil',
        ],
        'conditions': [
          [ 'OS=="win"', {
            'link_settings': {
              'libraries': [ '-lAdvAPI32.lib', '-lUser32.lib' ],
            },
          }],
        ],
      },
      'export_dependent_settings': [ 'icu_uconfig' ],
    },
    # This tool is needed to rebuild .res files from .txt,
    # or to build index (res_index.txt) files for small-icu
    {
      'target_name': 'genrb',
      'type': 'executable',
      'toolsets': [ 'host' ],
      'dependencies': [ 'icutools', 'icu_implementation' ],
      'sources': [
        '<@(icu_src_genrb)'
      ],
      # derb is a separate executable
      # (which is not currently built)
      'sources!': [
        '<@(icu_src_derb)',
        'no-op.cc',
      ],
      'conditions': [
        # Avoid excessive LTO
        ['enable_lto=="true"', {
          'ldflags': [ '-fno-lto' ],
        }],
      ],
    },
    # This tool is used to rebuild res_index.res manifests
    {
      'target_name': 'iculslocs',
      'toolsets': [ 'host' ],
      'type': 'executable',
      'dependencies': [ 'icutools' ],
      'sources': [
        'iculslocs.cc',
        'no-op.cc',
      ],
      'conditions': [
        # Avoid excessive LTO
        ['enable_lto=="true"', {
          'ldflags': [ '-fno-lto' ],
        }],
      ],
    },
    # This tool is used to package, unpackage, repackage .dat files
    # and convert endianesses
    {
      'target_name': 'icupkg',
      'toolsets': [ 'host' ],
      'type': 'executable',
      'dependencies': [ 'icutools' ],
      'sources': [
        '<@(icu_src_icupkg)',
        'no-op.cc',
      ],
      'conditions': [
        # Avoid excessive LTO
        ['enable_lto=="true"', {
          'ldflags': [ '-fno-lto' ],
        }],
      ],
    },
    # this is used to convert .dat directly into .obj
    {
      'target_name': 'genccode',
      'toolsets': [ 'host' ],
      'type': 'executable',
      'dependencies': [ 'icutools' ],
      'sources': [
        '<@(icu_src_genccode)',
        'no-op.cc',
      ],
      'conditions': [
        # Avoid excessive LTO
        ['enable_lto=="true"', {
          'ldflags': [ '-fno-lto' ],
        }],
      ],
    },
  ],
}
