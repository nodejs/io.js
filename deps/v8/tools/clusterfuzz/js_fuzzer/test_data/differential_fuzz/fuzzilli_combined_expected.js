// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Original: differential_fuzz/fake_resource.js
print("I'm a resource.");

// Original: differential_fuzz/fake_resource.js
print("I'm a resource.");

// Original: differential_fuzz/fake_resource.js
print("I'm a resource.");

// Original: differential_fuzz/fake_resource.js
print("I'm a resource.");

// Original: differential_fuzz/fake_resource.js
print("I'm a resource.");

/* DifferentialFuzzMutator: Print variables and exceptions from section */
try {
  print("Hash: " + __hash);
  print("Caught: " + __caught);
} catch (e) {}

print("v8-foozzie source: v8/differential_fuzz/input2.js");

// Original: v8/differential_fuzz/input2.js
let __v_0 = 1;

/* DifferentialFuzzMutator: Print variables and exceptions from section */
try {
  print("Hash: " + __hash);
  print("Caught: " + __caught);

  __prettyPrint(__v_0);
} catch (e) {}

print("v8-foozzie source: fuzzilli_source");

// Original: fuzzilli/fuzzdir-1/corpus/program_1.js
try {
  console.log(42);
} catch (e) {}

/* DifferentialFuzzMutator: Print variables and exceptions from section */
try {
  print("Hash: " + __hash);
  print("Caught: " + __caught);

  __prettyPrint(__v_0);
} catch (e) {}
