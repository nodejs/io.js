// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --wasm-staging

(function TestFunctionConstructedCoercions() {
  let obj1 = { valueOf: _ => 123.45 };
  let obj2 = { toString: _ => "456" };
  let gcer = { valueOf: _ => gc() };
  let testcases = [
    { params: { sig: [],
                val: [],
                exp: [], },
      result: { sig: ["i32", "f32"],
                val: [42.7, "xyz"],
                exp: [42, NaN] },
    },
    { params: { sig: [],
                val: [],
                exp: [], },
      result: { sig: ["i32", "f32", "f64"],
                val: (function* () { yield obj1;  yield obj2; yield "789" })(),
                exp: [123,   456,   789], },
    },
    { params: { sig: [],
                val: [],
                exp: [], },
      result: { sig: ["i32", "f32", "f64"],
                val: new Proxy([gcer, {}, "xyz"], {
                  get: function(obj, prop) { return Reflect.get(obj, prop); }
                }),
                exp: [0,     NaN,   NaN], },
    },
  ];
  testcases.forEach(function({params, result}) {
    let p = params.sig; let r = result.sig; var params_after;
    function testFun() { params_after = arguments; return result.val; }
    let fun = new WebAssembly.Function({parameters:p, results:r}, testFun);
    let result_after = fun.apply(undefined, params.val);
    assertArrayEquals(params.exp, params_after);
    assertEquals(result.exp, result_after);
  });
})();

(function TestFunctionConstructedCoercionsThrow() {
  let proxy_throw = new Proxy([1, 2], {
    get: function(obj, prop) {
      if (prop == 1) {
        throw new Error("abc");
      }
      return Reflect.get(obj, prop); },
  });
  function* generator_throw() {
    yield 1;
    throw new Error("def");
  }
  let testcases = [
    { val: 0,
      error: Error,
      msg: /not iterable/ },
    { val: [1],
      error: TypeError,
      msg: /multi-return length mismatch/ },
    { val: [1, 2, 3],
      error: TypeError,
      msg: /multi-return length mismatch/ },
    { val: proxy_throw,
      error: Error,
      msg: /abc/ },
    { val: generator_throw(),
      error: Error,
      msg: /def/ },
  ];
  testcases.forEach(function({val, error, msg}) {
    fun = new WebAssembly.Function({parameters:[], results:["i32", "i32"]},
        () => val);
    assertThrows(fun, error, msg);
  })
})();
