// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --wasm-staging

(function TestTableSetAndGetFunction() {
  let func = new WebAssembly.Function({ parameters: [], results: [] }, x => x);
  let table = new WebAssembly.Table({ element: "anyfunc", initial: 1 });
  table.set(0, func);
  table.get(0);
})();
