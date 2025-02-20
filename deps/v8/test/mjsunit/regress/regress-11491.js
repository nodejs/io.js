// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function test() {
  // Create a generator constructor with the maximum number of allowed parameters.
  const args = new Array(65526);
  function* gen() {}
  const c = gen.constructor.apply(null, args);

  // 'c' having 65535 parameters causes the parameters/registers fixed array
  // attached to the generator object to be considered a large object.
  // We call it twice so that it both covers the CreateJSGeneratorObject() C++
  // runtime function as well as the CreateGeneratorObject() CSA builtin.
  c();
  c();
}

test();
