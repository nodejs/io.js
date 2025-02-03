// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const v1 = Array.fromAsync;
function F2(a4) {
    this.toJSON = a4;
    JSON.stringify(this);
}
new F2(v1);
