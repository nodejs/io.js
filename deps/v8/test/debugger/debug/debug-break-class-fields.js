// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Flags: --allow-natives-syntax

Debug = debug.Debug

Debug.setListener(function() {});

class Y {
  x = 1;
  y = 2;
  z = 3;
}

var initializer = %GetInitializerFunction(Y);
var b1, b2, b3;

// class Y {
//   x = [B0]1;
//   y = [B1]2;
//   z = [B2]3;
// }
b1 = Debug.setBreakPoint(initializer, 0, 0);
assertTrue(Debug.showBreakPoints(initializer).indexOf("x = [B0]1;") >= 0);
Debug.clearBreakPoint(b1);
assertTrue(Debug.showBreakPoints(initializer).indexOf("x = [B0]1;") === -1);

b2 = Debug.setBreakPoint(initializer, 1, 0);
assertTrue(Debug.showBreakPoints(initializer).indexOf("y = [B0]2;") >= 0);
Debug.clearBreakPoint(b2);
assertTrue(Debug.showBreakPoints(initializer).indexOf("y = [B0]2;") === -1);

b3 = Debug.setBreakPoint(initializer, 2, 0);
assertTrue(Debug.showBreakPoints(initializer).indexOf("z = [B0]3") >= 0);
Debug.clearBreakPoint(b3);
assertTrue(Debug.showBreakPoints(initializer).indexOf("z = [B0]3") === -1);

b1 = Debug.setBreakPoint(initializer, 0, 0);
b2 = Debug.setBreakPoint(initializer, 1, 0);
assertTrue(Debug.showBreakPoints(initializer).indexOf("x = [B0]1;") >= 0);
assertTrue(Debug.showBreakPoints(initializer).indexOf("y = [B1]2;") >= 0);
Debug.clearBreakPoint(b1);
assertTrue(Debug.showBreakPoints(initializer).indexOf("x = [B0]1;") === -1);
Debug.clearBreakPoint(b2);
assertTrue(Debug.showBreakPoints(initializer).indexOf("y = [B1]2;") === -1);

b1 = Debug.setBreakPoint(initializer, 0, 0);
b3 = Debug.setBreakPoint(initializer, 2, 0);
assertTrue(Debug.showBreakPoints(initializer).indexOf("x = [B0]1;") >= 0);
assertTrue(Debug.showBreakPoints(initializer).indexOf("z = [B1]3") >= 0);
Debug.clearBreakPoint(b1);
assertTrue(Debug.showBreakPoints(initializer).indexOf("x = [B0]1;") === -1);
Debug.clearBreakPoint(b3);
assertTrue(Debug.showBreakPoints(initializer).indexOf("z = [B1]3") === -1);

b2 = Debug.setBreakPoint(initializer, 1, 0);
b3 = Debug.setBreakPoint(initializer, 2, 0);
assertTrue(Debug.showBreakPoints(initializer).indexOf("y = [B0]2;") >= 0);
assertTrue(Debug.showBreakPoints(initializer).indexOf("z = [B1]3") >= 0);
Debug.clearBreakPoint(b2);
assertTrue(Debug.showBreakPoints(initializer).indexOf("y = [B0]2;") === -1);
Debug.clearBreakPoint(b3);
assertTrue(Debug.showBreakPoints(initializer).indexOf("z = [B1]3") === -1);

// The computed properties are evaluated during class construction,
// not as part of the initializer function. As a consequence of which,
// they aren't breakable here in the initializer function, but
// instead, are part of the enclosing function.

function foo() {}
var bar = 'bar';

class X {
  [foo()] = 1;
  baz = foo();
}

// class X {
//  [foo()] = 1;
//  baz = [B0]foo();
// }

initializer = %GetInitializerFunction(X);
b1 = Debug.setBreakPoint(initializer, 0, 0);
assertTrue(Debug.showBreakPoints(initializer).indexOf('[foo()] = 1;') >= 0);
Debug.clearBreakPoint(b1);

b1 = Debug.setBreakPoint(initializer, 1, 0);
assertTrue(Debug.showBreakPoints(initializer).indexOf('baz = [B0]foo()') >= 0);
Debug.clearBreakPoint(b1);

function t() {
  class X {
    [foo()] = 1;
    [bar] = 2;
    baz = foo();
  }
}

// class X {
//   [[B0]foo()] = 1;
//   [[B1]bar] = 2;
//   baz = foo();
// }

b1 = Debug.setBreakPoint(t, 2, 0);
assertTrue(Debug.showBreakPoints(t).indexOf('[[B0]foo()] = 1;') >= 0);
Debug.clearBreakPoint(b1);
assertTrue(Debug.showBreakPoints(t).indexOf('[[B0]foo()] = 1;') === -1);

b2 = Debug.setBreakPoint(t, 3, 0);
assertTrue(Debug.showBreakPoints(t).indexOf('[[B0]bar] = 2;') >= 0);
Debug.clearBreakPoint(b2);
assertTrue(Debug.showBreakPoints(t).indexOf('[[B0]bar] = [B0]2;') === -1);

b3 = Debug.setBreakPoint(t, 4, 0);
assertTrue(Debug.showBreakPoints(t).indexOf('baz = foo()') >= 0);
Debug.clearBreakPoint(b3);

b1 = Debug.setBreakPoint(t, 2, 0);
b2 = Debug.setBreakPoint(t, 3, 0);
assertTrue(Debug.showBreakPoints(t).indexOf('[[B0]foo()] = 1;') >= 0);
assertTrue(Debug.showBreakPoints(t).indexOf('[[B1]bar] = 2;') >= 0);
Debug.clearBreakPoint(b1);
assertTrue(Debug.showBreakPoints(t).indexOf('[[B0]foo()] = 1;') === -1);
Debug.clearBreakPoint(b2);
assertTrue(Debug.showBreakPoints(t).indexOf('[[B1]bar] = 2;') === -1);

b1 = Debug.setBreakPoint(t, 2, 0);
b3 = Debug.setBreakPoint(initializer, 4, 0);
assertTrue(Debug.showBreakPoints(t).indexOf('[[B0]foo()] = 1;') >= 0);
assertTrue(Debug.showBreakPoints(t).indexOf('baz = foo()') >= 0);
Debug.clearBreakPoint(b1);
assertTrue(Debug.showBreakPoints(t).indexOf('[[B0]foo()] = 1;') === -1);
Debug.clearBreakPoint(b3);

b2 = Debug.setBreakPoint(t, 3, 0);
b3 = Debug.setBreakPoint(t, 4, 0);
assertTrue(Debug.showBreakPoints(t).indexOf('[[B0]bar] = 2;') >= 0);
assertTrue(Debug.showBreakPoints(t).indexOf('baz = foo()') >= 0);
Debug.clearBreakPoint(b2);
assertTrue(Debug.showBreakPoints(t).indexOf('[[B0]bar] = 2;') === -1);
Debug.clearBreakPoint(b3);
