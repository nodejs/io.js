// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --allow-natives-syntax

// Objects for which all our brand checks return false.
const commonFalseCases = [{}, function() {}, []];
// Values for which all our brand checks throw.
const commonThrowCases = [100, 'foo', undefined, null];

(function TestReturnValue() {
  class A {
    m() {
      assertEquals(typeof (#x in this), 'boolean');
      assertEquals(typeof (#x in {}), 'boolean');
    }
    #x = 1;
  }
})();

(function TestPrivateField() {
  class A {
    m(other) {
      return #x in other;
    }
    #x = 1;
  }
  let a = new A();
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(a.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { a.m(o) }, TypeError);
  }

  class B {
    #x = 5;
  }
  assertFalse(a.m(new B()));
})();

(function TestPrivateFieldWithValueUndefined() {
  class A {
    m(other) {
      return #x in other;
    }
    #x;
  }
  let a = new A();
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(a.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { a.m(o) }, TypeError);
  }

  class B {
    #x;
  }
  assertFalse(a.m(new B()));
})();

(function TestPrivateMethod() {
  class A {
    #pm() {
    }
    m(other) {
      return #pm in other;
    }
  }
  let a = new A();
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(a.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { a.m(o) }, TypeError);
  }

  class B {
    #pm() {}
  }
  assertFalse(a.m(new B()));
})();

(function TestPrivateGetter() {
  class A {
    get #foo() {
    }
    m(other) {
      return #foo in other;
    }
  }
  let a = new A();
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(a.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { a.m(o) }, TypeError);
  }

  class B {
    get #foo() {}
  }
  assertFalse(a.m(new B()));
})();

(function TestPrivateSetter() {
  class A {
    set #foo(a) {
    }
    m(other) {
      return #foo in other;
    }
  }
  let a = new A();
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(a.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { a.m(o) }, TypeError);
  }

  class B {
    set #foo(a) {}
  }
  assertFalse(a.m(new B()));
})();

(function TestPrivateGetterAndSetter() {
  class A {
    get #foo() {}
    set #foo(a) {
    }
    m(other) {
      return #foo in other;
    }
  }
  let a = new A();
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(a.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { a.m(o) }, TypeError);
  }

  class B {
    get #foo() {}
    set #foo(a) {}
  }
  assertFalse(a.m(new B()));
})();

(function TestPrivateStaticField() {
  class A {
    static m(other) {
      return #x in other;
    }
    static #x = 1;
  }
  assertTrue(A.m(A));
  assertFalse(A.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(A.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { A.m(o) }, TypeError);
  }

  class B {
    static #x = 5;
  }
  assertFalse(A.m(B));
})();

(function TestPrivateStaticMethod() {
  class A {
    static m(other) {
      return #pm in other;
    }
    static #pm() {}
  }
  assertTrue(A.m(A));
  assertFalse(A.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(A.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { A.m(o) }, TypeError);
  }

  class B {
    static #pm() {};
  }
  assertFalse(A.m(B));
})();

(function TestPrivateStaticGetter() {
  class A {
    static m(other) {
      return #x in other;
    }
    static get #x() {}
  }
  assertTrue(A.m(A));
  assertFalse(A.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(A.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { A.m(o) }, TypeError);
  }

  class B {
    static get #x() {};
  }
  assertFalse(A.m(B));
})();

(function TestPrivateStaticSetter() {
  class A {
    static m(other) {
      return #x in other;
    }
    static set #x(x) {}
  }
  assertTrue(A.m(A));
  assertFalse(A.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(A.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { A.m(o) }, TypeError);
  }

  class B {
    static set #x(x) {};
  }
  assertFalse(A.m(B));
})();

(function TestPrivateStaticGetterAndSetter() {
  class A {
    static m(other) {
      return #x in other;
    }
    static get #x() {}
    static set #x(x) {}
  }
  assertTrue(A.m(A));
  assertFalse(A.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(A.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { A.m(o) }, TypeError);
  }

  class B {
    static get #x() {}
    static set #x(x) {};
  }
  assertFalse(A.m(B));
})();

(function TestPrivateIdentifiersAreDistinct() {
  function GenerateClass() {
    class A {
      m(other) {
        return #x in other;
      }
      #x = 0;
    }
    return new A();
  }
  let a1 = GenerateClass();
  let a2 = GenerateClass();
  assertTrue(a1.m(a1));
  assertFalse(a1.m(a2));
  assertFalse(a2.m(a1));
  assertTrue(a2.m(a2));
})();

