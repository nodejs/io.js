// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbofan --no-always-turbofan --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/gc-js-interop-helpers.js');

// TODO(14034): Some operations can still trigger a deopt loop.
const ignoreDeopts = true;

let {struct, array} = CreateWasmObjects();
for (const wasm_obj of [struct, array]) {
  repeated(() => assertSame(undefined, wasm_obj.foo));
  testThrowsRepeated(() => wasm_obj.foo = 42, TypeError);
  repeated(() => assertSame(undefined, wasm_obj[0]));
  testThrowsRepeated(() => wasm_obj[0] = undefined, TypeError);
  repeated(() => assertSame(undefined, wasm_obj.__proto__));
  repeated(() => assertSame(
      null, Object.prototype.__lookupGetter__("__proto__").call(wasm_obj)));
  testThrowsRepeated(
      () => Object.prototype.__proto__.call(wasm_obj), TypeError);
  testThrowsRepeated(() => wasm_obj.__proto__ = null, TypeError);
  testThrowsRepeated(() => {
    for (let p in wasm_obj) {
    }
  }, TypeError);
  testThrowsRepeated(() => {
    for (let p of wasm_obj) {
    }
  }, TypeError);
  testThrowsRepeated(() => wasm_obj.toString(), TypeError);
  testThrowsRepeated(() => wasm_obj.valueOf(), TypeError);
  testThrowsRepeated(() => '' + wasm_obj, TypeError);
  testThrowsRepeated(() => 0 + wasm_obj, TypeError);
  testThrowsRepeated(() => { delete wasm_obj.foo; }, TypeError);

  {
    let js_obj = {};
    js_obj.foo = wasm_obj;
    repeated(() => assertSame(wasm_obj, js_obj.foo));
    js_obj[0] = wasm_obj;
    repeated(() => assertSame(wasm_obj, js_obj[0]));
  }

  repeated(() => assertEquals(42, wasm_obj ? 42 : 0));
  testThrowsRepeated(() => wasm_obj(), TypeError);

  testThrowsRepeated(() => [...wasm_obj], TypeError);
  repeated(() => assertEquals({}, {...wasm_obj}));
  repeated(() => ((...wasm_obj) => {})());
  repeated(() => assertSame(wasm_obj, ({wasm_obj}).wasm_obj));
  testThrowsRepeated(() => ({[wasm_obj]: null}), TypeError);
  testThrowsRepeated(() => `${wasm_obj}`, TypeError);
  testThrowsRepeated(() => wasm_obj`test`, TypeError);
  testThrowsRepeated(() => new wasm_obj, TypeError);
  repeated(() => assertSame(undefined, wasm_obj?.property));

  repeated(() => assertEquals(undefined, void wasm_obj));
  // These deopt loops can also be triggered with JS only code, e.g.
  // `2 == {valueOf: () => +2n}`.
  testThrowsRepeated(() => 2 == wasm_obj, TypeError, ignoreDeopts);
  repeated(() => assertFalse(2 === wasm_obj));
  repeated(() => assertFalse({} === wasm_obj));
  repeated(() => assertTrue(wasm_obj == wasm_obj));
  repeated(() => assertTrue(wasm_obj === wasm_obj));
  repeated(() => assertFalse(wasm_obj != wasm_obj));
  repeated(() => assertFalse(wasm_obj !== wasm_obj));
  repeated(() => assertFalse(struct == array));
  repeated(() => assertTrue(struct != array));
  // JS Symbols also have the issue of triggering deopt loops on relational
  // comparisons, e.g. `2 < Symbol("test")`.
  testThrowsRepeated(() => wasm_obj < wasm_obj, TypeError, ignoreDeopts);
  testThrowsRepeated(() => wasm_obj <= wasm_obj, TypeError, ignoreDeopts);
  testThrowsRepeated(() => wasm_obj >= wasm_obj, TypeError, ignoreDeopts);

  testThrowsRepeated(() => { let [] = wasm_obj; }, TypeError);
  testThrowsRepeated(() => { let [a, b] = wasm_obj; }, TypeError);
  testThrowsRepeated(() => { let [...all] = wasm_obj; }, TypeError);
  repeated(() => { let {a} = wasm_obj; assertSame(undefined, a); });
  repeated(() => { let {} = wasm_obj; }, TypeError);
  repeated(() => {
    let {...rest} = wasm_obj;
    assertTrue(rest instanceof Object);
  });
  testThrowsRepeated(() => {
    with(wasm_obj) test;
  }, ReferenceError);
  repeated(() => {
    let tmp = 1;
    with(wasm_obj) var with_lookup = tmp;
    assertEquals(tmp, with_lookup);
  });
  repeated(() => {
    switch (wasm_obj) {
      case 0:
      default:
        throw 1;
      case wasm_obj:
        break;
    }
  });
  repeated(() => {
    try {
      throw wasm_obj;
    } catch (e) {
      assertEquals(e, wasm_obj);
    }
  });
  testThrowsRepeated(
      () => {class SubClass extends wasm_obj {}}, TypeError,
      'Class extends value [object Object] is not a constructor or null');
  repeated(() => {
    class TestMemberInit {
      x = wasm_obj;
    };
    assertSame(wasm_obj, new TestMemberInit().x);
  });
  repeated(() => assertSame(wasm_obj, eval('wasm_obj')));

  // Test functions of the global object.
  testThrowsRepeated(() => decodeURI(wasm_obj), TypeError);
  testThrowsRepeated(() => decodeURIComponent(wasm_obj), TypeError);
  testThrowsRepeated(() => encodeURI(wasm_obj), TypeError);
  testThrowsRepeated(() => encodeURIComponent(wasm_obj), TypeError);

  {
    let fct = function(x) {
      return [this, x]
    };
    repeated(() => assertEquals([wasm_obj, 1], fct.apply(wasm_obj, [1])));
    repeated(
        () =>
            assertEquals([new Number(1), wasm_obj], fct.apply(1, [wasm_obj])));
    repeated(
        () => assertEquals([new Number(1), undefined], fct.apply(1, wasm_obj)));
    repeated(() => assertEquals([wasm_obj, 1], fct.bind(wasm_obj)(1)));
    repeated(() => assertEquals([wasm_obj, 1], fct.call(wasm_obj, 1)));
  }

  testThrowsRepeated(() => Symbol.for(wasm_obj), TypeError);
  testThrowsRepeated(() => Symbol.keyFor(wasm_obj), TypeError);
  testThrowsRepeated(() => Date.parse(wasm_obj), TypeError);
  testThrowsRepeated(() => Date.UTC(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setDate(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setFullYear(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setHours(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setMilliseconds(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setMinutes(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setMonth(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setSeconds(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setTime(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setUTCDate(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setUTCFullYear(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setUTCHours(wasm_obj), TypeError);
  testThrowsRepeated(
      () => (new Date()).setUTCMilliseconds(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setUTCMinutes(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setUTCMonth(wasm_obj), TypeError);
  testThrowsRepeated(() => (new Date()).setUTCSeconds(wasm_obj), TypeError);
  // Date.prototype.toJSON() parameter `key` is ignored.
  repeated(() => (new Date()).toJSON(wasm_obj));
  testThrowsRepeated(() => String.fromCharCode(wasm_obj), TypeError);
  testThrowsRepeated(() => String.fromCodePoint(wasm_obj), TypeError);
  testThrowsRepeated(() => String.raw(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.at(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.charAt(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.charCodeAt(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.codePointAt(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.concat(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.endsWith(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.endsWith('t', wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.includes(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.includes('t', wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.indexOf(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.lastIndexOf(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.localeCompare(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.match(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.matchAll(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.normalize(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.padEnd(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.padStart(10, wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.repeat(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.replace(wasm_obj, ''), TypeError);
  testThrowsRepeated(() => 'test'.replace('t', wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.replaceAll(wasm_obj, ''), TypeError);
  testThrowsRepeated(() => 'test'.search(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.slice(wasm_obj, 2), TypeError);
  testThrowsRepeated(() => 'test'.split(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.startsWith(wasm_obj), TypeError);
  testThrowsRepeated(() => 'test'.substring(wasm_obj), TypeError);

  let i8Array = new Int8Array(32);
  testThrowsRepeated(() => Atomics.add(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.add(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.add(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.and(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.and(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.and(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(
      () => Atomics.compareExchange(wasm_obj, 1, 2, 3), TypeError);
  testThrowsRepeated(
      () => Atomics.compareExchange(i8Array, wasm_obj, 2, 3), TypeError);
  testThrowsRepeated(
      () => Atomics.compareExchange(i8Array, 1, wasm_obj, 3), TypeError);
  testThrowsRepeated(
      () => Atomics.compareExchange(i8Array, 1, 2, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.exchange(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.exchange(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.exchange(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.isLockFree(wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.load(wasm_obj, 1), TypeError);
  testThrowsRepeated(() => Atomics.load(i8Array, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.or(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.or(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.or(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.store(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.store(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.store(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.sub(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.sub(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.sub(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.wait(wasm_obj, 1, 2, 3), TypeError);
  testThrowsRepeated(() => Atomics.wait(i8Array, wasm_obj, 2, 3), TypeError);
  testThrowsRepeated(() => Atomics.wait(i8Array, 1, wasm_obj, 3), TypeError);
  testThrowsRepeated(() => Atomics.wait(i8Array, 1, 2, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.notify(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.notify(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.notify(i8Array, 1, wasm_obj), TypeError);
  testThrowsRepeated(() => Atomics.xor(wasm_obj, 1, 2), TypeError);
  testThrowsRepeated(() => Atomics.xor(i8Array, wasm_obj, 2), TypeError);
  testThrowsRepeated(() => Atomics.xor(i8Array, 1, wasm_obj), TypeError);

  testThrowsRepeated(() => JSON.parse(wasm_obj), TypeError);
  repeated(() => assertEquals({x: 1}, JSON.parse('{"x": 1}', wasm_obj)));
  repeated(() => assertEquals(undefined, JSON.stringify(wasm_obj)));
  repeated(() => assertEquals('{"x":1}', JSON.stringify({x: 1}, wasm_obj)));
  repeated(
      () => assertEquals('{"x":1}', JSON.stringify({x: 1}, null, wasm_obj)));
  repeated(
      () => assertEquals("{}", JSON.stringify({wasm_obj})));

  // Yielding wasm objects from a generator function is valid.
  repeated(() => {
    let gen = (function*() {
      yield wasm_obj;
    })();
    assertSame(wasm_obj, gen.next().value);
  });
  // Test passing wasm objects via next() back to a generator function.
  repeated(() => {
    let gen = (function*() {
      assertSame(wasm_obj, yield 1);
    })();
    assertEquals(1, gen.next().value);
    assertTrue(gen.next(wasm_obj).done);  // triggers the assertEquals.
  });
  // Test passing wasm objects via return() to a generator function.
  repeated(() => {
    let gen = (function*() {
      yield 1;
      assertTrue(false);
    })();
    assertEquals({value: wasm_obj, done: true}, gen.return(wasm_obj));
  });
  // Test passing wasm objects via throw() to a generator function.
  repeated(() => {
    let gen = (function*() {
      try {
        yield 1;
        assertTrue(false);  // unreached
      } catch (e) {
        assertSame(wasm_obj, e);
        return 2;
      }
    })();
    assertEquals({value: 1, done: false}, gen.next());
    // wasm_obj is caught inside the generator
    assertEquals({value: 2, done: true}, gen.throw(wasm_obj));
  });
  // Treating wasm objects as generators is invalid.
  repeated(() => {
    let gen = function*() {
      yield* wasm_obj;
    };
    assertThrows(() => gen().next(), TypeError);
  });

  // Ensure no statement re-assigned wasm_obj by accident.
  assertTrue(wasm_obj == struct || wasm_obj == array);
}
