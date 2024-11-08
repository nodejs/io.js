'use strict';

const { hasCrypto } = require('../common');
const assert = require('assert');
const util = require('util');
const { test } = require('node:test');
const { AssertionError } = assert;
const defaultMsgStart = 'Expected values to be strictly deep-equal:\n';
const defaultMsgStartFull = `${defaultMsgStart}+ actual - expected`;

// Disable colored output to prevent color codes from breaking assertion
// message comparisons. This should only be an issue when process.stdout
// is a TTY.
if (process.stdout.isTTY)
  process.env.NODE_DISABLE_COLORS = '1';

// Template tag function turning an error message into a RegExp
// for assert.throws()
function re(literals, ...values) {
  let result = 'Expected values to be loosely deep-equal:\n\n';
  for (const [i, value] of values.entries()) {
    const str = util.inspect(value, {
      compact: false,
      depth: 1000,
      customInspect: false,
      maxArrayLength: Infinity,
      breakLength: Infinity,
      sorted: true,
      getters: true
    });
    // Need to escape special characters.
    result += `${str}${literals[i + 1]}`;
  }
  return {
    code: 'ERR_ASSERTION',
    message: result
  };
}

const date = new Date('2016');

class MyDate extends Date {
  constructor(...args) {
    super(...args);
    this[0] = '1';
  }
}

const date2 = new MyDate('2016');

class MyRegExp extends RegExp {
  constructor(...args) {
    super(...args);
    this[0] = '1';
  }
}

// The following deepEqual tests might seem very weird.
// They just describe what it is now.
// That is why we discourage using deepEqual in our own tests.

// Turn off no-restricted-properties because we are testing deepEqual!
/* eslint-disable no-restricted-properties */

test('deepEqual', () => {
  const arr = new Uint8Array([120, 121, 122, 10]);
  const buf = Buffer.from(arr);
  // They have different [[Prototype]]
  assert.throws(
    () => assert.deepStrictEqual(arr, buf),
    {
      code: 'ERR_ASSERTION',
      message: 'Expected values to be strictly deep-equal:\n' +
        '+ actual - expected\n' +
        '\n' +
        '+ Uint8Array(4) [\n' +
        '- Buffer(4) [Uint8Array] [\n' +
        '    120,\n' +
        '    121,\n' +
        '    122,\n' +
        '    10\n' +
        '  ]\n'
    }
  );
  assert.deepEqual(arr, buf);

  {
    const buf2 = Buffer.from(arr);
    buf2.prop = 1;

    assert.throws(
      () => assert.deepStrictEqual(buf2, buf),
      {
        code: 'ERR_ASSERTION',
        message: `${defaultMsgStartFull}\n\n` +
                '  Buffer(4) [Uint8Array] [\n' +
                '    120,\n' +
                '    121,\n' +
                '    122,\n' +
                '    10,\n' +
                '+   prop: 1\n' +
                '  ]\n'
      }
    );
    assert.notDeepEqual(buf2, buf);
  }

  {
    const arr2 = new Uint8Array([120, 121, 122, 10]);
    arr2.prop = 5;
    assert.throws(
      () => assert.deepStrictEqual(arr, arr2),
      {
        code: 'ERR_ASSERTION',
        message: `${defaultMsgStartFull}\n\n` +
                '  Uint8Array(4) [\n' +
                '    120,\n' +
                '    121,\n' +
                '    122,\n' +
                '    10,\n' +
                '-   prop: 5\n' +
                '  ]\n'
      }
    );
    assert.notDeepEqual(arr, arr2);
  }
});

test('date', () => {
  assertNotDeepOrStrict(date, date2);
  assert.throws(
    () => assert.deepStrictEqual(date, date2),
    {
      code: 'ERR_ASSERTION',
      message: `${defaultMsgStartFull}\n\n` +
              '+ 2016-01-01T00:00:00.000Z\n- MyDate 2016-01-01T00:00:00.000Z' +
              " {\n-   '0': '1'\n- }\n"
    }
  );
  assert.throws(
    () => assert.deepStrictEqual(date2, date),
    {
      code: 'ERR_ASSERTION',
      message: `${defaultMsgStartFull}\n\n` +
              '+ MyDate 2016-01-01T00:00:00.000Z {\n' +
              "+   '0': '1'\n+ }\n- 2016-01-01T00:00:00.000Z\n"
    }
  );
});

test('regexp', () => {
  const re1 = new RegExp('test');
  const re2 = new MyRegExp('test');

  assertNotDeepOrStrict(re1, re2);
  assert.throws(
    () => assert.deepStrictEqual(re1, re2),
    {
      code: 'ERR_ASSERTION',
      message: `${defaultMsgStartFull}\n\n` +
              "+ /test/\n- MyRegExp /test/ {\n-   '0': '1'\n- }\n"
    }
  );
});

// For these weird cases, deepEqual should pass (at least for now),
// but deepStrictEqual should throw.
test('deepEqual should pass for these weird cases', () => {
  const re2 = new MyRegExp('test');
  const similar = new Set([
    { 0: 1 },  // Object
    new String('1'),  // Object
    [1],  // Array
    date2,  // Date with this[0] = '1'
    re2,  // RegExp with this[0] = '1'
    new Int8Array([1]), // Int8Array
    new Int16Array([1]), // Int16Array
    new Uint16Array([1]), // Uint16Array
    new Int32Array([1]), // Int32Array
    new Uint32Array([1]), // Uint32Array
    Buffer.from([1]), // Uint8Array
    (function() { return arguments; })(1),
  ]);

  for (const a of similar) {
    for (const b of similar) {
      if (a !== b) {
        assert.notDeepEqual(a, b);
        assert.throws(
          () => assert.deepStrictEqual(a, b),
          { code: 'ERR_ASSERTION' }
        );
      }
    }
  }
});

function assertDeepAndStrictEqual(a, b) {
  assert.deepEqual(a, b);
  assert.deepStrictEqual(a, b);

  assert.deepEqual(b, a);
  assert.deepStrictEqual(b, a);
}

