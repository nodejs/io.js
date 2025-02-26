// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-stats-nvp

function Module() {
  "use asm";
  function f() {}
  return { f: f };
}

Module();
