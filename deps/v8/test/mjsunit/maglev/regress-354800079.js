// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

(function () {
  const join = Array.prototype.join;
  const map = Array.prototype.map;
  const classOf = function () {
  };
  prettyPrinted = function prettyPrinted(value = 4) {
    switch (typeof value) {
      case "object":
            return prettyPrintedObject(value);
    }
  };
  function prettyPrintedObject(object, depth) {
    const keys = Object.keys(object);
    const prettyValues = map.call(keys, key => {
      return `${key}: ${prettyPrinted(object[key], depth - 1)}`;
    });
    const content = join.call(prettyValues);
    return `${object.constructor.name || "Object"}{${content}}`;
  }
  __prettyPrint = function (value = false) {
    let str = prettyPrinted(value);
    print(str);
  };
})();
this.WScript = new Proxy({}, {
});
assertEquals = (expected, found) => {
  __prettyPrint(found);
};

(async function () {
  function __f_0() {
    let __v_14 = {
      next() {
      }
    };
    return {
    };
  }
  async function __f_6() {
    let __v_59 = 0,
        __v_60 = [];
    try {
      for await (var [__v_63 =  __v_62] of __f_0()) {
      }
    } catch (e) {
    }
    return {
      keys: __v_60,
    };
  }
  %PrepareFunctionForOptimization(__f_6);
  __f_6();
  __f_6();
  %OptimizeMaglevOnNextCall(__f_6);
  __f_6()
})().catch();
