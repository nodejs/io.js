// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

function builder() {
  return new WasmModuleBuilder;
}

function assertCompileError(bytes, msg) {
  assertThrows(
      () => new WebAssembly.Module(bytes), WebAssembly.CompileError,
      'WebAssembly.Module(): ' + msg);
  assertThrowsAsync(
      WebAssembly.compile(bytes), WebAssembly.CompileError,
      'WebAssembly.compile(): ' + msg);
}

function assertInstantiateError(error, bytes, imports = {}, msg) {
  assertThrows(
      () => new WebAssembly.Instance(new WebAssembly.Module(bytes), imports),
      error, 'WebAssembly.Instance(): ' + msg);
  assertThrowsAsync(
      WebAssembly.instantiate(bytes, imports), error,
      'WebAssembly.instantiate(): ' + msg);
}

// default imports to {} so we get LinkError by default, thus allowing us to
// distinguish the TypeError we want to catch
function assertTypeError(bytes, imports = {}, msg) {
  assertInstantiateError(TypeError, bytes, imports, msg);
}

function assertLinkError(bytes, imports, msg) {
  assertInstantiateError(WebAssembly.LinkError, bytes, imports, msg);
}

function assertConversionError(bytes, imports, msg) {
  let instance =
      new WebAssembly.Instance(new WebAssembly.Module(bytes), imports);
  assertThrows(() => instance.exports.run(), TypeError, msg);
}

(function TestDecodingError() {
  print(arguments.callee.name);
  assertCompileError(bytes(), 'BufferSource argument is empty');
  assertCompileError(bytes('X'), 'expected 4 bytes, fell off end @+0');
  assertCompileError(
      bytes('\0x00asm'),
      'expected magic word 00 61 73 6d, found 00 78 30 30 @+0');
})();

(function TestValidationError() {
  print(arguments.callee.name);
  let error = msg => 'Compiling function #0 failed: ' + msg;
  let f_error = msg => 'Compiling function #0:"f" failed: ' + msg;
  assertCompileError(
      (function build() {
        let b = builder();
        b.addType(kSig_v_v);
        // Use explicit section because the builder would automatically emit
        // e.g. locals declarations.
        // 1 function with type 0.
        b.addExplicitSection([kFunctionSectionCode, 2, 1, 0]);
        // 1 function body with length 0.
        b.addExplicitSection([kCodeSectionCode, 2, 1, 0]);
        return b.toBuffer();
      })(),
      error('reached end while decoding local decls count @+22'));
  assertCompileError(
      builder().addFunction('f', kSig_i_v).end().toBuffer(),
      f_error('function body must end with "end" opcode @+24'));
  assertCompileError(
      builder().addFunction('f', kSig_i_v).addBody([kExprReturn])
          .end().toBuffer(),
      f_error('expected 1 elements on the stack for return, found 0 @+24'));
  assertCompileError(builder().addFunction('f', kSig_v_v).addBody([
    kExprLocalGet, 0
  ]).end().toBuffer(), f_error('invalid local index: 0 @+24'));
  assertCompileError(
      builder().addStart(0).toBuffer(),
      'function index 0 out of bounds (0 entries) @+10');
})();

function import_error(index, module, func, msg) {
  let full_msg = 'Import #' + index + ' \"' + module + '\"';
  if (func !== undefined) full_msg += ' \"' + func + '\"';
  return full_msg + ': ' + msg;
}

(function TestTypeError() {
  print(arguments.callee.name);
  let b = builder();
  b.addImport('foo', 'bar', kSig_v_v);
  let msg =
      import_error(0, 'foo', undefined, 'module is not an object or function');
  assertTypeError(b.toBuffer(), {}, msg);

  b = builder();
  b.addImportedGlobal('foo', 'bar', kWasmI32);
  assertTypeError(b.toBuffer(), {}, msg);

  b = builder();
  b.addImportedMemory('foo', 'bar');
  assertTypeError(b.toBuffer(), {}, msg);
})();

(function TestLinkingError() {
  print(arguments.callee.name);
  let b;
  let msg;

  b = builder();
  msg = import_error(0, 'foo', 'bar', 'function import requires a callable');
  b.addImport('foo', 'bar', kSig_v_v);
  assertLinkError(b.toBuffer(), {foo: {}}, msg);
  b = builder();
  b.addImport('foo', 'bar', kSig_v_v);
  assertLinkError(b.toBuffer(), {foo: {bar: 9}}, msg);

  b = builder();
  msg = import_error(
      0, 'foo', 'bar',
      'global import must be a number, valid Wasm reference, '
        + 'or WebAssembly.Global object');
  b.addImportedGlobal('foo', 'bar', kWasmI32);
  assertLinkError(b.toBuffer(), {foo: {}}, msg);
  b = builder();
  b.addImportedGlobal('foo', 'bar', kWasmI32);
  assertLinkError(b.toBuffer(), {foo: {bar: ''}}, msg);
  b = builder();
  b.addImportedGlobal('foo', 'bar', kWasmI32);
  assertLinkError(b.toBuffer(), {foo: {bar: () => 9}}, msg);

  b = builder();
  msg = import_error(
      0, 'foo', 'bar', 'memory import must be a WebAssembly.Memory object');
  b.addImportedMemory('foo', 'bar');
  assertLinkError(b.toBuffer(), {foo: {}}, msg);
  b = builder();
  b.addImportedMemory('foo', 'bar', 1);
  assertLinkError(
      b.toBuffer(), {foo: {bar: () => new WebAssembly.Memory({initial: 0})}},
      msg);
})();

(function TestTrapUnreachable() {
  print(arguments.callee.name);
  let instance = builder().addFunction('run', kSig_v_v)
    .addBody([kExprUnreachable]).exportFunc().end().instantiate();
  assertTraps(kTrapUnreachable, instance.exports.run);
})();

(function TestTrapDivByZero() {
  print(arguments.callee.name);
  let instance = builder().addFunction('run', kSig_v_v).addBody(
     [kExprI32Const, 1, kExprI32Const, 0, kExprI32DivS, kExprDrop])
    .exportFunc().end().instantiate();
  assertTraps(kTrapDivByZero, instance.exports.run);
})();

(function TestUnreachableInStart() {
  print(arguments.callee.name);

  let b = builder().addFunction("start", kSig_v_v).addBody(
     [kExprUnreachable]).end().addStart(0);
  assertTraps(kTrapUnreachable, () => b.instantiate());
})();

(function InternalDebugTrace() {
  print(arguments.callee.name);
  var builder = new WasmModuleBuilder();
  var sig = builder.addType(kSig_i_dd);
  builder.addImport("mod", "func", sig);
  builder.addFunction("main", sig)
    .addBody([kExprLocalGet, 0, kExprLocalGet, 1, kExprCallFunction, 0])
    .exportAs("main");
  var main = builder.instantiate({
    mod: {
      func: ()=>{%DebugTrace();}
    }
  }).exports.main;
  main();
})();

(function TestMultipleCorruptFunctions() {
  print(arguments.callee.name);
  // Generate a module with multiple corrupt functions. The error message must
  // be deterministic.
  var builder = new WasmModuleBuilder();
  var sig = builder.addType(kSig_v_v);
  for (let i = 0; i < 10; ++i) {
    builder.addFunction('f' + i, sig).addBody([kExprEnd]);
  }
  assertCompileError(
      builder.toBuffer(),
      'Compiling function #0:"f0" failed: ' +
          'trailing code after function end @+33');
})();
