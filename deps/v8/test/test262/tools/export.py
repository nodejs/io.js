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
      '--approver',
      help="Flag indicating that this run will only approve and merge "
      "exisiting PRs. Approval of these PRs needs to be done by an account "
      "different from the one that created the PR.",
      action="store_true")
  args, exporter_args = parser.parse_known_args(sys.argv)

  sys.path.append(args.blink_tools_path)
  from blinkpy.common import exit_codes
  from blinkpy.common.host import Host
  from blinkpy.common.path_finder import add_depot_tools_dir_to_os_path

  from v8_exporter import V8TestExporter, V8TestApprover
  from v8configs import config_from_file

  add_depot_tools_dir_to_os_path()
  host = Host(project_config_factory=config_from_file(args.config_path))
  exporter = (V8TestApprover if args.approver else V8TestExporter)(host)
  try:
    success = exporter.main(exporter_args[1:])
    host.exit(0 if success else 1)
  except KeyboardInterrupt:
    host.print_('Interrupted, exiting')
    host.exit(exit_codes.INTERRUPTED_EXIT_STATUS)


if __name__ == '__main__':
  main()
