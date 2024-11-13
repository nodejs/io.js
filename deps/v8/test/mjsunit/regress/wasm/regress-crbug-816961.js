// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function() {
  var memory = new WebAssembly.Memory({initial: 64 * 1024 * 1024 / 0x10000});
  var array = new Uint8Array(memory.buffer);
  Uint8Array.of.call(function() { return array },
                    {valueOf() { memory.grow(1); } });
})();

(function() {
  var memory = new WebAssembly.Memory({initial: 64 * 1024 * 1024 / 0x10000});
  var array = new Uint8Array(memory.buffer);
  Uint8Array.from.call(function() { return array },
                       [{valueOf() { memory.grow(1); } }],
                       x => x);
})();
