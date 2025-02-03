// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

{
  // Basic functionality
  let log = [];
  class C {
    static { log.push("block1"); }
    static { log.push("block2"); }
  }
  assertArrayEquals(["block1", "block2"], log);
}

{
  // Static blocks run in textual order interleaved with field initializers.
  let log = [];
  class C {
    static { log.push("block1"); }
    static public_static_method() {}
    static public_field = log.push("public_field");
    static { log.push("block2"); }
    static #private_field = log.push("private_field");
    static { log.push("block3"); }
  }
  assertArrayEquals(["block1",
                     "public_field",
                     "block2",
                     "private_field",
                     "block3"], log);
}

{
  // Static blocks have access to private fields.
  let exfil;
  class C {
    #foo;
    constructor(x) { this.#foo = x; }
    static {
      exfil = function(o) { return o.#foo; };
    }
  }
  assertEquals(exfil(new C(42)), 42);
}

{
  // 'this' is the constructor.
  let log = [];
  class C {
    static x = 42;
    static {
      log.push(this.x);
    }
  }
  assertArrayEquals([42], log);
}

{
  // super.property accesses work as expected.
  let log = [];
  class B {
    static foo = 42;
    static get field_getter() { return "field_getter"; }
    static set field_setter(x) { log.push(x); };
    static method() { return "bar"; }
  }
  class C extends B {
    static {
      log.push(super.foo);
      log.push(super.field_getter);
      super.field_setter = "C";
      log.push(super.method());
    }
  }
  assertArrayEquals([42, "field_getter", "C", "bar"], log);
}

{
  // Each static block is its own var and let scope.
  let log = [];
  let f;
  class C {
    static {
      var x = "x1";
      let y = "y1";
      log.push(x);
      log.push(y);
    }
    static {
      var x = "x2";
      let y = "y2";
      f = () => [x, y];
    }
    static {
      assertThrows(() => x, ReferenceError);
      assertThrows(() => y, ReferenceError);
    }
  }
  assertArrayEquals(["x1", "y1"], log);
  assertArrayEquals(["x2", "y2"], f());
}

{
  // new.target is undefined.
  let log = [];
  class C {
    static {
      log.push(new.target);
    }
  }
  assertArrayEquals([undefined], log);
}

{
  // 'await' is allowed as an identifier name in named function expressions.
  class C1 { static { (function await() {}); } }
  // 'return' is allowed across function boundaries inside static blocks.
  class C2 { static {
      function f1() { return; }
      function f2() { return 0; }
      let f3 = (x => { return x; })
      let f4 = (x => { return; })
    }
  }
}

function assertDoesntParse(expr, context_start, context_end) {
  assertThrows(() => {
    eval(`${context_start} class C { static { ${expr} } } ${context_end}`);
  }, SyntaxError);
}

for (let [s, e] of [['', ''],
                    ['function* g() {', '}'],
                    ['async function af() {', '}'],
                    ['async function* ag() {', '}']]) {
  assertDoesntParse('arguments;', s, e);
  assertDoesntParse('arguments[0] = 42;', s, e);
  assertDoesntParse('super();', s, e);
  assertDoesntParse('yield 42;', s, e);
  assertDoesntParse('await 42;', s, e);
  // 'await' is disallowed as an identifier.
  assertDoesntParse('let await;', s, e);
  assertDoesntParse('await;', s, e);
  assertDoesntParse('function await() {}', s, e);
  assertDoesntParse('class await() {}', s, e);
  assertDoesntParse('try {} catch (await) {}', s, e);
  assertDoesntParse('try {} catch ({await}) {}', s, e);
  assertDoesntParse('var {await} = 0;', s, e);
  assertDoesntParse('({await} = 0);', s, e);
  assertDoesntParse('return;', s, e);
  assertDoesntParse('return 0;', s, e);
  assertDoesntParse('{ return; }', s, e);
  assertDoesntParse('{ return 0; }', s, e);
  // lets that hoist over vars inside blocks are redeclaration errors.
  //
  // At the static block top level this is actually allowed, because
  // LexicallyDeclaredNames recurs into ClassStaticBlockStatementList using
  // TopLevelLexicallyDeclaredNames, which regards function declarations as var
  // names, not lexical.
  assertDoesntParse('{ var f; function f() {} }', s, e);
  assertDoesntParse('{ var f; let f; }', s, e);
  assertDoesntParse('{ var f; const f; }', s, e);
}
