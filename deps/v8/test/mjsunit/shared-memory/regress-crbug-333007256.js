// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --harmony-struct --expose-gc

const Probe = function() {
  const ProxyConstructor = Proxy;
  const getPrototypeOf = Object.getPrototypeOf;
  const setPrototypeOf = Object.setPrototypeOf;
  function probe(value) {
    originalPrototype = getPrototypeOf(value);
    newPrototype = new ProxyConstructor(originalPrototype, function(){});
    setPrototypeOf(value, newPrototype);
  }
  return {
    probe: probe,
  };
}();

gc();

for (let v2 = 0; v2 < 5; v2++) {
  // Creates a derived map with non-JSObject prototype.
  Probe.probe(Atomics);
  Atomics.h = Atomics.Mutex();
}
