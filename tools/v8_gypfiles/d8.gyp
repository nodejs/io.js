# Copyright 2018 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'V8_ROOT': '../../deps/v8',
    'v8_code': 1,
    # Enable support for Intel VTune. Supported on ia32/x64 only
    'v8_enable_vtunejit%': 0,
    'v8_enable_i18n_support%': 1,
  },
  'includes': ['toolchain.gypi', 'features.gypi'],
  'targets': [
    {
      'target_name': 'd8',
      'type': 'executable',
      'dependencies': [
        'v8.gyp:v8',
        'v8.gyp:v8_libbase',
        'v8.gyp:v8_libplatform',
        'v8.gyp:generate_bytecode_builtins_list',
        'v8.gyp:v8_abseil',
        'v8.gyp:fp16',
      ],
      # Generated source files need this explicitly:
      'include_dirs+': [
        '<(V8_ROOT)',
        '<(DEPTH)',
        '<(SHARED_INTERMEDIATE_DIR)',
      ],
      'sources': [
        '<!@pymod_do_main(GN-scraper "<(V8_ROOT)/BUILD.gn"  "v8_executable.\\"d8\\".*?sources = ")',
      ],
      'conditions': [
        [ 'want_separate_host_toolset==1', {
          'toolsets': [ 'target', ],
          'dependencies': [
            'd8_js2c#host',
          ],
        }],
        ['(OS=="linux" or OS=="mac" or OS=="freebsd" or OS=="netbsd" \
           or OS=="openbsd" or OS=="solaris" or OS=="android" \
           or OS=="qnx" or OS=="aix" or OS=="os400")', {
             'sources': [ '<(V8_ROOT)/src/d8/d8-posix.cc', ]
           }],
        [ 'OS=="win"', {
          'sources': [ '<(V8_ROOT)/src/d8/d8-windows.cc', ]
        }],
        ['v8_enable_vtunejit==1', {
          'dependencies': [
            'v8vtune.gyp:v8_vtune',
          ],
        }],
        ['v8_enable_i18n_support==1', {
          'dependencies': [
            '<(icu_gyp_path):icui18n',
            '<(icu_gyp_path):icuuc',
          ],
        }],
        ['OS=="win" and v8_enable_i18n_support==1', {
          'dependencies': [
            '<(icu_gyp_path):icudata',
          ],
        }],
        # Avoid excessive LTO
        ['enable_lto=="true"', {
          'ldflags': [ '-fno-lto' ],
        }],
      ],
    },
  ],
}
