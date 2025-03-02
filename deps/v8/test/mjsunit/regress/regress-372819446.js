// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function f0() {
    const v1 = /2/dvg;
    class C2 {
    }
    const v7 = C2.toString();
    v7.match(v1);
    return v7;
}
function F16() {
}
const v19 = new F16();
class C20 {
}
const v21 = new C20();
const v22 = delete v21.c;
const v23 = [];
v23.toString = f0;
v19[v23] ^= v22;
