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
const v0 = [68.95300664263982,1.7976931348623157e+308,-Infinity,-453845.87562507356,-1000000.0,529.2939311050484];
Intl.length = -595354.0618115567;
Probe.probe("v17", Intl);
Intl.length = v0;
Probe.probe("v26", Intl);
