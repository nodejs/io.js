from __future__ import print_function

import json
import sys
import errno
import argparse
import os
import pprint
import re
import shlex
import subprocess
import shutil
import bz2
import io
from pathlib import Path

# If not run from node/, cd to node/.
os.chdir(Path(__file__).parent)

original_argv = sys.argv[1:]

# gcc and g++ as defaults matches what GYP's Makefile generator does,
# except on OS X.
CC = os.environ.get('CC', 'cc' if sys.platform == 'darwin' else 'gcc')
CXX = os.environ.get('CXX', 'c++' if sys.platform == 'darwin' else 'g++')

tools_path = Path('tools')

sys.path.insert(0, str(tools_path / 'gyp' / 'pylib'))
from gyp.common import GetFlavor
from packaging.version import Version

# imports in tools/configure.d
sys.path.insert(0, str(tools_path / 'configure.d'))
import nodedownload

# imports in tools/
sys.path.insert(0, 'tools')
import getmoduleversion
import getnapibuildversion
import getsharedopensslhasquic
from gyp_node import run_gyp
from utils import SearchFiles

# parse our options
parser = argparse.ArgumentParser()

valid_os = ('win', 'mac', 'solaris', 'freebsd', 'openbsd', 'linux',
            'android', 'aix', 'cloudabi', 'os400', 'ios')
valid_arch = ('arm', 'arm64', 'ia32', 'mips', 'mipsel', 'mips64el', 'ppc',
              'ppc64', 'x64', 'x86', 'x86_64', 's390x', 'riscv64', 'loong64')
valid_arm_float_abi = ('soft', 'softfp', 'hard')
valid_arm_fpu = ('vfp', 'vfpv3', 'vfpv3-d16', 'neon')
valid_mips_arch = ('loongson', 'r1', 'r2', 'r6', 'rx')
valid_mips_fpu = ('fp32', 'fp64', 'fpxx')
valid_mips_float_abi = ('soft', 'hard')
valid_intl_modes = ('none', 'small-icu', 'full-icu', 'system-icu')
icu_versions = json.loads((tools_path / 'icu' / 'icu_versions.json').read_text(encoding='utf-8'))
maglev_enabled_architectures = ('x64', 'arm', 'arm64')

# builtins may be removed later if they have been disabled by options
shareable_builtins = {'cjs_module_lexer/lexer': 'deps/cjs-module-lexer/lexer.js',
                     'cjs_module_lexer/dist/lexer': 'deps/cjs-module-lexer/dist/lexer.js',
                     'undici/undici': 'deps/undici/undici.js',
                     'amaro/dist/index': 'deps/amaro/dist/index.js'
}

# create option groups
shared_optgroup = parser.add_argument_group("Shared libraries",
    "Flags that allows you to control whether you want to build against "
    "built-in dependencies or its shared representations. If necessary, "
    "provide multiple libraries with comma.")
static_optgroup = parser.add_argument_group("Static libraries",
    "Flags that allows you to control whether you want to build against "
    "additional static libraries.")
intl_optgroup = parser.add_argument_group("Internationalization",
    "Flags that lets you enable i18n features in Node.js as well as which "
    "library you want to build against.")
http2_optgroup = parser.add_argument_group("HTTP2",
    "Flags that allows you to control HTTP2 features in Node.js")
shared_builtin_optgroup = parser.add_argument_group("Shared builtins",
    "Flags that allows you to control whether you want to build against "
    "internal builtins or shared files.")

# Options should be in alphabetical order but keep --prefix at the top,
# that's arguably the one people will be looking for most.
parser.add_argument('--prefix',
    action='store',
    dest='prefix',
    default='/usr/local',
    help='select the install prefix [default: %(default)s]')

parser.add_argument('--coverage',
    action='store_true',
    dest='coverage',
    default=None,
    help='Build node with code coverage enabled')

parser.add_argument('--debug',
    action='store_true',
    dest='debug',
    default=None,
    help='also build debug build')

parser.add_argument('--debug-node',
    action='store_true',
    dest='debug_node',
    default=None,
    help='build the Node.js part of the binary with debugging symbols')

parser.add_argument('--dest-cpu',
    action='store',
    dest='dest_cpu',
    choices=valid_arch,
    help=f"CPU architecture to build for ({', '.join(valid_arch)})")

parser.add_argument('--cross-compiling',
    action='store_true',
    dest='cross_compiling',
    default=None,
    help='force build to be considered as cross compiled')
parser.add_argument('--no-cross-compiling',
    action='store_false',
    dest='cross_compiling',
    default=None,
    help='force build to be considered as NOT cross compiled')

parser.add_argument('--use-prefix-to-find-headers',
    action='store_true',
    dest='use_prefix_to_find_headers',
    default=None,
    help='use the prefix to look for pre-installed headers')

parser.add_argument('--dest-os',
    action='store',
    dest='dest_os',
    choices=valid_os,
    help=f"operating system to build for ({', '.join(valid_os)})")

parser.add_argument('--error-on-warn',
    action='store_true',
    dest='error_on_warn',
    default=None,
    help='Turn compiler warnings into errors for node core sources.')

parser.add_argument('--gdb',
    action='store_true',
    dest='gdb',
    default=None,
    help='add gdb support')

parser.add_argument('--no-ifaddrs',
    action='store_true',
    dest='no_ifaddrs',
    default=None,
    help='use on deprecated SunOS systems that do not support ifaddrs.h')

parser.add_argument('--disable-single-executable-application',
    action='store_true',
    dest='disable_single_executable_application',
    default=None,
    help='Disable Single Executable Application support.')

parser.add_argument("--fully-static",
    action="store_true",
    dest="fully_static",
    default=None,
    help="Generate an executable without external dynamic libraries. This "
         "will not work on macOS when using the default compilation environment")

parser.add_argument("--partly-static",
    action="store_true",
    dest="partly_static",
    default=None,
    help="Generate an executable with libgcc and libstdc++ libraries. This "
         "will not work on macOS when using the default compilation environment")

parser.add_argument("--enable-vtune-profiling",
    action="store_true",
    dest="enable_vtune_profiling",
    help="Enable profiling support for Intel VTune profiler to profile "
         "JavaScript code executed in Node.js. This feature is only available "
         "for x32, x86, and x64 architectures.")

parser.add_argument("--enable-pgo-generate",
    action="store_true",
    dest="enable_pgo_generate",
    default=None,
    help="Enable profiling with pgo of a binary. This feature is only available "
         "on linux with gcc and g++ 5.4.1 or newer.")

parser.add_argument("--enable-pgo-use",
    action="store_true",
    dest="enable_pgo_use",
    default=None,
    help="Enable use of the profile generated with --enable-pgo-generate. This "
         "feature is only available on linux with gcc and g++ 5.4.1 or newer.")

parser.add_argument("--enable-lto",
    action="store_true",
    dest="enable_lto",
    default=None,
    help="Enable compiling with lto of a binary. This feature is only available "
         "with gcc 5.4.1+ or clang 3.9.1+.")

parser.add_argument("--link-module",
    action="append",
    dest="linked_module",
    help="Path to a JS file to be bundled in the binary as a builtin. "
         "This module will be referenced by path without extension; "
         "e.g. /root/x/y.js will be referenced via require('root/x/y'). "
         "Can be used multiple times")

parser.add_argument("--openssl-conf-name",
    action="store",
    dest="openssl_conf_name",
    default='nodejs_conf',
    help="The OpenSSL config appname (config section name) used by Node.js")

parser.add_argument('--openssl-default-cipher-list',
    action='store',
    dest='openssl_default_cipher_list',
    help='Use the specified cipher list as the default cipher list')

parser.add_argument("--openssl-no-asm",
    action="store_true",
    dest="openssl_no_asm",
    default=None,
    help="Do not build optimized assembly for OpenSSL")

parser.add_argument('--openssl-is-fips',
    action='store_true',
    dest='openssl_is_fips',
    default=None,
    help='specifies that the OpenSSL library is FIPS compatible')

parser.add_argument('--openssl-use-def-ca-store',
    action='store_true',
    dest='use_openssl_ca_store',
    default=None,
    help='Use OpenSSL supplied CA store instead of compiled-in Mozilla CA copy.')

parser.add_argument('--openssl-system-ca-path',
    action='store',
    dest='openssl_system_ca_path',
    help='Use the specified path to system CA (PEM format) in addition to '
         'the OpenSSL supplied CA store or compiled-in Mozilla CA copy.')

parser.add_argument('--experimental-http-parser',
    action='store_true',
    dest='experimental_http_parser',
    default=None,
    help='(no-op)')

shared_optgroup.add_argument('--shared-http-parser',
    action='store_true',
    dest='shared_http_parser',
    default=None,
    help='link to a shared http_parser DLL instead of static linking')

shared_optgroup.add_argument('--shared-http-parser-includes',
    action='store',
    dest='shared_http_parser_includes',
    help='directory containing http_parser header files')

shared_optgroup.add_argument('--shared-http-parser-libname',
    action='store',
    dest='shared_http_parser_libname',
    default='http_parser',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-http-parser-libpath',
    action='store',
    dest='shared_http_parser_libpath',
    help='a directory to search for the shared http_parser DLL')

shared_optgroup.add_argument('--shared-libuv',
    action='store_true',
    dest='shared_libuv',
    default=None,
    help='link to a shared libuv DLL instead of static linking')

shared_optgroup.add_argument('--shared-libuv-includes',
    action='store',
    dest='shared_libuv_includes',
    help='directory containing libuv header files')

shared_optgroup.add_argument('--shared-libuv-libname',
    action='store',
    dest='shared_libuv_libname',
    default='uv',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-libuv-libpath',
    action='store',
    dest='shared_libuv_libpath',
    help='a directory to search for the shared libuv DLL')

shared_optgroup.add_argument('--shared-nghttp2',
    action='store_true',
    dest='shared_nghttp2',
    default=None,
    help='link to a shared nghttp2 DLL instead of static linking')

shared_optgroup.add_argument('--shared-nghttp2-includes',
    action='store',
    dest='shared_nghttp2_includes',
    help='directory containing nghttp2 header files')

shared_optgroup.add_argument('--shared-nghttp2-libname',
    action='store',
    dest='shared_nghttp2_libname',
    default='nghttp2',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-nghttp2-libpath',
    action='store',
    dest='shared_nghttp2_libpath',
    help='a directory to search for the shared nghttp2 DLLs')

shared_optgroup.add_argument('--shared-nghttp3',
    action='store_true',
    dest='shared_nghttp3',
    default=None,
    help='link to a shared nghttp3 DLL instead of static linking')