function assertNotDeepOrStrict(a, b, err) {
  assert.throws(
    () => assert.deepEqual(a, b),
    err || re`${a}\n\nshould loosely deep-equal\n\n${b}`
  );
  assert.throws(
    () => assert.deepStrictEqual(a, b),
    err || { code: 'ERR_ASSERTION' }
  );

  assert.throws(
    () => assert.deepEqual(b, a),
    err || re`${b}\n\nshould loosely deep-equal\n\n${a}`
  );
  assert.throws(
    () => assert.deepStrictEqual(b, a),
    err || { code: 'ERR_ASSERTION' }
  );
}

function assertOnlyDeepEqual(a, b, err) {
  assert.deepEqual(a, b);
  assert.throws(
    () => assert.deepStrictEqual(a, b),
    err || { code: 'ERR_ASSERTION' }
  );

  assert.deepEqual(b, a);
  assert.throws(
    () => assert.deepStrictEqual(b, a),
    err || { code: 'ERR_ASSERTION' }
  );
}

test('es6 Maps and Sets', () => {
  assertDeepAndStrictEqual(new Set(), new Set());
  assertDeepAndStrictEqual(new Map(), new Map());

  assertDeepAndStrictEqual(new Set([1, 2, 3]), new Set([1, 2, 3]));
  assertNotDeepOrStrict(new Set([1, 2, 3]), new Set([1, 2, 3, 4]));
  assertNotDeepOrStrict(new Set([1, 2, 3, 4]), new Set([1, 2, 3]));
  assertDeepAndStrictEqual(new Set(['1', '2', '3']), new Set(['1', '2', '3']));
  assertDeepAndStrictEqual(new Set([[1, 2], [3, 4]]), new Set([[3, 4], [1, 2]]));
  assertNotDeepOrStrict(new Set([{ a: 0 }]), new Set([{ a: 1 }]));
  assertNotDeepOrStrict(new Set([Symbol()]), new Set([Symbol()]));

  {
    const a = [ 1, 2 ];
    const b = [ 3, 4 ];
    const c = [ 1, 2 ];
    const d = [ 3, 4 ];

    assertDeepAndStrictEqual(
      { a: a, b: b, s: new Set([a, b]) },
      { a: c, b: d, s: new Set([d, c]) }
    );
  }

  assertDeepAndStrictEqual(new Map([[1, 1], [2, 2]]), new Map([[1, 1], [2, 2]]));
  assertDeepAndStrictEqual(new Map([[1, 1], [2, 2]]), new Map([[2, 2], [1, 1]]));
  assertNotDeepOrStrict(new Map([[1, 1], [2, 2]]), new Map([[1, 2], [2, 1]]));
  assertNotDeepOrStrict(
    new Map([[[1], 1], [{}, 2]]),
    new Map([[[1], 2], [{}, 1]])
  );

  assertNotDeepOrStrict(new Set([1]), [1]);
  assertNotDeepOrStrict(new Set(), []);
  assertNotDeepOrStrict(new Set(), {});

  assertNotDeepOrStrict(new Map([['a', 1]]), { a: 1 });
  assertNotDeepOrStrict(new Map(), []);
  assertNotDeepOrStrict(new Map(), {});

  assertOnlyDeepEqual(new Set(['1']), new Set([1]));

  assertOnlyDeepEqual(new Map([['1', 'a']]), new Map([[1, 'a']]));
  assertOnlyDeepEqual(new Map([['a', '1']]), new Map([['a', 1]]));
  assertNotDeepOrStrict(new Map([['a', '1']]), new Map([['a', 2]]));

  assertDeepAndStrictEqual(new Set([{}]), new Set([{}]));

  // Ref: https://github.com/nodejs/node/issues/13347
  assertNotDeepOrStrict(
    new Set([{ a: 1 }, { a: 1 }]),
    new Set([{ a: 1 }, { a: 2 }])
  );
  assertNotDeepOrStrict(
    new Set([{ a: 1 }, { a: 1 }, { a: 2 }]),
    new Set([{ a: 1 }, { a: 2 }, { a: 2 }])
  );
  assertNotDeepOrStrict(
    new Map([[{ x: 1 }, 5], [{ x: 1 }, 5]]),
    new Map([[{ x: 1 }, 5], [{ x: 2 }, 5]])
  );

  assertNotDeepOrStrict(new Set([3, '3']), new Set([3, 4]));
  assertNotDeepOrStrict(new Map([[3, 0], ['3', 0]]), new Map([[3, 0], [4, 0]]));

  assertNotDeepOrStrict(
    new Set([{ a: 1 }, { a: 1 }, { a: 2 }]),
    new Set([{ a: 1 }, { a: 2 }, { a: 2 }])
  );

  // Mixed primitive and object keys
  assertDeepAndStrictEqual(
    new Map([[1, 'a'], [{}, 'a']]),
    new Map([[1, 'a'], [{}, 'a']])
  );
  assertDeepAndStrictEqual(
    new Set([1, 'a', [{}, 'a']]),
    new Set([1, 'a', [{}, 'a']])
  );

  // This is an awful case, where a map contains multiple equivalent keys:
  assertOnlyDeepEqual(
    new Map([[1, 'a'], ['1', 'b']]),
    new Map([['1', 'a'], [true, 'b']])
  );
  assertNotDeepOrStrict(
    new Set(['a']),
    new Set(['b'])
  );
  assertDeepAndStrictEqual(
    new Map([[{}, 'a'], [{}, 'b']]),
    new Map([[{}, 'b'], [{}, 'a']])
  );
  assertOnlyDeepEqual(
    new Map([[true, 'a'], ['1', 'b'], [1, 'a']]),
    new Map([['1', 'a'], [1, 'b'], [true, 'a']])
  );
  assertNotDeepOrStrict(
    new Map([[true, 'a'], ['1', 'b'], [1, 'c']]),
    new Map([['1', 'a'], [1, 'b'], [true, 'a']])
  );

  // Similar object keys
  assertNotDeepOrStrict(
    new Set([{}, {}]),
    new Set([{}, 1])
  );
  assertNotDeepOrStrict(
    new Set([[{}, 1], [{}, 1]]),
    new Set([[{}, 1], [1, 1]])
  );
  assertNotDeepOrStrict(
    new Map([[{}, 1], [{}, 1]]),
    new Map([[{}, 1], [1, 1]])
  );
  assertOnlyDeepEqual(
    new Map([[{}, 1], [true, 1]]),
    new Map([[{}, 1], [1, 1]])
  );

  // Similar primitive key / values
  assertNotDeepOrStrict(
    new Set([1, true, false]),
    new Set(['1', 0, '0'])
  );
  assertNotDeepOrStrict(
    new Map([[1, 5], [true, 5], [false, 5]]),
    new Map([['1', 5], [0, 5], ['0', 5]])
  );

  // Undefined value in Map
  assertDeepAndStrictEqual(
    new Map([[1, undefined]]),
    new Map([[1, undefined]])
  );
  assertOnlyDeepEqual(
    new Map([[1, null], ['', '0']]),
    new Map([['1', undefined], [false, 0]])
  );
  assertNotDeepOrStrict(
    new Map([[1, undefined]]),
    new Map([[2, undefined]])
  );

  // null as key
  assertDeepAndStrictEqual(
    new Map([[null, 3]]),
    new Map([[null, 3]])
  );
  assertOnlyDeepEqual(
    new Map([[undefined, null], ['+000', 2n]]),
    new Map([[null, undefined], [false, '2']]),
  );
  const xarray = ['x'];
  assertDeepAndStrictEqual(
    new Set([xarray, ['y']]),
    new Set([xarray, ['y']])
  );
  assertOnlyDeepEqual(
    new Set([null, '', 1n, 5, 2n, false]),
    new Set([undefined, 0, 5n, true, '2', '-000'])
  );
  assertNotDeepOrStrict(
    new Set(['']),
    new Set(['0'])
  );
  assertOnlyDeepEqual(
    new Map([[1, {}]]),
    new Map([[true, {}]])
  );
  assertOnlyDeepEqual(
    new Map([[undefined, true]]),
    new Map([[null, true]])
  );
  assertNotDeepOrStrict(
    new Map([[undefined, true]]),
    new Map([[true, true]])
  );
  {
    const values = [
      123,
      Infinity,
      0,
      null,
      undefined,
      false,
      true,
      {},
      [],
      () => {},
    ];
    assertDeepAndStrictEqual(new Set(values), new Set(values));
    assertDeepAndStrictEqual(new Set(values), new Set(values.reverse()));

    const mapValues = values.map((v) => [v, { a: 5 }]);
    assertDeepAndStrictEqual(new Map(mapValues), new Map(mapValues));
    assertDeepAndStrictEqual(new Map(mapValues), new Map(mapValues.reverse()));
  }

  {
    const s1 = new Set();
    const s2 = new Set();
    s1.add(1);
    s1.add(2);
    s2.add(2);
    s2.add(1);
    assertDeepAndStrictEqual(s1, s2);
  }

  {
    const m1 = new Map();
    const m2 = new Map();
    const obj = { a: 5, b: 6 };
    m1.set(1, obj);
    m1.set(2, 'hi');
    m1.set(3, [1, 2, 3]);

    m2.set(2, 'hi'); // different order
    m2.set(1, obj);
    m2.set(3, [1, 2, 3]); // Deep equal, but not reference equal.

    assertDeepAndStrictEqual(m1, m2);
  }

  {
    const m1 = new Map();
    const m2 = new Map();

    // m1 contains itself.
    m1.set(1, m1);
    m2.set(1, new Map());

    assertNotDeepOrStrict(m1, m2);
  }

  {
    const map1 = new Map([[1, 1]]);
    const map2 = new Map([[1, '1']]);
    assert.deepEqual(map1, map2);
    assert.throws(
      () => assert.deepStrictEqual(map1, map2),
      {
        code: 'ERR_ASSERTION',
        message: `${defaultMsgStartFull}\n\n` +
                 "  Map(1) {\n+   1 => 1\n-   1 => '1'\n  }\n"
      }
    );
  }

  {
    // Two equivalent sets / maps with different key/values applied shouldn't be
    // the same. This is a terrible idea to do in practice, but deepEqual should
    // still check for it.
    const s1 = new Set();
    const s2 = new Set();
    s1.x = 5;
    assertNotDeepOrStrict(s1, s2);

    const m1 = new Map();
    const m2 = new Map();
    m1.x = 5;
    assertNotDeepOrStrict(m1, m2);
  }

  {
    // Circular references.
    const s1 = new Set();
    s1.add(s1);
    const s2 = new Set();
    s2.add(s2);
    assertDeepAndStrictEqual(s1, s2);

    const m1 = new Map();
    m1.set(2, m1);
    const m2 = new Map();
    m2.set(2, m2);
    assertDeepAndStrictEqual(m1, m2);

    const m3 = new Map();
    m3.set(m3, 2);
    const m4 = new Map();
    m4.set(m4, 2);
    assertDeepAndStrictEqual(m3, m4);
  }
});

