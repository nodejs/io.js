#!/usr/bin/env python3
# Copyright (c) 2013-2019 GitHub Inc.
# Copyright 2019 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# This script reads the configurations of GN and outputs a config.gypi file that
# will be used to populate process.config.variables.

import argparse
import json
import re
import os
import subprocess
import sys

sys.path.append(os.path.dirname(__file__))
import getnapibuildversion

# Regex used for parsing results of "gn args".
GN_RE = re.compile(r'(\w+)\s+=\s+(.*?)$', re.MULTILINE)
GN = 'gn.bat' if sys.platform == 'win32' else 'gn'

def bool_to_number(v):
  return 1 if v else 0

def bool_string_to_number(v):
  return bool_to_number(v == 'true')

def get_gn_config(out_dir):
  # Read args from GN configurations.
  gn_args = subprocess.check_output(
      [GN, 'args', '--list', '--short', '-C', out_dir])
  config = dict(re.findall(GN_RE, gn_args.decode()))
  # Get napi_build_version from Node, which is not part of GN args.
  config['napi_build_version'] = getnapibuildversion.get_napi_version()
  return config

def get_v8_config(out_dir, node_gn_path):
  with open(os.path.join(out_dir, 'v8_features.json')) as f:
    v8_config = json.load(f)
  for key, value in v8_config.items():
    if isinstance(value, bool):
      v8_config[key] = bool_to_number(value)
  return v8_config

def translate_config(out_dir, config, v8_config):
  config_gypi = {
    'target_defaults': {
      'default_configuration':
          'Debug' if config['is_debug'] == 'true' else 'Release',
    },
    'variables': {
      'asan': bool_string_to_number(config['is_asan']),
      'clang': bool_to_number(config['is_clang']),
      'enable_lto': config['use_thin_lto'],
      'is_debug': bool_string_to_number(config['is_debug']),
      'llvm_version': 13,
      'napi_build_version': config['napi_build_version'],
      'node_builtin_shareable_builtins':
          eval(config['node_builtin_shareable_builtins']),
      'node_module_version': int(config['node_module_version']),
      'node_use_openssl': config['node_use_openssl'],
      'node_use_amaro': config['node_use_amaro'],
      'node_use_node_code_cache': config['node_use_node_code_cache'],
      'node_use_node_snapshot': config['node_use_node_snapshot'],
      'v8_enable_inspector':  # this is actually a node misnomer
          bool_string_to_number(config['node_enable_inspector']),
      'shlib_suffix': 'dylib' if sys.platform == 'darwin' else 'so',
      'tsan': bool_string_to_number(config['is_tsan']),
      # TODO(zcbenz): Shared components are not supported in GN config yet.
      'node_shared': 'false',
      'node_shared_brotli': 'false',
      'node_shared_cares': 'false',
      'node_shared_http_parser': 'false',
      'node_shared_libuv': 'false',
      'node_shared_nghttp2': 'false',
      'node_shared_nghttp3': 'false',
      'node_shared_ngtcp2': 'false',
      'node_shared_openssl': 'false',
      'node_shared_sqlite': 'false',
      'node_shared_zlib': 'false',
    }
  }
  config_gypi['variables'].update(v8_config)
  return config_gypi

def main():
  parser = argparse.ArgumentParser(
      description='Generate config.gypi file from GN configurations')
  parser.add_argument('target', help='path to generated config.gypi file')
  parser.add_argument('--out-dir', help='path to the output directory',
                      default='out/Release')
  parser.add_argument('--node-gn-path', help='path of the node target in GN',
                      default='//node')
  parser.add_argument('--dep-file', help='path to an optional dep file',
                      default=None)
  args, unknown_args = parser.parse_known_args()

  config = get_gn_config(args.out_dir)
  v8_config = get_v8_config(args.out_dir, args.node_gn_path)

  # Write output.
  with open(args.target, 'w') as f:
    f.write(repr(translate_config(args.out_dir, config, v8_config)))

  # Write depfile. Force regenerating config.gypi when GN configs change.
  if args.dep_file:
    with open(args.dep_file, 'w') as f:
      f.write('%s: %s '%(args.target, 'build.ninja'))

if __name__ == '__main__':
  main()