shared_optgroup.add_argument('--shared-nghttp3-includes',
    action='store',
    dest='shared_nghttp3_includes',
    help='directory containing nghttp3 header files')

shared_optgroup.add_argument('--shared-nghttp3-libname',
    action='store',
    dest='shared_nghttp3_libname',
    default='nghttp3',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-nghttp3-libpath',
    action='store',
    dest='shared_nghttp3_libpath',
    help='a directory to search for the shared nghttp3 DLLs')

shared_optgroup.add_argument('--shared-ngtcp2',
    action='store_true',
    dest='shared_ngtcp2',
    default=None,
    help='link to a shared ngtcp2 DLL instead of static linking')

shared_optgroup.add_argument('--shared-ngtcp2-includes',
    action='store',
    dest='shared_ngtcp2_includes',
    help='directory containing ngtcp2 header files')

shared_optgroup.add_argument('--shared-ngtcp2-libname',
    action='store',
    dest='shared_ngtcp2_libname',
    default='ngtcp2',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-ngtcp2-libpath',
    action='store',
    dest='shared_ngtcp2_libpath',
    help='a directory to search for the shared tcp2 DLLs')

shared_optgroup.add_argument('--shared-openssl',
    action='store_true',
    dest='shared_openssl',
    default=None,
    help='link to a shared OpenSSl DLL instead of static linking')

shared_optgroup.add_argument('--shared-openssl-includes',
    action='store',
    dest='shared_openssl_includes',
    help='directory containing OpenSSL header files')

shared_optgroup.add_argument('--shared-openssl-libname',
    action='store',
    dest='shared_openssl_libname',
    default='crypto,ssl',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-openssl-libpath',
    action='store',
    dest='shared_openssl_libpath',
    help='a directory to search for the shared OpenSSL DLLs')

shared_optgroup.add_argument('--shared-uvwasi',
    action='store_true',
    dest='shared_uvwasi',
    default=None,
    help='link to a shared uvwasi DLL instead of static linking')

shared_optgroup.add_argument('--shared-uvwasi-includes',
    action='store',
    dest='shared_uvwasi_includes',
    help='directory containing uvwasi header files')

shared_optgroup.add_argument('--shared-uvwasi-libname',
    action='store',
    dest='shared_uvwasi_libname',
    default='uvwasi',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-uvwasi-libpath',
    action='store',
    dest='shared_uvwasi_libpath',
    help='a directory to search for the shared uvwasi DLL')

shared_optgroup.add_argument('--shared-zlib',
    action='store_true',
    dest='shared_zlib',
    default=None,
    help='link to a shared zlib DLL instead of static linking')

shared_optgroup.add_argument('--shared-zlib-includes',
    action='store',
    dest='shared_zlib_includes',
    help='directory containing zlib header files')

shared_optgroup.add_argument('--shared-zlib-libname',
    action='store',
    dest='shared_zlib_libname',
    default='z',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-zlib-libpath',
    action='store',
    dest='shared_zlib_libpath',
    help='a directory to search for the shared zlib DLL')

shared_optgroup.add_argument('--shared-simdjson',
    action='store_true',
    dest='shared_simdjson',
    default=None,
    help='link to a shared simdjson DLL instead of static linking')

shared_optgroup.add_argument('--shared-simdjson-includes',
    action='store',
    dest='shared_simdjson_includes',
    help='directory containing simdjson header files')

shared_optgroup.add_argument('--shared-simdjson-libname',
    action='store',
    dest='shared_simdjson_libname',
    default='simdjson',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-simdjson-libpath',
    action='store',
    dest='shared_simdjson_libpath',
    help='a directory to search for the shared simdjson DLL')


shared_optgroup.add_argument('--shared-simdutf',
    action='store_true',
    dest='shared_simdutf',
    default=None,
    help='link to a shared simdutf DLL instead of static linking')

shared_optgroup.add_argument('--shared-simdutf-includes',
    action='store',
    dest='shared_simdutf_includes',
    help='directory containing simdutf header files')

shared_optgroup.add_argument('--shared-simdutf-libname',
    action='store',
    dest='shared_simdutf_libname',
    default='simdutf',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-simdutf-libpath',
    action='store',
    dest='shared_simdutf_libpath',
    help='a directory to search for the shared simdutf DLL')


shared_optgroup.add_argument('--shared-ada',
    action='store_true',
    dest='shared_ada',
    default=None,
    help='link to a shared ada DLL instead of static linking')

shared_optgroup.add_argument('--shared-ada-includes',
    action='store',
    dest='shared_ada_includes',
    help='directory containing ada header files')

shared_optgroup.add_argument('--shared-ada-libname',
    action='store',
    dest='shared_ada_libname',
    default='ada',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-ada-libpath',
    action='store',
    dest='shared_ada_libpath',
    help='a directory to search for the shared ada DLL')

shared_optgroup.add_argument('--shared-brotli',
    action='store_true',
    dest='shared_brotli',
    default=None,
    help='link to a shared brotli DLL instead of static linking')

shared_optgroup.add_argument('--shared-brotli-includes',
    action='store',
    dest='shared_brotli_includes',
    help='directory containing brotli header files')

shared_optgroup.add_argument('--shared-brotli-libname',
    action='store',
    dest='shared_brotli_libname',
    default='brotlidec,brotlienc',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-brotli-libpath',
    action='store',
    dest='shared_brotli_libpath',
    help='a directory to search for the shared brotli DLL')

shared_optgroup.add_argument('--shared-cares',
    action='store_true',
    dest='shared_cares',
    default=None,
    help='link to a shared cares DLL instead of static linking')

shared_optgroup.add_argument('--shared-cares-includes',
    action='store',
    dest='shared_cares_includes',
    help='directory containing cares header files')

shared_optgroup.add_argument('--shared-cares-libname',
    action='store',
    dest='shared_cares_libname',
    default='cares',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-cares-libpath',
    action='store',
    dest='shared_cares_libpath',
    help='a directory to search for the shared cares DLL')

shared_optgroup.add_argument('--shared-sqlite',
    action='store_true',
    dest='shared_sqlite',
    default=None,
    help='link to a shared sqlite DLL instead of static linking')

shared_optgroup.add_argument('--shared-sqlite-includes',
    action='store',
    dest='shared_sqlite_includes',
    help='directory containing sqlite header files')

shared_optgroup.add_argument('--shared-sqlite-libname',
    action='store',
    dest='shared_sqlite_libname',
    default='sqlite3',
    help='alternative lib name to link to [default: %(default)s]')

shared_optgroup.add_argument('--shared-sqlite-libpath',
    action='store',
    dest='shared_sqlite_libpath',
    help='a directory to search for the shared sqlite DLL')


for builtin in shareable_builtins:
  builtin_id = 'shared_builtin_' + builtin + '_path'
  shared_builtin_optgroup.add_argument('--shared-builtin-' + builtin + '-path',
    action='store',
    dest='node_shared_builtin_' + builtin.replace('/', '_') + '_path',
    help='Path to shared file for ' + builtin + ' builtin. '
         'Will be used instead of bundled version at runtime')

static_optgroup.add_argument('--static-zoslib-gyp',
    action='store',
    dest='static_zoslib_gyp',
    help='path to zoslib.gyp file for includes and to link to static zoslib library')

parser.add_argument('--tag',
    action='store',
    dest='tag',
    help='custom build tag')

parser.add_argument('--release-urlbase',
    action='store',
    dest='release_urlbase',
    help='Provide a custom URL prefix for the `process.release` properties '
         '`sourceUrl` and `headersUrl`. When compiling a release build, this '
         'will default to https://nodejs.org/download/release/')

parser.add_argument('--enable-d8',
    action='store_true',
    dest='enable_d8',
    default=None,
    help=argparse.SUPPRESS)  # Unsupported, undocumented.

parser.add_argument('--enable-trace-maps',
    action='store_true',
    dest='trace_maps',
    default=None,
    help='Enable the --trace-maps flag in V8 (use at your own risk)')

parser.add_argument('--experimental-enable-pointer-compression',
    action='store_true',
    dest='enable_pointer_compression',
    default=None,
    help='[Experimental] Enable V8 pointer compression (limits max heap to 4GB and breaks ABI compatibility)')

parser.add_argument('--disable-shared-readonly-heap',
    action='store_true',
    dest='disable_shared_ro_heap',
    default=None,
    help='Disable the shared read-only heap feature in V8')

parser.add_argument('--v8-options',
    action='store',
    dest='v8_options',
    help='v8 options to pass, see `node --v8-options` for examples.')

parser.add_argument('--with-ossfuzz',
    action='store_true',
    dest='ossfuzz',
    default=None,
    help='Enables building of fuzzers. This command should be run in an OSS-Fuzz Docker image.')

parser.add_argument('--with-arm-float-abi',
    action='store',
    dest='arm_float_abi',
    choices=valid_arm_float_abi,
    help=f"specifies which floating-point ABI to use ({', '.join(valid_arm_float_abi)}).")

parser.add_argument('--with-arm-fpu',
    action='store',
    dest='arm_fpu',
    choices=valid_arm_fpu,
    help=f"ARM FPU mode ({', '.join(valid_arm_fpu)}) [default: %(default)s]")

parser.add_argument('--with-mips-arch-variant',
    action='store',
    dest='mips_arch_variant',
    default='r2',
    choices=valid_mips_arch,
    help=f"MIPS arch variant ({', '.join(valid_mips_arch)}) [default: %(default)s]")

parser.add_argument('--with-mips-fpu-mode',
    action='store',
    dest='mips_fpu_mode',
    default='fp32',
    choices=valid_mips_fpu,
    help=f"MIPS FPU mode ({', '.join(valid_mips_fpu)}) [default: %(default)s]")

parser.add_argument('--with-mips-float-abi',
    action='store',
    dest='mips_float_abi',
    default='hard',
    choices=valid_mips_float_abi,
    help=f"MIPS floating-point ABI ({', '.join(valid_mips_float_abi)}) [default: %(default)s]")

parser.add_argument('--use-largepages',
    action='store_true',
    dest='node_use_large_pages',
    default=None,
    help='This option has no effect. --use-largepages is now a runtime option.')

parser.add_argument('--use-largepages-script-lld',
    action='store_true',
    dest='node_use_large_pages_script_lld',
    default=None,
    help='This option has no effect. --use-largepages is now a runtime option.')

parser.add_argument('--use-section-ordering-file',
    action='store',
    dest='node_section_ordering_info',
    default='',
    help='Pass a section ordering file to the linker. This requires that ' +
         'Node.js be linked using the gold linker. The gold linker must have ' +
         'version 1.2 or greater.')

