// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --verify-heap

d8.file.execute("test/mjsunit/wasm/user-properties-common.js");

(function ConstructedTest() {
  print("ConstructedTest");

  var memory = undefined, table = undefined;
  for (let i = 0; i < 4; i++) {
    print("  iteration " + i);

    let m = new WebAssembly.Memory({initial: 1});
    let t = new WebAssembly.Table({element: "anyfunc", initial: 1});
    m.old = memory;
    t.old = table;

    memory = m;
    table = t;
    testProperties(memory);
    testProperties(table);
  }
})();
