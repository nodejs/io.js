'use strict';

// This tests the v8.queryObjects() API.

const common = require('../common');
const v8 = require('v8');
const assert = require('assert');
const { inspect } = require('util');

function format(obj) {
  return inspect(obj, { depth: 0 });
}

common.expectWarning(
  'ExperimentalWarning',
  'v8.queryObjects() is an experimental feature and might change at any time',
);

{
  for (const invalid of [undefined, 1, null, false, {}, 'foo']) {
    assert.throws(() => v8.queryObjects(invalid), { code: 'ERR_INVALID_ARG_TYPE' });
  }
  for (const invalid of [1, null, false, 'foo']) {
    assert.throws(() => v8.queryObjects(() => {}, invalid), { code: 'ERR_INVALID_ARG_TYPE' });
  }
  assert.throws(() => v8.queryObjects(() => {}, { format: 'invalid' }), { code: 'ERR_INVALID_ARG_VALUE' });
}

{
  class TestV8QueryObjectsClass {}
  // By default, returns count of objects with the constructor on the prototype.
  assert.strictEqual(v8.queryObjects(TestV8QueryObjectsClass), 0);
  assert.strictEqual(v8.queryObjects(TestV8QueryObjectsClass, { format: 'count' }), 0);
  // 'summary' format returns an array.
  assert.deepStrictEqual(v8.queryObjects(TestV8QueryObjectsClass, { format: 'summary' }), []);

  // Create an instance and check that it shows up in the results.
  const obj = new TestV8QueryObjectsClass();
  assert.strictEqual(v8.queryObjects(TestV8QueryObjectsClass), 1);
  assert.strictEqual(v8.queryObjects(TestV8QueryObjectsClass, { format: 'count' }), 1);
  assert.deepStrictEqual(
    v8.queryObjects(TestV8QueryObjectsClass, { format: 'summary' }),
    [ format(obj)]
  );
}

{
  // ES6 class inheritance.
  class TestV8QueryObjectsBaseClass {}
  class TestV8QueryObjectsChildClass extends TestV8QueryObjectsBaseClass {}
  const summary = v8.queryObjects(TestV8QueryObjectsBaseClass, { format: 'summary' });
  // TestV8QueryObjectsChildClass's prototype's [[Prototype]] slot is
  // TestV8QueryObjectsBaseClass's prototoype so it shows up in the query.
  assert.deepStrictEqual(summary, [
    format(TestV8QueryObjectsChildClass.prototype),
  ]);
  const obj = new TestV8QueryObjectsChildClass();
  assert.deepStrictEqual(
    v8.queryObjects(TestV8QueryObjectsBaseClass, { format: 'summary' }).sort(),
    [
      format(TestV8QueryObjectsChildClass.prototype),
      format(obj),
    ].sort()
  );
  assert.deepStrictEqual(
    v8.queryObjects(TestV8QueryObjectsChildClass, { format: 'summary' }),
    [ format(obj) ],
  );
}

{
  function TestV8QueryObjectsCtor() {}
  assert.strictEqual(v8.queryObjects(TestV8QueryObjectsCtor), 0);
  assert.strictEqual(v8.queryObjects(TestV8QueryObjectsCtor, { format: 'count' }), 0);
  assert.deepStrictEqual(v8.queryObjects(TestV8QueryObjectsCtor, { format: 'summary' }), []);

  // Create an instance and check that it shows up in the results.
  const obj = new TestV8QueryObjectsCtor();
  assert.strictEqual(v8.queryObjects(TestV8QueryObjectsCtor), 1);
  assert.strictEqual(v8.queryObjects(TestV8QueryObjectsCtor, { format: 'count' }), 1);
  assert.deepStrictEqual(
    v8.queryObjects(TestV8QueryObjectsCtor, { format: 'summary' }),
    [ format(obj)]
  );
}

{
  // Classic inheritance.
  function TestV8QueryObjectsBaseCtor() {}

  function TestV8QueryObjectsChildCtor() {}
  Object.setPrototypeOf(TestV8QueryObjectsChildCtor.prototype, TestV8QueryObjectsBaseCtor.prototype);
  Object.setPrototypeOf(TestV8QueryObjectsChildCtor, TestV8QueryObjectsBaseCtor);

  const summary = v8.queryObjects(TestV8QueryObjectsBaseCtor, { format: 'summary' });
  assert.deepStrictEqual(summary, [
    format(TestV8QueryObjectsChildCtor.prototype),
  ]);
  const obj = new TestV8QueryObjectsChildCtor();
  assert.deepStrictEqual(
    v8.queryObjects(TestV8QueryObjectsChildCtor, { format: 'summary' }),
    [ format(obj) ],
  );
}
