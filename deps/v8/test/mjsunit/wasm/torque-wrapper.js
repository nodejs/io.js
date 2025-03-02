// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc
//
d8.file.execute('test/mjsunit/wasm/wasm-module-builder.js');

// This test randomly generates WebAssembly signatures, creates WebAssembly
// functions with the generated signature which randomly maps parameters to
// returns, and then calls the WebAssembly function from JavaScript with some
// inputs and checks the return values. `RunTest` is the core function, which
// runs the test case for a specific signature and value mapping. `RunTest can
// be used well for debugging a specific signature.
// `GenerateAndRunTest` additionally generates the inputs for `RunTest`, which
// is good for fuzzing the js-to-wasm wrappers.

// Some documentation if the test fails in the CQ.
console.log("This test is a fuzzer, it tests the generic js-to-wasm wrapper");
console.log("with random signatures. If this test fails, then it may not fail");
console.log("for the CL that actually introduced the issue, but for a");
console.log("later CL in the CQ. You may want to use flako to identify that");
console.log("actual culprit.");

const debug = false;

let kSig_r_i = makeSig([kWasmI32], [kWasmExternRef]);
const kPossibleTypes = [kWasmI32, kWasmI64, kWasmF32, kWasmF64, kWasmExternRef];

function getRandomInt(max) {
  return Math.floor(Math.random() * max);
}

function TypeString(type) {
  switch (type) {
    case kWasmI32:
      return 'kWasmI32';
    case kWasmI64:
      return 'kWasmI64';
    case kWasmF32:
      return 'kWasmF32';
    case kWasmF64:
      return 'kWasmF64';
    case kWasmExternRef:
      return 'kWasmExternRef';
  }
}

function ConvertFunctionName(to, from) {
  return TypeString(from) + 'To' + TypeString(to);
}

// The ConversionFunction mimics the value conversion done within the wasm
// function, in addition to the `ToPrimitive` conversion done in the js-to-wasm
// wrapper.
function ConversionFunction(to, from, val) {
  if (to == from) {
    // If {to} and {from} are the same type, then we only have to do the
    // conversion done in the js-to-wasm wrapper.
    if (to == kWasmI64) return BigInt.asIntN(64, val);
    if (to == kWasmExternRef) return val;

    // In all other cases convert {val} into a Number so that the `valueOf`
    // function of an object gets called.
    return Number(val);
  }
  if (from == kWasmExternRef) {
    if (val == null) {
      val = 0;
    } else {
      val = val.value;
    }
  }
  if (from == kWasmI64) {
    // Convert BigInt to BigInt64.
    val = BigInt.asIntN(64, val);
  }
  switch (to) {
    case kWasmI32:
      if (from == kWasmI64) return Number(BigInt.asIntN(32, val));
      // Imitate the saturating conversions in WebAssembly.
      if (val > 0x7fff_ffff) return 0x7fff_ffff;
      if (val < -0x8000_0000) return -0x8000_0000;
      return val | 0;
    case kWasmI64:
      // Imitate the saturating conversions in WebAssembly.
      // Compare with 0x8000..., because 0x7fff... is not representable as
      // float64.
      if (val >= 0x8000_0000_0000_0000) return 0x7fff_ffff_ffff_ffffn;
      if (val < -0x8000_0000_0000_0000) return -0x8000_0000_0000_0000n;
      return BigInt(Math.trunc(val));
    case kWasmF32:
      return Math.fround(Number(val));
    case kWasmF64:
      return Number(val);
    case kWasmExternRef:
      return itoo(ConversionFunction(kWasmI32, from, val));
  }
}

