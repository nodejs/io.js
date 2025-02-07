// Copyright 2009 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// This regression includes a number of cases where we did not correctly
// update an accessor property to a data property using Object.defineProperty.

var obj = { get value() {}, set value (v) { throw "Error";} };
Object.defineProperty(obj, "value",
                      { value: 5, writable:true, configurable: true });
var desc = Object.getOwnPropertyDescriptor(obj, "value");
assertEquals(obj.value, 5);
assertTrue(desc.configurable);
assertTrue(desc.enumerable);
assertTrue(desc.writable);
assertEquals(desc.get, undefined);
assertEquals(desc.set, undefined);


var proto = {
  get value() {},
  set value(v) { Object.defineProperty(this, "value", {value: v}); }
};

var create = Object.create(proto);

assertEquals(create.value, undefined);
create.value = 4;
assertEquals(create.value, 4);

// These tests where provided in bug 959, but are all related to the this issue.
var obj1 = {};
Object.defineProperty(obj1, 'p', {get: undefined, set: undefined});
assertTrue("p" in obj1);
desc = Object.getOwnPropertyDescriptor(obj1, "p");
assertFalse(desc.configurable);
assertFalse(desc.enumerable);
assertEquals(desc.value, undefined);
assertEquals(desc.get, undefined);
assertEquals(desc.set, undefined);


var obj2 = { get p() {}};
Object.defineProperty(obj2, 'p', {get: undefined})
assertTrue("p" in obj2);
desc = Object.getOwnPropertyDescriptor(obj2, "p");
assertTrue(desc.configurable);
assertTrue(desc.enumerable);
assertEquals(desc.value, undefined);
assertEquals(desc.get, undefined);
assertEquals(desc.set, undefined);
