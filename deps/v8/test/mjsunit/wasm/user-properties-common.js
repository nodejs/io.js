// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --expose-gc --verify-heap

d8.file.execute("test/mjsunit/wasm/wasm-module-builder.js");

const verifyHeap = gc;
let globalCounter = 10000000;

function testProperties(obj) {
  for (let i = 0; i < 3; i++) {
    obj.x = 1001;
    assertEquals(1001, obj.x);

    obj.y = "old";
    assertEquals("old", obj.y);

    delete obj.y;
    assertEquals("undefined", typeof obj.y);

    let uid = globalCounter++;
    let fresh = "f_" + uid;

    obj.z = fresh;
    assertEquals(fresh, obj.z);

    obj[fresh] = uid;
    assertEquals(uid, obj[fresh]);

    verifyHeap();

    assertEquals(1001, obj.x);
    assertEquals(fresh, obj.z);
    assertEquals(uid, obj[fresh]);
  }

  // These properties are special for JSFunctions.
  Object.defineProperty(obj, 'name', {value: "crazy"});
  Object.defineProperty(obj, 'length', {value: 999});
}

function minus18(x) { return x - 18; }
function id(x) { return x; }

function printName(when, f) {
  print("    " + when + ": name=" + f.name + ", length=" + f.length);
}

// Note that this test is a helper with common code for user-properties-*.js.
