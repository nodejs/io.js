// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function func() {
  this.postMessage(true);
  while (true) {
    import('data:text/javascript,export function v4(){}');
  }
}
const worker = new Worker(func, {
  type: "function"
});
worker.getMessage();
