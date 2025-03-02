// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


function shouldThrowSyntaxError(script) {
  let error;
  try {
    eval(script);
  } catch (e) {
    error = e;
  }

  if (!(error instanceof SyntaxError)) {
    throw new Error('Expected SyntaxError!');
  }
}

assertEquals(undefined?.valueOf(), undefined);
assertEquals(null?.valueOf(), undefined);
assertEquals(true?.valueOf(), true);
assertEquals(false?.valueOf(), false);
assertEquals(0?.valueOf(), 0);
assertEquals(1?.valueOf(), 1);
assertEquals(''?.valueOf(), '');
assertEquals('hi'?.valueOf(), 'hi');
assertEquals(({})?.constructor, Object);
assertEquals(({ x: 'hi' })?.x, 'hi');
assertEquals([]?.length, 0);
assertEquals(['hi']?.length, 1);

assertEquals(undefined?.['valueOf'](), undefined);
assertEquals(null?.['valueOf'](), undefined);
assertEquals(true?.['valueOf'](), true);
assertEquals(false?.['valueOf'](), false);
assertEquals(0?.['valueOf'](), 0);
assertEquals(1?.['valueOf'](), 1);
assertEquals(''?.['valueOf'](), '');
assertEquals('hi'?.['valueOf'](), 'hi');
assertEquals(({})?.['constructor'], Object);
assertEquals(({ x: 'hi' })?.['x'], 'hi');
assertEquals([]?.['length'], 0);
assertEquals(['hi']?.[0], 'hi');

assertEquals(undefined?.(), undefined);
assertEquals(null?.(), undefined);
assertThrows(() => true?.(), TypeError);
assertThrows(() => false?.(), TypeError);
assertThrows(() => 0?.(), TypeError);
assertThrows(() => 1?.(), TypeError);
assertThrows(() => ''?.(), TypeError);
assertThrows(() => 'hi'?.(), TypeError);
assertThrows(() => ({})?.(), TypeError);
assertThrows(() => ({ x: 'hi' })?.(), TypeError);
assertThrows(() => []?.(), TypeError);
assertThrows(() => ['hi']?.(), TypeError);

assertThrows(() => ({})?.a['b'], TypeError);
assertEquals(({})?.a?.['b'], undefined);
assertEquals(null?.a['b']().c, undefined);
assertThrows(() => ({})?.['a'].b);
assertEquals(({})?.['a']?.b, undefined);
assertEquals(null?.['a'].b()['c'], undefined);
assertThrows(() => (() => {})?.()(), TypeError);
assertEquals((() => {})?.()?.(), undefined);
assertEquals(null?.()().a['b'], undefined);

assertEquals(delete undefined?.foo, true);
assertEquals(delete null?.foo, true);
assertEquals(delete undefined?.['foo'], true);
assertEquals(delete null?.['foo'], true);
assertEquals(delete undefined?.(), true);
assertEquals(delete null?.(), true);

assertEquals(undefined?.(...a), undefined);
assertEquals(null?.(1, ...a), undefined);
assertEquals(({}).a?.(...a), undefined);
assertEquals(({ a: null }).a?.(...a), undefined);
assertEquals(undefined?.(...a)?.(1, ...a), undefined);
assertThrows(() => 5?.(...[]), TypeError);

const o1 = { x: 0, y: 0, z() {} };
assertEquals(delete o1?.x, true);
assertEquals(o1.x, undefined);
assertEquals(delete o1?.x, true);
assertEquals(delete o1?.['y'], true);
assertEquals(o1.y, undefined);
assertEquals(delete o1?.['y'], true);
assertEquals(delete o1.z?.(), true);
assertThrows(() => { delete ({})?.foo.bar; });

shouldThrowSyntaxError('class C {} class D extends C { foo() { return super?.bar; } }');
shouldThrowSyntaxError('class C {} class D extends C { foo() { return super?.["bar"]; } }');
shouldThrowSyntaxError('class C {} class D extends C { constructor() { super?.(); } }');

shouldThrowSyntaxError('const o = { C: class {} }; new o?.C();');
shouldThrowSyntaxError('const o = { C: class {} }; new o?.["C"]();');
shouldThrowSyntaxError('class C {} new C?.();');
shouldThrowSyntaxError('function foo() { new?.target; }');

shouldThrowSyntaxError('function tag() {} tag?.``;');
shouldThrowSyntaxError('const o = { tag() {} }; o?.tag``;');

const o2 = {
  count: 0,
  get x() {
    this.count += 1;
    return () => {};
  },
};
o2.x?.y;
assertEquals(o2.count, 1);
o2.x?.['y'];
assertEquals(o2.count, 2);
o2.x?.();
assertEquals(o2.count, 3);

assertEquals(true?.5:5, 0.5);
