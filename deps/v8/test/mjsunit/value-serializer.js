// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test the basic d8 value serializer interface
// Flags: --js-float16array

var largeArray = [];
largeArray[0xFFFF00] = 123;

let proto_obj = { fn1() { return 1 } }
let obj_with_enum_cache = {
  __proto__: proto_obj,
  a: 1,
  b: 2,
  c: "c"
};

for (let k in obj_with_enum_cache) {
  // do something
  obj_with_enum_cache.a += obj_with_enum_cache.fn1();
}
let string_1 = "aasdfasdfasdfasdf asd fa sdf asdf as dfa sdf asd f"
let string_2 = "aasdfasdfasdfasdf asd fa sdf UC16\u2028asdf as dfa sdf asd f"
var objects = [
    true, false, null, undefined,
    1, -1, 1.1, -2.2, -0, 0,
    9007199254740991.0, 9007199254740991.0 + 10,
    -9007199254740992.0, -9007199254740992.0 - 10,
    Infinity, -Infinity, NaN,
    string_1, string_1+"b", string_1.slice(1),
    string_2, string_2+"b", string_2.slice(1),
    {}, {1:1}, {a:1}, {1:1, 2:2}, Object.create(null),
    obj_with_enum_cache,
    [], [{}, {}], [1, 1, 1], [1.1, 1.1, 1.1, 1.1, 2], largeArray,
    // new Proxy({},{}),
    new Date(), new String(" a"),
    new Uint8Array(12), new Float16Array([1, 2, 3, 4, 5]),
    new Float16Array([NaN, Infinity, -Infinity]),
    new Float32Array([1, 2, 4, 5]),
    new Uint8ClampedArray(2048),
    /asdf/, new RegExp(),
    new Map(), new Set(),

];

for (var o of objects) {
  const serialised = d8.serializer.serialize(o)
  const deserialised = d8.serializer.deserialize(serialised);
  assertEquals(typeof deserialised, typeof o);
}