intl_optgroup.add_argument('--with-intl',
    action='store',
    dest='with_intl',
    default='full-icu',
    choices=valid_intl_modes,
    help=f"Intl mode (valid choices: {', '.join(valid_intl_modes)}) [default: %(default)s]")

intl_optgroup.add_argument('--without-intl',
    action='store_const',
    dest='with_intl',
    const='none',
    help='Disable Intl, same as --with-intl=none')

intl_optgroup.add_argument('--with-icu-path',
    action='store',
    dest='with_icu_path',
    help='Path to icu.gyp (ICU i18n, Chromium version only.)')

icu_default_locales='root,en'

intl_optgroup.add_argument('--with-icu-locales',
    action='store',
    dest='with_icu_locales',
    default=icu_default_locales,
    help='Comma-separated list of locales for "small-icu". "root" is assumed. '
        '[default: %(default)s]')

intl_optgroup.add_argument('--with-icu-source',
    action='store',
    dest='with_icu_source',
    help='Intl mode: optional local path to icu/ dir, or path/URL of '
        'the icu4c source archive. '
        f"v{icu_versions['minimum_icu']}.x or later recommended.")

intl_optgroup.add_argument('--with-icu-default-data-dir',
    action='store',
    dest='with_icu_default_data_dir',
    help='Path to the icuXXdt{lb}.dat file. If unspecified, ICU data will '
         'only be read if the NODE_ICU_DATA environment variable or the '
         '--icu-data-dir runtime argument is used. This option has effect '
         'only when Node.js is built with --with-intl=small-icu.')

parser.add_argument('--with-ltcg',
    action='store_true',
    dest='with_ltcg',
    default=None,
    help='Use Link Time Code Generation. This feature is only available on Windows.')

parser.add_argument('--write-snapshot-as-array-literals',
    action='store_true',
    dest='write_snapshot_as_array_literals',
    default=None,
    help='Write the snapshot data as array literals for readability.'
         'By default the snapshot data may be written as string literals on some '
         'platforms to speed up compilation.')

parser.add_argument('--without-node-snapshot',
    action='store_true',
    dest='without_node_snapshot',
    default=None,
    help='Turn off V8 snapshot integration. Currently experimental.')

parser.add_argument('--without-node-code-cache',
    action='store_true',
    dest='without_node_code_cache',
    default=None,
    help='Turn off V8 Code cache integration.')

intl_optgroup.add_argument('--download',
    action='store',
    dest='download_list',
    help=nodedownload.help())

intl_optgroup.add_argument('--download-path',
    action='store',
    dest='download_path',
    default='deps',
    help='Download directory [default: %(default)s]')

parser.add_argument('--debug-lib',
    action='store_true',
    dest='node_debug_lib',
    default=None,
    help='build lib with DCHECK macros')

http2_optgroup.add_argument('--debug-nghttp2',
    action='store_true',
    dest='debug_nghttp2',
    default=None,
    help='build nghttp2 with DEBUGBUILD (default is false)')

parser.add_argument('--without-amaro',
    action='store_true',
    dest='without_amaro',
    default=None,
    help='do not install the bundled Amaro (TypeScript utils)')

parser.add_argument('--without-npm',
    action='store_true',
    dest='without_npm',
    default=None,
    help='do not install the bundled npm (package manager)')

parser.add_argument('--without-corepack',
    action='store_true',
    dest='without_corepack',
    default=None,
    help='do not install the bundled Corepack')

# Dummy option for backwards compatibility
parser.add_argument('--without-report',
    action='store_true',
    dest='unused_without_report',
    default=None,
    help=argparse.SUPPRESS)

parser.add_argument('--with-snapshot',
    action='store_true',
    dest='unused_with_snapshot',
    default=None,
    help=argparse.SUPPRESS)

parser.add_argument('--without-snapshot',
    action='store_true',
    dest='unused_without_snapshot',
    default=None,
    help=argparse.SUPPRESS)

parser.add_argument('--without-siphash',
    action='store_true',
    dest='without_siphash',
    default=None,
    help=argparse.SUPPRESS)

# End dummy list.

parser.add_argument('--without-ssl',
    action='store_true',
    dest='without_ssl',
    default=None,
    help='build without SSL (disables crypto, https, inspector, etc.)')

parser.add_argument('--without-node-options',
    action='store_true',
    dest='without_node_options',
    default=None,
    help='build without NODE_OPTIONS support')

parser.add_argument('--ninja',
    action='store_true',
    dest='use_ninja',
    default=None,
    help='generate build files for use with Ninja')

parser.add_argument('--enable-asan',
    action='store_true',
    dest='enable_asan',
    default=None,
    help='compile for Address Sanitizer to find memory bugs')

parser.add_argument('--enable-ubsan',
    action='store_true',
    dest='enable_ubsan',
    default=None,
    help='compile for Undefined Behavior Sanitizer')

parser.add_argument('--enable-static',
    action='store_true',
    dest='enable_static',
    default=None,
    help='build as static library')

parser.add_argument('--no-browser-globals',
    action='store_true',
    dest='no_browser_globals',
    default=None,
    help='do not export browser globals like setTimeout, console, etc. ' +
         '(This mode is deprecated and not officially supported for regular ' +
         'applications)')

parser.add_argument('--without-inspector',
    action='store_true',
    dest='without_inspector',
    default=None,
    help='disable the V8 inspector protocol')

parser.add_argument('--shared',
    action='store_true',
    dest='shared',
    default=None,
    help='compile shared library for embedding node in another project. ' +
         '(This mode is not officially supported for regular applications)')

parser.add_argument('--libdir',
    action='store',
    dest='libdir',
    default='lib',
    help='a directory to install the shared library into relative to the '
         'prefix. This is a no-op if --shared is not specified. ' +
         '(This mode is not officially supported for regular applications)')

parser.add_argument('--without-v8-platform',
    action='store_true',
    dest='without_v8_platform',
    default=False,
    help='do not initialize v8 platform during node.js startup. ' +
         '(This mode is not officially supported for regular applications)')

parser.add_argument('--without-bundled-v8',
    action='store_true',
    dest='without_bundled_v8',
    default=False,
    help='do not use V8 includes from the bundled deps folder. ' +
         '(This mode is not officially supported for regular applications)')

parser.add_argument('--verbose',
    action='store_true',
    dest='verbose',
    default=False,
    help='get more output from this script')

parser.add_argument('--v8-non-optimized-debug',
    action='store_true',
    dest='v8_non_optimized_debug',
    default=False,
    help='compile V8 with minimal optimizations and with runtime checks')

parser.add_argument('--v8-with-dchecks',
    action='store_true',
    dest='v8_with_dchecks',
    default=False,
    help='compile V8 with debug checks and runtime debugging features enabled')

parser.add_argument('--v8-lite-mode',
    action='store_true',
    dest='v8_lite_mode',
    default=False,
    help='compile V8 in lite mode for constrained environments (lowers V8 '+
         'memory footprint, but also implies no just-in-time compilation ' +
         'support, thus much slower execution)')

parser.add_argument('--v8-enable-object-print',
    action='store_true',
    dest='v8_enable_object_print',
    default=True,
    help='compile V8 with auxiliary functions for native debuggers')

parser.add_argument('--v8-disable-object-print',
    action='store_true',
    dest='v8_disable_object_print',
    default=False,
    help='disable the V8 auxiliary functions for native debuggers')

parser.add_argument('--v8-enable-hugepage',
    action='store_true',
    dest='v8_enable_hugepage',
    default=None,
    help='Enable V8 transparent hugepage support. This feature is only '+
         'available on Linux platform.')

maglev_enabled_by_default_help = f"(Maglev is enabled by default on {','.join(maglev_enabled_architectures)})"

parser.add_argument('--v8-disable-maglev',
    action='store_true',
    dest='v8_disable_maglev',
    default=None,
    help=f"Disable V8's Maglev compiler. {maglev_enabled_by_default_help}")

parser.add_argument('--v8-enable-short-builtin-calls',
    action='store_true',
    dest='v8_enable_short_builtin_calls',
    default=None,
    help='Enable V8 short builtin calls support. This feature is enabled '+
         'on x86_64 platform by default.')

parser.add_argument('--v8-enable-snapshot-compression',
    action='store_true',
    dest='v8_enable_snapshot_compression',
    default=None,
    help='Enable the built-in snapshot compression in V8.')

parser.add_argument('--node-builtin-modules-path',
    action='store',
    dest='node_builtin_modules_path',
    default=False,
    help='node will load builtin modules from disk instead of from binary')

parser.add_argument('--node-snapshot-main',
    action='store',
    dest='node_snapshot_main',
    default=None,
    help='Run a file when building the embedded snapshot. Currently ' +
         'experimental.')

# Create compile_commands.json in out/Debug and out/Release.
parser.add_argument('-C',
    action='store_true',
    dest='compile_commands_json',
    default=None,
    help=argparse.SUPPRESS)

parser.add_argument('--clang-cl',
    action='store',
    dest='clang_cl',
    default=None,
    help='Configure for clang-cl on Windows. This flag sets the GYP "clang" ' +
         'variable to 1 and "llvm_version" to the specified value.')

(options, args) = parser.parse_known_args()

# Expand ~ in the install prefix now, it gets written to multiple files.
options.prefix = str(Path(options.prefix or '').expanduser())

# set up auto-download list
auto_downloads = nodedownload.parse(options.download_list)


def error(msg):
  prefix = '\033[1m\033[31mERROR\033[0m' if os.isatty(1) else 'ERROR'
  print(f'{prefix}: {msg}')
  sys.exit(1)

def warn(msg):
  warn.warned = True
  prefix = '\033[1m\033[93mWARNING\033[0m' if os.isatty(1) else 'WARNING'
  print(f'{prefix}: {msg}')

# track if warnings occurred
warn.warned = False

def info(msg):
  prefix = '\033[1m\033[32mINFO\033[0m' if os.isatty(1) else 'INFO'
  print(f'{prefix}: {msg}')

def print_verbose(x):
  if not options.verbose:
    return
  if isinstance(x, str):
    print(x)
  else:
    pprint.pprint(x, indent=2)

def b(value):
  """Returns the string 'true' if value is truthy, 'false' otherwise."""
  return 'true' if value else 'false'

def B(value):
  """Returns 1 if value is truthy, 0 otherwise."""
  return 1 if value else 0

def to_utf8(s):
  return s if isinstance(s, str) else s.decode("utf-8")

