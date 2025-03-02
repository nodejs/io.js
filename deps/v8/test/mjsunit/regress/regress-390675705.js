// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --fuzzing --expose-fast-api

const fast_c_api = new d8.test.FastCAPI();
let ptr = fast_c_api.get_pointer();

class B {
  constructor() {
    return ptr;
  }
}

class C extends B {
  #x = 1;
}

let c = new C();
