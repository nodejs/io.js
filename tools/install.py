#!/usr/bin/env python3

import argparse
import ast
import errno
import os
import platform
import shutil
import sys
import re

current_system = platform.system()

SYSTEM_AIX = "AIX"

def abspath(*args):
  path = os.path.join(*args)
  return os.path.abspath(path)

def is_child_dir(child, parent):
  p = os.path.abspath(parent)
  c = os.path.abspath(child)
  return c.startswith(p) and c != p

def try_unlink(path):
  try:
    os.unlink(path)
  except OSError as e:
    if e.errno != errno.ENOENT: raise

def try_symlink(options, source_path, link_path):
  if not options.silent:
    print('symlinking %s -> %s' % (source_path, link_path))
  try_unlink(link_path)
  try_mkdir_r(os.path.dirname(link_path))
  os.symlink(source_path, link_path)

def try_mkdir_r(path):
  try:
    os.makedirs(path)
  except OSError as e:
    if e.errno != errno.EEXIST: raise

def try_rmdir_r(options, path):
  path = abspath(path)
  while is_child_dir(path, options.install_path):
    try:
      os.rmdir(path)
    except OSError as e:
      if e.errno == errno.ENOTEMPTY: return
      if e.errno == errno.ENOENT: return
      if e.errno == errno.EEXIST and current_system == SYSTEM_AIX: return
      raise
    path = abspath(path, '..')

def mkpaths(options, path, dest):
  if dest.endswith('/') or dest.endswith('\\'):
    target_path = abspath(options.install_path, dest, os.path.basename(path))
  else:
    target_path = abspath(options.install_path, dest)
  if os.path.isabs(path):
    source_path = path
  else:
    source_path = abspath(options.root_dir, path)
  return source_path, target_path

def try_copy(options, path, dest):
  source_path, target_path = mkpaths(options, path, dest)
  if not options.silent:
    print('installing %s' % target_path)
  try_mkdir_r(os.path.dirname(target_path))
  try_unlink(target_path) # prevent ETXTBSY errors
  return shutil.copy2(source_path, target_path)

def try_remove(options, path, dest):
  source_path, target_path = mkpaths(options, path, dest)
  if not options.silent:
    print('removing %s' % target_path)
  try_unlink(target_path)
  try_rmdir_r(options, os.path.dirname(target_path))

def install(options, paths, dest):
  for path in paths:
    try_copy(options, path, dest)

def uninstall(options, paths, dest):
  for path in paths:
    try_remove(options, path, dest)

def package_files(options, action, name, bins):
  target_path = os.path.join('lib/node_modules', name)

  # don't install npm if the target path is a symlink, it probably means
  # that a dev version of npm is installed there
  if os.path.islink(abspath(options.install_path, target_path)): return

  # npm has a *lot* of files and it'd be a pain to maintain a fixed list here
  # so we walk its source directory instead...
  root = os.path.join('deps', name)
  for dirname, subdirs, basenames in os.walk(root, topdown=True):
    subdirs[:] = [subdir for subdir in subdirs if subdir != 'test']
    paths = [os.path.join(dirname, basename) for basename in basenames]
    action(options, paths,
           os.path.join(target_path, dirname[len(root) + 1:]) + os.path.sep)

  # create/remove symlinks
  for bin_name, bin_target in bins.items():
    link_path = abspath(options.install_path, os.path.join('bin', bin_name))
    if action == uninstall:
      action(options, [link_path], os.path.join('bin', bin_name))
    elif action == install:
      try_symlink(options, os.path.join('../lib/node_modules', name, bin_target), link_path)
    else:
      assert 0  # unhandled action type

def npm_files(options, action):
  package_files(options, action, 'npm', {
    'npm': 'bin/npm-cli.js',
    'npx': 'bin/npx-cli.js',
  })

