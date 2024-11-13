// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

for (let i = 0; i < 50; i++) {
  for (let j = 2; -2147483647 < j; j--) {
    if (j == -30000) {
      break;
    }
  }
}
