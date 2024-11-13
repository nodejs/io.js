// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbo-fast-api-calls --expose-fast-api
// Flags: --fast-api-allow-float-in-sim

d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

assertThrows(() => d8.test.FastCAPI());
const fast_c_api = new d8.test.FastCAPI();

function buildWasm(name, sig, body) {
  const builder = new WasmModuleBuilder();
  const add_all_no_options = builder.addImport(
    'fast_c_api',
    'add_all_no_options',
    makeSig(
      [kWasmI32, kWasmI32, kWasmI64, kWasmI64, kWasmF32, kWasmF64],
      [kWasmF64],
    ),
  );
  const add_all_no_options_mismatch = builder.addImport(
    'fast_c_api',
    'add_all_no_options',
    makeSig(
      [kWasmI32, kWasmI32, kWasmI64, kWasmF32, kWasmI64, kWasmF64],
      [kWasmF64],
    ),
  );
  const add_all_nested_bound = builder.addImport(
    'fast_c_api',
    'add_all_nested_bound',
    makeSig(
      [kWasmI32, kWasmI32, kWasmI64, kWasmI64, kWasmF32, kWasmF64],
      [kWasmF64],
    ),
  );
  const overloaded_add_all_32bit_int = builder.addImport(
    'fast_c_api',
    'overloaded_add_all_32bit_int',
    makeSig(
      [kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32, kWasmI32],
      [kWasmI32],
    ),
  );
  const test_wasm_memory = builder.addImport(
    'fast_c_api',
    'test_wasm_memory',
    makeSig([kWasmI32], [kWasmI32]),
  );

  const throw_no_fallback = builder.addImport(
      'fast_c_api', 'throw_no_fallback', makeSig([], [kWasmI32]));

  const mem_index = builder.addMemory(1, 1);
  builder.exportMemoryAs("memory", mem_index);
  builder.addFunction(name, sig)
      .addBody(body({
        add_all_no_options,
        add_all_no_options_mismatch,
        add_all_nested_bound,
        overloaded_add_all_32bit_int,
        test_wasm_memory,
        throw_no_fallback
      }))
      .exportFunc();

  const x = {};
  const module = builder.instantiate({
    fast_c_api: {
      add_all_no_options: fast_c_api.add_all_no_options.bind(fast_c_api),
      add_all_no_options_mismatch: fast_c_api.add_all_no_options.bind(fast_c_api),
      add_all_nested_bound: fast_c_api.add_all_no_options
        .bind(fast_c_api)
        .bind(x),
      overloaded_add_all_32bit_int: fast_c_api.overloaded_add_all_32bit_int_no_sig.bind(fast_c_api),
      test_wasm_memory: fast_c_api.test_wasm_memory.bind(fast_c_api),
      throw_no_fallback: fast_c_api.throw_no_fallback.bind(fast_c_api),
    },
  });
  fast_c_api.wasm_memory = module.exports.memory;
  return module.exports[name];
}

// ----------- add_all -----------
// `add_all` has the following signature:
// double add_all(bool /*should_fallback*/, int32_t, uint32_t,
//   int64_t, uint64_t, float, double)

const max_safe_float = 2**24 - 1;
const add_all_result = -42 + 45 + Number.MIN_SAFE_INTEGER + Number.MAX_SAFE_INTEGER +
  max_safe_float * 0.5 + Math.PI;

const add_all_wasm = buildWasm(
  'add_all_wasm', makeSig([], [kWasmF64]),
  ({ add_all_no_options }) => [
    ...wasmI32Const(-42),
    ...wasmI32Const(45),
    kExprI64Const, 0x81, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x70, // Number.MIN_SAFE_INTEGER
    kExprI64Const, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, // Number.MAX_SAFE_INTEGER
    ...wasmF32Const(max_safe_float * 0.5),
    ...wasmF64Const(Math.PI),
    kExprCallFunction, add_all_no_options,
    kExprReturn,
  ],
);