test('GH-6416. Make sure circular refs do not throw', () => {
  const b = {};
  b.b = b;
  const c = {};
  c.b = c;

  assertDeepAndStrictEqual(b, c);

  const d = {};
  d.a = 1;
  d.b = d;
  const e = {};
  e.a = 1;
  e.b = {};

  assertNotDeepOrStrict(d, e);
});

test('GH-14441. Circular structures should be consistent', () => {
  {
    const a = {};
    a.a = a;

    const b = {};
    b.a = {};
    b.a.a = a;

    assertDeepAndStrictEqual(a, b);
  }

  {
    const a = {};
    a.a = a;
    const b = {};
    b.a = b;
    const c = {};
    c.a = a;
    assertDeepAndStrictEqual(b, c);
  }

  {
    const a = new Set();
    a.add(a);
    const b = new Set();
    b.add(b);
    const c = new Set();
    c.add(a);
    assertDeepAndStrictEqual(b, c);
  }
});

// https://github.com/nodejs/node-v0.x-archive/pull/7178
test('Ensure reflexivity of deepEqual with `arguments` objects.', () => {
  const args = (function() { return arguments; })();
  assertNotDeepOrStrict([], args);
});

test('More checking that arguments objects are handled correctly', () => {
  // eslint-disable-next-line func-style
  const returnArguments = function() { return arguments; };

  const someArgs = returnArguments('a');
  const sameArgs = returnArguments('a');
  const diffArgs = returnArguments('b');

  assertNotDeepOrStrict(someArgs, ['a']);
  assertNotDeepOrStrict(someArgs, { '0': 'a' });
  assertNotDeepOrStrict(someArgs, diffArgs);
  assertDeepAndStrictEqual(someArgs, sameArgs);
});

