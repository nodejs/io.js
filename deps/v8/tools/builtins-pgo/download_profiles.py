#!/usr/bin/env python3

# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can
# be found in the LICENSE file.
"""
Download PGO profiles for V8 builtins. The version is pulled from V8's version
file (include/v8-version.h).

See argparse documentation for usage details.
"""

import argparse
import contextlib
import os
import pathlib
import re
import sys

FILENAME = os.path.basename(__file__)
PGO_PROFILE_BUCKET = 'chromium-v8-builtins-pgo'
PGO_PROFILE_DIR = pathlib.Path(os.path.dirname(__file__)) / 'profiles'

V8_DIR = PGO_PROFILE_DIR.parents[2]
DEPOT_TOOLS_DEFAULT_PATH = os.path.join(V8_DIR, 'third_party', 'depot_tools')
VERSION_FILE = V8_DIR / 'include' / 'v8-version.h'
VERSION_RE = r"""#define V8_MAJOR_VERSION (\d+)
#define V8_MINOR_VERSION (\d+)
#define V8_BUILD_NUMBER (\d+)
#define V8_PATCH_LEVEL (\d+)"""


def main(cmd_args=None):
  args = parse_args(cmd_args)
  import_gsutil(args)
  version = retrieve_version(args)
  perform_action(version, args)
  sys.exit(0)


def parse_args(cmd_args):
  parser = argparse.ArgumentParser(
      description=(
          f'Download PGO profiles for V8 builtins generated for the version '
          f'defined in {VERSION_FILE}. If the current checkout has no version '
          f'(i.e. build and patch level are 0 in {VERSION_FILE}), no profiles '
          f'exist and the script returns without errors.'),
      formatter_class=argparse.RawDescriptionHelpFormatter,
      epilog='\n'.join([
          f'examples:', f'  {FILENAME} download',
          f'  {FILENAME} validate --bucket=chromium-v8-builtins-pgo-staging',
          f'', f'return codes:',
          f'  0 - profiles successfully downloaded or validated',
          f'  1 - unexpected error, see stdout',
          f'  2 - invalid arguments specified, see {FILENAME} --help',
          f'  3 - invalid path to depot_tools provided'
          f'  4 - gsutil was unable to retrieve data from the bucket'
      ]),
  )

  parser.add_argument(
      'action',
      choices=['download', 'validate'],
      help=(
          'download or validate profiles for the currently checked out version'
      ),
  )

  parser.add_argument(
      '--version',
      help=('download (or validate) profiles for this version (e.g. 11.0.226.0 '
            'or 11.0.226.2), defaults to the version in v8\'s version file'),
  )

  parser.add_argument(
      '--depot-tools',
      help=('path to depot tools, defaults to V8\'s version in '
            f'{DEPOT_TOOLS_DEFAULT_PATH}.'),
      type=pathlib.Path,
      default=DEPOT_TOOLS_DEFAULT_PATH,
  )

  parser.add_argument(
      '--force',
      help=('force download, overwriting existing profiles'),
      action='store_true',
  )

  return parser.parse_args(cmd_args)


def import_gsutil(args):
  abs_depot_tools_path = os.path.abspath(args.depot_tools)
  file = os.path.join(abs_depot_tools_path, 'download_from_google_storage.py')
  if not pathlib.Path(file).is_file():
    print(f'{file} does not exist; check --depot-tools path.', file=sys.stderr)
    sys.exit(3)

  # Put this path at the beginning of the PATH to give it priority.
  sys.path.insert(0, abs_depot_tools_path)
  globals()['gcs_download'] = __import__('download_from_google_storage')


def retrieve_version(args):
  if args.version:
    return args.version

  with open(VERSION_FILE) as f:
    version_tuple = re.search(VERSION_RE, f.read()).groups(0)

  version = '.'.join(version_tuple)
  if version_tuple[2] == version_tuple[3] == '0':
    print(f'The version file specifies {version}, which has no profiles.')
    sys.exit(0)

  return version


def download_profiles(version_path, requested_version, args):
  if args.force:
    return True

  if not version_path.is_file():
    return True

  with open(version_path) as version_file:
    profiles_version = version_file.read()

  if profiles_version != requested_version:
    return True

  print('Profiles already downloaded, use --force to overwrite.')
  return False


@contextlib.contextmanager
def ensure_profiles(version, args):
  version_path = PGO_PROFILE_DIR / 'profiles_version'
  require_profiles = download_profiles(version_path, version, args)
  yield require_profiles
  if require_profiles:
    with open(version_path, 'w') as version_file:
      version_file.write(version)


def perform_action(version, args):
  path = f'{PGO_PROFILE_BUCKET}/by-version/{version}'

  if args.action == 'download':
    with ensure_profiles(version, args) as require_profiles:
      if not require_profiles:
        return

      cmd = ['cp', '-R', f'gs://{path}/*.profile', str(PGO_PROFILE_DIR)]
      failure_hint = f'https://storage.googleapis.com/{path} does not exist.'
      call_gsutil(cmd, failure_hint)
      return

  if args.action == 'validate':
    meta_json = f'{path}/meta.json'
    cmd = ['stat', f'gs://{meta_json}']
    failure_hint = f'https://storage.googleapis.com/{meta_json} does not exist.'
    call_gsutil(cmd, failure_hint)
    return

  raise AssertionError(f'Invalid action: {args.action}')


def call_gsutil(cmd, failure_hint):
  # Load gsutil from depot tools, and execute command
  gsutil = gcs_download.Gsutil(gcs_download.GSUTIL_DEFAULT_PATH)
  returncode, stdout, stderr = gsutil.check_call(*cmd)
  if returncode != 0:
    print_error(['gsutil', *cmd], returncode, stdout, stderr, failure_hint)
    sys.exit(4)


def print_error(cmd, returncode, stdout, stderr, failure_hint):
  message = [
      'The following command did not succeed:',
      f'  $ {" ".join(cmd)}',
  ]
  sections = [
      ('return code', str(returncode)),
      ('stdout', stdout.strip()),
      ('stderr', stderr.strip()),
      ('hint', failure_hint),
  ]
  for label, output in sections:
    if not output:
      continue
    message += [f'{label}:', "  " + "\n  ".join(output.split("\n"))]

  print('\n'.join(message), file=sys.stderr)


if __name__ == '__main__':
  main()
