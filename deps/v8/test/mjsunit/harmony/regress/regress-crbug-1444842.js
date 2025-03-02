// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//

let iter = Iterator.from({
  next() {
    return { value: 42, done: false };
  }
});
function f() {}
Object.defineProperty(iter, 100, { set: f });
Object.defineProperty(iter, 100, { set: f });
