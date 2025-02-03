// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --fuzzing --turboshaft-from-maglev --jit-fuzzing

this.WScript = new Proxy({}, {
  get() {
    switch (name) {
      case 'Echo':
    }
  }
});


var V8OptimizationStatus = {
  kIsFunction: 1 << 0,
};

function __isPropertyOfType(obj, name, type) {
  let desc;

  desc = Object.getOwnPropertyDescriptor(obj, name);

  if (!desc) return false;
  return typeof type === 'undefined' || typeof desc.value === type;
}

function __getProperties(obj, type) {
  let properties = [];

  for (let name of Object.getOwnPropertyNames(obj)) {
    if (__isPropertyOfType(obj, name, type)) {
      properties.push(name);
    }
  }

  return properties;
}
function* __getObjects(root = this, level = 0) {

  let obj_names = __getProperties(root, 'object');
  for (let obj_name of obj_names) {
    let obj = root[obj_name];
    if (obj === root) continue;
    yield obj;
    yield* __getObjects(obj, level + 1);
  }
}

function __getRandomObject(seed) {
  let objects = [];
  for (let obj of __getObjects()) {
    objects.push(obj);
  }

  return objects[seed % objects.length];
}

function __getRandomProperty(obj, seed) {
  let properties = __getProperties(obj);
  return properties[seed % properties.length];
}

function __callRandomFunction(obj, seed, ...args) {
  let functions = __getProperties(obj);
  let random_function = functions[0];

  obj[random_function](...args);
}



function __f_3() {
  return [[]];
}

/* VariableOrObjectMutator: Random mutation */
try {

  __v_0, {
    get: function () {

      if (__v_1 != null && typeof __v_1 == "object") try {
        Object.defineProperty(__v_1, __v_0, 148676, {
        });
      } catch (e) {}

    }
  };

} catch (e) {}

try {
  __v_0 = __f_3();
  __v_1 = __f_3();
  ;
} catch (e) {}

function __f_4(__v_2, __v_3) {
  var __v_6 = { deopt: 0 };

  try {

    Object.defineProperty(__v_6, __getRandomProperty(__v_6, 874354), {
      get: function () {
        __callRandomFunction(__getRandomObject(501943), 864774, __v_6[1], __getRandomObject());
        return __getRandomObject();
      }
    });

  } catch (e) {}
  try {
    __v_4 = __v_3(__v_2,
                  30, __v_6);
    ;
  } catch (e) {}

  try {
    if (__v_0 != null && typeof __v_0 == "object") Object.defineProperty(__v_0, __getRandomProperty(__v_0), {
      value: __getRandomObject(107254)
    });
  } catch (e) {}
}


function __f_6(__v_13, __v_14, __v_15, __v_16) {
  new __v_13(__v_14, __v_15, __v_16);
}

function __f_8(__v_21) {
  try {
    __f_4();
  } catch (e) {}

  __f_4(__v_21, __f_6);

}

function __f_9(__v_22, __v_23) {
  try {
    __v_23.deopt;
  } catch (e) {}
}

try {
  __f_8(__f_9);
} catch (e) {}