def corepack_files(options, action):
  package_files(options, action, 'corepack', {
    'corepack': 'dist/corepack.js',
#   Not the default just yet:
#   'yarn': 'dist/yarn.js',
#   'yarnpkg': 'dist/yarn.js',
#   'pnpm': 'dist/pnpm.js',
#   'pnpx': 'dist/pnpx.js',
  })

  # On z/OS, we install node-gyp for convenience, as some vendors don't have
  # external access and may want to build native addons.
  if sys.platform == 'zos':
    link_path = abspath(options.install_path, 'bin/node-gyp')
    if action == uninstall:
      action(options, [link_path], 'bin/node-gyp')
    elif action == install:
      try_symlink(options, '../lib/node_modules/npm/node_modules/node-gyp/bin/node-gyp.js', link_path)
    else:
      assert 0 # unhandled action type

def subdir_files(options, path, dest, action):
  source_path, _ = mkpaths(options, path, dest)
  ret = {}
  for dirpath, dirnames, filenames in os.walk(source_path):
    files_in_path = [os.path.join(os.path.relpath(dirpath, options.root_dir), f) for f in filenames if f.endswith('.h')]
    ret[os.path.join(dest, os.path.relpath(dirpath, source_path))] = files_in_path
  for subdir, files_in_path in ret.items():
    action(options, files_in_path, subdir + os.path.sep)

def files(options, action):
  node_bin = 'node'
  if options.is_win:
    node_bin += '.exe'
  action(options, [os.path.join(options.build_dir, node_bin)], os.path.join('bin', node_bin))

  if 'true' == options.variables.get('node_shared'):
    if options.is_win:
      action(options, [os.path.join(options.build_dir, 'libnode.dll')], 'bin/libnode.dll')
      action(options, [os.path.join(options.build_dir, 'libnode.lib')], 'lib/libnode.lib')
    elif sys.platform == 'zos':
      # GYP will output to lib.target; see _InstallableTargetInstallPath
      # function in tools/gyp/pylib/gyp/generator/make.py
      output_prefix = os.path.join(options.build_dir, 'lib.target')

      output_lib = 'libnode.' + options.variables.get('shlib_suffix')
      action(options, [os.path.join(output_prefix, output_lib)], os.path.join('lib', output_lib))

      # create libnode.x that references libnode.so (C++ addons compat)
      os.system(os.path.dirname(os.path.realpath(__file__)) +
                '/zos/modifysidedeck.sh ' +
                abspath(options.install_path, 'lib', output_lib) + ' ' +
                abspath(options.install_path, 'lib/libnode.x') + ' libnode.so')

      # install libnode.version.so
      so_name = 'libnode.' + re.sub(r'\.x$', '.so', options.variables.get('shlib_suffix'))
      action(options, [os.path.join(output_prefix, so_name)], options.variables.get('libdir') + '/' + so_name)

      # create symlink of libnode.so -> libnode.version.so (C++ addons compat)
      link_path = abspath(options.install_path, 'lib/libnode.so')
      try_symlink(options, so_name, link_path)
    else:
      output_lib = 'libnode.' + options.variables.get('shlib_suffix')
      action(options, [os.path.join(options.build_dir, output_lib)],
             os.path.join(options.variables.get('libdir'), output_lib))

  action(options, [os.path.join(options.v8_dir, 'tools/gdbinit')], 'share/doc/node/')
  action(options, [os.path.join(options.v8_dir, 'tools/lldb_commands.py')], 'share/doc/node/')

  if 'openbsd' in sys.platform:
    action(options, ['doc/node.1'], 'man/man1/')
  else:
    action(options, ['doc/node.1'], 'share/man/man1/')

  if 'true' == options.variables.get('node_install_npm'):
    npm_files(options, action)

  if 'true' == options.variables.get('node_install_corepack'):
    corepack_files(options, action)

  headers(options, action)

