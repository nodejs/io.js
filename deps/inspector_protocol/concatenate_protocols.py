#!/usr/bin/env python3
# Copyright 2016 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os.path
import sys

try:
  import json
except ImportError:
  import simplejson as json

import pdl

def main(argv):
  if len(argv) < 1:
    sys.stderr.write(
        "Usage: %s <protocol-1> [<protocol-2> [, <protocol-3>...]] "
        "<output-file>\n" % sys.argv[0])
    return 1

  domains = []
  version = None
  for protocol in argv[:-1]:
    file_name = os.path.normpath(protocol)
    if not os.path.isfile(file_name):
      sys.stderr.write("Cannot find %s\n" % file_name)
      return 1
    input_file = open(file_name, "r")
    parsed_json = pdl.loads(input_file.read(), file_name)
    domains += parsed_json["domains"]
    version = parsed_json["version"]

  output_file = open(argv[-1], "w")
  json.dump({"version": version, "domains": domains}, output_file,
            indent=4, sort_keys=False, separators=(',', ': '))
  output_file.close()


if __name__ == '__main__':
  sys.exit(main(sys.argv[1:]))