def pkg_config(pkg):
  """Run pkg-config on the specified package
  Returns ("-l flags", "-I flags", "-L flags", "version")
  otherwise (None, None, None, None)"""
  pkg_config = os.environ.get('PKG_CONFIG', 'pkg-config')
  args = []  # Print pkg-config warnings on first round.
  retval = []
  for flag in ['--libs-only-l', '--cflags-only-I',
               '--libs-only-L', '--modversion']:
    args += [flag]
    if isinstance(pkg, list):
      args += pkg
    else:
      args += [pkg]
    try:
      proc = subprocess.Popen(shlex.split(pkg_config) + args,
                              stdout=subprocess.PIPE)
      with proc:
        val = to_utf8(proc.communicate()[0]).strip()
    except OSError as e:
      if e.errno != errno.ENOENT:
        raise e  # Unexpected error.
      return (None, None, None, None)  # No pkg-config/pkgconf installed.
    retval.append(val)
    args = ['--silence-errors']
  return tuple(retval)


def try_check_compiler(cc, lang):
  try:
    proc = subprocess.Popen(shlex.split(cc) + ['-E', '-P', '-x', lang, '-'],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE)
  except OSError:
    return (False, False, '', '')

  with proc:
    proc.stdin.write(b'__clang__ __GNUC__ __GNUC_MINOR__ __GNUC_PATCHLEVEL__ '
                     b'__clang_major__ __clang_minor__ __clang_patchlevel__')

    if sys.platform == 'zos':
      values = (to_utf8(proc.communicate()[0]).split('\n')[-2].split() + ['0'] * 7)[0:7]
    else:
      values = (to_utf8(proc.communicate()[0]).split() + ['0'] * 7)[0:7]

  is_clang = values[0] == '1'
  gcc_version = tuple(map(int, values[1:1+3]))
  clang_version = tuple(map(int, values[4:4+3])) if is_clang else None

  return (True, is_clang, clang_version, gcc_version)


#
# The version of asm compiler is needed for building openssl asm files.
# See deps/openssl/openssl.gypi for detail.
# Commands and regular expressions to obtain its version number are taken from
# https://github.com/openssl/openssl/blob/OpenSSL_1_0_2-stable/crypto/sha/asm/sha512-x86_64.pl#L112-L129
#
def get_version_helper(cc, regexp):
  try:
    proc = subprocess.Popen(shlex.split(cc) + ['-v'], stdin=subprocess.PIPE,
                            stderr=subprocess.PIPE, stdout=subprocess.PIPE)
  except OSError:
    error('''No acceptable C compiler found!

       Please make sure you have a C compiler installed on your system and/or
       consider adjusting the CC environment variable if you installed
       it in a non-standard prefix.''')

  with proc:
    match = re.search(regexp, to_utf8(proc.communicate()[1]))

  return match.group(2) if match else '0.0'

def get_nasm_version(asm):
  try:
    proc = subprocess.Popen(shlex.split(asm) + ['-v'],
                            stdin=subprocess.PIPE, stderr=subprocess.PIPE,
                            stdout=subprocess.PIPE)
  except OSError:
    warn('''No acceptable ASM compiler found!
         Please make sure you have installed NASM from https://www.nasm.us
         and refer BUILDING.md.''')
    return '0.0'

  with proc:
    match = re.match(r"NASM version ([2-9]\.[0-9][0-9]+)",
                     to_utf8(proc.communicate()[0]))

  return match.group(1) if match else '0.0'

def get_llvm_version(cc):
  return get_version_helper(
    cc, r"(^(?:.+ )?clang version|based on LLVM) ([0-9]+\.[0-9]+)")

def get_xcode_version(cc):
  return get_version_helper(
    cc, r"(^Apple (?:clang|LLVM) version) ([0-9]+\.[0-9]+)")

def get_gas_version(cc):
  try:
    custom_env = os.environ.copy()
    custom_env["LC_ALL"] = "C"
    proc = subprocess.Popen(shlex.split(cc) + ['-Wa,-v', '-c', '-o',
                                               '/dev/null', '-x',
                                               'assembler',  '/dev/null'],
                            stdin=subprocess.PIPE, stderr=subprocess.PIPE,
                            stdout=subprocess.PIPE, env=custom_env)
  except OSError:
    error('''No acceptable C compiler found!

       Please make sure you have a C compiler installed on your system and/or
       consider adjusting the CC environment variable if you installed
       it in a non-standard prefix.''')

  with proc:
    gas_ret = to_utf8(proc.communicate()[1])

  match = re.match(r"GNU assembler version ([2-9]\.[0-9]+)", gas_ret)

  if match:
    return match.group(1)

  warn(f'Could not recognize `gas`: {gas_ret}')
  return '0.0'

# Note: Apple clang self-reports as clang 4.2.0 and gcc 4.2.1.  It passes
# the version check more by accident than anything else but a more rigorous
# check involves checking the build number against an allowlist.  I'm not
# quite prepared to go that far yet.
def check_compiler(o):
  if sys.platform == 'win32':
    if options.clang_cl:
      o['variables']['clang'] = 1
      o['variables']['llvm_version'] = options.clang_cl
    else:
      o['variables']['clang'] = 0
      o['variables']['llvm_version'] = '0.0'

    if not options.openssl_no_asm and options.dest_cpu in ('x86', 'x64'):
      nasm_version = get_nasm_version('nasm')
      o['variables']['nasm_version'] = nasm_version
      if nasm_version == '0.0':
        o['variables']['openssl_no_asm'] = 1
    return

  ok, is_clang, clang_version, gcc_version = try_check_compiler(CXX, 'c++')
  o['variables']['clang'] = B(is_clang)
  version_str = ".".join(map(str, clang_version if is_clang else gcc_version))
  print_verbose(f"Detected {'clang ' if is_clang else ''}C++ compiler (CXX={CXX}) version: {version_str}")
  if not ok:
    warn(f'failed to autodetect C++ compiler version (CXX={CXX})')
  elif clang_version < (8, 0, 0) if is_clang else gcc_version < (12, 2, 0):
    warn(f'C++ compiler (CXX={CXX}, {version_str}) too old, need g++ 12.2.0 or clang++ 8.0.0')

  ok, is_clang, clang_version, gcc_version = try_check_compiler(CC, 'c')
  version_str = ".".join(map(str, clang_version if is_clang else gcc_version))
  print_verbose(f"Detected {'clang ' if is_clang else ''}C compiler (CC={CC}) version: {version_str}")
  if not ok:
    warn(f'failed to autodetect C compiler version (CC={CC})')
  elif not is_clang and gcc_version < (4, 2, 0):
    # clang 3.2 is a little white lie because any clang version will probably
    # do for the C bits.  However, we might as well encourage people to upgrade
    # to a version that is not completely ancient.
    warn(f'C compiler (CC={CC}, {version_str}) too old, need gcc 4.2 or clang 3.2')

  o['variables']['llvm_version'] = get_llvm_version(CC) if is_clang else '0.0'

  # Need xcode_version or gas_version when openssl asm files are compiled.
  if options.without_ssl or options.openssl_no_asm or options.shared_openssl:
    return

  if is_clang:
    if sys.platform == 'darwin':
      o['variables']['xcode_version'] = get_xcode_version(CC)
  else:
    o['variables']['gas_version'] = get_gas_version(CC)