test('Handle sparse arrays', () => {
  /* eslint-disable no-sparse-arrays */
  assertDeepAndStrictEqual([1, , , 3], [1, , , 3]);
  assertNotDeepOrStrict([1, , , 3], [1, , , 3, , , ]);
  /* eslint-enable no-sparse-arrays */
  const a = new Array(3);
  const b = new Array(3);
  a[2] = true;
  b[1] = true;
  assertNotDeepOrStrict(a, b);
  b[2] = true;
  assertNotDeepOrStrict(a, b);
  a[0] = true;
  assertNotDeepOrStrict(a, b);
});

test('Handle different error messages', () => {
  const err1 = new Error('foo1');
  assertNotDeepOrStrict(err1, new Error('foo2'), assert.AssertionError);
  assertNotDeepOrStrict(err1, new TypeError('foo1'), assert.AssertionError);
  assertDeepAndStrictEqual(err1, new Error('foo1'));
  assertNotDeepOrStrict(err1, {}, AssertionError);
});

test('Handle NaN', () => {
  assertDeepAndStrictEqual(NaN, NaN);
  assertDeepAndStrictEqual({ a: NaN }, { a: NaN });
  assertDeepAndStrictEqual([ 1, 2, NaN, 4 ], [ 1, 2, NaN, 4 ]);
});

test('Handle boxed primitives', () => {
  const boxedString = new String('test');
  const boxedSymbol = Object(Symbol());

  const fakeBoxedSymbol = {};
  Object.setPrototypeOf(fakeBoxedSymbol, Symbol.prototype);
  Object.defineProperty(
    fakeBoxedSymbol,
    Symbol.toStringTag,
    { enumerable: false, value: 'Symbol' }
  );

  assertNotDeepOrStrict(new Boolean(true), Object(false));
  assertNotDeepOrStrict(Object(true), new Number(1));
  assertNotDeepOrStrict(new Number(2), new Number(1));
  assertNotDeepOrStrict(boxedSymbol, Object(Symbol()));
  assertNotDeepOrStrict(boxedSymbol, {});
  assertNotDeepOrStrict(boxedSymbol, fakeBoxedSymbol);
  assertDeepAndStrictEqual(boxedSymbol, boxedSymbol);
  assertDeepAndStrictEqual(Object(true), Object(true));
  assertDeepAndStrictEqual(Object(2), Object(2));
  assertDeepAndStrictEqual(boxedString, Object('test'));
  boxedString.slow = true;
  assertNotDeepOrStrict(boxedString, Object('test'));
  boxedSymbol.slow = true;
  assertNotDeepOrStrict(boxedSymbol, {});
  assertNotDeepOrStrict(boxedSymbol, fakeBoxedSymbol);
});

test('Minus zero', () => {
  assertOnlyDeepEqual(0, -0);
  assertDeepAndStrictEqual(-0, -0);
});

test('Handle symbols (enumerable only)', () => {
  const symbol1 = Symbol();
  const obj1 = { [symbol1]: 1 };
  const obj2 = { [symbol1]: 1 };
  const obj3 = { [Symbol()]: 1 };
  // Add a non enumerable symbol as well. It is going to be ignored!
  Object.defineProperty(obj2, Symbol(), { value: 1 });
  assertOnlyDeepEqual(obj1, obj3);
  assertDeepAndStrictEqual(obj1, obj2);
  obj2[Symbol()] = true;
  assertOnlyDeepEqual(obj1, obj2);
  // TypedArrays have a fast path. Test for this as well.
  const a = new Uint8Array(4);
  const b = new Uint8Array(4);
  a[symbol1] = true;
  b[symbol1] = false;
  assertOnlyDeepEqual(a, b);
  b[symbol1] = true;
  assertDeepAndStrictEqual(a, b);
  // The same as TypedArrays is valid for boxed primitives
  const boxedStringA = new String('test');
  const boxedStringB = new String('test');
  boxedStringA[symbol1] = true;
  assertOnlyDeepEqual(boxedStringA, boxedStringB);
  boxedStringA[symbol1] = true;
  assertDeepAndStrictEqual(a, b);
  // Loose equal arrays should not compare symbols.
  const arr = [1];
  const arr2 = [1];
  arr[symbol1] = true;
  assertOnlyDeepEqual(arr, arr2);
  arr2[symbol1] = false;
  assertOnlyDeepEqual(arr, arr2);
});

test('Additional tests', () => {
  assert.throws(
    () => assert.notDeepEqual(1, true),
    {
      message: /1\n\nshould not loosely deep-equal\n\ntrue/
    }
  );

  assert.throws(
    () => assert.notDeepEqual(1, 1),
    {
      message: /Expected "actual" not to be loosely deep-equal to:\n\n1/
    }
  );

  assertDeepAndStrictEqual(new Date(2000, 3, 14), new Date(2000, 3, 14));

  assert.throws(() => { assert.deepEqual(new Date(), new Date(2000, 3, 14)); },
                AssertionError,
                'deepEqual(new Date(), new Date(2000, 3, 14))');

  assert.throws(
    () => { assert.notDeepEqual(new Date(2000, 3, 14), new Date(2000, 3, 14)); },
    AssertionError,
    'notDeepEqual(new Date(2000, 3, 14), new Date(2000, 3, 14))'
  );

  assert.throws(
    () => { assert.notDeepEqual('a'.repeat(1024), 'a'.repeat(1024)); },
    AssertionError,
    'notDeepEqual("a".repeat(1024), "a".repeat(1024))'
  );

  assertNotDeepOrStrict(new Date(), new Date(2000, 3, 14));

  assertDeepAndStrictEqual(/a/, /a/);
  assertDeepAndStrictEqual(/a/g, /a/g);
  assertDeepAndStrictEqual(/a/i, /a/i);
  assertDeepAndStrictEqual(/a/m, /a/m);
  assertDeepAndStrictEqual(/a/igm, /a/igm);
  assertNotDeepOrStrict(/ab/, /a/);
  assertNotDeepOrStrict(/a/g, /a/);
  assertNotDeepOrStrict(/a/i, /a/);
  assertNotDeepOrStrict(/a/m, /a/);
  assertNotDeepOrStrict(/a/igm, /a/im);

  {
    const re1 = /a/g;
    re1.lastIndex = 3;
    assert.notDeepEqual(re1, /a/g);
  }

  assert.deepEqual(4, '4');
  assert.deepEqual(true, 1);
  assert.throws(() => assert.deepEqual(4, '5'),
                AssertionError,
                'deepEqual( 4, \'5\')');
});

