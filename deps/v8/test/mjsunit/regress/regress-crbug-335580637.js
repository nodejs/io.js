// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const Probe =function() {
    const ProxyConstructor = Proxy;
    const setPrototypeOf = Object.setPrototypeOf;
    function probe(id, value) {
        let handler = {
        };
            originalPrototype = value;
            newPrototype = new ProxyConstructor(originalPrototype, handler);
            setPrototypeOf(value, newPrototype);
    }
    function probeWithErrorHandling(id, value) {
            probe(id, value);
    }
    return {
        probe: probeWithErrorHandling,
    };
}();
const v0 = /a+b/isum;
const v1 = /f6hn/ism;
function f2(a3, a4) {
    const o5 = {
        __proto__: v1,
        "h": a3,
        7: a4,
        "b": v1,
        142: v0,
    };
    Probe.probe("v17", o5);
    return o5;
}
function f7() {
    return f2;
}
function f8() {
}
Object.defineProperty(this, "toJSON", { enumerable: true, get: f7, set: f8 });
JSON.stringify(this);
