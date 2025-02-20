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
from functools import cached_property
import json
import os
import pathlib
import re
import sys

FILE = pathlib.Path(os.path.abspath(__file__))
V8_DIR = FILE.parents[2]
PGO_PROFILE_DIR = V8_DIR / 'tools/builtins-pgo/profiles'

PGO_PROFILE_BUCKET = 'chromium-v8-builtins-pgo'
CHROMIUM_DEPS_V8_REVISION = r"'v8_revision': '([0-9a-f]{40})',"

DEPOT_TOOLS_DEFAULT_PATH = V8_DIR / 'third_party/depot_tools'

VERSION_FILE = V8_DIR / 'include/v8-version.h'
VERSION_RE = r"""#define V8_MAJOR_VERSION (\d+)
#define V8_MINOR_VERSION (\d+)
#define V8_BUILD_NUMBER (\d+)
#define V8_PATCH_LEVEL (\d+)"""


class ProfileDownloader:
  def __init__(self, cmd_args=None):
    self.args = self._parse_args(cmd_args)
    self._import_gsutil()

  def run(self):
    if self.args.action == 'download':
      self._download()
      sys.exit(0)

    if self.args.action == 'validate':
      self._validate()
      sys.exit(0)

    raise AssertionError(f'Invalid action: {args.action}')

  def _parse_args(self, cmd_args):
    parser = argparse.ArgumentParser(
        description=(
            f'Download PGO profiles for V8 builtins generated for the version '
            f'defined in {VERSION_FILE}. If the current checkout has no '
            f'version (i.e. build and patch level are 0 in {VERSION_FILE}), no '
            f'profiles exist and the script returns without errors.'),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='\n'.join([
            f'examples:', f'  {FILE.name} download',
            f'  {FILE.name} validate --bucket=chromium-v8-builtins-pgo-staging',
            f'', f'return codes:',
            f'  0 - profiles successfully downloaded or validated',
            f'  1 - unexpected error, see stdout',
            f'  2 - invalid arguments specified, see {FILE.name} --help',
            f'  3 - invalid path to depot_tools provided'
            f'  4 - gsutil was unable to retrieve data from the bucket'
            f'  5 - profiles have been generated for a different revision'
            f'  6 - chromium DEPS file found without v8 revision'
            f'  7 - no chromium DEPS file found'
        ]),
    )

    parser.add_argument(
        'action',
        choices=['download', 'validate'],
        help=(
            'download or validate profiles for the currently checked out '
            'version'
        ),
    )

    parser.add_argument(
        '--version',
        help=('download (or validate) profiles for this version (e.g. '
              '11.0.226.0 or 11.0.226.2), defaults to the version in v8\'s '
              'version file'),
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
        help='force download, overwriting existing profiles',
        action='store_true',
    )

    parser.add_argument(
        '--quiet',
        help='run silently, still display errors',
        action='store_true',
    )

    parser.add_argument(
        '--check-v8-revision',
        help='validate profiles are built for chromium\'s V8 revision',
        action='store_true',
    )

    return parser.parse_args(cmd_args)

  def _import_gsutil(self):
    abs_depot_tools_path = os.path.abspath(self.args.depot_tools)
    file = os.path.join(abs_depot_tools_path, 'download_from_google_storage.py')
    if not pathlib.Path(file).is_file():
      message = f'{file} does not exist; check --depot-tools path.'
      self._fail(3, message)

    # Put this path at the beginning of the PATH to give it priority.
    sys.path.insert(0, abs_depot_tools_path)
    globals()['gcs_download'] = __import__('download_from_google_storage')

  @cached_property
  def version(self):
    if self.args.version:
      return self.args.version

    with VERSION_FILE.open() as f:
      version_tuple = re.search(VERSION_RE, f.read()).groups(0)

    version = '.'.join(version_tuple)
    if version_tuple[2] == version_tuple[3] == '0':
      self._log(f'The version file specifies {version}, which has no profiles.')
      sys.exit(0)

    return version

  @cached_property
  def _remote_profile_path(self):
    return f'{PGO_PROFILE_BUCKET}/by-version/{self.version}'

  @cached_property
  def _meta_json_path(self):
    return PGO_PROFILE_DIR / 'meta.json'

  @cached_property
  def _v8_revision(self):
    """If this script is executed within a chromium checkout, return the V8
    revision defined in chromium. Otherwise return None."""

    chromium_deps_file = V8_DIR.parent / 'DEPS'
    if not chromium_deps_file.is_file():
      message = (
          f'File {chromium_deps_file} not found. Verify the parent directory '
          f'of V8 is a valid chromium checkout.'
      )
      self._fail(7, message)

    with chromium_deps_file.open() as f:
      chromum_deps = f.read()

    match = re.search(CHROMIUM_DEPS_V8_REVISION, chromum_deps)
    if not match:
      message = (
          f'No V8 revision can be found in {chromium_deps_file}. Verify this '
          f'is a valid chromium DEPS file including a v8 version entry.'
      )
      self._fail(6, message)

    return match.group(1)

  def _download(self):
    if self._require_download():
      # Wipe profiles directory.
      for file in PGO_PROFILE_DIR.glob('*'):
        if file.name.startswith('.'):
          continue
        file.unlink()

      # Download new profiles.
      path = self._remote_profile_path
      cmd = ['cp', '-R', f'gs://{path}/*', str(PGO_PROFILE_DIR)]
      failure_hint = f'https://storage.googleapis.com/{path} does not exist.'
      self._call_gsutil(cmd, failure_hint)

    # Validate profile revision matches the current V8 revision.
    if self.args.check_v8_revision:
      with self._meta_json_path.open() as meta_json_file:
        meta_json = json.load(meta_json_file)

      if meta_json['revision'] != self._v8_revision:
        message = (
            f'V8 Builtins PGO profiles have been built for '
            f'{meta_json["revision"]}, but this chromium checkout uses '
            f'{self._v8_revision} in its DEPS file. Invalid profiles might '
            f'cause the build to fail or result in performance regressions. '
            f'Select a V8 revision which has up-to-date profiles or build with '
            f'pgo disabled.'
        )
        self._fail(5, message)

  def _require_download(self):
    if self.args.force:
      return True

    if not self._meta_json_path.is_file():
      return True

    with self._meta_json_path.open() as meta_json_file:
      try:
        meta_json = json.load(meta_json_file)
      except json.decoder.JSONDecodeError:
        return True

    if meta_json['version'] != self.version:
      return True

    self._log('Profiles already downloaded, use --force to overwrite.')
    return False

  def _validate(self):
    meta_json = f'{self._remote_profile_path}/meta.json'
    cmd = ['stat', f'gs://{meta_json}']
    failure_hint = (
        f'https://storage.googleapis.com/{meta_json} does not exist. This '
        f'error might be transient. Creating PGO data takes ~20 min after a '
        f'merge to a release branch. You can follow current PGO creation at '
        f'https://ci.chromium.org/ui/p/v8/builders/ci-hp/PGO%20Builder and '
        f'retry the release builder when it\'s ready.')
    self._call_gsutil(cmd, failure_hint)

  def _call_gsutil(self, cmd, failure_hint):
    # Load gsutil from depot tools, and execute command
    gsutil = gcs_download.Gsutil(gcs_download.GSUTIL_DEFAULT_PATH)
    returncode, stdout, stderr = gsutil.check_call(*cmd)
    if returncode != 0:
      self._print_error(['gsutil', *cmd], returncode, stdout, stderr, failure_hint)
      sys.exit(4)

  def _print_error(self, cmd, returncode, stdout, stderr, failure_hint):
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

  def _log(self, message):
    if self.args.quiet:
      return
    print(message)

  def _fail(self, returncode, message):
    print(message, file=sys.stderr)
    sys.exit(returncode)


if __name__ == '__main__':
  downloader = ProfileDownloader()
  downloader.run()
