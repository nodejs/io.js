// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

let tag = new WebAssembly.Tag({'parameters': []});
let proxy = new Proxy({}, {
  'get': () => {
    throw new Error('boom')
  }
});

assertThrows(() => new WebAssembly.Exception(tag, [], proxy), Error, 'boom');