function ConvertOpcode(to, from, itooIndex, otoiIndex) {
  if (from == kWasmExternRef) {
    if (to == kWasmExternRef) {
      return [kExprNop];
    }
    return [
      kExprCallFunction, otoiIndex,
      ...ConvertOpcode(to, kWasmI32, itooIndex, otoiIndex)
    ];
  }
  switch (to) {
    case kWasmI32:
      switch (from) {
        case kWasmI32:
          return [kExprNop];
        case kWasmI64:
          return [kExprI32ConvertI64];
        case kWasmF32:
          return [kNumericPrefix, kExprI32SConvertSatF32];
        case kWasmF64:
          return [kNumericPrefix, kExprI32SConvertSatF64];
      }
    case kWasmI64:
      switch (from) {
        case kWasmI32:
          return [kExprI64SConvertI32];
        case kWasmI64:
          return [kExprNop];
        case kWasmF32:
          return [kNumericPrefix, kExprI64SConvertSatF32];
        case kWasmF64:
          return [kNumericPrefix, kExprI64SConvertSatF64];
      }
    case kWasmF32:
      switch (from) {
        case kWasmI32:
          return [kExprF32SConvertI32];
        case kWasmI64:
          return [kExprF32SConvertI64];
        case kWasmF32:
          return [kExprNop];
        case kWasmF64:
          return [kExprF32ConvertF64];
      }
    case kWasmF64:
      switch (from) {
        case kWasmI32:
          return [kExprF64SConvertI32];
        case kWasmI64:
          return [kExprF64SConvertI64];
        case kWasmF32:
          return [kExprF64ConvertF32];
        case kWasmF64:
          return [kExprNop];
      }
    case kWasmExternRef:
      return [
        ...ConvertOpcode(kWasmI32, from, itooIndex, otoiIndex),
        kExprCallFunction, itooIndex
      ];
  }
}

function itoo(val) {
  return {value: val};
}

function otoi(val) {
  return val.value;
}

// A set of interesting values that will be used as parameters.
let interestingParams = {};
interestingParams[kWasmI32] = [
  -0x8000_0000, -1500000000, -0x4000_0000, -0x3fff_ffff, -1, 0, 1, 2,
  0x3fff_ffff, 0x4000_0000, 1500000000, 0x7fff_ffff, {valueOf: () => 15}, {
    toString: () => {
      gc();
      return '17';
    }
  }
];

interestingParams[kWasmI64] = [
  -0xffffffffffffffffn,
  -0x8000_0000_0000_0000n,
  -0x7fff_ffff_ffff_ffffn,
  -0xffff_ffffn,
  -0x8000_0000n,
  -1500000000n,
  -0x4000_0000n,
  -0x3fff_ffffn,
  -1n,
  0n,
  1n,
  2n,
  0x3fff_ffffn,
  0x4000_0000n,
  1500000000n,
  0x7fff_ffffn,
  0x8000_0000n,
  0xffff_ffffn,
  0x7fff_ffff_ffff_ffffn,
  0x8000_0000_0000_0000n,
  0xffff_ffff_ffff_ffffn,
  {valueOf: () => 15n},
  {
    toString: () => {
      gc();
      return '17';
    }
  }
];

interestingParams[kWasmF32] = [
  -Infinity,
  -(2 ** 65),
  -(2 ** 64),
  -(2 ** 63),
  -(2 ** 62),
  -(2 ** 29),
  -(2 ** 33),
  -(2 ** 32),
  -(2 ** 31),
  -(2 ** 30),
  -(2 ** 29),
  -1.5,
  -1,
  0,
  -0,
  1,
  1.5,
  2,
  2 ** 29,
  2 ** 30,
  2 ** 31,
  2 ** 32,
  2 ** 33,
  2 ** 62,
  2 ** 63,
  2 ** 64,
  2 ** 65,
  {valueOf: () => 16},
  {
    toString: () => {
      gc();
      return '18';
    }
  },
  Infinity
];

interestingParams[kWasmF64] = [
  -Infinity,
  -(2 ** 65),
  -(2 ** 64),
  -(2 ** 63),
  -(2 ** 62),
  -(2 ** 29),
  -(2 ** 33),
  -(2 ** 32),
  -(2 ** 31),
  -(2 ** 30),
  -(2 ** 29),
  -1.5,
  -1,
  0,
  -0,
  1,
  1.5,
  2,
  // 28 set bits, too many for float32.
  0xfffffff,
  2 ** 29,
  2 ** 30,
  2 ** 31,
  2 ** 32,
  2 ** 33,
  2 ** 62,
  2 ** 63,
  2 ** 64,
  2 ** 65,
  {valueOf: () => 14},
  {
    toString: () => {
      gc();
      return '19';
    }
  },
  Infinity
];

