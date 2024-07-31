#!/usr/bin/env python3
# Copyright 2008 the V8 project authors.
# Copyright 2023 Microsoft Inc.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys

from utils import SearchFiles


if __name__ == '__main__':
  try:
    files = SearchFiles(*sys.argv[2:])
    files = [ os.path.relpath(x, sys.argv[1]) for x in files ]
    print('\n'.join(files))
  except Exception as e:
    print(str(e))
    sys.exit(1)
