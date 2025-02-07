// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Create a prototype object which has a lot of fast properties.
var body = "";
for (var i = 0; i < 100; i++) {
  body += `this.a${i} = 0;\n`;
}
var Proto = new Function(body);

function A() {}
A.prototype = new Proto();

// Create an object and add properties that already exist in the prototype.
// At some point the object will turn into a dictionary mode and one of
// the fast details from the prototype will be reinterpreted as a details
// for a new property ...
var o = new A();
for (var i = 0; i < 100; i++) {
  o["a" + i] = i;
}

// ... which will break the enumeration order of the slow properties.
var names = Object.getOwnPropertyNames(o);
for (var i = 0; i < 100; i++) {
  assertEquals("a" + i, names[i]);
}
