// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Flags: --turbofan --no-always-turbofan --allow-natives-syntax

d8.file.execute('test/mjsunit/wasm/gc-js-interop-helpers.js');

let {struct, array} = CreateWasmObjects();
for (const wasm_obj of [struct, array]) {

  // Test Object.
  testThrowsRepeated(() => Object.freeze(wasm_obj), TypeError);
  testThrowsRepeated(() => Object.seal(wasm_obj), TypeError);
  repeated(() => assertSame(
      undefined, Object.prototype.__lookupGetter__.call(wasm_obj, 'foo')));
  repeated(() => assertSame(
      undefined, Object.prototype.__lookupSetter__.call(wasm_obj, 'foo')));
  testThrowsRepeated(
      () => Object.prototype.__defineGetter__.call(wasm_obj, 'foo', () => 42),
      TypeError);
  testThrowsRepeated(
      () => Object.prototype.__defineSetter__.call(wasm_obj, 'foo', () => {}),
      TypeError);
  testThrowsRepeated(
      () => Object.defineProperty(wasm_obj, 'foo', {value: 42}), TypeError);

  repeated(() => assertEquals([], Object.getOwnPropertyNames(wasm_obj)));
  repeated(() => assertEquals([], Object.getOwnPropertySymbols(wasm_obj)));
  repeated(() => assertEquals({}, Object.getOwnPropertyDescriptors(wasm_obj)));
  repeated(() => assertEquals([], Object.keys(wasm_obj)));
  repeated(() => assertEquals([], Object.entries(wasm_obj)));
  repeated(
      () => assertEquals(
          undefined, Object.getOwnPropertyDescriptor(wasm_obj, 'foo')));
  repeated(() => assertEquals(false, 'foo' in wasm_obj));
  repeated(
      () => assertEquals(
          false, Object.prototype.hasOwnProperty.call(wasm_obj, 'foo')));
  repeated(() => assertEquals(true, Object.isSealed(wasm_obj)));
  repeated(() => assertEquals(true, Object.isFrozen(wasm_obj)));
  repeated(() => assertEquals(false, Object.isExtensible(wasm_obj)));
  repeated(() => assertEquals('object', typeof wasm_obj));
  repeated(
      () => assertEquals(
          '[object Object]', Object.prototype.toString.call(wasm_obj)));

  repeated(() => {
    let tgt = {};
    Object.assign(tgt, wasm_obj);
    assertEquals({}, tgt);
  });
  repeated(() => Object.create(wasm_obj));
  repeated(() => ({}).__proto__ = wasm_obj);
  testThrowsRepeated(
      () => Object.defineProperties(wasm_obj, {prop: {value: 1}}), TypeError);
  testThrowsRepeated(
      () => Object.defineProperty(wasm_obj, 'prop', {value: 1}), TypeError);
  testThrowsRepeated(() => Object.fromEntries(wasm_obj), TypeError);
  repeated(() => assertSame(null, Object.getPrototypeOf(wasm_obj)));
  repeated(() => assertFalse(Object.hasOwn(wasm_obj, 'test')));
  testThrowsRepeated(() => Object.preventExtensions(wasm_obj), TypeError);
  testThrowsRepeated(() => Object.setPrototypeOf(wasm_obj, Object), TypeError);
  repeated(() => assertEquals([], Object.values(wasm_obj)));
  testThrowsRepeated(() => wasm_obj.toString(), TypeError);

  // Test prototype chain containing a wasm object.
  {
    let obj = Object.create(wasm_obj);
    repeated(() => assertSame(wasm_obj, Object.getPrototypeOf(obj)));
    repeated(() => assertSame(wasm_obj, Reflect.getPrototypeOf(obj)));
    repeated(() => assertSame(undefined, obj.__proto__));
    testThrowsRepeated(() => obj.__proto__ = wasm_obj, TypeError);
    // Property access fails.
    repeated(() => assertSame(undefined, obj[0]));
    repeated(() => assertSame(undefined, obj.prop));
    testThrowsRepeated(() => obj.toString(), TypeError);
    // Most conversions fail as it will use .toString(), .valueOf(), ...
    testThrowsRepeated(() => `${obj}`, TypeError);
    testThrowsRepeated(() => obj + 1, TypeError);
    repeated(() => assertTrue(!!obj));
  }
  repeated(() => {
    let obj = {};
    Object.setPrototypeOf(obj, wasm_obj);
    assertSame(wasm_obj, Object.getPrototypeOf(obj));
    Object.setPrototypeOf(obj, null);
    assertSame(null, Object.getPrototypeOf(obj));
    Reflect.setPrototypeOf(obj, wasm_obj);
    assertSame(wasm_obj, Reflect.getPrototypeOf(obj));
  })

  // Test Reflect.
  {
    let fct = function(x) {
      return [this, x]
    };
    repeated(
        () => assertEquals([wasm_obj, 1], Reflect.apply(fct, wasm_obj, [1])));
    repeated(
        () => assertEquals([{}, wasm_obj], Reflect.apply(fct, {}, [wasm_obj])));
    repeated(() => assertEquals([new Number(1), undefined], Reflect.apply(fct, 1, wasm_obj)));
    testThrowsRepeated(() => Reflect.apply(wasm_obj, null, []), TypeError);
  }
  testThrowsRepeated(() => Reflect.construct(wasm_obj, []), TypeError);
  repeated(() => assertEquals({}, Reflect.construct(Object, wasm_obj)));
  testThrowsRepeated(() => Reflect.construct(Object, [], wasm_obj), TypeError);
  testThrowsRepeated(
      () => Reflect.defineProperty(wasm_obj, 'prop', {value: 1}), TypeError);
  testThrowsRepeated(
      () => Reflect.defineProperty({}, wasm_obj, {value: 1}), TypeError);

  // Reflect.defineProperty performs ToPropertyDescriptor on the third
  // argument which checks whether {value} etc. exist before accessing them.
  // Therefore it does not throw but add the property with value undefined.
  repeated(() => {
    let obj = {};
    assertTrue(Reflect.defineProperty(obj, 'prop', wasm_obj));
    assertTrue(obj.hasOwnProperty('prop'));
    assertEquals(undefined, obj.prop);
  });
  repeated(() => {
    let obj = {};
    assertTrue(Reflect.defineProperty(obj, 'prop2', {value: wasm_obj}));
    assertSame(wasm_obj, obj.prop2);
  });
  testThrowsRepeated(() => Reflect.deleteProperty(wasm_obj, 'prop'), TypeError);
  testThrowsRepeated(() => Reflect.deleteProperty({}, wasm_obj), TypeError);
  repeated(() => assertSame(undefined, Reflect.get(wasm_obj, 'prop')));
  repeated(() => assertSame(null, Reflect.getPrototypeOf(wasm_obj)));
  repeated(() => assertFalse(Reflect.has(wasm_obj, 'prop')));
  repeated(() => assertTrue(Reflect.has({wasm_obj}, 'wasm_obj')));

  repeated(() => assertFalse(Reflect.isExtensible(wasm_obj)));
  repeated(() => assertEquals([], Reflect.ownKeys(wasm_obj)));
  testThrowsRepeated(() => Reflect.preventExtensions(wasm_obj), TypeError);
  testThrowsRepeated(() => Reflect.set(wasm_obj, 'prop', 123), TypeError);
  testThrowsRepeated(() => Reflect.set([], 0, 0, wasm_obj), TypeError);
  testThrowsRepeated(
      () => Reflect.setPrototypeOf(wasm_obj, Object.prototype), TypeError);
  repeated(() => Reflect.setPrototypeOf({}, wasm_obj));

  // Test Proxy.
  {
    const handler = {
      get(target, prop, receiver) {
        return 'proxied';
      }
    };
    let proxy = new Proxy(wasm_obj, handler);
    repeated(() => assertEquals('proxied', proxy.abc));
    testThrowsRepeated(() => proxy.abc = 123, TypeError);
  }
  {
    let underlyingObject = {};
    let proxy = new Proxy(underlyingObject, wasm_obj);
    repeated(() => assertSame(undefined, proxy.abc));
    underlyingObject.abc = 123;
    repeated(() => assertSame(123, proxy.abc));
  }
  {
    const handler = {
      get(target, prop, receiver) {
        return 'proxied';
      }
    };
    let {proxy, revoke} = Proxy.revocable(wasm_obj, handler);
    repeated(() => assertEquals('proxied', proxy.abc));
    testThrowsRepeated(() => proxy.abc = 123, TypeError);
    revoke();
    testThrowsRepeated(() => proxy.abc, TypeError);
  }
  {
    let proxy = Proxy.revocable({}, wasm_obj).proxy;
    repeated(() => assertSame(undefined, proxy.abc));
  }

  // Ensure no statement re-assigned wasm_obj by accident.
  assertTrue(wasm_obj == struct || wasm_obj == array);
}
