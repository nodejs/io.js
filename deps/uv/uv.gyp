{
  'variables': {
    'conditions': [
      ['OS=="win"', {
        'shared_unix_defines': [ ],
      }, {
        'shared_unix_defines': [
          '_LARGEFILE_SOURCE',
          '_FILE_OFFSET_BITS=64',
        ],
      }],
      ['OS in "mac ios"', {
        'shared_mac_defines': [ '_DARWIN_USE_64_BIT_INODE=1' ],
      }, {
        'shared_mac_defines': [ ],
      }],
      ['OS=="zos"', {
        'shared_zos_defines': [
          '_UNIX03_THREADS',
          '_UNIX03_SOURCE',
          '_UNIX03_WITHDRAWN',
          '_OPEN_SYS_IF_EXT',
          '_OPEN_SYS_SOCK_EXT3',
          '_OPEN_SYS_SOCK_IPV6',
          '_OPEN_MSGQ_EXT',
          '_XOPEN_SOURCE_EXTENDED',
          '_ALL_SOURCE',
          '_LARGE_TIME_API',
          '_OPEN_SYS_FILE_EXT',
          '_AE_BIMODAL',
          'PATH_MAX=255'
        ],
      }, {
        'shared_zos_defines': [ ],
      }],
    ],
    'uv_sources_common': [
      'include/uv.h',
      'include/uv/tree.h',
      'include/uv/errno.h',
      'include/uv/threadpool.h',
      'include/uv/version.h',
      'src/fs-poll.c',
      'src/heap-inl.h',
      'src/idna.c',
      'src/idna.h',
      'src/inet.c',
      'src/queue.h',
      'src/random.c',
      'src/strscpy.c',
      'src/strscpy.h',
      'src/strtok.c',
      'src/strtok.h',
      'src/thread-common.c',
      'src/threadpool.c',
      'src/timer.c',
      'src/uv-data-getter-setters.c',
      'src/uv-common.c',
      'src/uv-common.h',
      'src/version.c',
    ],
    'uv_sources_win': [
      'include/uv/win.h',
      'src/win/async.c',
      'src/win/atomicops-inl.h',
      'src/win/core.c',
      'src/win/detect-wakeup.c',
      'src/win/dl.c',
      'src/win/error.c',
      'src/win/fs.c',
      'src/win/fs-event.c',
      'src/win/getaddrinfo.c',
      'src/win/getnameinfo.c',
      'src/win/handle.c',
      'src/win/handle-inl.h',
      'src/win/internal.h',
      'src/win/loop-watcher.c',
      'src/win/pipe.c',
      'src/win/thread.c',
      'src/win/poll.c',
      'src/win/process.c',
      'src/win/process-stdio.c',
      'src/win/req-inl.h',
      'src/win/signal.c',
      'src/win/snprintf.c',
      'src/win/stream.c',
      'src/win/stream-inl.h',
      'src/win/tcp.c',
      'src/win/tty.c',
      'src/win/udp.c',
      'src/win/util.c',
      'src/win/winapi.c',
      'src/win/winapi.h',
      'src/win/winsock.c',
      'src/win/winsock.h',
    ],
    'uv_sources_posix': [
      'include/uv/unix.h',
      'include/uv/linux.h',
      'include/uv/sunos.h',
      'include/uv/darwin.h',
      'include/uv/bsd.h',
      'include/uv/aix.h',
      'src/unix/async.c',
      'src/unix/core.c',
      'src/unix/dl.c',
      'src/unix/fs.c',
      'src/unix/getaddrinfo.c',
      'src/unix/getnameinfo.c',
      'src/unix/internal.h',
      'src/unix/loop.c',
      'src/unix/loop-watcher.c',
      'src/unix/pipe.c',
      'src/unix/poll.c',
      'src/unix/process.c',
      'src/unix/random-devurandom.c',
      'src/unix/signal.c',
      'src/unix/stream.c',
      'src/unix/tcp.c',
      'src/unix/thread.c',
      'src/unix/tty.c',
      'src/unix/udp.c',
    ],
    'uv_sources_apple': [
      'src/unix/darwin.c',
      'src/unix/fsevents.c',
      'src/unix/darwin-syscalls.h',
      'src/unix/darwin-proctitle.c',
      'src/unix/random-getentropy.c',
    ],
    'uv_sources_linux': [
      'src/unix/linux.c',
      'src/unix/procfs-exepath.c',
      'src/unix/random-getrandom.c',
      'src/unix/random-sysctl-linux.c',
    ],
    'uv_sources_android': [
      'src/unix/linux.c',
      'src/unix/procfs-exepath.c',
      'src/unix/random-getentropy.c',
      'src/unix/random-getrandom.c',
      'src/unix/random-sysctl-linux.c',
    ],
    'uv_sources_solaris': [
      'src/unix/no-proctitle.c',
      'src/unix/sunos.c',
    ],
    'uv_sources_bsd_common': [
      'src/unix/bsd-ifaddrs.c',
      'src/unix/kqueue.c',
    ],
  },

  'targets': [
    {
      'target_name': 'libuv',
      'toolsets': ['host', 'target'],
      'type': '<(uv_library)',
      'include_dirs': [
        'include',
        'src/',
      ],
      'defines': [
        '<@(shared_mac_defines)',
        '<@(shared_unix_defines)',
        '<@(shared_zos_defines)',
      ],
      'direct_dependent_settings': {
        'defines': [
          '<@(shared_mac_defines)',
          '<@(shared_unix_defines)',
          '<@(shared_zos_defines)',
        ],
        'include_dirs': [ 'include' ],
        'conditions': [
          ['OS == "linux"', {
            'defines': [ '_POSIX_C_SOURCE=200112' ],
          }],
        ],
      },
      'sources': [
        'common.gypi',
        '<@(uv_sources_common)',
      ],
      'xcode_settings': {
        'GCC_SYMBOLS_PRIVATE_EXTERN': 'YES',  # -fvisibility=hidden
        'WARNING_CFLAGS': [
          '-Wall',
          '-Wextra',
          '-Wno-unused-parameter',
          '-Wstrict-prototypes',
        ],
        'OTHER_CFLAGS': [ '-g', '--std=gnu89' ],
      },
      'conditions': [
        [ 'OS=="win"', {
          'defines': [
            '_WIN32_WINNT=0x0602',
            '_GNU_SOURCE',
          ],
          'sources': [
            '<@(uv_sources_win)',
          ],
          'link_settings': {
            'libraries': [
              '-ladvapi32',
              '-ldbghelp',
              '-lole32',
              '-liphlpapi',
              '-lpsapi',
              '-lshell32',
              '-luser32',
              '-luserenv',
              '-luuid',
              '-lws2_32'
            ],
          },
        }, { # Not Windows i.e. POSIX
          'sources': [
            '<@(uv_sources_posix)',
          ],
          'link_settings': {
            'libraries': [ '-lm' ],
            'conditions': [
              ['OS=="solaris"', {
                'ldflags': [ '-pthreads' ],
              }],
              [ 'OS=="zos" and uv_library=="shared_library"', {
                'ldflags': [ '-Wl,DLL' ],
              }],
              ['OS != "solaris" and OS != "android" and OS != "zos"', {
                'ldflags': [ '-pthread' ],
              }],
            ],
          },
          'conditions': [
            ['uv_library=="shared_library"', {
              'conditions': [
                ['OS=="zos"', {
                  'cflags': [ '-qexportall' ],
                }, {
                  'cflags': [ '-fPIC' ],
                }],
              ],
            }],
            ['uv_library=="shared_library" and OS!="mac" and OS!="zos"', {
              # This will cause gyp to set soname
              # Must correspond with UV_VERSION_MAJOR
              # in include/uv/version.h
              'product_extension': 'so.1',
            }],
          ],
        }],
        [ 'OS in "linux mac ios android zos"', {
          'sources': [ 'src/unix/proctitle.c' ],
        }],
        [ 'OS != "zos"', {
          'cflags': [
            '-fvisibility=hidden',
            '-g',
            '--std=gnu89',
            '-Wall',
            '-Wextra',
            '-Wno-unused-parameter',
            '-Wstrict-prototypes',
            '-fno-strict-aliasing',
          ],
        }],
        [ 'OS in "mac ios"', {
          'sources': [
            '<@(uv_sources_apple)',
          ],
          'defines': [
            '_DARWIN_USE_64_BIT_INODE=1',
            '_DARWIN_UNLIMITED_SELECT=1',
          ]
        }],
        [ 'OS=="linux"', {
          'defines': [ '_GNU_SOURCE' ],
          'sources': [
            '<@(uv_sources_linux)',
          ],
          'link_settings': {
            'libraries': [ '-ldl', '-lrt' ],
          },
        }],
        [ 'OS=="android"', {
          'defines': [
            '_GNU_SOURCE',
          ],
          'sources': [
            '<@(uv_sources_android)',
          ],
          'link_settings': {
            'libraries': [ '-ldl' ],
          },
        }],
        [ 'OS=="solaris"', {
          'sources': [
            '<@(uv_sources_solaris)',
          ],
          'defines': [
            '__EXTENSIONS__',
            '_XOPEN_SOURCE=500',
            '_REENTRANT',
          ],
          'link_settings': {
            'libraries': [
              '-lkstat',
              '-lnsl',
              '-lsendfile',
              '-lsocket',
            ],
          },
        }],
        [ 'OS=="aix"', {
          'variables': {
            'os_name': '<!(uname -s)',
          },
          'sources': [
            'src/unix/aix-common.c',
          ],
          'defines': [
            '_ALL_SOURCE',
            '_XOPEN_SOURCE=500',
            '_LINUX_SOURCE_COMPAT',
            '_THREAD_SAFE',
          ],
          'conditions': [
            [ '"<(os_name)"=="OS400"', {
              'sources': [
                'src/unix/ibmi.c',
                'src/unix/posix-poll.c',
                'src/unix/no-fsevents.c',
                'src/unix/no-proctitle.c',
              ],
            }, {
              'sources': [
                'src/unix/aix.c'
              ],
              'defines': [
                'HAVE_SYS_AHAFS_EVPRODS_H'
              ],
              'link_settings': {
                'libraries': [
                  '-lperfstat',
                ],
              },
            }],
          ]
        }],
        [ 'OS=="os400"', {
          'sources': [
            'src/unix/aix-common.c',
            'src/unix/ibmi.c',
            'src/unix/posix-poll.c',
            'src/unix/no-fsevents.c',
            'src/unix/no-proctitle.c',
          ],
          'defines': [
            '_ALL_SOURCE',
            '_XOPEN_SOURCE=500',
            '_LINUX_SOURCE_COMPAT',
            '_THREAD_SAFE',
          ],
        }],
        [ 'OS=="freebsd" or OS=="dragonflybsd"', {
          'sources': [ 'src/unix/freebsd.c' ],
        }],
        [ 'OS=="freebsd"', {
          'sources': [ 'src/unix/random-getrandom.c' ],
        }],
        [ 'OS=="openbsd"', {
          'sources': [
            'src/unix/openbsd.c',
            'src/unix/random-getentropy.c',
          ],
        }],
        [ 'OS=="netbsd"', {
          'link_settings': {
            'libraries': [ '-lkvm' ],
          },
          'sources': [ 'src/unix/netbsd.c' ],
        }],
        [ 'OS in "freebsd dragonflybsd openbsd netbsd".split()', {
          'sources': [
            'src/unix/posix-hrtime.c',
            'src/unix/bsd-proctitle.c'
          ],
        }],
        [ 'OS in "ios mac freebsd dragonflybsd openbsd netbsd".split()', {
          'sources': [
            '<@(uv_sources_bsd_common)',
          ],
        }],
        ['uv_library=="shared_library"', {
          'defines': [ 'BUILDING_UV_SHARED=1' ]
        }],
        ['OS=="zos"', {
          'sources': [
            'src/unix/os390.c',
            'src/unix/os390-syscalls.c'
          ]
        }],
      ]
    },
  ]
}