test('Having the same number of owned properties && the same set of keys', () => {
  assert.deepEqual({ a: 4 }, { a: 4 });
  assert.deepEqual({ a: 4, b: '2' }, { a: 4, b: '2' });
  assert.deepEqual([4], ['4']);
  assert.throws(
    () => assert.deepEqual({ a: 4 }, { a: 4, b: true }), AssertionError);
  assert.notDeepEqual(['a'], { 0: 'a' });
  assert.deepEqual({ a: 4, b: '1' }, { b: '1', a: 4 });
  const a1 = [1, 2, 3];
  const a2 = [1, 2, 3];
  a1.a = 'test';
  a1.b = true;
  a2.b = true;
  a2.a = 'test';
  assert.throws(() => assert.deepEqual(Object.keys(a1), Object.keys(a2)),
                AssertionError);
  assertDeepAndStrictEqual(a1, a2);
});

test('Having an identical prototype property', () => {
  const nbRoot = {
    toString() { return `${this.first} ${this.last}`; }
  };

  function nameBuilder(first, last) {
    this.first = first;
    this.last = last;
    return this;
  }
  nameBuilder.prototype = nbRoot;

  function nameBuilder2(first, last) {
    this.first = first;
    this.last = last;
    return this;
  }
  nameBuilder2.prototype = nbRoot;

  const nb1 = new nameBuilder('Ryan', 'Dahl');
  let nb2 = new nameBuilder2('Ryan', 'Dahl');

  assert.deepEqual(nb1, nb2);

  nameBuilder2.prototype = Object;
  nb2 = new nameBuilder2('Ryan', 'Dahl');
  assert.deepEqual(nb1, nb2);
});

test('Primitives', () => {
  assertNotDeepOrStrict(null, {});
  assertNotDeepOrStrict(undefined, {});
  assertNotDeepOrStrict('a', ['a']);
  assertNotDeepOrStrict('a', { 0: 'a' });
  assertNotDeepOrStrict(1, {});
  assertNotDeepOrStrict(true, {});
  assertNotDeepOrStrict(Symbol(), {});
  assertNotDeepOrStrict(Symbol(), Symbol());

  assertOnlyDeepEqual(4, '4');
  assertOnlyDeepEqual(true, 1);

  {
    const s = Symbol();
    assertDeepAndStrictEqual(s, s);
  }

  // Primitive wrappers and object.
  assertNotDeepOrStrict(new String('a'), ['a']);
  assertNotDeepOrStrict(new String('a'), { 0: 'a' });
  assertNotDeepOrStrict(new Number(1), {});
  assertNotDeepOrStrict(new Boolean(true), {});
});

test('Additional tests', () => {
  // Same number of keys but different key names.
  assertNotDeepOrStrict({ a: 1 }, { b: 1 });

  assert.deepStrictEqual(new Date(2000, 3, 14), new Date(2000, 3, 14));

  assert.throws(
    () => assert.deepStrictEqual(new Date(), new Date(2000, 3, 14)),
    AssertionError,
    'deepStrictEqual(new Date(), new Date(2000, 3, 14))'
  );

  assert.throws(
    () => assert.notDeepStrictEqual(new Date(2000, 3, 14), new Date(2000, 3, 14)),
    {
      name: 'AssertionError',
      message: 'Expected "actual" not to be strictly deep-equal to:\n\n' +
              util.inspect(new Date(2000, 3, 14))
    }
  );

  assert.throws(
    () => assert.strictEqual('apple', 'pear'),
    {
      name: 'AssertionError',
      message: 'Expected values to be strictly equal:\n\n\'apple\' !== \'pear\'\n'
    }
  );

  assert.throws(
    () => assert.strictEqual('ABABABABABAB', 'BABABABABABA'),
    {
      name: 'AssertionError',
      message: 'Expected values to be strictly equal:\n' +
        '+ actual - expected\n' +
        '\n' +
        "+ 'ABABABABABAB'\n" +
        "- 'BABABABABABA'\n"
    }
  );

  assert.notDeepStrictEqual(new Date(), new Date(2000, 3, 14));

  assert.throws(
    () => assert.deepStrictEqual(/ab/, /a/),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: 'Expected values to be strictly deep-equal:\n' +
        '+ actual - expected\n' +
        '\n' +
        '+ /ab/\n' +
        '- /a/\n'
    });
  assert.throws(
    () => assert.deepStrictEqual(/a/g, /a/),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: 'Expected values to be strictly deep-equal:\n' +
        '+ actual - expected\n' +
        '\n' +
        '+ /a/g\n' +
        '- /a/\n'
    });
  assert.throws(
    () => assert.deepStrictEqual(/a/i, /a/),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: 'Expected values to be strictly deep-equal:\n' +
        '+ actual - expected\n' +
        '\n' +
        '+ /a/i\n' +
        '- /a/\n'
    });
  assert.throws(
    () => assert.deepStrictEqual(/a/m, /a/),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: 'Expected values to be strictly deep-equal:\n' +
        '+ actual - expected\n' +
        '\n' +
        '+ /a/m\n' +
        '- /a/\n'
    });
  assert.throws(
    () => assert.deepStrictEqual(/aa/igm, /aa/im),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: 'Expected values to be strictly deep-equal:\n' +
        '+ actual - expected\n' +
        '\n' +
        '+ /aa/gim\n' +
        '- /aa/im\n'
    });

  {
    const re1 = /a/;
    re1.lastIndex = 3;
    assert.notDeepStrictEqual(re1, /a/);
  }

  assert.throws(
    // eslint-disable-next-line no-restricted-syntax
    () => assert.deepStrictEqual(4, '4'),
    { message: `${defaultMsgStart}\n4 !== '4'\n` }
  );

  assert.throws(
    // eslint-disable-next-line no-restricted-syntax
    () => assert.deepStrictEqual(true, 1),
    { message: `${defaultMsgStart}\ntrue !== 1\n` }
  );

  assertDeepAndStrictEqual({ a: 4, b: '1' }, { b: '1', a: 4 });

  assert.throws(
    () => assert.deepStrictEqual([0, 1, 2, 'a', 'b'], [0, 1, 2, 'b', 'a']),
    AssertionError);
});

