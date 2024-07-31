#!/usr/bin/env python3

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT_DIR = os.path.abspath(os.path.join(__file__, '..', '..'))

# Run install.py to install headers.
def generate_headers(headers_dir, install_args):
  print('Generating headers')
  subprocess.check_call([
      sys.executable,
      os.path.join(ROOT_DIR, 'tools/install.py'),
      'install',
      '--silent',
      '--headers-only',
      '--prefix', '/',
      '--dest-dir', headers_dir,
  ] + install_args)

# Rebuild addons in parallel.
def rebuild_addons(args):
  headers_dir = os.path.abspath(args.headers_dir)
  out_dir = os.path.abspath(args.out_dir)
  node_bin = os.path.join(out_dir, 'node')
  if args.is_win:
    node_bin += '.exe'

  if os.path.isabs(args.node_gyp):
    node_gyp = args.node_gyp
  else:
    node_gyp = os.path.join(ROOT_DIR, args.node_gyp)

  # Copy node.lib.
  if args.is_win:
    node_lib_dir = os.path.join(headers_dir, args.config)
    os.makedirs(node_lib_dir)
    shutil.copy2(os.path.join(args.out_dir, 'node.lib'),
                 os.path.join(node_lib_dir, 'node.lib'))

  def node_gyp_rebuild(test_dir):
    print('Building addon in', test_dir)
    try:
      process = subprocess.Popen([
          node_bin,
          node_gyp,
          'rebuild',
          '--directory', test_dir,
          '--nodedir', headers_dir,
          '--python', sys.executable,
          '--loglevel', args.loglevel,
      ], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

      # We buffer the output and print it out once the process is done in order
      # to avoid interleaved output from multiple builds running at once.
      return_code = process.wait()
      stdout, stderr = process.communicate()
      if return_code != 0:
        print(f'Failed to build addon in {test_dir}:')
        if stdout:
          print(stdout.decode())
        if stderr:
          print(stderr.decode())
      return return_code

    except Exception as e:
      print(f'Unexpected error when building addon in {test_dir}. Error: {e}')

  test_dirs = []
  skip_tests = args.skip_tests.split(',')
  only_tests = args.only_tests.split(',') if args.only_tests else None
  for child_dir in os.listdir(args.target):
    full_path = os.path.join(args.target, child_dir)
    if not os.path.isdir(full_path):
      continue
    if 'binding.gyp' not in os.listdir(full_path):
      continue
    if child_dir in skip_tests:
      continue
    if only_tests and child_dir not in only_tests:
      continue
    test_dirs.append(full_path)

  with ThreadPoolExecutor() as executor:
    codes = executor.map(node_gyp_rebuild, test_dirs)
    return 0 if all(code == 0 for code in codes) else 1

def get_default_out_dir(args):
  default_out_dir = os.path.join('out', args.config)
  if not args.is_win:
    # POSIX platforms only have one out dir.
    return default_out_dir
  # On Windows depending on the args of GYP and configure script, the out dir
  # could be 'out/Release', 'out/Debug' or just 'Release' or 'Debug'.
  if os.path.exists(default_out_dir):
    return default_out_dir
  if os.path.exists(args.config):
    return args.config
  raise RuntimeError('Cannot find out dir, did you run build?')

def main():
  if sys.platform == 'cygwin':
    raise RuntimeError('This script does not support running with cygwin python.')

  parser = argparse.ArgumentParser(
      description='Install headers and rebuild child directories')
  parser.add_argument('target', help='target directory to build addons')
  parser.add_argument('--headers-dir',
                      help='path to node headers directory, if not specified '
                           'new headers will be generated for building',
                      default=None)
  parser.add_argument('--out-dir', help='path to the output directory',
                      default=None)
  parser.add_argument('--loglevel', help='loglevel of node-gyp',
                      default='silent')
  parser.add_argument('--skip-tests', help='skip building tests',
                      default='')
  parser.add_argument('--only-tests', help='only build tests in the list',
                      default='')
  parser.add_argument('--node-gyp', help='path to node-gyp script',
                      default='deps/npm/node_modules/node-gyp/bin/node-gyp.js')
  parser.add_argument('--is-win', help='build for Windows target',
                      action='store_true', default=(sys.platform == 'win32'))
  parser.add_argument('--config', help='build config (Release or Debug)',
                      default='Release')
  args, unknown_args = parser.parse_known_args()

  if not args.out_dir:
    args.out_dir = get_default_out_dir(args)

  exit_code = 1
  if args.headers_dir:
    exit_code = rebuild_addons(args)
  else:
    # When --headers-dir is not specified, generate headers into a temp dir and
    # build with the new headers.
    try:
      args.headers_dir = tempfile.mkdtemp()
      generate_headers(args.headers_dir, unknown_args)
      exit_code = rebuild_addons(args)
    finally:
      shutil.rmtree(args.headers_dir)
  return exit_code

if __name__ == '__main__':
  sys.exit(main())
