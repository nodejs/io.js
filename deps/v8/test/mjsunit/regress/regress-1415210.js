// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function source() {
  return 1;
}

const options = {
  arguments: [
    {
      get value() { d8.terminate(); return "something" },
    }
  ],
  type: "function",
};
const v12 = new Worker(source, options);
