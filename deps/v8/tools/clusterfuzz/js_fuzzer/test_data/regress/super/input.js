// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class A {
  constructor() {
    console.log(42);
  }

  method() {
    console.log(42);
  }
}

class B extends A{
  constructor() {
    console.log(42);
  }

  method() {
    console.log(42);
  }
}
