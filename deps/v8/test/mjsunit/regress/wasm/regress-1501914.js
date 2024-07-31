// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax --jit-fuzzing

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

builder.addImport('e', 'f', kSig_v_v);
builder.addExport('f', 0);

const f = builder
              .instantiate({
                'e': {
                  'f': () => {
                    try {
                      throw new Error();
                    } catch (e) { }
                  }
                }
              })
              .exports.f;

function wrapper() {
  f();
}

wrapper();
%PrepareFunctionForOptimization(wrapper);
%OptimizeFunctionOnNextCall(wrapper);
wrapper();
