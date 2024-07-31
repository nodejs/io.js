// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Reduce the stack size to test that we are indeed doing return calls (instead
// of standard calls which consume stack space).
// Flags: --stack-size=128

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

(function TestFactorialReturnCall() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  const sig_i_iii = builder.addType(kSig_i_iii);

  // construct the code for the function
  // f_aux(N,X) where N=<1 => X
  // f_aux(N,X) => f_aux(N-1,X*N)
  let fact_aux = builder.addFunction("fact_aux",kSig_i_ii);
  fact_aux.addBody([
    kExprLocalGet, 0, kExprI32Const, 1, kExprI32LeS,
    kExprIf, kWasmI32,
      kExprLocalGet, 1,
    kExprElse,
      kExprLocalGet, 0,
      kExprI32Const, 1,
      kExprI32Sub,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI32Mul,
      kExprReturnCall, fact_aux.index,
    kExprEnd
      ]);

  //main(N)=>fact_aux(N,1)
  let main = builder.addFunction("main", kSig_i_i)
        .addBody([
          kExprLocalGet, 0,
          kExprI32Const, 1,
          kExprReturnCall,0
    ]).exportFunc();

  let module = builder.instantiate();

  print(" --three--");
  assertEquals(6, module.exports.main(3));
  print(" --four--");
  assertEquals(24, module.exports.main(4));
})();

(function TestIndirectFactorialReturnCall() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  const sig_i_iii = builder.addType(kSig_i_iii);

  // construct the code for the function
  // fact(N) => f_ind(N,1,f).
  //
  // f_ind(N,X,_) where N=<1 => X
  // f_ind(N,X,F) => F(N-1,X*N,F).

  let f_ind = builder.addFunction("f_ind",kSig_i_iii).
      addBody([
    kExprLocalGet, 0, kExprI32Const, 1, kExprI32LeS,
    kExprIf, kWasmI32,
      kExprLocalGet, 1,
    kExprElse,
      kExprLocalGet, 0,
      kExprI32Const, 1,
      kExprI32Sub,
      kExprLocalGet, 0,
      kExprLocalGet, 1,
      kExprI32Mul,
      kExprLocalGet, 2,
      kExprLocalGet, 2,
      kExprReturnCallIndirect, sig_i_iii, kTableZero,
    kExprEnd
      ]);

  //main(N)=>fact_aux(N,1)
  let main = builder.addFunction("main", kSig_i_i)
        .addBody([
      kExprLocalGet, 0,
      kExprI32Const, 1,
      kExprI32Const, f_ind.index,
      kExprReturnCall, f_ind.index
    ]).exportFunc();

  builder.appendToTable([f_ind.index, main.index]);

  let module = builder.instantiate();

  print(" --three--");
  assertEquals(6, module.exports.main(3));
  print(" --four--");
  assertEquals(24, module.exports.main(4));
})();

(function TestImportReturnCall() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  const sig_i_iii = builder.addType(kSig_i_iii);

  let pick = builder.addImport("q", "pick", sig_i_iii);

  let main = builder.addFunction("main", kSig_i_iii)
        .addBody([
          kExprLocalGet, 1,
          kExprLocalGet, 2,
          kExprLocalGet, 0,
          kExprReturnCall, pick
        ])
        .exportFunc();

  let module = builder.instantiate({q: {
    pick: function(a, b, c) { return c ? a : b; }}});

  print(" --left--");
  assertEquals(-2, module.exports.main(1, -2, 3));
  print(" --right--");
  assertEquals(3, module.exports.main(0, -2, 3));
})();

(function TestImportIndirectReturnCall() {
  print(arguments.callee.name);

  let builder = new WasmModuleBuilder();

  const sig_i_iii = builder.addType(kSig_i_iii);

  let pick = builder.addImport("q", "pick", sig_i_iii);
  builder.addTable(kWasmAnyFunc, 4);
  // Arbitrary location in the table.
  const tableIndex = 3;

  builder.addActiveElementSegment(0, wasmI32Const(tableIndex),[pick]);

  let main = builder.addFunction("main", kSig_i_iii)
        .addBody([
          kExprLocalGet, 1,
          kExprLocalGet, 2,
          kExprLocalGet, 0,
          kExprI32Const, tableIndex,
          kExprReturnCallIndirect, sig_i_iii, kTableZero
        ])
        .exportFunc();
  builder.appendToTable([pick, main.index]);

  let module = builder.instantiate({q: {
    pick: function(a, b, c) { return c ? a : b; }
  }});

  print(" --left--");
  assertEquals(-2, module.exports.main(1, -2, 3));
  print(" --right--");
  assertEquals(3, module.exports.main(0, -2, 3));
})();

(function TestMultiReturnCallWithLongSig() {
  print(arguments.callee.name);
  const callee_inputs = 10;
  // Tail call from a function with less, as many, or more parameters than the
  // callee.
  for (caller_inputs = 9; caller_inputs <= 11; ++caller_inputs) {
    let builder = new WasmModuleBuilder();

    // f just returns its arguments in reverse order.
    const f_params = new Array(callee_inputs).fill(kWasmI32);
    const f_returns = f_params;
    const f_sig = builder.addType(makeSig(f_params, f_returns));
    let f_body = [];
    for (i = 0; i < callee_inputs; ++i) {
      f_body.push(kExprLocalGet, callee_inputs - i - 1);
    }
    const f = builder.addFunction("f", f_sig).addBody(f_body);

    // Slice or pad the caller inputs to match the callee.
    const main_params = new Array(caller_inputs).fill(kWasmI32);
    const main_sig = builder.addType(makeSig(main_params, f_returns));
    let main_body = [];
    for (i = 0; i < callee_inputs; ++i) {
      main_body.push(kExprLocalGet, Math.min(caller_inputs - 1, i));
    }
    main_body.push(kExprReturnCall, f.index);
    builder.addFunction("main", main_sig).addBody(main_body).exportFunc();

    let module = builder.instantiate();

    inputs = [];
    for (i = 0; i < caller_inputs; ++i) {
      inputs.push(i);
    }
    let expect = inputs.slice(0, callee_inputs);
    while (expect.length < callee_inputs) {
      expect.push(inputs[inputs.length - 1]);
    }
    expect.reverse();
    assertEquals(expect, module.exports.main(...inputs));
  }
})();
