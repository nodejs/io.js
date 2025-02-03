// META: global=window,dedicatedworker,shadowrealm
"use strict";
// https://console.spec.whatwg.org/

test(() => {
    console.log(new Array(10000000).fill("x"));
    console.log(new Uint8Array(10000000));
}, "Logging large arrays works");
