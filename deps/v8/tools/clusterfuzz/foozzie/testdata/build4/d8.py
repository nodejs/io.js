# Copyright 2024 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import sys

if '--crash-the-smoke-test' in sys.argv:
  sys.exit(73)

# Simulate different output during the smoke test.
print("""
Different smoke-test output.
___foozzie___smoke_test_end___

Other different output.
""")
