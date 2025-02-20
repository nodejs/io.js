// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

  // Make a function with 65535 args. This should throw a SyntaxError because -1
  // is reserved for the "don't adapt arguments" sentinel.

var a = [];
for (let i = 0; i < 65535; i++) {
  a.push("x" + i);
}
assertThrows(()=>eval("(" + a.join(",") + ")=>{}"));
