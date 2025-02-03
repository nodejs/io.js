#!/usr/bin/env python3

# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Pushes changes to test262 inside V8 to the upstream repo."""

from argparse import ArgumentParser
from pathlib import Path
import sys


def main():
  parser = ArgumentParser()
  parser.add_argument(
      '-t',
      '--blink-tools-path',
      help=("Absolute path to where blink tools are located "
            "(usually inside a local chromium checkout)."))
  parser.add_argument(
      '--config-path',
      help="Absolute path to a project configuration json file.",
      default=(Path(__file__).parent / 'v8configs.json'))
  parser.add_argument(
      '--phase',
      help=("Phase of the import process to run. One of 'ALL', 'PREBUILD', "
            "'POSTBUILD', 'UPLOAD'. ALL will run all phases; suitable for "
            "local runs. PREBUILD and POSTBUILD are to be used when building "
            "and running the tests is delegated to a proper builder. UPLOAD is "
            "used for creating and uploading the CL for the import. UPLOAD is "
            "deferred to the recipe on the import builder. This importer "
            "script gets called twice on the builder, first with PREBUILD and "
            "second with POSTBUILD. Build and UPLOAD phases are deferred to "
            "the recipe on the builder."),
      choices=['ALL', 'PREBUILD', 'POSTBUILD', 'UPLOAD'],
      default='ALL')
  parser.add_argument(
      '--test262-failure-file',
      help=('Absolute path to a file with names of the test to be skipped for '
            'this import. Used only in PREBUILD phase.'),
      default=None)
  parser.add_argument(
      '--v8-test262-last-revision',
      help=('Used only in POSTBUILD when called separately. The old revision was'
            ' overwritten at this point by the PREBUILD phase.'),
      default=None)
  args, exporter_args = parser.parse_known_args(sys.argv)

  sys.path.append(args.blink_tools_path)
  from blinkpy.common import exit_codes
  from blinkpy.common.host import Host
  from blinkpy.common.path_finder import add_depot_tools_dir_to_os_path

  from v8configs import config_from_file
  from v8_importer import V8TestImporter

  add_depot_tools_dir_to_os_path()
  host = Host(project_config_factory=config_from_file(args.config_path))
  importer = V8TestImporter(
      phase=args.phase,
      host=host,
      test262_failure_file=args.test262_failure_file,
      v8_test262_last_rev=args.v8_test262_last_revision)
  try:
    success = importer.main(exporter_args[1:])
    host.exit(0 if success else 1)
  except KeyboardInterrupt:
    host.print_('Interrupted, exiting')
    host.exit(exit_codes.INTERRUPTED_EXIT_STATUS)


if __name__ == '__main__':
  main()