def headers(options, action):
  def wanted_v8_headers(options, files_arg, dest):
    v8_headers = [
      # The internal cppgc headers are depended on by the public
      # ones, so they need to be included as well.
      'include/cppgc/internal/api-constants.h',
      'include/cppgc/internal/atomic-entry-flag.h',
      'include/cppgc/internal/base-page-handle.h',
      'include/cppgc/internal/caged-heap-local-data.h',
      'include/cppgc/internal/caged-heap.h',
      'include/cppgc/internal/compiler-specific.h',
      'include/cppgc/internal/finalizer-trait.h',
      'include/cppgc/internal/gc-info.h',
      'include/cppgc/internal/logging.h',
      'include/cppgc/internal/member-storage.h',
      'include/cppgc/internal/name-trait.h',
      'include/cppgc/internal/persistent-node.h',
      'include/cppgc/internal/pointer-policies.h',
      'include/cppgc/internal/write-barrier.h',
      # cppgc headers
      'include/cppgc/allocation.h',
      'include/cppgc/common.h',
      'include/cppgc/cross-thread-persistent.h',
      'include/cppgc/custom-space.h',
      'include/cppgc/default-platform.h',
      'include/cppgc/ephemeron-pair.h',
      'include/cppgc/explicit-management.h',
      'include/cppgc/garbage-collected.h',
      'include/cppgc/heap-consistency.h',
      'include/cppgc/heap-handle.h',
      'include/cppgc/heap-state.h',
      'include/cppgc/heap-statistics.h',
      'include/cppgc/heap.h',
      'include/cppgc/liveness-broker.h',
      'include/cppgc/macros.h',
      'include/cppgc/member.h',
      'include/cppgc/name-provider.h',
      'include/cppgc/object-size-trait.h',
      'include/cppgc/persistent.h',
      'include/cppgc/platform.h',
      'include/cppgc/prefinalizer.h',
      'include/cppgc/process-heap-statistics.h',
      'include/cppgc/sentinel-pointer.h',
      'include/cppgc/source-location.h',
      'include/cppgc/testing.h',
      'include/cppgc/trace-trait.h',
      'include/cppgc/type-traits.h',
      'include/cppgc/visitor.h',
      # libplatform headers
      'include/libplatform/libplatform-export.h',
      'include/libplatform/libplatform.h',
      'include/libplatform/v8-tracing.h',
      # v8 headers
      'include/v8-array-buffer.h',
      'include/v8-callbacks.h',
      'include/v8-container.h',
      'include/v8-context.h',
      'include/v8-cppgc.h',
      'include/v8-data.h',
      'include/v8-date.h',
      'include/v8-debug.h',
      'include/v8-embedder-heap.h',
      'include/v8-embedder-state-scope.h',
      'include/v8-exception.h',
      'include/v8-extension.h',
      'include/v8-external.h',
      'include/v8-forward.h',
      'include/v8-function-callback.h',
      'include/v8-function.h',
      'include/v8-handle-base.h',
      'include/v8-initialization.h',
      'include/v8-internal.h',
      'include/v8-isolate.h',
      'include/v8-json.h',
      'include/v8-local-handle.h',
      'include/v8-locker.h',
      'include/v8-maybe.h',
      'include/v8-memory-span.h',
      'include/v8-message.h',
      'include/v8-microtask-queue.h',
      'include/v8-microtask.h',
      'include/v8-object.h',
      'include/v8-persistent-handle.h',
      'include/v8-platform.h',
      'include/v8-primitive-object.h',
      'include/v8-primitive.h',
      'include/v8-profiler.h',
      'include/v8-promise.h',
      'include/v8-proxy.h',
      'include/v8-regexp.h',
      'include/v8-sandbox.h',
      'include/v8-script.h',
      'include/v8-snapshot.h',
      'include/v8-source-location.h',
      'include/v8-statistics.h',
      'include/v8-template.h',
      'include/v8-traced-handle.h',
      'include/v8-typed-array.h',
      'include/v8-unwinder.h',
      'include/v8-value-serializer.h',
      'include/v8-value.h',
      'include/v8-version.h',
      'include/v8-wasm.h',
      'include/v8-weak-callback-info.h',
      'include/v8.h',
      'include/v8config.h',
    ]
    if sys.platform == 'win32':
      # Native win32 python uses \ for path separator.
      v8_headers = [os.path.normpath(path) for path in v8_headers]
    if os.path.isabs(options.v8_dir):
      rel_v8_dir = os.path.relpath(options.v8_dir, options.root_dir)
    else:
      rel_v8_dir = options.v8_dir
    files_arg = [name for name in files_arg if os.path.relpath(name, rel_v8_dir) in v8_headers]
    action(options, files_arg, dest)

  def wanted_zoslib_headers(options, files_arg, dest):
    import glob
    zoslib_headers = glob.glob(zoslibinc + '/*.h')
    files_arg = [name for name in files_arg if name in zoslib_headers]
    action(options, files_arg, dest)

  action(options, [
    options.config_gypi_path,
    'common.gypi',
    'src/node.h',
    'src/node_api.h',
    'src/js_native_api.h',
    'src/js_native_api_types.h',
    'src/node_api_types.h',
    'src/node_buffer.h',
    'src/node_object_wrap.h',
    'src/node_version.h',
  ], 'include/node/')

  # Add the expfile that is created on AIX
  if sys.platform.startswith('aix') or sys.platform == "os400":
    action(options, ['out/Release/node.exp'], 'include/node/')

  subdir_files(options, os.path.join(options.v8_dir, 'include'), 'include/node/', wanted_v8_headers)

  if 'false' == options.variables.get('node_shared_libuv'):
    subdir_files(options, 'deps/uv/include', 'include/node/', action)

  if 'true' == options.variables.get('node_use_openssl') and \
     'false' == options.variables.get('node_shared_openssl'):
    subdir_files(options, 'deps/openssl/openssl/include/openssl', 'include/node/openssl/', action)
    subdir_files(options, 'deps/openssl/config/archs', 'include/node/openssl/archs', action)
    subdir_files(options, 'deps/openssl/config', 'include/node/openssl', action)

  if 'false' == options.variables.get('node_shared_zlib'):
    action(options, [
      'deps/zlib/zconf.h',
      'deps/zlib/zlib.h',
    ], 'include/node/')

  if sys.platform == 'zos':
    zoslibinc = os.environ.get('ZOSLIB_INCLUDES')
    if not zoslibinc:
      raise RuntimeError('Environment variable ZOSLIB_INCLUDES is not set\n')
    if not os.path.isfile(zoslibinc + '/zos-base.h'):
      raise RuntimeError('ZOSLIB_INCLUDES is not set to a valid location\n')
    subdir_files(options, zoslibinc, 'include/node/zoslib/', wanted_zoslib_headers)