def cc_macros(cc=None):
  """Checks predefined macros using the C compiler command."""

  try:
    p = subprocess.Popen(shlex.split(cc or CC) + ['-dM', '-E', '-'],
                         stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
  except OSError:
    error('''No acceptable C compiler found!

       Please make sure you have a C compiler installed on your system and/or
       consider adjusting the CC environment variable if you installed
       it in a non-standard prefix.''')

  with p:
    p.stdin.write(b'\n')
    out = to_utf8(p.communicate()[0]).split('\n')

  k = {}
  for line in out:
    lst = shlex.split(line)
    if len(lst) > 2:
      key = lst[1]
      val = lst[2]
      k[key] = val
  return k


def is_arch_armv7():
  """Check for ARMv7 instructions"""
  cc_macros_cache = cc_macros()
  return cc_macros_cache.get('__ARM_ARCH') == '7'


def is_arch_armv6():
  """Check for ARMv6 instructions"""
  cc_macros_cache = cc_macros()
  return cc_macros_cache.get('__ARM_ARCH') == '6'


def is_arm_hard_float_abi():
  """Check for hardfloat or softfloat eabi on ARM"""
  # GCC versions 4.6 and above define __ARM_PCS or __ARM_PCS_VFP to specify
  # the Floating Point ABI used (PCS stands for Procedure Call Standard).
  # We use these as well as a couple of other defines to statically determine
  # what FP ABI used.

  return '__ARM_PCS_VFP' in cc_macros()


def host_arch_cc():
  """Host architecture check using the CC command."""

  if sys.platform.startswith('zos'):
    return 's390x'
  k = cc_macros(os.environ.get('CC_host'))

  matchup = {
    '__aarch64__' : 'arm64',
    '__arm__'     : 'arm',
    '__i386__'    : 'ia32',
    '__MIPSEL__'  : 'mipsel',
    '__mips__'    : 'mips',
    '__PPC64__'   : 'ppc64',
    '__PPC__'     : 'ppc64',
    '__x86_64__'  : 'x64',
    '__s390x__'   : 's390x',
    '__riscv'     : 'riscv',
    '__loongarch64': 'loong64',
  }

  rtn = 'ia32' # default

  for key, value in matchup.items():
    if k.get(key, 0) and k[key] != '0':
      rtn = value
      break

  if rtn == 'mipsel' and '_LP64' in k:
    rtn = 'mips64el'

  if rtn == 'riscv':
    if k['__riscv_xlen'] == '64':
      rtn = 'riscv64'
    else:
      rtn = 'riscv32'

  return rtn


def host_arch_win():
  """Host architecture check using environ vars (better way to do this?)"""

  observed_arch = os.environ.get('PROCESSOR_ARCHITECTURE', 'AMD64')
  arch = os.environ.get('PROCESSOR_ARCHITEW6432', observed_arch)

  matchup = {
    'AMD64'  : 'x64',
    'arm'    : 'arm',
    'mips'   : 'mips',
    'ARM64'  : 'arm64'
  }

  return matchup.get(arch, 'x64')

def set_configuration_variable(configs, name, release=None, debug=None):
  configs['Release'][name] = release
  configs['Debug'][name] = debug

def configure_arm(o):
  if options.arm_float_abi:
    arm_float_abi = options.arm_float_abi
  elif is_arm_hard_float_abi():
    arm_float_abi = 'hard'
  else:
    arm_float_abi = 'default'

  arm_fpu = 'vfp'

  if is_arch_armv7():
    arm_fpu = 'vfpv3'
    o['variables']['arm_version'] = '7'
  else:
    o['variables']['arm_version'] = '6' if is_arch_armv6() else 'default'

  o['variables']['arm_thumb'] = 0      # -marm
  o['variables']['arm_float_abi'] = arm_float_abi

  if options.dest_os == 'android':
    arm_fpu = 'vfpv3'
    o['variables']['arm_version'] = '7'

  o['variables']['arm_fpu'] = options.arm_fpu or arm_fpu


def configure_mips(o, target_arch):
  can_use_fpu_instructions = options.mips_float_abi != 'soft'
  o['variables']['v8_can_use_fpu_instructions'] = b(can_use_fpu_instructions)
  o['variables']['v8_use_mips_abi_hardfloat'] = b(can_use_fpu_instructions)
  o['variables']['mips_arch_variant'] = options.mips_arch_variant
  o['variables']['mips_fpu_mode'] = options.mips_fpu_mode
  host_byteorder = 'little' if target_arch in ('mipsel', 'mips64el') else 'big'
  o['variables']['v8_host_byteorder'] = host_byteorder

def configure_zos(o):
  o['variables']['node_static_zoslib'] = b(True)
  if options.static_zoslib_gyp:
    # Apply to all Node.js components for now
    o['variables']['zoslib_include_dir'] = Path(options.static_zoslib_gyp).parent / 'include'
    o['include_dirs'] += [o['variables']['zoslib_include_dir']]
  else:
    raise Exception('--static-zoslib-gyp=<path to zoslib.gyp file> is required.')

def clang_version_ge(version_checked):
  for compiler in [(CC, 'c'), (CXX, 'c++')]:
    _, is_clang, clang_version, _1 = (
      try_check_compiler(compiler[0], compiler[1])
    )
    if is_clang and clang_version >= version_checked:
      return True
  return False

def gcc_version_ge(version_checked):
  for compiler in [(CC, 'c'), (CXX, 'c++')]:
    _, is_clang, _1, gcc_version = (
      try_check_compiler(compiler[0], compiler[1])
    )
    if is_clang or gcc_version < version_checked:
      return False
  return True

def configure_node_lib_files(o):
  o['variables']['node_library_files'] = SearchFiles('lib', 'js')

def configure_node(o):
  if options.dest_os == 'android':
    o['variables']['OS'] = 'android'
  o['variables']['node_prefix'] = options.prefix
  o['variables']['node_install_npm'] = b(not options.without_npm)
  o['variables']['node_install_corepack'] = b(not options.without_corepack)
  o['variables']['node_use_amaro'] = b(not options.without_amaro)
  o['variables']['debug_node'] = b(options.debug_node)
  o['default_configuration'] = 'Debug' if options.debug else 'Release'
  o['variables']['error_on_warn'] = b(options.error_on_warn)
  o['variables']['use_prefix_to_find_headers'] = b(options.use_prefix_to_find_headers)

  host_arch = host_arch_win() if os.name == 'nt' else host_arch_cc()
  target_arch = options.dest_cpu or host_arch
  # ia32 is preferred by the build tools (GYP) over x86 even if we prefer the latter
  # the Makefile resets this to x86 afterward
  if target_arch == 'x86':
    target_arch = 'ia32'
  # x86_64 is common across linuxes, allow it as an alias for x64
  if target_arch == 'x86_64':
    target_arch = 'x64'
  o['variables']['host_arch'] = host_arch
  o['variables']['target_arch'] = target_arch
  o['variables']['node_byteorder'] = sys.byteorder

  cross_compiling = (options.cross_compiling
                     if options.cross_compiling is not None
                     else target_arch != host_arch)
  if cross_compiling:
    os.environ['GYP_CROSSCOMPILE'] = "1"
  if options.unused_without_snapshot:
    warn('building --without-snapshot is no longer possible')

  o['variables']['want_separate_host_toolset'] = int(cross_compiling)

  if target_arch == 'arm64':
    o['variables']['arm_fpu'] = options.arm_fpu or 'neon'

  if options.node_snapshot_main is not None:
    if options.shared:
      # This should be possible to fix, but we will need to refactor the
      # libnode target to avoid building it twice.
      error('--node-snapshot-main is incompatible with --shared')
    if options.without_node_snapshot:
      error('--node-snapshot-main is incompatible with ' +
            '--without-node-snapshot')
    if cross_compiling:
      error('--node-snapshot-main is incompatible with cross compilation')
    o['variables']['node_snapshot_main'] = options.node_snapshot_main

  if options.without_node_snapshot or options.node_builtin_modules_path:
    o['variables']['node_use_node_snapshot'] = 'false'
  else:
    o['variables']['node_use_node_snapshot'] = b(
      not cross_compiling and not options.shared)

  # Do not use code cache when Node.js is built for collecting coverage of itself, this allows more
  # precise coverage for the JS built-ins.
  if options.without_node_code_cache or options.without_node_snapshot or options.node_builtin_modules_path or options.coverage:
    o['variables']['node_use_node_code_cache'] = 'false'
  else:
    # TODO(refack): fix this when implementing embedded code-cache when cross-compiling.
    o['variables']['node_use_node_code_cache'] = b(
      not cross_compiling and not options.shared)

  if options.write_snapshot_as_array_literals is not None:
     o['variables']['node_write_snapshot_as_array_literals'] = b(options.write_snapshot_as_array_literals)
  else:
     o['variables']['node_write_snapshot_as_array_literals'] = b(flavor != 'mac' and flavor != 'linux')

  if target_arch == 'arm':
    configure_arm(o)
  elif target_arch in ('mips', 'mipsel', 'mips64el'):
    configure_mips(o, target_arch)
  elif sys.platform == 'zos':
    configure_zos(o)

  if flavor in ('aix', 'os400'):
    o['variables']['node_target_type'] = 'static_library'

  if target_arch in ('x86', 'x64', 'ia32', 'x32'):
    o['variables']['node_enable_v8_vtunejit'] = b(options.enable_vtune_profiling)
  elif options.enable_vtune_profiling:
    raise Exception(
       'The VTune profiler for JavaScript is only supported on x32, x86, and x64 '
       'architectures.')
  else:
    o['variables']['node_enable_v8_vtunejit'] = 'false'

  if flavor != 'linux' and (options.enable_pgo_generate or options.enable_pgo_use):
    raise Exception(
      'The pgo option is supported only on linux.')

  if flavor == 'linux':
    if options.enable_pgo_generate or options.enable_pgo_use:
      version_checked = (5, 4, 1)
      if not gcc_version_ge(version_checked):
        version_checked_str = ".".join(map(str, version_checked))
        raise Exception(
          'The options --enable-pgo-generate and --enable-pgo-use '
          f'are supported for gcc and gxx {version_checked_str} or newer only.')

    if options.enable_pgo_generate and options.enable_pgo_use:
      raise Exception(
        'Only one of the --enable-pgo-generate or --enable-pgo-use options '
        'can be specified at a time. You would like to use '
        '--enable-pgo-generate first, profile node, and then recompile '
        'with --enable-pgo-use')

  o['variables']['enable_pgo_generate'] = b(options.enable_pgo_generate)
  o['variables']['enable_pgo_use']      = b(options.enable_pgo_use)

  if flavor == 'win' and (options.enable_lto):
    raise Exception(
      'Use Link Time Code Generation instead.')

  if options.enable_lto:
    gcc_version_checked = (5, 4, 1)
    clang_version_checked = (3, 9, 1)
    if not gcc_version_ge(gcc_version_checked) and not clang_version_ge(clang_version_checked):
      gcc_version_checked_str = ".".join(map(str, gcc_version_checked))
      clang_version_checked_str = ".".join(map(str, clang_version_checked))
      raise Exception(
        f'The option --enable-lto is supported for gcc {gcc_version_checked_str}+'
        f'or clang {clang_version_checked_str}+ only.')

  o['variables']['enable_lto'] = b(options.enable_lto)

  if options.node_use_large_pages or options.node_use_large_pages_script_lld:
    warn('''The `--use-largepages` and `--use-largepages-script-lld` options
         have no effect during build time. Support for mapping to large pages is
         now a runtime option of Node.js. Run `node --use-largepages` or add
         `--use-largepages` to the `NODE_OPTIONS` environment variable once
         Node.js is built to enable mapping to large pages.''')

  if options.no_ifaddrs:
    o['defines'] += ['SUNOS_NO_IFADDRS']

  o['variables']['single_executable_application'] = b(not options.disable_single_executable_application)
  if options.disable_single_executable_application:
    o['defines'] += ['DISABLE_SINGLE_EXECUTABLE_APPLICATION']

  o['variables']['node_with_ltcg'] = b(options.with_ltcg)
  if flavor != 'win' and options.with_ltcg:
    raise Exception('Link Time Code Generation is only supported on Windows.')

  if options.tag:
    o['variables']['node_tag'] = '-' + options.tag
  else:
    o['variables']['node_tag'] = ''

  o['variables']['node_release_urlbase'] = options.release_urlbase or ''

  if options.v8_options:
    o['variables']['node_v8_options'] = options.v8_options.replace('"', '\\"')

  if options.enable_static:
    o['variables']['node_target_type'] = 'static_library'

  o['variables']['node_debug_lib'] = b(options.node_debug_lib)

  if options.debug_nghttp2:
    o['variables']['debug_nghttp2'] = 1
  else:
    o['variables']['debug_nghttp2'] = 'false'

  o['variables']['node_no_browser_globals'] = b(options.no_browser_globals)

  o['variables']['node_shared'] = b(options.shared)
  o['variables']['libdir'] = options.libdir
  node_module_version = getmoduleversion.get_version()

  if options.dest_os == 'android':
    shlib_suffix = 'so'
  elif sys.platform == 'darwin':
    shlib_suffix = '%s.dylib'
  elif sys.platform.startswith('aix'):
    shlib_suffix = '%s.a'
  elif sys.platform == 'os400':
    shlib_suffix = '%s.a'
  elif sys.platform.startswith('zos'):
    shlib_suffix = '%s.x'
  else:
    shlib_suffix = 'so.%s'
  if '%s' in shlib_suffix:
    shlib_suffix %= node_module_version

  o['variables']['node_module_version'] = int(node_module_version)
  o['variables']['shlib_suffix'] = shlib_suffix

  if options.linked_module:
    o['variables']['linked_module_files'] = options.linked_module

  o['variables']['asan'] = int(options.enable_asan or 0)
  o['variables']['ubsan'] = int(options.enable_ubsan or 0)

  if options.coverage:
    o['variables']['coverage'] = 'true'
  else:
    o['variables']['coverage'] = 'false'

  if options.shared:
    o['variables']['node_target_type'] = 'shared_library'
  elif options.enable_static:
    o['variables']['node_target_type'] = 'static_library'
  else:
    o['variables']['node_target_type'] = 'executable'

  if options.node_builtin_modules_path:
    print('Warning! Loading builtin modules from disk is for development')
    o['variables']['node_builtin_modules_path'] = options.node_builtin_modules_path

def configure_napi(output):
  version = getnapibuildversion.get_napi_version()
  output['variables']['napi_build_version'] = version

def configure_library(lib, output, pkgname=None):
  shared_lib = 'shared_' + lib
  output['variables']['node_' + shared_lib] = b(getattr(options, shared_lib))

  if getattr(options, shared_lib):
    (pkg_libs, pkg_cflags, pkg_libpath, _) = pkg_config(pkgname or lib)

    if options.__dict__[shared_lib + '_includes']:
      output['include_dirs'] += [options.__dict__[shared_lib + '_includes']]
    elif pkg_cflags:
      stripped_flags = [flag.strip() for flag in pkg_cflags.split('-I')]
      output['include_dirs'] += [flag for flag in stripped_flags if flag]

    # libpath needs to be provided ahead libraries
    if options.__dict__[shared_lib + '_libpath']:
      if flavor == 'win':
        if 'msvs_settings' not in output:
          output['msvs_settings'] = { 'VCLinkerTool': { 'AdditionalOptions': [] } }
        output['msvs_settings']['VCLinkerTool']['AdditionalOptions'] += [
          f"/LIBPATH:{options.__dict__[shared_lib + '_libpath']}"]
      else:
        output['libraries'] += [
            f"-L{options.__dict__[shared_lib + '_libpath']}"]
    elif pkg_libpath:
      output['libraries'] += [pkg_libpath]

    default_libs = getattr(options, shared_lib + '_libname')
    default_libs = [f'-l{l}' for l in default_libs.split(',')]

    if default_libs:
      output['libraries'] += default_libs
    elif pkg_libs:
      output['libraries'] += pkg_libs.split()


def configure_v8(o, configs):
  set_configuration_variable(configs, 'v8_enable_v8_checks', release=1, debug=0)

  o['variables']['v8_enable_webassembly'] = 0 if options.v8_lite_mode else 1
  o['variables']['v8_enable_javascript_promise_hooks'] = 1
  o['variables']['v8_enable_lite_mode'] = 1 if options.v8_lite_mode else 0
  o['variables']['v8_enable_gdbjit'] = 1 if options.gdb else 0
  o['variables']['v8_optimized_debug'] = 0 if options.v8_non_optimized_debug else 1
  o['variables']['dcheck_always_on'] = 1 if options.v8_with_dchecks else 0
  o['variables']['v8_enable_object_print'] = 0 if options.v8_disable_object_print else 1
  o['variables']['v8_random_seed'] = 0  # Use a random seed for hash tables.
  o['variables']['v8_promise_internal_field_count'] = 1 # Add internal field to promises for async hooks.
  o['variables']['v8_use_siphash'] = 0 if options.without_siphash else 1
  o['variables']['v8_enable_maglev'] = B(not options.v8_disable_maglev and
                                         o['variables']['target_arch'] in maglev_enabled_architectures)
  o['variables']['v8_enable_pointer_compression'] = 1 if options.enable_pointer_compression else 0
  o['variables']['v8_enable_sandbox'] = 1 if options.enable_pointer_compression else 0
  o['variables']['v8_enable_31bit_smis_on_64bit_arch'] = 1 if options.enable_pointer_compression else 0
  o['variables']['v8_enable_shared_ro_heap'] = 0 if options.enable_pointer_compression or options.disable_shared_ro_heap else 1
  o['variables']['v8_enable_extensible_ro_snapshot'] = 0
  o['variables']['v8_trace_maps'] = 1 if options.trace_maps else 0
  o['variables']['node_use_v8_platform'] = b(not options.without_v8_platform)
  o['variables']['node_use_bundled_v8'] = b(not options.without_bundled_v8)
  o['variables']['force_dynamic_crt'] = 1 if options.shared else 0
  o['variables']['node_enable_d8'] = b(options.enable_d8)
  if options.enable_d8:
    o['variables']['test_isolation_mode'] = 'noop'  # Needed by d8.gyp.
  if options.without_bundled_v8 and options.enable_d8:
    raise Exception('--enable-d8 is incompatible with --without-bundled-v8.')
  if options.static_zoslib_gyp:
    o['variables']['static_zoslib_gyp'] = options.static_zoslib_gyp
  if flavor != 'linux' and options.v8_enable_hugepage:
    raise Exception('--v8-enable-hugepage is supported only on linux.')
  o['variables']['v8_enable_hugepage'] = 1 if options.v8_enable_hugepage else 0
  if options.v8_enable_short_builtin_calls or o['variables']['target_arch'] == 'x64':
    o['variables']['v8_enable_short_builtin_calls'] = 1
  if options.v8_enable_snapshot_compression:
    o['variables']['v8_enable_snapshot_compression'] = 1
  if all(opt in sys.argv for opt in ['--v8-enable-object-print', '--v8-disable-object-print']):
    raise Exception(
        'Only one of the --v8-enable-object-print or --v8-disable-object-print options '
        'can be specified at a time.')

def configure_openssl(o):
  variables = o['variables']
  variables['node_use_openssl'] = b(not options.without_ssl)
  variables['node_shared_openssl'] = b(options.shared_openssl)
  variables['node_shared_ngtcp2'] = b(options.shared_ngtcp2)
  variables['node_shared_nghttp3'] = b(options.shared_nghttp3)
  variables['openssl_is_fips'] = b(options.openssl_is_fips)
  variables['node_fipsinstall'] = b(False)

  if options.openssl_no_asm:
    variables['openssl_no_asm'] = 1

  o['defines'] += ['NODE_OPENSSL_CONF_NAME=' + options.openssl_conf_name]

  if options.without_ssl:
    def without_ssl_error(option):
      error(f'--without-ssl is incompatible with {option}')
    if options.shared_openssl:
      without_ssl_error('--shared-openssl')
    if options.openssl_no_asm:
      without_ssl_error('--openssl-no-asm')
    if options.openssl_is_fips:
      without_ssl_error('--openssl-is-fips')
    if options.openssl_default_cipher_list:
      without_ssl_error('--openssl-default-cipher-list')
    return

  if options.use_openssl_ca_store:
    o['defines'] += ['NODE_OPENSSL_CERT_STORE']
  if options.openssl_system_ca_path:
    variables['openssl_system_ca_path'] = options.openssl_system_ca_path
  variables['node_without_node_options'] = b(options.without_node_options)
  if options.without_node_options:
      o['defines'] += ['NODE_WITHOUT_NODE_OPTIONS']
  if options.openssl_default_cipher_list:
    variables['openssl_default_cipher_list'] = \
            options.openssl_default_cipher_list

  if not options.shared_openssl and not options.openssl_no_asm:
    is_x86 = 'x64' in variables['target_arch'] or 'ia32' in variables['target_arch']

    # supported asm compiler for AVX2. See https://github.com/openssl/openssl/
    # blob/OpenSSL_1_1_0-stable/crypto/modes/asm/aesni-gcm-x86_64.pl#L52-L69
    openssl110_asm_supported = \
      ('gas_version' in variables and Version(variables['gas_version']) >= Version('2.23')) or \
      ('xcode_version' in variables and Version(variables['xcode_version']) >= Version('5.0')) or \
      ('llvm_version' in variables and Version(variables['llvm_version']) >= Version('3.3')) or \
      ('nasm_version' in variables and Version(variables['nasm_version']) >= Version('2.10'))

    if is_x86 and not openssl110_asm_supported:
      error('''Did not find a new enough assembler, install one or build with
       --openssl-no-asm.
       Please refer to BUILDING.md''')

  elif options.openssl_no_asm:
    warn('''--openssl-no-asm will result in binaries that do not take advantage
         of modern CPU cryptographic instructions and will therefore be slower.
         Please refer to BUILDING.md''')

  if options.openssl_no_asm and options.shared_openssl:
    error('--openssl-no-asm is incompatible with --shared-openssl')

  if options.openssl_is_fips:
    o['defines'] += ['OPENSSL_FIPS']

  if options.openssl_is_fips and not options.shared_openssl:
    variables['node_fipsinstall'] = b(True)

  if options.shared_openssl:
    has_quic = getsharedopensslhasquic.get_has_quic(options.__dict__['shared_openssl_includes'])
  else:
    has_quic = getsharedopensslhasquic.get_has_quic('deps/openssl/openssl/include')

  variables['openssl_quic'] = b(has_quic)
  if has_quic:
    o['defines'] += ['NODE_OPENSSL_HAS_QUIC']

  configure_library('openssl', o)


def configure_static(o):
  if options.fully_static or options.partly_static:
    if flavor == 'mac':
      warn("Generation of static executable will not work on macOS "
            "when using the default compilation environment")
      return

    if options.fully_static:
      o['libraries'] += ['-static']
    elif options.partly_static:
      o['libraries'] += ['-static-libgcc', '-static-libstdc++']
      if options.enable_asan:
        o['libraries'] += ['-static-libasan']


def write(filename, data):
  print_verbose(f'creating {filename}')
  with Path(filename).open(mode='w+', encoding='utf-8') as f:
    f.write(data)

do_not_edit = '# Do not edit. Generated by the configure script.\n'

def glob_to_var(dir_base, dir_sub, patch_dir):
  file_list = []
  dir_all = f'{dir_base}/{dir_sub}'
  files = os.walk(dir_all)
  for ent in files:
    (_, _1, files) = ent
    for file in files:
      if file.endswith(('.cpp', '.c', '.h')):
        # srcfile uses "slash" as dir separator as its output is consumed by gyp
        srcfile = f'{dir_sub}/{file}'
        if patch_dir:
          patchfile = Path(dir_base, patch_dir, file)
          if patchfile.is_file():
            srcfile = f'{patch_dir}/{file}'
            info(f'Using floating patch "{patchfile}" from "{dir_base}"')
        file_list.append(srcfile)
    break
  return file_list

def configure_intl(o):
  def icu_download(path):
    depFile = tools_path / 'icu' / 'current_ver.dep'
    icus = json.loads(depFile.read_text(encoding='utf-8'))
    # download ICU, if needed
    if not os.access(options.download_path, os.W_OK):
      error('''Cannot write to desired download path.
        Either create it or verify permissions.''')
    attemptdownload = nodedownload.candownload(auto_downloads, "icu")
    for icu in icus:
      url = icu['url']
      (expectHash, hashAlgo, allAlgos) = nodedownload.findHash(icu)
      if not expectHash:
        error(f'''Could not find a hash to verify ICU download.
          {depFile} may be incorrect.
          For the entry {url},
          Expected one of these keys: {' '.join(allAlgos)}''')
      local = url.split('/')[-1]
      targetfile = Path(options.download_path, local)
      if not targetfile.is_file():
        if attemptdownload:
          nodedownload.retrievefile(url, targetfile)
      else:
        print(f'Re-using existing {targetfile}')
      if targetfile.is_file():
        print(f'Checking file integrity with {hashAlgo}:\r')
        gotHash = nodedownload.checkHash(targetfile, hashAlgo)
        print(f'{hashAlgo}:      {gotHash}  {targetfile}')
        if expectHash == gotHash:
          return targetfile

        warn(f'Expected: {expectHash}      *MISMATCH*')
        warn(f'\n ** Corrupted ZIP? Delete {targetfile} to retry download.\n')
    return None
  icu_config = {
    'variables': {}
  }
  icu_config_name = 'icu_config.gypi'

  # write an empty file to start with
  write(icu_config_name, do_not_edit +
        pprint.pformat(icu_config, indent=2, width=1024) + '\n')

  # always set icu_small, node.gyp depends on it being defined.
  o['variables']['icu_small'] = b(False)

  # prevent data override
  o['defines'] += ['ICU_NO_USER_DATA_OVERRIDE']

  with_intl = options.with_intl
  with_icu_source = options.with_icu_source
  have_icu_path = bool(options.with_icu_path)
  if have_icu_path and with_intl != 'none':
    error('Cannot specify both --with-icu-path and --with-intl')
  elif have_icu_path:
    # Chromium .gyp mode: --with-icu-path
    o['variables']['v8_enable_i18n_support'] = 1
    # use the .gyp given
    o['variables']['icu_gyp_path'] = options.with_icu_path
    return

  # --with-intl=<with_intl>
  # set the default
  if with_intl in (None, 'none'):
    o['variables']['v8_enable_i18n_support'] = 0
    return  # no Intl

  if with_intl == 'small-icu':
    # small ICU (English only)
    o['variables']['v8_enable_i18n_support'] = 1
    o['variables']['icu_small'] = b(True)
    locs = set(options.with_icu_locales.split(','))
    locs.add('root')  # must have root
    o['variables']['icu_locales'] = ','.join(str(loc) for loc in sorted(locs))
    # We will check a bit later if we can use the canned deps/icu-small
    o['variables']['icu_default_data'] = options.with_icu_default_data_dir or ''
  elif with_intl == 'full-icu':
    # full ICU
    o['variables']['v8_enable_i18n_support'] = 1
  elif with_intl == 'system-icu':
    # ICU from pkg-config.
    o['variables']['v8_enable_i18n_support'] = 1
    pkgicu = pkg_config(['icu-i18n', 'icu-uc'])
    if not pkgicu[0]:
      error('''Could not load pkg-config data for "icu-i18n".
       See above errors or the README.md.''')
    (libs, cflags, libpath, icuversion) = pkgicu
    icu_ver_major = icuversion.split('.')[0]
    o['variables']['icu_ver_major'] = icu_ver_major
    if int(icu_ver_major) < icu_versions['minimum_icu']:
      error(f"icu4c v{icuversion} is too old, v{icu_versions['minimum_icu']}.x or later is required.")
    # libpath provides linker path which may contain spaces
    if libpath:
      o['libraries'] += [libpath]
    # safe to split, cannot contain spaces
    o['libraries'] += libs.split()
    if cflags:
      stripped_flags = [flag.strip() for flag in cflags.split('-I')]
      o['include_dirs'] += [flag for flag in stripped_flags if flag]
    # use the "system" .gyp
    o['variables']['icu_gyp_path'] = 'tools/icu/icu-system.gyp'
    return

  # this is just the 'deps' dir. Used for unpacking.
  icu_parent_path = 'deps'

  # The full path to the ICU source directory. Should not include './'.
  icu_deps_path = 'deps/icu'
  icu_full_path = icu_deps_path

  # icu-tmp is used to download and unpack the ICU tarball.
  icu_tmp_path = Path(icu_parent_path, 'icu-tmp')

  # canned ICU. see tools/icu/README.md to update.
  canned_icu_dir = 'deps/icu-small'

  # use the README to verify what the canned ICU is
  canned_icu_path = Path(canned_icu_dir)
  canned_is_full = (canned_icu_path / 'README-FULL-ICU.txt').is_file()
  canned_is_small = (canned_icu_path / 'README-SMALL-ICU.txt').is_file()
  if canned_is_small:
    warn(f'Ignoring {canned_icu_dir} - in-repo small icu is no longer supported.')

  # We can use 'deps/icu-small' - pre-canned ICU *iff*
  # - canned_is_full AND
  # - with_icu_source is unset (i.e. no other ICU was specified)
  #
  # This is *roughly* equivalent to
  # $ configure --with-intl=full-icu --with-icu-source=deps/icu-small
  # .. Except that we avoid copying icu-small over to deps/icu.
  # In this default case, deps/icu is ignored, although make clean will
  # still harmlessly remove deps/icu.

  if (not with_icu_source) and canned_is_full:
    # OK- we can use the canned ICU.
    icu_full_path = canned_icu_dir
    icu_config['variables']['icu_full_canned'] = 1
  # --with-icu-source processing
  # now, check that they didn't pass --with-icu-source=deps/icu
  elif with_icu_source and Path(icu_full_path).resolve() == Path(with_icu_source).resolve():
    warn(f'Ignoring redundant --with-icu-source={with_icu_source}')
    with_icu_source = None
  # if with_icu_source is still set, try to use it.
  if with_icu_source:
    if Path(icu_full_path).is_dir():
      print(f'Deleting old ICU source: {icu_full_path}')
      shutil.rmtree(icu_full_path)
    # now, what path was given?
    if Path(with_icu_source).is_dir():
      # it's a path. Copy it.
      print(f'{with_icu_source} -> {icu_full_path}')
      shutil.copytree(with_icu_source, icu_full_path)
    else:
      # could be file or URL.
      # Set up temporary area
      if Path(icu_tmp_path).is_dir():
        shutil.rmtree(icu_tmp_path)
      icu_tmp_path.mkdir()
      icu_tarball = None
      if Path(with_icu_source).is_file():
        # it's a file. Try to unpack it.
        icu_tarball = with_icu_source
      else:
        # Can we download it?
        local = icu_tmp_path / with_icu_source.split('/')[-1]  # local part
        icu_tarball = nodedownload.retrievefile(with_icu_source, local)
      # continue with "icu_tarball"
      nodedownload.unpack(icu_tarball, icu_tmp_path)
      # Did it unpack correctly? Should contain 'icu'
      tmp_icu = icu_tmp_path / 'icu'
      if tmp_icu.is_dir():
        tmp_icu.rename(icu_full_path)
        shutil.rmtree(icu_tmp_path)
      else:
        shutil.rmtree(icu_tmp_path)
        error(f'--with-icu-source={with_icu_source} did not result in an "icu" dir.')

  # ICU mode. (icu-generic.gyp)
  o['variables']['icu_gyp_path'] = 'tools/icu/icu-generic.gyp'
  # ICU source dir relative to tools/icu (for .gyp file)
  o['variables']['icu_path'] = icu_full_path
  if not Path(icu_full_path).is_dir():
    # can we download (or find) a zipfile?
    localzip = icu_download(icu_full_path)
    if localzip:
      nodedownload.unpack(localzip, icu_parent_path)
    else:
      warn(f"* ECMA-402 (Intl) support didn't find ICU in {icu_full_path}..")
  if not Path(icu_full_path).is_dir():
    error(f'''Cannot build Intl without ICU in {icu_full_path}.
       Fix, or disable with "--with-intl=none"''')
  else:
    print_verbose(f'* Using ICU in {icu_full_path}')
  # Now, what version of ICU is it? We just need the "major", such as 54.
  # uvernum.h contains it as a #define.
  uvernum_h = Path(icu_full_path, 'source', 'common', 'unicode', 'uvernum.h')
  if not uvernum_h.is_file():
    error(f'Could not load {uvernum_h} - is ICU installed?')
  icu_ver_major = None
  matchVerExp = r'^\s*#define\s+U_ICU_VERSION_SHORT\s+"([^"]*)".*'
  match_version = re.compile(matchVerExp)
  with io.open(uvernum_h, encoding='utf8') as in_file:
    for line in in_file:
      m = match_version.match(line)
      if m:
        icu_ver_major = str(m.group(1))
  if not icu_ver_major:
    error(f'Could not read U_ICU_VERSION_SHORT version from {uvernum_h}')
  elif int(icu_ver_major) < icu_versions['minimum_icu']:
    error(f"icu4c v{icu_ver_major}.x is too old, v{icu_versions['minimum_icu']}.x or later is required.")
  icu_endianness = sys.byteorder[0]
  o['variables']['icu_ver_major'] = icu_ver_major
  o['variables']['icu_endianness'] = icu_endianness
  icu_data_file_l = f'icudt{icu_ver_major}l.dat' # LE filename
  icu_data_file = f'icudt{icu_ver_major}{icu_endianness}.dat'
  # relative to configure
  icu_data_path = Path(icu_full_path, 'source', 'data', 'in', icu_data_file_l) # LE
  compressed_data = f'{icu_data_path}.bz2'
  if not icu_data_path.is_file() and Path(compressed_data).is_file():
    # unpack. deps/icu is a temporary path
    if icu_tmp_path.is_dir():
      shutil.rmtree(icu_tmp_path)
    icu_tmp_path.mkdir()
    icu_data_path = icu_tmp_path / icu_data_file_l
    with icu_data_path.open(mode='wb') as outf:
        inf = bz2.BZ2File(compressed_data, 'rb')
        try:
          shutil.copyfileobj(inf, outf)
        finally:
          inf.close()
    # Now, proceed..

  # relative to dep..
  icu_data_in = Path('..', '..', icu_data_path)
  if not icu_data_path.is_file() and icu_endianness != 'l':
    # use host endianness
    icu_data_path = Path(icu_full_path, 'source', 'data', 'in', icu_data_file) # will be generated
  if not icu_data_path.is_file():
    # .. and we're not about to build it from .gyp!
    error(f'''ICU prebuilt data file {icu_data_path} does not exist.
       See the README.md.''')

  # this is the input '.dat' file to use .. icudt*.dat
  # may be little-endian if from a icu-project.org tarball
  o['variables']['icu_data_in'] = str(icu_data_in)

  # map from variable name to subdirs
  icu_src = {
    'stubdata': 'stubdata',
    'common': 'common',
    'i18n': 'i18n',
    'tools': 'tools/toolutil',
    'genccode': 'tools/genccode',
    'genrb': 'tools/genrb',
    'icupkg': 'tools/icupkg',
  }
  # this creates a variable icu_src_XXX for each of the subdirs
  # with a list of the src files to use
  for key, value in icu_src.items():
    var  = f'icu_src_{key}'
    path = f'../../{icu_full_path}/source/{value}'
    icu_config['variables'][var] = glob_to_var('tools/icu', path, f'patches/{icu_ver_major}/source/{value}')
  # calculate platform-specific genccode args
  # print("platform %s, flavor %s" % (sys.platform, flavor))
  # if sys.platform == 'darwin':
  #   shlib_suffix = '%s.dylib'
  # elif sys.platform.startswith('aix'):
  #   shlib_suffix = '%s.a'
  # else:
  #   shlib_suffix = 'so.%s'
  if flavor == 'win':
    icu_config['variables']['icu_asm_ext'] = 'obj'
    icu_config['variables']['icu_asm_opts'] = [ '-o ' ]
  elif with_intl == 'small-icu' or options.cross_compiling:
    icu_config['variables']['icu_asm_ext'] = 'c'
    icu_config['variables']['icu_asm_opts'] = []
  elif flavor == 'mac':
    icu_config['variables']['icu_asm_ext'] = 'S'
    icu_config['variables']['icu_asm_opts'] = [ '-a', 'gcc-darwin' ]
  elif sys.platform == 'os400':
    icu_config['variables']['icu_asm_ext'] = 'S'
    icu_config['variables']['icu_asm_opts'] = [ '-a', 'xlc' ]
  elif sys.platform.startswith('aix'):
    icu_config['variables']['icu_asm_ext'] = 'S'
    icu_config['variables']['icu_asm_opts'] = [ '-a', 'xlc' ]
  elif sys.platform == 'zos':
    icu_config['variables']['icu_asm_ext'] = 'S'
    icu_config['variables']['icu_asm_opts'] = [ '-a', 'zos' ]
  else:
    # assume GCC-compatible asm is OK
    icu_config['variables']['icu_asm_ext'] = 'S'
    icu_config['variables']['icu_asm_opts'] = [ '-a', 'gcc' ]

  # write updated icu_config.gypi with a bunch of paths
  write(icu_config_name, do_not_edit +
        pprint.pformat(icu_config, indent=2, width=1024) + '\n')
  return  # end of configure_intl

def configure_inspector(o):
  disable_inspector = (options.without_inspector or
                       options.without_ssl)
  o['variables']['v8_enable_inspector'] = 0 if disable_inspector else 1

def configure_section_file(o):
  try:
    proc = subprocess.Popen(['ld.gold'] + ['-v'], stdin = subprocess.PIPE,
                            stdout = subprocess.PIPE, stderr = subprocess.PIPE)
  except OSError:
    if options.node_section_ordering_info != "":
      warn('''No acceptable ld.gold linker found!''')
    return 0

  with proc:
    match = re.match(r"^GNU gold.*([0-9]+)\.([0-9]+)$",
                     proc.communicate()[0].decode("utf-8"))

  if match:
    gold_major_version = match.group(1)
    gold_minor_version = match.group(2)
    if int(gold_major_version) == 1 and int(gold_minor_version) <= 1:
      error('''GNU gold version must be greater than 1.2 in order to use section
            reordering''')

  if options.node_section_ordering_info != "":
    o['variables']['node_section_ordering_info'] = os.path.realpath(
      str(options.node_section_ordering_info))
  else:
    o['variables']['node_section_ordering_info'] = ""

def make_bin_override():
  if sys.platform == 'win32':
    raise Exception('make_bin_override should not be called on win32.')
  # If the system python is not the python we are running (which should be
  # python 3.8+), then create a directory with a symlink called `python` to our
  # sys.executable. This directory will be prefixed to the PATH, so that
  # other tools that shell out to `python` will use the appropriate python

  which_python = shutil.which('python')
  if (which_python and
      os.path.realpath(which_python) == os.path.realpath(sys.executable)):
    return

  bin_override = Path('out', 'tools', 'bin').resolve()
  try:
    bin_override.mkdir(parents=True)
  except OSError as e:
    if e.errno != errno.EEXIST:
      raise e

  python_link = bin_override / 'python'
  try:
    python_link.unlink()
  except OSError as e:
    if e.errno != errno.ENOENT:
      raise e
  os.symlink(sys.executable, python_link)

  # We need to set the environment right now so that when gyp (in run_gyp)
  # shells out, it finds the right python (specifically at
  # https://github.com/nodejs/node/blob/d82e107/deps/v8/gypfiles/toolchain.gypi#L43)
  os.environ['PATH'] = str(bin_override) + ':' + os.environ['PATH']

  return bin_override

output = {
  'variables': {},
  'include_dirs': [],
  'libraries': [],
  'defines': [],
  'cflags': [],
}
configurations = {
  'Release': { 'variables': {} },
  'Debug': { 'variables': {} },
}

# Print a warning when the compiler is too old.
check_compiler(output)

# determine the "flavor" (operating system) we're building for,
# leveraging gyp's GetFlavor function
flavor_params = {}
if options.dest_os:
  flavor_params['flavor'] = options.dest_os
flavor = GetFlavor(flavor_params)

configure_node(output)
configure_node_lib_files(output)
configure_napi(output)
configure_library('zlib', output)
configure_library('http_parser', output)
configure_library('libuv', output)
configure_library('ada', output)
configure_library('simdjson', output)
configure_library('simdutf', output)
configure_library('brotli', output, pkgname=['libbrotlidec', 'libbrotlienc'])
configure_library('cares', output, pkgname='libcares')
configure_library('nghttp2', output, pkgname='libnghttp2')
configure_library('nghttp3', output, pkgname='libnghttp3')
configure_library('ngtcp2', output, pkgname='libngtcp2')
configure_library('sqlite', output, pkgname='sqlite3')
configure_library('uvwasi', output, pkgname='libuvwasi')
configure_v8(output, configurations)
configure_openssl(output)
configure_intl(output)
configure_static(output)
configure_inspector(output)
configure_section_file(output)

# remove builtins that have been disabled
if options.without_amaro:
    del shareable_builtins['amaro/dist/index']

# configure shareable builtins
output['variables']['node_builtin_shareable_builtins'] = []
for builtin, value in shareable_builtins.items():
  builtin_id = 'node_shared_builtin_' + builtin.replace('/', '_') + '_path'
  if getattr(options, builtin_id):
    output['defines'] += [builtin_id.upper() + '=' + getattr(options, builtin_id)]
  else:
    output['variables']['node_builtin_shareable_builtins'] += [value]

# Forward OSS-Fuzz settings
output['variables']['ossfuzz'] = b(options.ossfuzz)

# variables should be a root level element,
# move everything else to target_defaults
variables = output['variables']
del output['variables']

# make_global_settings should be a root level element too
if 'make_global_settings' in output:
  make_global_settings = output['make_global_settings']
  del output['make_global_settings']
else:
  make_global_settings = False

# Add configurations to target defaults
output['configurations'] = configurations

output = {
  'variables': variables,
  'target_defaults': output,
}
if make_global_settings:
  output['make_global_settings'] = make_global_settings

print_verbose(output)

write('config.gypi', do_not_edit +
      pprint.pformat(output, indent=2, width=128) + '\n')

write('config.status', '#!/bin/sh\nset -x\nexec ./configure ' +
      ' '.join([shlex.quote(arg) for arg in original_argv]) + '\n')
Path('config.status').chmod(0o775)


config = {
  'BUILDTYPE': 'Debug' if options.debug else 'Release',
  'NODE_TARGET_TYPE': variables['node_target_type'],
}

# Not needed for trivial case. Useless when it's a win32 path.
if sys.executable != 'python' and ':\\' not in sys.executable:
  config['PYTHON'] = sys.executable

if options.prefix:
  config['PREFIX'] = options.prefix

if options.use_ninja:
  config['BUILD_WITH'] = 'ninja'

# On Windows there is another find.exe in C:\Windows\System32
if sys.platform == 'win32':
  config['FIND'] = '/usr/bin/find'

config_lines = ['='.join((k,v)) for k,v in config.items()]
# Add a blank string to get a blank line at the end.
config_lines += ['']
config_str = '\n'.join(config_lines)

# On Windows there's no reason to search for a different python binary.
bin_override = None if sys.platform == 'win32' else make_bin_override()
if bin_override:
  config_str = 'export PATH:=' + str(bin_override) + ':$(PATH)\n' + config_str

write('config.mk', do_not_edit + config_str)



gyp_args = ['--no-parallel', '-Dconfiguring_node=1']
gyp_args += ['-Dbuild_type=' + config['BUILDTYPE']]

# Remove the trailing .exe from the executable name, otherwise the python.exe
# would be rewrote as python_host.exe due to hack in GYP for supporting cross
# compilation on Windows.
# See https://github.com/nodejs/node/pull/32867 for related change.
python = sys.executable
if flavor == 'win' and python.lower().endswith('.exe'):
  python = python[:-4]
# Always set 'python' variable, otherwise environments that only have python3
# will fail to run python scripts.
gyp_args += ['-Dpython=' + python]

if options.use_ninja:
  gyp_args += ['-f', 'ninja-' + flavor]
elif flavor == 'win' and sys.platform != 'msys':
  gyp_args += ['-f', 'msvs', '-G', 'msvs_version=auto']
else:
  gyp_args += ['-f', 'make-' + flavor]

if options.compile_commands_json:
  gyp_args += ['-f', 'compile_commands_json']
  if sys.platform != 'win32':
    os.path.lexists('./compile_commands.json') and os.unlink('./compile_commands.json')
    os.symlink('./out/' + config['BUILDTYPE'] + '/compile_commands.json', './compile_commands.json')

# pass the leftover non-whitespace positional arguments to GYP
gyp_args += [arg for arg in args if not str.isspace(arg)]

if warn.warned and not options.verbose:
  warn('warnings were emitted in the configure phase')

print_verbose("running: \n    " + " ".join(['python', 'tools/gyp_node.py'] + gyp_args))
run_gyp(gyp_args)
if options.compile_commands_json and sys.platform == 'win32':
  os.path.isfile('./compile_commands.json') and os.unlink('./compile_commands.json')
  shutil.copy2('./out/' + config['BUILDTYPE'] + '/compile_commands.json', './compile_commands.json')
info('configure completed successfully')
