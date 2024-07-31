// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


(function TestBigPacked() {
  let a = [];
  for (let i = 0; i < 50000; i++) a.push(i);
  let r = a.toSpliced();
})();