test('Having the same number of owned properties && the same set of keys', () => {
  assert.deepStrictEqual({ a: 4 }, { a: 4 });
  assert.deepStrictEqual({ a: 4, b: '2' }, { a: 4, b: '2' });
  assert.throws(() => assert.deepStrictEqual([4], ['4']),
                {
                  code: 'ERR_ASSERTION',
                  name: 'AssertionError',
                  message: `${defaultMsgStartFull}\n\n  [\n+   4\n-   '4'\n  ]\n`
                });
  assert.throws(
    () => assert.deepStrictEqual({ a: 4 }, { a: 4, b: true }),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: `${defaultMsgStartFull}\n\n  ` +
              '{\n    a: 4,\n-   b: true\n  }\n'
    });
  assert.throws(
    () => assert.deepStrictEqual(['a'], { 0: 'a' }),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: `${defaultMsgStartFull}\n\n` +
              "+ [\n+   'a'\n+ ]\n- {\n-   '0': 'a'\n- }\n"
    });
});

/* eslint-enable */

test('Prototype check', () => {
  function Constructor1(first, last) {
    this.first = first;
    this.last = last;
  }

  function Constructor2(first, last) {
    this.first = first;
    this.last = last;
  }

  const obj1 = new Constructor1('Ryan', 'Dahl');
  let obj2 = new Constructor2('Ryan', 'Dahl');

  assert.throws(() => assert.deepStrictEqual(obj1, obj2), AssertionError);

  Constructor2.prototype = Constructor1.prototype;
  obj2 = new Constructor2('Ryan', 'Dahl');

  assertDeepAndStrictEqual(obj1, obj2);
});

test('Check extra properties on errors', () => {
  const a = new TypeError('foo');
  const b = new TypeError('foo');
  a.foo = 'bar';
  b.foo = 'baz.';

  assert.throws(
    () => assert.throws(
      () => assert.deepStrictEqual(a, b),
      {
        operator: 'throws',
        message: '',
      }
    ),
    {
      message: 'Expected values to be strictly deep-equal:\n' +
        '+ actual - expected\n' +
        '\n' +
        '  Comparison {\n' +
        "+   message: 'Expected values to be strictly deep-equal:\\n' +\n" +
        "+     '+ actual - expected\\n' +\n" +
        "+     '\\n' +\n" +
        "+     '  [TypeError: foo] {\\n' +\n" +
        `+     "+   foo: 'bar'\\n" +\n` +
        `+     "-   foo: 'baz.'\\n" +\n` +
        "+     '  }\\n',\n" +
        "+   operator: 'deepStrictEqual'\n" +
        "-   message: '',\n" +
        "-   operator: 'throws'\n" +
        '  }\n'
    }
  );
});

test('Check proxies', () => {
  const arrProxy = new Proxy([1, 2], {});
  assert.deepStrictEqual(arrProxy, [1, 2]);
  const tmp = util.inspect.defaultOptions;
  util.inspect.defaultOptions = { showProxy: true };
  assert.throws(
    () => assert.deepStrictEqual(arrProxy, [1, 2, 3]),
    { message: `${defaultMsgStartFull}\n\n` +
               '  [\n    1,\n    2,\n-   3\n  ]\n' }
  );
  util.inspect.defaultOptions = tmp;

  const invalidTrap = new Proxy([1, 2, 3], {
    ownKeys() { return []; }
  });
  assert.throws(
    () => assert.deepStrictEqual(invalidTrap, [1, 2, 3]),
    {
      name: 'TypeError',
      message: "'ownKeys' on proxy: trap result did not include 'length'"
    }
  );
});

test('Strict equal with identical objects that are not identical ' +
     'by reference and longer than 50 elements', () => {
  // E.g., assert.deepStrictEqual({ a: Symbol() }, { a: Symbol() })
  const a = {};
  const b = {};
  for (let i = 0; i < 55; i++) {
    a[`symbol${i}`] = Symbol();
    b[`symbol${i}`] = Symbol();
  }

  assert.throws(
    () => assert.deepStrictEqual(a, b),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: /\.\.\./g
    }
  );
});

test('Basic valueOf check', () => {
  const a = new String(1);
  a.valueOf = undefined;
  assertNotDeepOrStrict(a, new String(1));
});

test('Basic array out of bounds check', () => {
  const arr = [1, 2, 3];
  arr[2 ** 32] = true;
  assertNotDeepOrStrict(arr, [1, 2, 3]);

  assert.throws(
    () => assert.deepStrictEqual([1, 2, 3], [1, 2]),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: `${defaultMsgStartFull}\n\n` +
              '  [\n' +
              '    1,\n' +
              '    2,\n' +
              '+   3\n' +
              '  ]\n'
    }
  );
});

test('Verify that manipulating the `getTime()` function has no impact on the time ' +
     'verification.', () => {
  const a = new Date('2000');
  const b = new Date('2000');
  Object.defineProperty(a, 'getTime', {
    value: () => 5
  });
  assertDeepAndStrictEqual(a, b);
});

test('Verify that an array and the equivalent fake array object ' +
     'are correctly compared', () => {
  const a = [1, 2, 3];
  const o = {
    __proto__: Array.prototype,
    0: 1,
    1: 2,
    2: 3,
    length: 3,
  };
  Object.defineProperty(o, 'length', { enumerable: false });
  assertNotDeepOrStrict(o, a);
});

