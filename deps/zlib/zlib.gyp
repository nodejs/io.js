# Copyright (c) 2009 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'ZLIB_ROOT': '.',
    'use_system_zlib%': 0,
    'arm_fpu%': '',
  },
  'conditions': [
    ['use_system_zlib==0', {
      'targets': [
        {
          'target_name': 'zlib_adler32_simd',
          'type': 'static_library',
          'conditions': [
            ['target_arch in "ia32 x64" and OS!="ios"', {
              'defines': [ 'ADLER32_SIMD_SSSE3' ],
              'conditions': [
                ['OS=="win"', {
                  'defines': [ 'X86_WINDOWS' ],
                },{
                  'defines': [ 'X86_NOT_WINDOWS' ],
                }],
                ['OS!="win" or clang==1', {
                  'cflags': [ '-mssse3' ],
                  'xcode_settings': {
                    'OTHER_CFLAGS': [ '-mssse3' ],
                  },
                  'msvs_settings': {
                    'VCCLCompilerTool': {
                      'AdditionalOptions': [ '-mssse3' ],
                    },
                  },
                }],
              ],
            }],
            ['arm_fpu=="neon"', {
              'defines': [ 'ADLER32_SIMD_NEON' ],
            }],
          ],
          'include_dirs': [ '<(ZLIB_ROOT)' ],
          'direct_dependent_settings': {
            'conditions': [
              ['target_arch in "ia32 x64" and OS!="ios"', {
                'defines': [ 'ADLER32_SIMD_SSSE3' ],
                'conditions': [
                  ['OS=="win"', {
                    'defines': [ 'X86_WINDOWS' ],
                  },{
                    'defines': [ 'X86_NOT_WINDOWS' ],
                  }],
                ],
              }],
              ['arm_fpu=="neon"', {
                'defines': [ 'ADLER32_SIMD_NEON' ],
              }],
            ],
            'include_dirs': [ '<(ZLIB_ROOT)' ],
          },
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(ZLIB_ROOT)/BUILD.gn" "\\"zlib_adler32_simd\\".*?sources = ")',
          ],
        }, # zlib_adler32_simd
        {
          'target_name': 'zlib_arm_crc32',
          'type': 'static_library',
          'conditions': [
            ['OS!="ios"', {
              'conditions': [
                ['OS!="win" and clang==0', {
                  'cflags': [ '-march=armv8-a+aes+crc' ],
                }],
                ['OS=="android"', {
                  'defines': [ 'ARMV8_OS_ANDROID' ],
                }],
                ['OS=="linux"', {
                  'defines': [ 'ARMV8_OS_LINUX' ],
                }],
                ['OS=="mac"', {
                  'defines': [ 'ARMV8_OS_MACOS' ],
                }],
                ['OS=="win"', {
                  'defines': [ 'ARMV8_OS_WINDOWS' ],
                }],
              ],
              'defines': [ 'CRC32_ARMV8_CRC32' ],
              'include_dirs': [ '<(ZLIB_ROOT)' ],
              'direct_dependent_settings': {
                'defines': [ 'CRC32_ARMV8_CRC32' ],
                'conditions': [
                  ['OS=="android"', {
                    'defines': [ 'ARMV8_OS_ANDROID' ],
                  }],
                  ['OS=="linux"', {
                    'defines': [ 'ARMV8_OS_LINUX' ],
                  }],
                  ['OS=="mac"', {
                    'defines': [ 'ARMV8_OS_MACOS' ],
                  }],
                  ['OS=="win"', {
                    'defines': [ 'ARMV8_OS_WINDOWS' ],
                  }],
                ],
                'include_dirs': [ '<(ZLIB_ROOT)' ],
              },
              'sources': [
                '<!@pymod_do_main(GN-scraper "<(ZLIB_ROOT)/BUILD.gn" "\\"zlib_arm_crc32\\".*?sources = ")',
              ],
            }],
          ],
        }, # zlib_arm_crc32
        # {
        #   'target_name': 'zlib_crc32_simd',
        #   'type': 'static_library',
        #   'conditions': [
        #     ['OS!="win" or clang==1', {
        #       'cflags': [
        #         '-msse4.2',
        #         '-mpclmul',
        #       ],
        #       'xcode_settings': {
        #         'OTHER_CFLAGS': [
        #           '-msse4.2',
        #           '-mpclmul',
        #         ],
        #       },
        #     }]
        #   ],
        #   'defines': [ 'CRC32_SIMD_SSE42_PCLMUL' ],
        #   'include_dirs': [ '<(ZLIB_ROOT)' ],
        #   'direct_dependent_settings': {
        #     'defines': [ 'CRC32_SIMD_SSE42_PCLMUL' ],
        #     'include_dirs': [ '<(ZLIB_ROOT)' ],
        #   },
        #   'sources': [
        #     '<!@pymod_do_main(GN-scraper "<(ZLIB_ROOT)/BUILD.gn" "\\"zlib_crc32_simd\\".*?sources = ")',
        #   ],
        # }, # zlib_crc32_simd
        {
          'target_name': 'zlib_inflate_chunk_simd',
          'type': 'static_library',
          'conditions': [
            ['target_arch in "ia32 x64" and OS!="ios"', {
              'defines': [ 'INFLATE_CHUNK_SIMD_SSE2' ],
              'conditions': [
                ['target_arch=="x64"', {
                  'defines': [ 'INFLATE_CHUNK_READ_64LE' ],
                }],
              ],
            }],
            ['arm_fpu=="neon"', {
              'defines': [ 'INFLATE_CHUNK_SIMD_NEON' ],
              'conditions': [
                ['target_arch=="arm64"', {
                  'defines': [ 'INFLATE_CHUNK_READ_64LE' ],
                }],
              ],
            }],
          ],
          'include_dirs': [ '<(ZLIB_ROOT)' ],
          'direct_dependent_settings': {
            'conditions': [
              ['target_arch in "ia32 x64" and OS!="ios"', {
                'defines': [ 'INFLATE_CHUNK_SIMD_SSE2' ],
              }],
              ['arm_fpu=="neon"', {
                'defines': [ 'INFLATE_CHUNK_SIMD_NEON' ],
              }],
            ],
            'include_dirs': [ '<(ZLIB_ROOT)' ],
          },
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(ZLIB_ROOT)/BUILD.gn" "\\"zlib_inflate_chunk_simd\\".*?sources = ")',
          ],
        }, # zlib_inflate_chunk_simd
        {
          'target_name': 'zlib',
          'type': 'static_library',
          'sources': [
            '<!@pymod_do_main(GN-scraper "<(ZLIB_ROOT)/BUILD.gn" "\\"zlib\\".*?sources = ")',
          ],
          'include_dirs': [ '<(ZLIB_ROOT)' ],
          'direct_dependent_settings': {
            'include_dirs': [ '<(ZLIB_ROOT)' ],
          },
          'conditions': [
            ['OS!="win"', {
              'cflags!': [ '-ansi' ],
              'cflags': [ '-Wno-implicit-fallthrough' ],
              'defines': [ 'HAVE_HIDDEN' ],
            }, {
              'defines': [ 'ZLIB_DLL' ]
            }],
            ['OS=="mac" or OS=="ios" or OS=="freebsd" or OS=="android"', {
              # Mac, Android and the BSDs don't have fopen64, ftello64, or
              # fseeko64. We use fopen, ftell, and fseek instead on these
              # systems.
              'defines': [
                'USE_FILE32API'
              ],
            }],
            # Incorporate optimizations where possible.
            ['(target_arch in "ia32 x64" and OS!="ios") or arm_fpu=="neon"', {
              'dependencies': [ 'zlib_inflate_chunk_simd' ],
              'sources': [ '<(ZLIB_ROOT)/slide_hash_simd.h' ]
            }, {
              'defines': [ 'CPU_NO_SIMD' ],
              'sources': [ '<(ZLIB_ROOT)/inflate.c' ],
            }],
            ['target_arch in "ia32 x64" and OS!="ios"', {
              'dependencies': [
                'zlib_adler32_simd',
                # Disabled due to memory corruption.
                # See https://github.com/nodejs/node/issues/45268.
                # 'zlib_crc32_simd',
              ],
              'defines': [ 'DEFLATE_SLIDE_HASH_SSE2' ],
              'conditions': [
                ['target_arch=="x64"', {
                  'defines': [ 'INFLATE_CHUNK_READ_64LE' ],
                }],
              ],
            }],
            ['arm_fpu=="neon"', {
              'defines': [
                '__ARM_NEON__',
                'DEFLATE_SLIDE_HASH_NEON',
              ],
              'conditions': [
                ['OS=="win"', {
                  'defines': [ 'ARMV8_OS_WINDOWS' ],
                }, {
                  'conditions': [
                    ['OS!="ios"', {
                      'dependencies': [
                        'zlib_adler32_simd',
                        'zlib_arm_crc32',
                      ],
                    }],
                  ],
                }],
                ['target_arch=="arm64"', {
                  'defines': [ 'INFLATE_CHUNK_READ_64LE' ],
                }],
              ],
            }],
          ],
        },
      ],
    }, {
      'targets': [
        {
          'target_name': 'zlib',
          'type': 'static_library',
          'direct_dependent_settings': {
            'defines': [
              'USE_SYSTEM_ZLIB',
            ],
          },
          'defines': [
            'USE_SYSTEM_ZLIB',
          ],
          'link_settings': {
            'libraries': [
              '-lz',
            ],
          },
        },
      ],
    }],
  ],
}
