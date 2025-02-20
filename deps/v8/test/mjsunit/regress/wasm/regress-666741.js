// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --enable-slow-asserts

(function __f_7() {
  assertThrows(() => new WebAssembly.Memory({initial: 79199}), RangeError);
})();
