// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

function __isPropertyOfType() {}

function __getProperties(obj) {
    let properties = [];
    for (let name of Object.getOwnPropertyNames(obj)) {
        properties.push(name);
    }
    let proto = Object.getPrototypeOf(obj);
    while (proto && proto != Object.prototype) {
        Object.getOwnPropertyNames(proto).forEach(name => {
            if (name !== "constructor") {
                properties.push(name);
            }
        });
        proto = Object.getPrototypeOf(proto);
    }
    return properties;
}

function __getRandomProperty(obj, seed) {
    let properties = __getProperties(obj);
    return properties[seed % properties.length];
}

var __v_0 = [ "prototype", 0, 1, "$1", "message", "constructor" ];

function __f_0() {}

function __f_1() {
    return [ function() {}, [], new Date() ];
}

var __v_3 = function() {};

var __v_6 = [ [ Number.prototype ], [ __v_3, __v_3 ], [], [] ];

function __f_2(__v_8) {
    for (var __v_9 in __v_6) {
        var __v_10 = __v_6[__v_9][0];
        try {
            var __v_11 = __v_6[__v_9][1];
            var __v_12 = __f_1();
            for (var __v_13 in __v_12) {
                try {
                    var __v_14 = __v_12[__v_13];
                    __v_10[__getRandomProperty(__v_10, 31148)] = 1, __callGC();
                } catch (e) {}
                __v_11.__proto__ = __v_14;
                for (var __v_15 in __v_0) {
                    var __v_16 = __v_0[__v_15];
                    __v_8(__v_10, __v_16);
                }
            }
        } catch (e) {}
    }
}

__f_2(function(__v_20, __v_21) {
    return __v_20[__v_21] = {};
});
