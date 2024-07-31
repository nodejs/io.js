// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function f0() {
    function f1() {
        const v4 = Error();
        v4.message = v4;
        v4.stack;
    }
    new f1;
}
const t10 =[].values().__proto__;
t10.return = f0;
assertThrows(() => { new WeakSet([1]); }, TypeError,
             /Invalid value used in weak set/);
