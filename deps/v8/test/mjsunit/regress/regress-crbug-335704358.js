// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --verify-heap

function __isPropertyOfType(obj, name, type) {
    desc = Object.getOwnPropertyDescriptor(obj, name);
  return typeof type === 'undefined' || typeof desc.value === type;
}
function __getProperties(obj, type) {
  let properties = [];
  for (let name of Object.getOwnPropertyNames(obj)) {
    if (__isPropertyOfType(obj, name, type)) properties.push(name);
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
(function () {
  __callGC = function () {
      gc();
  };
})();
let __v_0 = [ false, "test", Symbol()];
  for (let __v_4 of __v_0) {
    let __v_5 = Object.__v_4;
    let __v_6 = new Proxy({}, {
    });
    try {
      __v_5 = __getRandomObject(926762), __callGC();
    } catch (e) {}
      Object.setPrototypeOf(__v_5, __v_6);
      __v_5[__getRandomProperty(__v_5, 799353)] =  __callGC();
  }