if (fast_c_api.supports_fp_params) {
  // Test wasm hits fast path.
  fast_c_api.reset_counts();
  assertEquals(add_all_result, add_all_wasm());
  assertEquals(1, fast_c_api.fast_call_count());
  assertEquals(0, fast_c_api.slow_call_count());
} else {
  // Test wasm hits slow path.
  fast_c_api.reset_counts();
  assertEquals(add_all_result, add_all_wasm());
  assertEquals(0, fast_c_api.fast_call_count());
  assertEquals(1, fast_c_api.slow_call_count());
}

// ----------- Test add_all signature mismatch -----------

const add_all_mismatch_wasm = buildWasm(
  'add_all_mismatch_wasm', makeSig([], [kWasmF64]),
  ({ add_all_no_options_mismatch }) => [
    ...wasmI32Const(45),
    ...wasmI32Const(-42),
    kExprI64Const, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, // Number.MAX_SAFE_INTEGER
    ...wasmF32Const(max_safe_float * 0.5),
    kExprI64Const, 0x81, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x70, // Number.MIN_SAFE_INTEGER
    ...wasmF64Const(Math.PI),
    kExprCallFunction, add_all_no_options_mismatch,
    kExprReturn,
  ],
);

// Test that wasm takes slow path.
fast_c_api.reset_counts();
add_all_mismatch_wasm();
assertEquals(0, fast_c_api.fast_call_count());
assertEquals(1, fast_c_api.slow_call_count());

// ----------- Test add_all nested bound function -----------

const add_all_nested_bound_wasm = buildWasm(
  'add_all_nested_bound_wasm', makeSig([], [kWasmF64]),
  ({ add_all_nested_bound }) => [
    ...wasmI32Const(-42),
    ...wasmI32Const(45),
    kExprI64Const, 0x81, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x70, // Number.MIN_SAFE_INTEGER
    kExprI64Const, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, // Number.MAX_SAFE_INTEGER
    ...wasmF32Const(max_safe_float * 0.5),
    ...wasmF64Const(Math.PI),
    kExprCallFunction, add_all_nested_bound,
    kExprReturn,
  ],
);

// Test wasm hits slow path.
fast_c_api.reset_counts();
assertEquals(add_all_result, add_all_nested_bound_wasm());
assertEquals(0, fast_c_api.fast_call_count());
assertEquals(1, fast_c_api.slow_call_count());

// ----------- Test overloaded_add_all_32bit_int -----------
const overloaded_add_all_32bit_int_wasm = buildWasm(
  'overloaded_add_all_32bit_int_wasm', makeSig([], [kWasmI32]),
  ({ overloaded_add_all_32bit_int }) => [
    ...wasmI32Const(1),
    ...wasmI32Const(2),
    ...wasmI32Const(3),
    ...wasmI32Const(4),
    ...wasmI32Const(5),
    ...wasmI32Const(6),
    kExprCallFunction, overloaded_add_all_32bit_int,
    kExprReturn,
  ],
);

const overload_result = 1 + 2 + 3 + 4 + 5 + 6;

// Test wasm hits fast path.
fast_c_api.reset_counts();
assertEquals(overload_result, overloaded_add_all_32bit_int_wasm());
assertEquals(1, fast_c_api.fast_call_count());
assertEquals(0, fast_c_api.slow_call_count());

// ------------- Test test_wasm_memory ---------------
const test_wasm_memory_wasm = buildWasm(
  'test_wasm_memory_wasm', makeSig([], [kWasmI32]),
  ({ test_wasm_memory }) => [
    ...wasmI32Const(12),
    kExprCallFunction, test_wasm_memory,
    kExprDrop,
    ...wasmI32Const(12),
    kExprI32LoadMem8U, 0, 0,
  ],
);

// Test hits fast path.
fast_c_api.reset_counts();
assertEquals(42, test_wasm_memory_wasm())
assertEquals(1, fast_c_api.fast_call_count());
assertEquals(0, fast_c_api.slow_call_count());

const test_throw_no_fallback = buildWasm(
    'test_throw_no_fallback', makeSig([], [kWasmI32]),
    ({throw_no_fallback}) => [kExprCallFunction, throw_no_fallback]);

fast_c_api.reset_counts();
assertThrows(test_throw_no_fallback, Error, 'Exception from fast callback');
assertEquals(1, fast_c_api.fast_call_count());
assertEquals(0, fast_c_api.slow_call_count());