function GetParam(type) {
  if (type == kWasmExternRef) {
    return itoo(interestingParams[kWasmI32][getRandomInt(
        interestingParams[kWasmI32].length)]);
  }
  return interestingParams[type][getRandomInt(interestingParams[type].length)];
}

function GenerateAndRunTest() {
  // Generate signature
  const kMaxParams = 20;
  const kMaxReturns = 10;
  const numParams = getRandomInt(kMaxParams) + 1;
  const numReturns = getRandomInt(kMaxReturns + 1);
  // The array of parameter types.
  const params = [];
  // The array of return types.
  const returns = [];
  // This array stores which parameters map to which returns. {map} has the same
  // length as {returns}. `map[i] == j` means that parameter {j} gets returned
  // by return {i}.
  const map = [];
  for (let i = 0; i < numParams; ++i) {
    params.push(kPossibleTypes[getRandomInt(kPossibleTypes.length)]);
  }
  for (let i = 0; i < numReturns; ++i) {
    returns.push(kPossibleTypes[getRandomInt(kPossibleTypes.length)]);
  }
  for (let i = 0; i < numReturns; ++i) {
    map.push(getRandomInt(numParams));
  }
  RunTest(params, returns, map);
}

function RunTest(params, returns, map) {
  if (debug) {
    for (let i = 0; i < map.length; ++i) {
      map[i] = map[i] % params.length;
    }
    while (map.length < returns.length) {
      map.push(0);
    }
    let testcase = 'RunTest([';
    for (let i = 0; i < params.length; ++i) {
      testcase += (i == 0 ? '' : ', ') + TypeString(params[i]);
    }
    testcase += '], [';
    if (returns.length > 0) {
      testcase += TypeString(returns[0]);
      for (let i = 1; i < returns.length; ++i) {
        testcase += ', ' + TypeString(returns[i]);
      }
    }
    testcase += '], [' + map + ']);';
    print(testcase);
  }
  const builder = new WasmModuleBuilder();

  const itooIndex = builder.addImport('imp', 'itoo', kSig_r_i);
  const otoiIndex = builder.addImport('imp', 'otoi', kSig_i_r);

  const sig = makeSig(params, returns);
  // Generate module
  const body = [];
  for (let i = 0; i < returns.length; ++i) {
    body.push(kExprLocalGet);
    body.push(map[i]);

    body.push(
        ...ConvertOpcode(returns[i], params[map[i]], itooIndex, otoiIndex));
  }
  builder.addFunction('main', sig).addBody(body).exportFunc();

  const instance = builder.instantiate({imp: {itoo: itoo, otoi: otoi}});

  // Generate call.
  const args = [];
  for (let i = 0; i < params.length; ++i) {
    args.push(GetParam(params[i]));
  }
  let result = instance.exports.main(...args);
  if (returns.length == 0) {
    assertEquals(result, undefined);
    return;
  }
  if (returns.length == 1) {
    // Turn result into an array, so that the code below can be used.
    result = [result];
  }

  // Check result
  for (let i = 0; i < returns.length; ++i) {
    let details = undefined;
    if (debug) {
      details = `${i}, converting ${args[map[i]]} \
                 from ${TypeString(params[map[i]])} \
                 to ${TypeString(returns[i])}`;
    }
    assertEquals(
        ConversionFunction(returns[i], params[map[i]], args[map[i]]),
        result[i], details);
  }
}

// Regression test for crbug.com/390035515.
RunTest(
    [
      kWasmI64, kWasmI64, kWasmI64, kWasmI64, kWasmI64, kWasmI64, kWasmI64,
      kWasmI64, kWasmI64, kWasmI64
    ],
    [kWasmI64], [0]);

for (let i = 0; i < (debug ? 200 : 2); ++i) {
  GenerateAndRunTest();
}
