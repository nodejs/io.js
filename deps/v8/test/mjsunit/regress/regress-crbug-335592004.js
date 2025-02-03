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
function f0(a1) {
    this.a = a1;
}
const v6 = new f0(1);
new f0(1.5);
function f9(a10) {
    Probe.probe("v56", a10);
    try{
      a10.prototype = a10;
    } catch(e) {}
}
f9(f0);
f9(v6);
