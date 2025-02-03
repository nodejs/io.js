// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test that there is no lock-order inversion between the `allocation_mutex_`
// (used by `NativeModule::EstimateCurrentMemoryConsumption`) and the `mutex_`
// (used by `DebugInfoImpl::SetBreakpoint`) when running concurrently in
// multiple isolates (see `run-tests.py --isolates`).

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

const builder = new WasmModuleBuilder();

const body_f = [kExprI32Const, 123];
const offset_in_body = body_f.indexOf(kExprI32Const);

builder.addFunction('f', kSig_i_v).addBody(body_f).exportFunc();
const instance = builder.instantiate();

Debug = debug.Debug;

const breakpoint = Debug.setBreakPoint(instance.exports.f, 0, offset_in_body);

%EstimateCurrentMemoryConsumption();
