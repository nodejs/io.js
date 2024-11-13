// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class C {
  static {
    const evil = eval.bind();
    assertThrows(() => { evil("C = 0"); });
  }
}
