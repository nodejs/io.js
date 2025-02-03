// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax --reuse-scope-infos

function __f_11() {
  for (let i =  __f_26 = function() { return i }; i < 1; ++i) {
  }
}
__f_11();
  %ForceFlush(__f_11);
      __f_11();
