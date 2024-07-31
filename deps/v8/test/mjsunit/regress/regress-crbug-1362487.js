// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const rab1 = new ArrayBuffer(2000, {'maxByteLength': 4000});
class MyInt8Array extends Int8Array {
    constructor() {
        super(rab1);
    }
};
const rab2 = new ArrayBuffer(1000, {'maxByteLength': 4000});
const ta = new Int8Array(rab2);
ta.constructor = MyInt8Array;
ta.slice();
