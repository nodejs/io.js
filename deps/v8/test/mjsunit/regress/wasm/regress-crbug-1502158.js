// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --log-code

function __f_5() {
  "use asm";
  function __f_6() {}
  return __f_6;
}

__f_5();
__f_5();
