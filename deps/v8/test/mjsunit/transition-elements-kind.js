// Copyright 2013 the V8 project authors. All rights reserved.
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

// Flags: --allow-natives-syntax --turbofan

// Allocation site for empty smi arrays.
function foo() {
  return new Array();
}
var a = foo();
// Transition from smi to double.
a[0] = 1.1;

// Emit a TransitionElementsKindStub which transitions from double to object.
function store(a, x) {
  a[0] = x;
};
%PrepareFunctionForOptimization(store);
store([1.1], 'a');
store([1.1], 1.1);
%OptimizeFunctionOnNextCall(store);

// Use the TransitionElementsKindStub to transition from double to object.
var b = foo();
b[0] = 1.1;
assertTrue(%HasDoubleElements(b));
store(b, 'a');
assertTrue(%HasObjectElements(b));
assertOptimized(store);

// Test transitions with polymorphic feedback.
function poly_store(a, x) {
  a[0] = x;
};
%PrepareFunctionForOptimization(poly_store);
poly_store([1.1], 'a');
poly_store([1.1], 2.1);
var x = foo();
x[0] = 1.1;
x.x = 12;
poly_store(x, 'a');
%OptimizeFunctionOnNextCall(poly_store);

var c = foo();
c[0] = 1.1;
assertTrue(%HasDoubleElements(c));
poly_store(c, 'a');
assertTrue(%HasObjectElements(c));
assertOptimized(poly_store);
