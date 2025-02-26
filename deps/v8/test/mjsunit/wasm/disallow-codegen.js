// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

let kReturnValue = 19;

let buffer = (function CreateBuffer() {
  let builder = new WasmModuleBuilder();
  builder.addMemory(1, 1);
  builder.addFunction('main', kSig_i_v)
      .addBody([kExprI32Const, kReturnValue])
      .exportFunc();

  return builder.toBuffer();
})();

%DisallowWasmCodegen(true);

function SyncTestOk() {
  print('sync module compile (ok)...');
  %DisallowWasmCodegen(false);
  let module = new WebAssembly.Module(buffer);
  assertInstanceof(module, WebAssembly.Module);
}

function SyncTestWasmFail() {
  print('sync wasm module compile (fail)...');
  %DisallowWasmCodegen(true);
  try {
    let module = new WebAssembly.Module(buffer);
    assertUnreachable();
  } catch (e) {
    print("  " + e);
    assertInstanceof(e, WebAssembly.CompileError);
  }
}

async function AsyncTestOk() {
  print('async module compile (ok)...');
  %DisallowWasmCodegen(false);
  let promise = WebAssembly.compile(buffer);
  assertPromiseResult(
    promise, module => assertInstanceof(module, WebAssembly.Module));
}

async function AsyncTestWithInstantiateOk() {
  print('async module instantiate (ok)...');
  %DisallowWasmCodegen(false);
  let promise = WebAssembly.instantiate(buffer);
  assertPromiseResult(
      promise,
      module => assertInstanceof(module.instance, WebAssembly.Instance));
}

async function AsyncTestWasmFail() {
  print('async wasm module compile (fail)...');
  %DisallowWasmCodegen(true);
  try {
    let m = await WebAssembly.compile(buffer);
    assertUnreachable();
  } catch (e) {
    print("  " + e);
    assertInstanceof(e, WebAssembly.CompileError);
  }
}

async function AsyncTestWasmWithInstantiateFail() {
  print('async wasm module instantiate (fail)...');
  %DisallowWasmCodegen(true);
  try {
    let m = await WebAssembly.instantiate(buffer);
    assertUnreachable();
  } catch (e) {
    print("  " + e);
    assertInstanceof(e, WebAssembly.CompileError);
  }
}

async function StreamingTestOk() {
  print('streaming module compile (ok)...');
  // TODO(titzer): compileStreaming must be supplied by embedder.
  // (and it takes a response, not a buffer)
  %DisallowWasmCodegen(false);
  if ("Function" != typeof WebAssembly.compileStreaming) {
    print("  no embedder for streaming compilation");
    return;
  }
  let promise = WebAssembly.compileStreaming(buffer);
  await assertPromiseResult(
    promise, module => assertInstanceof(module, WebAssembly.Module));
}

async function StreamingTestFail() {
  print('streaming module compile (fail)...');
  %DisallowWasmCodegen(false);
  // TODO(titzer): compileStreaming must be supplied by embedder.
  // (and it takes a response, not a buffer)
  if ("Function" != typeof WebAssembly.compileStreaming) {
    print("  no embedder for streaming compilation");
    return;
  }
  try {
    let m = await WebAssembly.compileStreaming(buffer);
    assertUnreachable();
  } catch (e) {
    print("  " + e);
    assertInstanceof(e, WebAssembly.CompileError);
  }
}


async function StreamingTestWasmFail() {
  print('streaming wasm module compile (fail)...');
  %DisallowWasmCodegen(true);
  // TODO(titzer): compileStreaming must be supplied by embedder.
  // (and it takes a response, not a buffer)
  if ("Function" != typeof WebAssembly.compileStreaming) {
    print("  no embedder for streaming compilation");
    return;
  }
  try {
    let m = await WebAssembly.compileStreaming(buffer);
    assertUnreachable();
  } catch (e) {
    print("  " + e);
    assertInstanceof(e, WebAssembly.CompileError);
  }
}

async function RunAll() {
  SyncTestOk();
  AsyncTestOk();
  await StreamingTestOk();
  await StreamingTestFail();
  SyncTestWasmFail();
  await AsyncTestWasmFail();
  await AsyncTestWasmWithInstantiateFail();
  await StreamingTestWasmFail()
}

assertPromiseResult(RunAll());
