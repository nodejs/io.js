// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

(function TestCopyBug() {
  // This was tickling a register allocation issue with
  // idiv in embenchen/copy.
  function asmModule(){
    'use asm';
    function func() {
      var ret = 0;
      var x = 1, y = 0, z = 0;
      var a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0;
      do {
        y = (x + 0) | 0;
        z = (y | 0) % 2 | 0;
        ret = (y + z + a + b + c + d + e + f + g) | 0;
      } while(0);
      return ret | 0;
    }
    return { func: func };
  }
  var wasm = asmModule();
  var js = eval('(' + asmModule.toString().replace('use asm', '') + ')')();
  assertEquals(js.func(), wasm.func());
})();