test('Verify that extra keys will be tested for when using fake arrays', () => {
  const a = {
    0: 1,
    1: 1,
    2: 'broken'
  };
  Object.setPrototypeOf(a, Object.getPrototypeOf([]));
  Object.defineProperty(a, Symbol.toStringTag, {
    value: 'Array',
  });
  Object.defineProperty(a, 'length', {
    value: 2
  });
  assertNotDeepOrStrict(a, [1, 1]);
});

test('Verify that changed tags will still check for the error message', () => {
  const err = new Error('foo');
  err[Symbol.toStringTag] = 'Foobar';
  const err2 = new Error('bar');
  err2[Symbol.toStringTag] = 'Foobar';
  assertNotDeepOrStrict(err, err2, AssertionError);
});

test('Check for non-native errors', () => {
  const source = new Error('abc');
  const err = Object.create(
    Object.getPrototypeOf(source), Object.getOwnPropertyDescriptors(source));
  Object.defineProperty(err, 'message', { value: 'foo' });
  const err2 = Object.create(
    Object.getPrototypeOf(source), Object.getOwnPropertyDescriptors(source));
  Object.defineProperty(err2, 'message', { value: 'bar' });
  err[Symbol.toStringTag] = 'Foo';
  err2[Symbol.toStringTag] = 'Foo';
  assert.notDeepStrictEqual(err, err2);
});

test('Check for Errors with cause property', () => {
  const e1 = new Error('err', { cause: new Error('cause e1') });
  const e2 = new Error('err', { cause: new Error('cause e2') });
  assertNotDeepOrStrict(e1, e2, AssertionError);
  assertNotDeepOrStrict(e1, new Error('err'), AssertionError);
  assertDeepAndStrictEqual(e1, new Error('err', { cause: new Error('cause e1') }));
});

test('Check for AggregateError', () => {
  const e1 = new Error('e1');
  const e1duplicate = new Error('e1');
  const e2 = new Error('e2');

  const e3 = new AggregateError([e1duplicate, e2], 'Aggregate Error');
  const e3duplicate = new AggregateError([e1, e2], 'Aggregate Error');
  const e4 = new AggregateError([e1], 'Aggregate Error');
  assertNotDeepOrStrict(e1, e3, AssertionError);
  assertNotDeepOrStrict(e3, e4, AssertionError);
  assertDeepAndStrictEqual(e3, e3duplicate);
});

test('Verify that `valueOf` is not called for boxed primitives', () => {
  const a = new Number(5);
  const b = new Number(5);
  Object.defineProperty(a, 'valueOf', {
    value: () => { throw new Error('failed'); }
  });
  Object.defineProperty(b, 'valueOf', {
    value: () => { throw new Error('failed'); }
  });
  assertDeepAndStrictEqual(a, b);
});

test('Check getters', () => {
  const a = {
    get a() { return 5; }
  };
  const b = {
    get a() { return 6; }
  };
  assert.throws(
    () => assert.deepStrictEqual(a, b),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: /a: \[Getter: 5]\n- {3}a: \[Getter: 6]\n {2}/
    }
  );

  // The descriptor is not compared.
  assertDeepAndStrictEqual(a, { a: 5 });
});

test('Verify object types being identical on both sides', () => {
  let a = Buffer.from('test');
  let b = Object.create(
    Object.getPrototypeOf(a),
    Object.getOwnPropertyDescriptors(a)
  );
  Object.defineProperty(b, Symbol.toStringTag, {
    value: 'Uint8Array'
  });
  assertNotDeepOrStrict(a, b);

  a = new Uint8Array(10);
  b = new Int8Array(10);
  Object.defineProperty(b, Symbol.toStringTag, {
    value: 'Uint8Array'
  });
  Object.setPrototypeOf(b, Uint8Array.prototype);
  assertNotDeepOrStrict(a, b);

  a = [1, 2, 3];
  b = { 0: 1, 1: 2, 2: 3 };
  Object.setPrototypeOf(b, Array.prototype);
  Object.defineProperty(b, 'length', { value: 3, enumerable: false });
  Object.defineProperty(b, Symbol.toStringTag, {
    value: 'Array'
  });
  assertNotDeepOrStrict(a, b);

  a = new Date(2000);
  b = Object.create(
    Object.getPrototypeOf(a),
    Object.getOwnPropertyDescriptors(a)
  );
  Object.defineProperty(b, Symbol.toStringTag, {
    value: 'Date'
  });
  assertNotDeepOrStrict(a, b);

  a = /abc/g;
  b = Object.create(
    Object.getPrototypeOf(a),
    Object.getOwnPropertyDescriptors(a)
  );
  Object.defineProperty(b, Symbol.toStringTag, {
    value: 'RegExp'
  });
  assertNotDeepOrStrict(a, b);

  a = [];
  b = /abc/;
  Object.setPrototypeOf(b, Array.prototype);
  Object.defineProperty(b, Symbol.toStringTag, {
    value: 'Array'
  });
  assertNotDeepOrStrict(a, b);

  a = { __proto__: null };
  b = new RangeError('abc');
  Object.defineProperty(a, Symbol.toStringTag, {
    value: 'Error'
  });
  Object.setPrototypeOf(b, null);
  assertNotDeepOrStrict(a, b, assert.AssertionError);
});

test('Verify commutativity', () => {
  // Regression test for https://github.com/nodejs/node/issues/37710
  const a = { x: 1 };
  const b = { y: 1 };
  Object.defineProperty(b, 'x', { value: 1 });

  assertNotDeepOrStrict(a, b);
});

