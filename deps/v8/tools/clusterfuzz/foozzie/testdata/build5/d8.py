# Copyright 2024 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys

print("""
Some smoke-test output.
___foozzie___smoke_test_end___

1
v8-foozzie source: name/to/a/file.js
2
v8-foozzie source: name/to/file.js
3
unknown
""")

if '--simulate-errors' in sys.argv:
  sys.exit(73)