(function TestSubclasses() {
  class A {
    m(other) { return #foo in other; }
    #foo;
  }
  class B extends A {}
  assertTrue((new A()).m(new B()));
})();

(function TestFakeSubclassesWithPrivateField() {
  class A {
    #foo;
    m() { return #foo in this; }
  }
  let a = new A();
  assertTrue(a.m());

  // Plug an object into the prototype chain; it's not a real instance of the
  // class.
  let fake = {__proto__: a};
  assertFalse(fake.m());
})();

(function TestFakeSubclassesWithPrivateMethod() {
  class A {
    #pm() {}
    m() { return #pm in this; }
  }
  let a = new A();
  assertTrue(a.m());

  // Plug an object into the prototype chain; it's not a real instance of the
  // class.
  let fake = {__proto__: a};
  assertFalse(fake.m());
})();

(function TestPrivateNameUnknown() {
  assertThrows(() => { eval(`
  class A {
    m(other) { return #lol in other; }
  }
  new A().m();
  `)}, SyntaxError, /must be declared in an enclosing class/);
})();

(function TestEvalWithPrivateField() {
  class A {
    m(other) {
      let result;
      eval('result = #x in other;');
      return result;
    }
    #x = 1;
  }
  let a = new A();
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(a.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { a.m(o) }, TypeError);
  }
})();

(function TestEvalWithPrivateMethod() {
  class A {
    m(other) {
      let result;
      eval('result = #pm in other;');
      return result;
    }
    #pm() {}
  }
  let a = new A();
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(a.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { a.m(o) }, TypeError);
  }
})();

(function TestEvalWithStaticPrivateField() {
  class A {
    static m(other) {
      let result;
      eval('result = #x in other;');
      return result;
    }
    static #x = 1;
  }
  assertTrue(A.m(A));
  assertFalse(A.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(A.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { A.m(o) }, TypeError);
  }
})();

(function TestEvalWithStaticPrivateMethod() {
  class A {
    static m(other) {
      let result;
      eval('result = #pm in other;');
      return result;
    }
    static #pm() {}
  }
  assertTrue(A.m(A));
  assertFalse(A.m(new A()));
  for (o of commonFalseCases) {
    assertFalse(A.m(o));
  }
  for (o of commonThrowCases) {
    assertThrows(() => { A.m(o) }, TypeError);
  }
})();

(function TestCombiningWithOtherExpressions() {
  class A {
    m() {
      assertFalse(#x in {} in {} in {});
      assertTrue(#x in this in {true: 0});
      assertTrue(#x in {} < 1 + 1);
      assertFalse(#x in this < 1);

      assertThrows(() => { eval('#x in {} = 4')});
      assertThrows(() => { eval('(#x in {}) = 4')});
    }
    #x;
  }
  new A().m();
})();

(function TestHalfConstructedObjects() {
  let half_constructed;
  class A {
    m() {
      assertTrue(#x in this);
      assertFalse(#y in this);
    }
    #x = 0;
    #y = (() => { half_constructed = this; throw 'lol';})();
  }

  try {
    new A();
  } catch {
  }
  half_constructed.m();
})();

(function TestPrivateFieldOpt() {
  class A {
    m(other) {
      return #x in other;
    }
    #x = 1;
  }
  let a = new A();
  %PrepareFunctionForOptimization(A.prototype.m);
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  %OptimizeFunctionOnNextCall(A.prototype.m);
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));

  class B {
    #x = 5;
  }
  assertFalse(a.m(new B()));
})();

(function TestPrivateMethodOpt() {
  class A {
    #pm() {
    }
    m(other) {
      return #pm in other;
    }
  }
  let a = new A();
  %PrepareFunctionForOptimization(A.prototype.m);
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));
  %OptimizeFunctionOnNextCall(A.prototype.m);
  assertTrue(a.m(a));
  assertTrue(a.m(new A()));

  class B {
    #pm() {}
  }
  assertFalse(a.m(new B()));
})();

(function TestPrivateStaticFieldOpt() {
  class A {
    static m(other) {
      return #x in other;
    }
    static #x = 1;
  }
  %PrepareFunctionForOptimization(A.m);
  assertTrue(A.m(A));
  %OptimizeFunctionOnNextCall(A.m);
  assertTrue(A.m(A));

  class B {
    static #x = 5;
  }
  assertFalse(A.m(B));
})();

(function TestPrivateStaticMethodOpt() {
  class A {
    static m(other) {
      return #pm in other;
    }
    static #pm() {}
  }
  %PrepareFunctionForOptimization(A.m);
  assertTrue(A.m(A));
  %OptimizeFunctionOnNextCall(A.m);
  assertTrue(A.m(A));

  class B {
    static #pm() {};
  }
  assertFalse(A.m(B));
})();

(function TestPrivateFieldWithProxy() {
  class A {
    m(other) {
      return #x in other;
    }
    #x = 1;
  }
  let a = new A();

  const p = new Proxy(a, {get: function() { assertUnreachable(); } });
  assertFalse(a.m(p));
})();

(function TestHeritagePosition() {
  class A {
    #x; // A.#x
    static C = class C extends (function () {
        return class D {
          exfil(obj) { return #x in obj; }
          exfilEval(obj) { return eval("#x in obj"); }
        };
      }) { // C body starts
        #x; // C.#x
       } // C body ends
      } // A ends
  let c = new A.C();
  let d = new c();
  // #x inside D binds to A.#x, so only objects of A pass the check.
  assertTrue(d.exfil(new A()));
  assertFalse(d.exfil(c));
  assertFalse(d.exfil(d));
  assertTrue(d.exfilEval(new A()));
  assertFalse(d.exfilEval(c));
  assertFalse(d.exfilEval(d));
})();

(function TestBinaryOperatorPrecedenceParseError() {
  assertThrows(() => eval(`class C { #x; test() { 0 << #x in {} } }`),
               SyntaxError);
})();
