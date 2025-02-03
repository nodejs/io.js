// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

(function TestCallableObjectAsPropertyGetterOrSetter() {
  let log = [];
  const target_function_name = "callable_obj_target";
  let property_storage = 42;

  globalThis[target_function_name] = function(...args) {
    if (args.length == 0) {
      // get request
      log.push("get");
      return property_storage;
    } else {
      // set request
      assertEquals(args.length, 1);
      log.push("set");
      property_storage = args[0];
      return true;
    }
  }

  let callable_obj = %GetCallable(target_function_name);
  log.push(callable_obj());
  property_storage = 55;
  log.push(callable_obj(153));
  assertEquals(property_storage, 153);

  assertEquals(["get", 42, "set", true], log);

  let p = {};
  Object.defineProperty(
      p, "x",
      {
        get: callable_obj,
        set: callable_obj,
        configurable: true
      });

  // Test accessors on receiver.
  log = [];
  function f(o, v) {
    o.x = v;
    return o.x;
  }
  %PrepareFunctionForOptimization(f);
  for (let i = 0; i < 5; i++) {
    log.push(property_storage);
    log.push(f(p, i));
  }
  %OptimizeFunctionOnNextCall(f);
  log.push(f(p, 572));

  assertEquals(
      [
        153, "set", "get", 0,
        0, "set", "get", 1,
        1, "set", "get", 2,
        2, "set", "get", 3,
        3, "set", "get", 4,
        "set", "get", 572
      ],
      log);

  // Test accessors on the prototype chain.
  log = [];
  function f(o, v) {
    o.x = v;
    return o.x;
  }

  let o = Object.create(p);

  %PrepareFunctionForOptimization(f);
  for (let i = 0; i < 5; i++) {
    log.push(property_storage);
    log.push(f(o, i));
  }
  %OptimizeFunctionOnNextCall(f);
  log.push(f(o, 157));

  assertEquals(
      [
        572, "set", "get", 0,
        0, "set", "get", 1,
        1, "set", "get", 2,
        2, "set", "get", 3,
        3, "set", "get", 4,
        "set", "get", 157
      ],
      log);

})();