test('Crypto', { skip: !hasCrypto }, async () => {
  const crypto = require('crypto');  // eslint-disable-line node-core/crypto-check
  const { subtle } = globalThis.crypto;

  {
    const a = crypto.createSecretKey(Buffer.alloc(1, 0));
    const b = crypto.createSecretKey(Buffer.alloc(1, 1));

    assertNotDeepOrStrict(a, b);
  }

  {
    const a = crypto.createSecretKey(Buffer.alloc(0));
    const b = crypto.createSecretKey(Buffer.alloc(0));

    assertDeepAndStrictEqual(a, b);
  }

  {
    const a = await subtle.importKey('raw', Buffer.alloc(1, 0),
                                     { name: 'HMAC', hash: 'SHA-256' },
                                     true, ['sign']);
    const b = await subtle.importKey('raw', Buffer.alloc(1, 1),
                                     { name: 'HMAC', hash: 'SHA-256' },
                                     true, ['sign']);

    assertNotDeepOrStrict(a, b);
  }

  {
    const a = await subtle.importKey('raw', Buffer.alloc(1),
                                     { name: 'HMAC', hash: 'SHA-256' },
                                     true, ['sign']);
    const b = await subtle.importKey('raw', Buffer.alloc(1),
                                     { name: 'HMAC', hash: 'SHA-256' },
                                     false, ['sign']);

    assertNotDeepOrStrict(a, b);
  }

  {
    const a = await subtle.importKey('raw', Buffer.alloc(1),
                                     { name: 'HMAC', hash: 'SHA-256' },
                                     true, ['sign']);
    const b = await subtle.importKey('raw', Buffer.alloc(1),
                                     { name: 'HMAC', hash: 'SHA-384' },
                                     true, ['sign']);

    assertNotDeepOrStrict(a, b);
  }

  {
    const a = await subtle.importKey('raw', Buffer.alloc(1),
                                     { name: 'HMAC', hash: 'SHA-256' },
                                     true, ['sign']);
    const b = await subtle.importKey('raw', Buffer.alloc(1),
                                     { name: 'HMAC', hash: 'SHA-256' },
                                     true, ['verify']);

    assertNotDeepOrStrict(a, b);
  }

  {
    const a = await subtle.importKey('raw', Buffer.alloc(1),
                                     { name: 'HMAC', hash: 'SHA-256' },
                                     true, ['sign']);
    const b = await subtle.importKey('raw', Buffer.alloc(1),
                                     { name: 'HMAC', hash: 'SHA-256' },
                                     true, ['sign']);

    assertDeepAndStrictEqual(a, b);
  }
});

test('Comparing two identical WeakMap instances', () => {
  const weakMap = new WeakMap();
  assertDeepAndStrictEqual(weakMap, weakMap);
});

test('Comparing two different WeakMap instances', () => {
  const weakMap1 = new WeakMap();
  const objA = {};
  weakMap1.set(objA, 'ok');

  const weakMap2 = new WeakMap();
  const objB = {};
  weakMap2.set(objB, 'ok');

  assertNotDeepOrStrict(weakMap1, weakMap2);
});

test('Comparing two identical WeakSet instances', () => {
  const weakSet = new WeakSet();
  assertDeepAndStrictEqual(weakSet, weakSet);
});

test('Comparing two different WeakSet instances', () => {
  const weakSet1 = new WeakSet();
  const weakSet2 = new WeakSet();
  assertNotDeepOrStrict(weakSet1, weakSet2);
});

test('Comparing two arrays nested inside object, with overlapping elements', () => {
  const actual = { a: { b: [1, 2, 3] } };
  const expected = { a: { b: [3, 4, 5] } };

  assert.throws(
    () => assert.deepStrictEqual(actual, expected),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: 'Expected values to be strictly deep-equal:\n' +
        '+ actual - expected\n' +
        '\n' +
        '  {\n' +
        '    a: {\n' +
        '      b: [\n' +
        '+       1,\n' +
        '+       2,\n' +
        '        3,\n' +
        '-       4,\n' +
        '-       5\n' +
        '      ]\n' +
        '    }\n' +
        '  }\n'
    }
  );
});

test('Comparing two arrays nested inside object, with overlapping elements, swapping keys', () => {
  const actual = { a: { b: [1, 2, 3], c: 2 } };
  const expected = { a: { b: 1, c: [3, 4, 5] } };

  assert.throws(
    () => assert.deepStrictEqual(actual, expected),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: 'Expected values to be strictly deep-equal:\n' +
        '+ actual - expected\n' +
        '\n' +
        '  {\n' +
        '    a: {\n' +
        '+     b: [\n' +
        '+       1,\n' +
        '+       2,\n' +
        '-     b: 1,\n' +
        '-     c: [\n' +
        '        3,\n' +
        '-       4,\n' +
        '-       5\n' +
        '      ],\n' +
        '+     c: 2\n' +
        '    }\n' +
        '  }\n'
    }
  );
});

test('Detects differences in deeply nested arrays instead of seeing a new object', () => {
  const actual = [
    { a: 1 },
    2,
    3,
    4,
    { c: [1, 2, 3] },
  ];
  const expected = [
    { a: 1 },
    2,
    3,
    4,
    { c: [3, 4, 5] },
  ];

  assert.throws(
    () => assert.deepStrictEqual(actual, expected),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: 'Expected values to be strictly deep-equal:\n' +
      '+ actual - expected\n' +
      '... Skipped lines\n' +
      '\n' +
      '  [\n' +
      '    {\n' +
      '      a: 1\n' +
      '    },\n' +
      '    2,\n' +
      '...\n' +
      '      c: [\n' +
      '+       1,\n' +
      '+       2,\n' +
      '        3,\n' +
      '-       4,\n' +
      '-       5\n' +
      '      ]\n' +
      '    }\n' +
      '  ]\n'
    }
  );
});

// check URL
{
  const a = new URL('http://foo');
  const b = new URL('http://bar');

  assertNotDeepOrStrict(a, b);
}

{
  const a = new URL('http://foo');
  const b = new URL('http://foo');

  assertDeepAndStrictEqual(a, b);
}

{
  const a = new URL('http://foo');
  const b = new URL('http://foo');
  a.bar = 1;
  b.bar = 2;
  assertNotDeepOrStrict(a, b);
}

{
  const a = new URL('http://foo');
  const b = new URL('http://foo');
  a.bar = 1;
  b.bar = 1;
  assertDeepAndStrictEqual(a, b);
}

{
  const a = new URL('http://foo');
  const b = new URL('http://bar');
  assert.throws(
    () => assert.deepStrictEqual(a, b),
    {
      code: 'ERR_ASSERTION',
      name: 'AssertionError',
      message: /http:\/\/bar/
    }
  );
}
