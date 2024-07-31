// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function ObjectWithKeys(count, keyOffset, keyGen) {
  if (keyOffset === undefined) keyOffset = 0;
  if (keyGen === undefined) keyGen = (i) => { return "key" + i };
  var o = {};
  for (var i = 0; i < count; i++) {
    var key = keyGen(i + keyOffset);
    o[key] = "value";
  }
  return o
}

function ObjectWithMixedKeys(count, keyOffset) {
  return ObjectWithKeys(count, keyOffset, (key) => {
    if (key % 2 == 0) return key;
    return "key" + key;
  });
}

// Create an object with #depth prototypes each having #keys properties.
function ObjectWithProtoKeys(depth, keys, cacheable) {
  var o = ObjectWithKeys(keys);
  var current = o;
  var keyOffset = 0;
  for (var i = 0; i < depth; i++) {
    keyOffset += keys;
    current.__proto__ = ObjectWithKeys(keys, keyOffset);
    current = current.__proto__;
  }
  if (cacheable === false) {
    // Add an empty proxy at the prototype chain to make caching properties
    // impossible.
    current.__proto__ = new Proxy({}, {});
  }
  return o;
}

function HoleyIntArray(size) {
  var array = new Array(size);
  for (var i = 0; i < size; i += 3) {
    array[i] = i;
  }
  return array
}

function IntArray(size) {
  var array = new Array(size);
  for (var i = 0; i < size; i++) {
    array[i] = i;
  }
  return array;
}

// ============================================================================
var object_empty = {};
var array_empty = [];

var array_int_50 = IntArray(50);
var array_int_50_proto_elements = IntArray(50);
array_int_50_proto_elements.__proto__ = [51, 52, 53, 54];
var array_int_holey_50 = HoleyIntArray(50);

var empty_proto_5_10 = ObjectWithKeys(5);
empty_proto_5_10.__proto__ = ObjectWithProtoKeys(10, 0);

var empty_proto_5_5_slow = ObjectWithKeys(5);
empty_proto_5_5_slow.__proto__ = ObjectWithProtoKeys(5, 0, false);

var object_elements_proto_5_10 = ObjectWithKeys(5);
object_elements_proto_5_10.__proto__ = ObjectWithProtoKeys(10, 0);
// Add some properties further up the prototype chain, the rest stays
// empty.
for (var i = 0; i < 5; i++) {
  object_elements_proto_5_10.__proto__.__proto__.__proto__["proto" + i] = true;
}

var TestObjects = {
  object_empty: object_empty,
  array_empty: array_empty,
  array_int_50: array_int_50,
  array_int_holey_50: array_int_holey_50,
  array_int_50_proto_elements: array_int_50_proto_elements,
  empty_proto_5_10: empty_proto_5_10,
  empty_proto_5_5_slow: empty_proto_5_5_slow,
  object_elements_proto_5_10: object_elements_proto_5_10
}

var TestArrays = {
  array_empty: array_empty,
  array_int_50: array_int_50,
  array_int_holey_50: array_int_holey_50,
  array_int_50_proto_elements: array_int_50_proto_elements,
}

// ============================================================================

function CreateTestFunctionGen(fn) {
  // Force a new function for each test-object to avoid side-effects due to ICs.
  return (object) => {
    var random_comment = "\n// random comment" + Math.random() + "\n";
    return eval(random_comment + fn.toString());
  }
}

var TestFunctions = {
  "Object.keys()": CreateTestFunctionGen(() => {return Object.keys(object)}),
  "for-in": CreateTestFunctionGen(() => {
    var count = 0;
    var result;
    for (var key in object) {
      count++;
      result = object[key];
    };
    return [result, count];
  }),
  "for-in hasOwnProperty()": CreateTestFunctionGen(() => {
    var count = 0;
    var result;
    for (var key in object) {
      if (!object.hasOwnProperty(key)) continue;
      count++;
      result = object[key];
    };
    return [result, count];
  }),
  "for (i < Object.keys().length)": CreateTestFunctionGen(() => {
    var count = 0;
    var result;
    var keys = Object.keys(object);
    for (var i = 0; i < keys.length; i++) {
      count++;
      result = object[keys[i]];
    };
    return [result, count];
  }),
  "Object.keys().forEach()": CreateTestFunctionGen(() => {
    var count = 0;
    var result;
    Object.keys(object).forEach((value, index, obj) => {
      count++;
      result = value;
    });
    return [result, count];
  }),
}

var TestFunctionsArrays = {
  "for (i < array.length)": CreateTestFunctionGen(() => {
    var count = 0;
    var result;
    for (var i = 0; i < object.length; i++) {
      count++;
      result = object[i];
    };
    return [result, count];
  }),
  "for (i < length)": CreateTestFunctionGen(() => {
    var count = 0;
    var result;
    var length = object.length;
    for (var i = 0; i < length; i++) {
      count++;
      result = object[i];
    };
    return [result, count];
  })
}

// ============================================================================
// Create the benchmark suites. We create a suite for each of the test
// functions above and each suite contains benchmarks for each object type.
var Benchmarks = [];

function NewBenchmark(
    test_function_gen, test_function_name, test_object, test_object_name) {
  var object = test_object;
  var name = test_function_name + " " + test_object_name;
  var test_function = test_function_gen(object);
  return new Benchmark(name, false, false, 0, test_function)
}

for (var test_function_name in TestFunctions) {
  var test_function_gen = TestFunctions[test_function_name];
  var benchmarks = [];
  for (var test_object_name in TestObjects) {
    var test_object = TestObjects[test_object_name];
    var benchmark = NewBenchmark(
        test_function_gen, test_function_name, test_object, test_object_name);
    benchmarks.push(benchmark);
  }
  Benchmarks.push(new BenchmarkSuite(test_function_name, [100], benchmarks));
}

for (var test_function_name in TestFunctionsArrays) {
  var test_function_gen = TestFunctionsArrays[test_function_name];
  var benchmarks = [];
  for (var test_array_name in TestArrays) {
    var test_array = TestArrays[test_array_name];
    var benchmark = NewBenchmark(
        test_function_gen, test_function_name, test_array, test_array_name);
    benchmarks.push(benchmark);
  }
  Benchmarks.push(new BenchmarkSuite(test_function_name, [100], benchmarks));
}

// ============================================================================
