# Copyright 2013 the V8 project authors. All rights reserved.
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
#       copyright notice, this list of conditions and the following
#       disclaimer in the documentation and/or other materials provided
#       with the distribution.
#     * Neither the name of Google Inc. nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Shared definitions for all V8-related targets.

{
  'variables': {
    'msvs_use_common_release': 0,
    'clang%': 0,
    'asan%': 0,
    'cfi_vptr%': 0,
    'lsan%': 0,
    'msan%': 0,
    'tsan%': 0,
    'ubsan%': 0,
    'ubsan_vptr%': 0,
    'has_valgrind%': 0,
    'coverage%': 0,
    'v8_target_arch%': '<(target_arch)',
    'v8_host_byteorder%': '<!("<(python)" -c "import sys; print(sys.byteorder)")',
    'force_dynamic_crt%': 0,

    # Setting 'v8_can_use_vfp32dregs' to 'true' will cause V8 to use the VFP
    # registers d16-d31 in the generated code, both in the snapshot and for the
    # ARM target. Leaving the default value of 'false' will avoid the use of
    # these registers in the snapshot and use CPU feature probing when running
    # on the target.
    'v8_can_use_vfp32dregs%': 'false',
    'arm_test_noprobe%': 'off',

    # Similar to vfp but on MIPS.
    'v8_can_use_fpu_instructions%': 'true',

    # Similar to the ARM hard float ABI but on MIPS.
    'v8_use_mips_abi_hardfloat%': 'true',

    # MIPS MSA support
    'mips_use_msa%': 0,

    # Print to stdout on Android.
    'v8_android_log_stdout%': 0,

    'v8_enable_backtrace%': 0,

    # Enable profiling support. Only required on Windows.
    'v8_enable_prof%': 0,

    # Chrome needs this definition unconditionally. For standalone V8 builds,
    # it's handled in gypfiles/standalone.gypi.
    'want_separate_host_toolset%': 1,

    # Toolset the shell binary should be compiled for. Possible values are
    # 'host' and 'target'.
    # The setting is ignored if want_separate_host_toolset is 0.
    'v8_toolset_for_shell%': 'target',

    'host_os%': '<(OS)',
    # For a shared library build, results in "libv8-<(soname_version).so".
    'soname_version%': '',

    # Override where to find binutils
    'binutils_dir%': '',

    'conditions': [
      ['OS=="linux" and host_arch=="x64"', {
        'binutils_dir%': 'third_party/binutils/Linux_x64/Release/bin',
      }],
      ['OS=="linux" and host_arch=="ia32"', {
        'binutils_dir%': 'third_party/binutils/Linux_ia32/Release/bin',
      }],
    ],

    # Indicates if gcmole tools are downloaded by a hook.
    'gcmole%': 0,
  },

  # [GYP] this needs to be outside of the top level 'variables'
  'conditions': [
    ['host_arch=="ia32" or host_arch=="x64" or \
      host_arch=="ppc" or host_arch=="ppc64" or \
      host_arch=="s390x" or \
      clang==1', {
      'variables': {
        'host_cxx_is_biarch%': 1,
       },
     }, {
      'variables': {
        'host_cxx_is_biarch%': 0,
      },
    }],
    ['target_arch=="ia32" or target_arch=="x64" or \
      target_arch=="ppc" or target_arch=="ppc64" or \
      target_arch=="s390x" or clang==1', {
      'variables': {
        'target_cxx_is_biarch%': 1,
       },
     }, {
      'variables': {
        'target_cxx_is_biarch%': 0,
      },
    }],
  ],
  'target_defaults': {
    'include_dirs': [
      '<(V8_ROOT)',
      '<(V8_ROOT)/include',
    ],
    'cflags!': ['-Wall', '-Wextra'],
    'conditions': [
      ['clang==0 and OS!="win"', {
        'cflags': [
          # In deps/v8/BUILD.gn: if (!is_clang && !is_win) { cflags += [...] }
          '-Wno-strict-overflow',
          '-Wno-return-type',
          '-Wno-int-in-bool-context',
          '-Wno-deprecated',
          '-Wno-stringop-overflow',
          '-Wno-stringop-overread',
          '-Wno-restrict',
          '-Wno-array-bounds',
          '-Wno-nonnull',
          '-Wno-dangling-pointer',
          # On by default in Clang and V8 requires it at least for arm64.
          '-flax-vector-conversions',
        ],
      }],
      ['clang==1 or OS!="win"', {
        'cflags_cc': [
          # In deps/v8/BUILD.gn: if (is_clang || !is_win) { cflags += [...] }
          '-Wno-invalid-offsetof',
        ],
        'xcode_settings': {
          # -Wno-invalid-offsetof
          'GCC_WARN_ABOUT_INVALID_OFFSETOF_MACRO': 'NO',
        },
        'msvs_settings': {
          'VCCLCompilerTool': {
            'AdditionalOptions': ['-Wno-invalid-offsetof'],
          },
        },
      }],
      ['v8_target_arch=="arm"', {
        'defines': [
          'V8_TARGET_ARCH_ARM',
        ],
        'conditions': [
          [ 'arm_version==7 or arm_version=="default"', {
            'defines': [
              'CAN_USE_ARMV7_INSTRUCTIONS',
            ],
          }],
          [ 'arm_fpu=="vfpv3-d16" or arm_fpu=="default"', {
            'defines': [
              'CAN_USE_VFP3_INSTRUCTIONS',
            ],
          }],
          [ 'arm_fpu=="vfpv3"', {
            'defines': [
              'CAN_USE_VFP3_INSTRUCTIONS',
              'CAN_USE_VFP32DREGS',
            ],
          }],
          [ 'arm_fpu=="neon"', {
            'defines': [
              'CAN_USE_VFP3_INSTRUCTIONS',
              'CAN_USE_VFP32DREGS',
              'CAN_USE_NEON',
            ],
          }],
          [ 'arm_test_noprobe=="on"', {
            'defines': [
              'ARM_TEST_NO_FEATURE_PROBE',
            ],
          }],
        ],
        'target_conditions': [
          ['_toolset=="host"', {
            'conditions': [
              ['v8_target_arch==host_arch', {
                # Host built with an Arm CXX compiler.
                'conditions': [
                  [ 'arm_version==7', {
                    'cflags': ['-march=armv7-a',],
                  }],
                  [ 'arm_version==7 or arm_version=="default"', {
                    'conditions': [
                      [ 'arm_fpu!="default"', {
                        'cflags': ['-mfpu=<(arm_fpu)',],
                      }],
                    ],
                  }],
                  [ 'arm_float_abi!="default"', {
                    'cflags': ['-mfloat-abi=<(arm_float_abi)',],
                  }],
                  [ 'arm_thumb==1', {
                    'cflags': ['-mthumb',],
                  }],
                  [ 'arm_thumb==0', {
                    'cflags': ['-marm',],
                  }],
                ],
              }, {
                # 'v8_target_arch!=host_arch'
                # Host not built with an Arm CXX compiler (simulator build).
                'conditions': [
                  [ 'arm_float_abi=="hard"', {
                    'defines': [
                      'USE_EABI_HARDFLOAT=1',
                    ],
                  }],
                  [ 'arm_float_abi=="softfp" or arm_float_abi=="default"', {
                    'defines': [
                      'USE_EABI_HARDFLOAT=0',
                    ],
                  }],
                ],
              }],
            ],
          }],  # _toolset=="host"
          ['_toolset=="target"', {
            'conditions': [
              ['v8_target_arch==target_arch', {
                # Target built with an Arm CXX compiler.
                'conditions': [
                  [ 'arm_version==7', {
                    'cflags': ['-march=armv7-a',],
                  }],
                  [ 'arm_version==7 or arm_version=="default"', {
                    'conditions': [
                      [ 'arm_fpu!="default"', {
                        'cflags': ['-mfpu=<(arm_fpu)',],
                      }],
                    ],
                  }],
                  [ 'arm_float_abi!="default"', {
                    'cflags': ['-mfloat-abi=<(arm_float_abi)',],
                  }],
                  [ 'arm_thumb==1', {
                    'cflags': ['-mthumb',],
                  }],
                  [ 'arm_thumb==0', {
                    'cflags': ['-marm',],
                  }],
                ],
              }, {
                # 'v8_target_arch!=target_arch'
                # Target not built with an Arm CXX compiler (simulator build).
                'conditions': [
                  [ 'arm_float_abi=="hard"', {
                    'defines': [
                      'USE_EABI_HARDFLOAT=1',
                    ],
                  }],
                  [ 'arm_float_abi=="softfp" or arm_float_abi=="default"', {
                    'defines': [
                      'USE_EABI_HARDFLOAT=0',
                    ],
                  }],
                ],
              }],
            ],
          }],  # _toolset=="target"
        ],
      }],  # v8_target_arch=="arm"
      ['v8_target_arch=="arm64"', {
        'defines': [
          'V8_TARGET_ARCH_ARM64',
        ],
        'conditions': [
          ['v8_control_flow_integrity==1', {
            'cflags': [ '-mbranch-protection=standard' ],
          }],
        ],
      }],
      ['v8_target_arch=="riscv64"', {
        'defines': [
          'V8_TARGET_ARCH_RISCV64',
          '__riscv_xlen=64',
          'CAN_USE_FPU_INSTRUCTIONS'
        ],
      }],
      ['v8_target_arch=="loong64"', {
        'defines': [
          'V8_TARGET_ARCH_LOONG64',
        ],
      }],
      ['v8_target_arch=="s390x"', {
        'defines': [
          'V8_TARGET_ARCH_S390',
        ],
        'cflags': [ '-ffp-contract=off' ],
        'conditions': [
          ['v8_target_arch=="s390x"', {
            'defines': [
              'V8_TARGET_ARCH_S390X',
            ],
          }],
          ['v8_host_byteorder=="little"', {
            'defines': [
              'V8_TARGET_ARCH_S390_LE_SIM',
            ],
          }, {
            'cflags': [ '-march=z196' ],
          }],
          ],
      }],  # s390x
      ['v8_target_arch=="ppc" or v8_target_arch=="ppc64"', {
        'conditions': [
          ['v8_target_arch=="ppc"', {
            'defines': [
              'V8_TARGET_ARCH_PPC',
            ],
          }],
          ['v8_target_arch=="ppc64"', {
            'defines': [
              'V8_TARGET_ARCH_PPC64',
            ],
            'cflags': [
              '-ffp-contract=off',
            ],
          }],
          ['v8_host_byteorder=="little"', {
            'defines': [
              'V8_TARGET_ARCH_PPC_LE',
            ],
          }],
          ['v8_host_byteorder=="big"', {
            'defines': [
              'V8_TARGET_ARCH_PPC_BE',
            ],
            'conditions': [
              ['OS=="aix" or OS=="os400"', {
                # Work around AIX ceil, trunc and round oddities.
                'cflags': [ '-mcpu=power5+ -mfprnd' ],
              }],
              ['OS=="aix" or OS=="os400"', {
                # Work around AIX assembler popcntb bug.
                'cflags': [ '-mno-popcntb' ],
              }],
            ],
          }],
        ],
      }],  # ppc
      ['v8_target_arch=="ia32"', {
        'defines': [
          'V8_TARGET_ARCH_IA32',
        ],
      }],  # v8_target_arch=="ia32"
      ['v8_target_arch=="mips64" or v8_target_arch=="mips64el"', {
        'target_conditions': [
          ['_toolset=="target"', {
            'conditions': [
              ['v8_target_arch==target_arch', {
                # Target built with a Mips CXX compiler.
                'variables': {
                  'ldso_path%': '<!(/bin/echo -n $LDSO_PATH)',
                  'ld_r_path%': '<!(/bin/echo -n $LD_R_PATH)',
                },
                'conditions': [
                  ['ldso_path!=""', {
                    'ldflags': ['-Wl,--dynamic-linker=<(ldso_path)'],
                  }],
                  ['ld_r_path!=""', {
                    'ldflags': ['-Wl,--rpath=<(ld_r_path)'],
                  }],
                  [ 'clang==1', {
                    'cflags': ['-integrated-as'],
                  }],
                  ['OS!="mac"', {
                    'defines': ['_MIPS_TARGET_HW',],
                  }, {
                    'defines': ['_MIPS_TARGET_SIMULATOR',],
                  }],
                ],
              }, {
                'defines': ['_MIPS_TARGET_SIMULATOR',],
              }],
            ],
          }],  #'_toolset=="target"
          ['_toolset=="host"', {
            'conditions': [
              ['v8_target_arch==target_arch and OS!="mac"', {
                'defines': ['_MIPS_TARGET_HW',],
              }, {
                'defines': ['_MIPS_TARGET_SIMULATOR',],
              }],
            ],
          }],  #'_toolset=="host"
        ],
      }],
      ['v8_target_arch=="mips64el" or v8_target_arch=="mips64"', {
        'defines': [
          'V8_TARGET_ARCH_MIPS64',
        ],
        'conditions': [
          [ 'v8_can_use_fpu_instructions=="true"', {
            'defines': [
              'CAN_USE_FPU_INSTRUCTIONS',
            ],
          }],
          [ 'v8_host_byteorder=="little"', {
            'defines': [
              'V8_TARGET_ARCH_MIPS64_LE',
            ],
          }],
          [ 'v8_host_byteorder=="big"', {
            'defines': [
              'V8_TARGET_ARCH_MIPS64_BE',
            ],
          }],
          [ 'v8_use_mips_abi_hardfloat=="true"', {
            'defines': [
              '__mips_hard_float=1',
              'CAN_USE_FPU_INSTRUCTIONS',
            ],
          }, {
            'defines': [
              '__mips_soft_float=1'
            ],
          }],
         ],
        'target_conditions': [
          ['_toolset=="target"', {
            'conditions': [
              ['v8_target_arch==target_arch', {
                'conditions': [
                  ['v8_target_arch=="mips64el"', {
                    'cflags': ['-EL'],
                    'ldflags': ['-EL'],
                  }],
                  ['v8_target_arch=="mips64"', {
                    'cflags': ['-EB'],
                    'ldflags': ['-EB'],
                  }],
                  [ 'v8_use_mips_abi_hardfloat=="true"', {
                    'cflags': ['-mhard-float'],
                    'ldflags': ['-mhard-float'],
                  }, {
                    'cflags': ['-msoft-float'],
                    'ldflags': ['-msoft-float'],
                  }],
                  ['mips_arch_variant=="r6"', {
                    'defines': ['_MIPS_ARCH_MIPS64R6',],
                    'conditions': [
                      [ 'clang==0', {
                        'cflags': ['-Wa,-mips64r6'],
                      }],
                    ],
                    'cflags': ['-mips64r6', '-mabi=64'],
                    'ldflags': ['-mips64r6', '-mabi=64'],
                  }],
                  ['mips_arch_variant=="r6" and mips_use_msa==1', {
                    'defines': [ '_MIPS_MSA' ],
                  }],
                  ['mips_arch_variant=="r2"', {
                    'defines': ['_MIPS_ARCH_MIPS64R2',],
                    'conditions': [
                      [ 'clang==0', {
                        'cflags': ['-Wa,-mips64r2'],
                      }],
                    ],
                    'cflags': ['-mips64r2', '-mabi=64'],
                    'ldflags': ['-mips64r2', '-mabi=64'],
                  }],
                ],
              }, {
                # 'v8_target_arch!=target_arch'
                # Target not built with an MIPS CXX compiler (simulator build).
                'conditions': [
                  ['mips_arch_variant=="r6"', {
                    'defines': ['_MIPS_ARCH_MIPS64R6',],
                  }],
                  ['mips_arch_variant=="r6" and mips_use_msa==1', {
                    'defines': [ '_MIPS_MSA' ],
                  }],
                  ['mips_arch_variant=="r2"', {
                    'defines': ['_MIPS_ARCH_MIPS64R2',],
                  }],
                ],
              }],
            ],
          }],  #'_toolset=="target"
          ['_toolset=="host"', {
            'conditions': [
              ['mips_arch_variant=="r6"', {
                'defines': ['_MIPS_ARCH_MIPS64R6',],
              }],
              ['mips_arch_variant=="r6" and mips_use_msa==1', {
                'defines': [ '_MIPS_MSA' ],
              }],
              ['mips_arch_variant=="r2"', {
                'defines': ['_MIPS_ARCH_MIPS64R2',],
              }],
            ],
          }],  #'_toolset=="host"
        ],
      }],  # v8_target_arch=="mips64el"
      ['v8_target_arch=="x64"', {
        'defines': [
          'V8_TARGET_ARCH_X64',
        ],
        'xcode_settings': {
          'ARCHS': [ 'x86_64' ],
        },
        'msvs_settings': {
          'VCLinkerTool': {
            'StackReserveSize': '2097152',
          },
        },
        'msvs_configuration_platform': 'x64',
      }],  # v8_target_arch=="x64"
      ['OS=="win"', {
        'defines': [
          'WIN32',
          'NOMINMAX',  # Refs: https://chromium-review.googlesource.com/c/v8/v8/+/1456620
          '_WIN32_WINNT=0x0A00',  # Windows 10
          '_SILENCE_ALL_CXX20_DEPRECATION_WARNINGS',
        ],
        # 4351: VS 2005 and later are warning us that they've fixed a bug
        #       present in VS 2003 and earlier.
        'msvs_disabled_warnings': [4351],
        'msvs_configuration_attributes': {
          'CharacterSet': '1',
        },
      }],
      ['OS=="win" and v8_enable_prof==1', {
        'msvs_settings': {
          'VCLinkerTool': {
            'GenerateMapFile': 'true',
          },
        },
      }],
      ['OS=="android"', {
        'defines': [
          'V8_HAVE_TARGET_OS',
          'V8_TARGET_OS_ANDROID',
        ]
      }],
      ['OS=="ios"', {
        'defines': [
          'V8_HAVE_TARGET_OS',
          'V8_TARGET_OS_IOS',
        ]
      }],
      ['OS=="linux"', {
        'defines': [
          'V8_HAVE_TARGET_OS',
          'V8_TARGET_OS_LINUX',
        ]
      }],
      ['OS=="mac"', {
        'defines': [
          'V8_HAVE_TARGET_OS',
          'V8_TARGET_OS_MACOS',
        ]
      }],
      ['OS=="win"', {
        'defines': [
          'V8_HAVE_TARGET_OS',
          'V8_TARGET_OS_WIN',
        ]
      }],
      ['(OS=="linux" or OS=="freebsd" or OS=="openbsd" or OS=="solaris" \
         or OS=="netbsd" or OS=="mac" or OS=="android" or OS=="qnx") and \
        v8_target_arch=="ia32"', {
        'cflags': [
          '-msse2',
          '-mfpmath=sse',
          '-mmmx',  # Allows mmintrin.h for MMX intrinsics.
        ],
      }],
      ['(OS=="linux" or OS=="freebsd" or OS=="openbsd" or OS=="solaris" \
         or OS=="netbsd" or OS=="mac" or OS=="android" or OS=="qnx") and \
        (v8_target_arch=="arm" or v8_target_arch=="ia32" or \
         v8_target_arch=="ppc")', {
        'target_conditions': [
          ['_toolset=="host"', {
            'conditions': [
              ['host_cxx_is_biarch==1', {
                'conditions': [
                  ['host_arch=="s390x"', {
                    'cflags': [ '-m31' ],
                    'ldflags': [ '-m31' ]
                  },{
                   'cflags': [ '-m32' ],
                   'ldflags': [ '-m32' ]
                  }],
                ],
              }],
            ],
            'xcode_settings': {
              'ARCHS': [ 'i386' ],
            },
          }],
          ['_toolset=="target"', {
            'conditions': [
              ['target_cxx_is_biarch==1', {
                'conditions': [
                  ['host_arch=="s390x"', {
                    'cflags': [ '-m31' ],
                    'ldflags': [ '-m31' ]
                  },{
                   'cflags': [ '-m32' ],
                   'ldflags': [ '-m32' ],
                  }],
                ],
              }],
            ],
            'xcode_settings': {
              'ARCHS': [ 'i386' ],
            },
          }],
        ],
      }],
      ['(OS=="linux" or OS=="android") and \
        (v8_target_arch=="x64" or v8_target_arch=="arm64" or \
         v8_target_arch=="ppc64" or v8_target_arch=="s390x")', {
        'target_conditions': [
          ['_toolset=="host"', {
            'conditions': [
              ['host_cxx_is_biarch==1', {
                'cflags': [ '-m64' ],
                'ldflags': [ '-m64' ]
              }],
             ],
           }],
          ['_toolset=="target"', {
             'conditions': [
               ['target_cxx_is_biarch==1', {
                 'cflags': [ '-m64' ],
                 'ldflags': [ '-m64' ],
               }],
             ]
           }],
         ],
      }],
      ['OS=="android" and v8_android_log_stdout==1', {
        'defines': [
          'V8_ANDROID_LOG_STDOUT',
        ],
      }],
      ['OS=="solaris"', {
        'defines': [ '__C99FEATURES__=1' ],  # isinf() etc.
      }],
      ['OS=="freebsd" or OS=="openbsd"', {
        'cflags': [ '-I/usr/local/include' ],
      }],
      ['OS=="netbsd"', {
        'cflags': [ '-I/usr/pkg/include' ],
      }],
      ['OS=="aix" or OS=="os400"', {
        'defines': [
          # Support for malloc(0)
          '_LINUX_SOURCE_COMPAT=1',
          '__STDC_FORMAT_MACROS',
          '_ALL_SOURCE=1'],
        'conditions': [
          [ 'v8_target_arch=="ppc"', {
            'ldflags': [ '-Wl,-bmaxdata:0x60000000/dsa' ],
          }],
          [ 'v8_target_arch=="ppc64"', {
            'cflags': [ '-maix64', '-fdollars-in-identifiers', '-fno-extern-tls-init' ],
            'ldflags': [ '-maix64 -Wl,-bbigtoc' ],
          }],
        ],
      }],
    ],  # conditions
    'configurations': {
      'Debug': {
        'defines': [
          'DEBUG',
        ],
        'conditions': [
          ['OS=="linux" and v8_enable_backtrace==1', {
            # Support for backtrace_symbols.
            'ldflags': [ '-rdynamic' ],
          }],
          ['OS=="aix" or OS=="os400"', {
            'ldflags': [ '-Wl,-bbigtoc' ],
            'conditions': [
              ['v8_target_arch=="ppc64"', {
                'cflags': [ '-maix64 -mcmodel=large' ],
              }],
            ],
          }],
          ['OS=="android"', {
            'variables': {
              'android_full_debug%': 1,
            },
            'conditions': [
              ['android_full_debug==0', {
                # Disable full debug if we want a faster v8 in a debug build.
                # TODO(2304): pass DISABLE_DEBUG_ASSERT instead of hiding DEBUG.
                'defines!': [
                  'DEBUG',
                  'ENABLE_SLOW_DCHECKS',
                ],
              }],
            ],
          }],
          ['v8_optimized_debug==0', {
            'msvs_settings': {
              'VCCLCompilerTool': {
                'Optimization': '0',
                'conditions': [
                  ['component=="shared_library" or force_dynamic_crt==1', {
                    'RuntimeLibrary': '3',  # /MDd
                  }, {
                     'RuntimeLibrary': '1',  # /MTd
                   }],
                ],
              },
              'VCLinkerTool': {
                'LinkIncremental': '2',
              },
            },
            'variables': {
              'v8_enable_slow_dchecks%': 1,
            },
            'conditions': [
              ['OS=="linux" or OS=="freebsd" or OS=="openbsd" or OS=="netbsd" or \
            OS=="qnx" or OS=="aix" or OS=="os400"', {
                'cflags!': [
                  '-O3',
                  '-O2',
                  '-O1',
                  '-Os',
                ],
                'cflags': [
                  '-fdata-sections',
                  '-ffunction-sections',
                ],
              }],
              ['OS=="mac"', {
                'xcode_settings': {
                  'GCC_OPTIMIZATION_LEVEL': '0',  # -O0
                },
              }],
              ['v8_enable_slow_dchecks==1', {
                'defines': [
                  'ENABLE_SLOW_DCHECKS',
                ],
              }],
            ],
          }, {
            'msvs_settings': {
              'VCCLCompilerTool': {
                'Optimization': '2',
                'InlineFunctionExpansion': '2',
                'EnableIntrinsicFunctions': 'true',
                'FavorSizeOrSpeed': '0',
                'StringPooling': 'true',
                'BasicRuntimeChecks': '0',
                'conditions': [
                  ['component=="shared_library" or force_dynamic_crt==1', {
                    'RuntimeLibrary': '3',  #/MDd
                  }, {
                     'RuntimeLibrary': '1',  #/MTd
                   }],
                ],
              },
              'VCLinkerTool': {
                'LinkIncremental': '1',
                'OptimizeReferences': '2',
                'EnableCOMDATFolding': '2',
              },
            },
            'variables': {
              'v8_enable_slow_dchecks%': 0,
            },
            'conditions': [
              ['OS=="linux" or OS=="freebsd" or OS=="openbsd" or OS=="netbsd" or \
            OS=="qnx" or OS=="aix" or OS=="os400"', {
                'cflags!': [
                  '-O0',
                  '-O1',
                  '-Os',
                ],
                'cflags': [
                  '-fdata-sections',
                  '-ffunction-sections',
                ],
                'conditions': [
                  # Don't use -O3 with sanitizers.
                  ['asan==0 and msan==0 and lsan==0 \
                and tsan==0 and ubsan==0 and ubsan_vptr==0', {
                    'cflags': ['-O3'],
                    'cflags!': ['-O2'],
                  }, {
                     'cflags': ['-O2'],
                     'cflags!': ['-O3'],
                   }],
                ],
              }],
              ['OS=="mac"', {
                'xcode_settings': {
                  'GCC_OPTIMIZATION_LEVEL': '3',  # -O3
                },
              }],
              ['v8_enable_slow_dchecks==1', {
                'defines': [
                  'ENABLE_SLOW_DCHECKS',
                ],
              }],
            ],
          }],
          # Temporary refs: https://github.com/nodejs/node/pull/23801
          ['v8_enable_handle_zapping==1', {
            'defines': ['ENABLE_HANDLE_ZAPPING',],
          }],
        ],

      },  # DebugBaseCommon
      'Release': {
        'variables': {
          'v8_enable_slow_dchecks%': 0,
        },
         # Temporary refs: https://github.com/nodejs/node/pull/23801
        'defines!': ['ENABLE_HANDLE_ZAPPING',],
        'conditions': [
          ['OS=="linux" or OS=="freebsd" or OS=="openbsd" or OS=="netbsd" \
            or OS=="aix" or OS=="os400"', {
            'cflags!': [
              '-Os',
            ],
            'cflags': [
              '-fdata-sections',
              '-ffunction-sections',
            ],
            'conditions': [
              # Don't use -O3 with sanitizers.
              ['asan==0 and msan==0 and lsan==0 \
                and tsan==0 and ubsan==0 and ubsan_vptr==0', {
                'cflags': ['-O3'],
                'cflags!': ['-O2'],
              }, {
                'cflags': ['-O2'],
                'cflags!': ['-O3'],
              }],
            ],
          }],
          ['OS=="android"', {
            'cflags!': [
              '-O3',
              '-Os',
            ],
            'cflags': [
              '-fdata-sections',
              '-ffunction-sections',
              '-O2',
            ],
          }],
          ['OS=="mac"', {
            'xcode_settings': {
              'GCC_OPTIMIZATION_LEVEL': '3',  # -O3
            },
          }],  # OS=="mac"
          ['OS=="win"', {
            'msvs_settings': {
              'VCCLCompilerTool': {
                'Optimization': '2',
                'InlineFunctionExpansion': '2',
                'EnableIntrinsicFunctions': 'true',
                'FavorSizeOrSpeed': '0',
                'StringPooling': 'true',
                'conditions': [
                  ['component=="shared_library" or force_dynamic_crt==1', {
                    'RuntimeLibrary': '2',  #/MD
                  }, {
                    'RuntimeLibrary': '0',  #/MT
                  }],
                ],
              },
              'VCLinkerTool': {
                'LinkIncremental': '1',
                'OptimizeReferences': '2',
                'EnableCOMDATFolding': '2',
              },
            },
          }],  # OS=="win"
          ['v8_enable_slow_dchecks==1', {
            'defines': [
              'ENABLE_SLOW_DCHECKS',
            ],
          }],
        ],  # conditions
      },  # Release
    },  # configurations
    'msvs_disabled_warnings': [
      4129,  # unrecognized character escape sequence (torque-generated)
      4245,  # Conversion with signed/unsigned mismatch.
      4267,  # Conversion with possible loss of data.
      4324,  # Padding structure due to alignment.
      # 4351, # [refack] Old issue with array init.
      4355,  # 'this' used in base member initializer list
      4506,  # Benign "no definition for inline function"
      4661,  # no suitable definition provided for explicit template instantiation request
      4701,  # Potentially uninitialized local variable.
      4702,  # Unreachable code.
      4703,  # Potentially uninitialized local pointer variable.
      4709,  # Comma operator within array index expr (bugged).
      4714,  # Function marked forceinline not inlined.
      4715,  # Not all control paths return a value. (see https://crbug.com/v8/7658)
      4718,  # Recursive call has no side-effect.
      4723,  # https://crbug.com/v8/7771
      4724,  # https://crbug.com/v8/7771
      4800,  # Forcing value to bool.
    ],
  },  # target_defaults
}