def run(options):
  if options.headers_only:
    if options.command == 'install':
      headers(options, install)
      return
    if options.command == 'uninstall':
      headers(options, uninstall)
      return
  else:
    if options.command == 'install':
      files(options, install)
      return
    if options.command == 'uninstall':
      files(options, uninstall)
      return

  raise RuntimeError('Bad command: %s\n' % options.command)

def parse_options(args):
  parser = argparse.ArgumentParser(
      description='Install headers and binaries into filesystem')
  parser.add_argument('command', choices=['install', 'uninstall'])
  parser.add_argument('--dest-dir', help='custom install prefix for packagers, i.e. DESTDIR',
                      default=os.getcwd())
  parser.add_argument('--prefix', help='custom install prefix, i.e. PREFIX',
                      default='/usr/local')
  parser.add_argument('--headers-only', help='only install headers',
                      action='store_true', default=False)
  parser.add_argument('--root-dir', help='the root directory of source code',
                      default=os.getcwd())
  parser.add_argument('--build-dir', help='the location of built binaries',
                      default='out/Release')
  parser.add_argument('--v8-dir', help='the location of V8',
                      default='deps/v8')
  parser.add_argument('--config-gypi-path', help='the location of config.gypi',
                      default='config.gypi')
  parser.add_argument('--is-win', help='build for Windows target',
                      action='store_true',
                      default=(sys.platform in ['win32', 'cygwin']))
  parser.add_argument('--silent', help='do not output log',
                      action='store_true', default=False)
  options = parser.parse_args(args)

  # |dest_dir| is a custom install prefix for packagers (think DESTDIR)
  # |prefix| is a custom install prefix (think PREFIX)
  # Difference is that dest_dir won't be included in shebang lines etc.
  # |install_path| thus becomes the base target directory.
  options.install_path = os.path.join(options.dest_dir + options.prefix)

  # Read variables from the config.gypi.
  with open(options.config_gypi_path) as f:
    config = ast.literal_eval(f.read())
    options.variables = config['variables']

  return options

if __name__ == '__main__':
  run(parse_options(sys.argv[1:]))
