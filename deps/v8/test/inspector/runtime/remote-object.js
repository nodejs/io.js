// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const {session, contextGroup, Protocol} =
    InspectorTest.start('Tests Runtime.RemoteObject.');

function evaluate(options) {
  InspectorTest.log(`'${options.expression}', ` +
    `returnByValue: ${options.returnByValue || false}, ` +
    `generatePreview: ${options.generatePreview || false}`);
  return Protocol.Runtime.evaluate(options);
}

InspectorTest.runAsyncTestSuite([
  async function testNull() {
    InspectorTest.logMessage((await evaluate({
      expression: 'null'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'null',
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'null',
      generatePreview: true
    })).result);
  },
  async function testBoolean() {
    InspectorTest.logMessage((await evaluate({
      expression: 'true'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'false'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'true',
      returnByValue: true,
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'true',
      generatePreview: true,
    })).result);
  },
  async function testNumber() {
    InspectorTest.logMessage((await evaluate({
      expression: '0 / {}'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '-0'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '0'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '1/0'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '-1/0'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '2.3456'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '2.3456',
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '1/0',
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '({a: 1/0})',
      returnByValue: true
    })).result);
  },
  async function testUndefined() {
    InspectorTest.logMessage((await evaluate({
      expression: 'undefined'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'undefined',
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '({a : undefined})',
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '([1, undefined])',
      returnByValue: true
    })).result);
  },
  async function testString() {
    InspectorTest.logMessage((await evaluate({
      expression: '\'Hello!\''
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '\'Hello!\'',
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '\'Hello!\'',
      generatePreview: true
    })).result);
  },
  async function testSymbol() {
    InspectorTest.logMessage((await evaluate({
      expression: 'Symbol()',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'Symbol(42)',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `Symbol('abc')`,
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `Symbol('abc')`,
      returnByValue: true
    })));
  },
  async function testReturnByValue() {
    InspectorTest.log('Empty object');
    InspectorTest.logMessage((await evaluate({
      expression: '({})', returnByValue: true
    })).result);
    InspectorTest.log('Object with properties');
    InspectorTest.logMessage((await evaluate({
      expression: '({a:1, b:2})', returnByValue: true
    })).result);
    InspectorTest.log('Object with cycle');
    InspectorTest.logMessage((await evaluate({
      expression: 'a = {};a.a = a; a', returnByValue: true
    })).error);
    InspectorTest.log('Function () => 42');
    InspectorTest.logMessage((await evaluate({
      expression: '() => 42', returnByValue: true
    })).result);
    InspectorTest.log('Symbol(42)');
    InspectorTest.logMessage((await evaluate({
      expression: 'Symbol(42)', returnByValue: true
    })).error);
    InspectorTest.log('Error object');
    InspectorTest.logMessage((await evaluate({
      expression: 'new Error()', returnByValue: true
    })).result);
  },
  async function testFunction() {
    InspectorTest.logMessage((await evaluate({
      expression: '(() => 42)'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '(function() { return 42 })'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '(function name() { return 42 })'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '(async function asyncName() { return 42 })'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '(async () => 42)'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '(function (a) { return a; }).bind(null, 42)'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'a = (function() { return 42 }); a.b = 2; a',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '(function() { return 42 })',
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'a = (function() { return 42 }); a.b = 2; a',
      returnByValue: true
    })).result);
  },
  async function testBigInt() {
    InspectorTest.logMessage((await evaluate({
      expression: '1n'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '-5n'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '1234567890123456789012345678901234567890n'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '-5n',
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '-5n',
      generatePreview: true
    })).result);
    let result = (await evaluate({
      expression: '1n << 9_999_999n'
    })).result;
    if (result.result.unserializableValue === '0x8' + '0'.repeat(2_499_999) + 'n')
      result.result.unserializableValue = '<expected unserializableValue>';
    InspectorTest.logMessage(result);
    result = (await evaluate({
      expression: '-1n << 9_999_999n'
    })).result;
    InspectorTest.logMessage(result.result.description.length);
    if (result.result.unserializableValue === '-0x8' + '0'.repeat(2_499_998) + 'n')
      result.result.unserializableValue = '<expected unserializableValue>';
    InspectorTest.logMessage(result);
  },
  async function testRegExp() {
    InspectorTest.logMessage((await evaluate({
      expression: '/\w+/d'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '/\w+/g'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '/\w+/i'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '/\w+/m'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '/\w+/s'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '/\w+/u'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '/\w+/y'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '/\w+/v'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '/\w+/dgimsuy'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '/\w+/dgimsvy'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `new RegExp('\\w+', 'g')`,
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `new RegExp('foo/bar')`
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `var re = /./dgimsuy;
        re.toString = () => 'foo';
        re`
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `var re = /./dgimsvy;
        re.toString = () => 'foo';
        re`
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `var re = new RegExp('\\w+', 'g');
        re.prop = 32;
        re`,
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `var re = new RegExp('\\w+', 'g');
        re.prop = 32;
        re`,
      returnByValue: true
    })).result);
  },
  async function testDate() {
    let result = (await evaluate({
      expression: `new Date('May 18, 1991 03:24:00')`,
      generatePreview: true
    })).result;
    if (result.result.description === new Date('May 18, 1991 03:24:00') + '')
      result.result.description = '<expected description>';
    if (result.result.preview.description === new Date('May 18, 1991 03:24:00') + '')
      result.result.preview.description = '<expected description>';
    InspectorTest.logMessage(result);

    result = (await evaluate({
      expression: `new Date(2018, 9, 31)`,
      generatePreview: true
    })).result;
    if (result.result.description === new Date(2018, 9, 31) + '')
      result.result.description = '<expected description>';
    if (result.result.preview.description === new Date(2018, 9, 31) + '')
      result.result.preview.description = '<expected description>';
    InspectorTest.logMessage(result);

    result = (await evaluate({
      expression: `a = new Date(2018, 9, 31); a.b = 2; a`,
      generatePreview: true
    })).result;
    if (result.result.description === new Date(2018, 9, 31) + '')
      result.result.description = '<expected description>';
    if (result.result.preview.description === new Date(2018, 9, 31) + '')
      result.result.preview.description = '<expected description>';
    InspectorTest.logMessage(result);

    result = (await evaluate({
               expression:
                   `a = new Date(2018, 9, 31); a.toString = date => 'bar'; a`,
               generatePreview: true
             })).result;
    if (result.result.description === new Date(2018, 9, 31) + '')
      result.result.description = '<expected description>';
    if (result.result.preview.description === new Date(2018, 9, 31) + '')
      result.result.preview.description = '<expected description>';
    InspectorTest.logMessage(result);

    result =
        (await evaluate({
          expression:
              `a = new Date(2018, 9, 31); a[Symbol.toPrimitive] = date => 'bar'; a`,
          generatePreview: true
        })).result;
    if (result.result.description === new Date(2018, 9, 31) + '')
      result.result.description = '<expected description>';
    if (result.result.preview.description === new Date(2018, 9, 31) + '')
      result.result.preview.description = '<expected description>';
    InspectorTest.logMessage(result);
  },
  async function testMap() {
    InspectorTest.logMessage((await evaluate({
      expression: 'new Map()',
      generatePreview: true,
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new Map([[1,2]])',
      generatePreview: true,
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'a = new Map(); a.set(a, a); a',
      generatePreview: true,
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `new Map([['a','b']])`
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `({ a: new Map([['a','b']]) })`,
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `m = new Map([['a', {b: 2}]])
        m.d = 42;
        m`,
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `m = new Map([['a', {b: 2}]])
        m.d = 42;
        m`,
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `new Map([['a', {b: 2}]]).values()`
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `new Map([['a', {b: 2}]]).values()`,
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `it = new Map([['a', {b: 2}]]).values(); it.next(); it`,
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `new Map([['a', {b: 2}]]).values()`,
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `new Map([['a', {b: 2}]]).entries()`
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `new Map([['a', {b: 2}]]).entries()`,
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `it = new Map([['a', {b: 2}]]).entries(); it.next(); it`,
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `new Map([['a', {b: 2}]]).entries()`,
      returnByValue: true
    })).result);
  },
  async function testSet() {
    InspectorTest.logMessage((await evaluate({
      expression: 'new Set([1])',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new Set([1])',
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new Set([1,2,3,4,5,6,7])',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new Set([1,2,3]).values()',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'it = new Set([1,2,3]).values(); it.next(); it',
      generatePreview: true
    })).result);
  },
  async function testWeakMap() {
    InspectorTest.logMessage((await evaluate({
      expression: 'new WeakMap()',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new WeakMap([[this, 1]])',
      generatePreview: true
    })).result);
  },
  async function testWeakSet() {
    InspectorTest.logMessage((await evaluate({
      expression: 'new WeakSet()',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new WeakSet([this])',
      generatePreview: true
    })).result);
  },
  async function testGenerator() {
    InspectorTest.logMessage((await evaluate({
      expression: 'g = (function*(){ yield 42; })(); g.a = 2; g',
      generatePreview: true
    })).result);
  },
  async function testError() {
    InspectorTest.logMessage((await evaluate({
      expression: 'new Error()'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `new Error('abc')`
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `new Error('at\\nat')`
    })).result);

    InspectorTest.logMessage((await evaluate({
      expression: `new Error('preview')`,
      returnByValue: true
    })).result);

    InspectorTest.logMessage((await evaluate({
      expression: `new Error('preview')`,
      generatePreview: true
    })).result);

    InspectorTest.logMessage((await evaluate({
      expression: `({a: new Error('preview')})`,
      generatePreview: true
    })).result);

    InspectorTest.logMessage((await evaluate({
      expression: `a = new Error('preview and a'); a.a = 123; a`,
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `a = new Error('preview and a'); a.a = 123; a`,
      returnByValue: true
    })).result);
  },
  async function testCustomError() {
    InspectorTest.logMessage((await evaluate({
      expression: `class CustomError extends Error {}; a = new CustomError(); delete a.stack; a`
    })).result);
  },
  async function testCustomErrorWithMessage() {
    InspectorTest.logMessage((await evaluate( {
      expression: `class CustomMsgError extends Error {}; a = new CustomMsgError(); delete a.stack; a.message = 'foobar'; a`
    })).result);
  },
  async function testProxy() {
    InspectorTest.logMessage((await evaluate({
      expression: 'new Proxy({}, {})'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new Proxy(new Error(), {})'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new Proxy({c: 3}, {d: 4})',
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new Proxy({a: 1}, {b: 2})',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '({e: new Proxy({a: 1}, {b: 2})})',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new Proxy([1, 2], {})',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'revocable = Proxy.revocable({}, {}); revocable.revoke(); revocable.proxy',
      generatePreview: true
    })).result);
  },
  async function testPromise() {
    InspectorTest.logMessage((await evaluate({
      expression: 'Promise.resolve(42)'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'Promise.reject(42)'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '(async function(){})()'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: `Promise.resolve('a'.repeat(101))`,
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'Promise.reject(42)',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new Promise(resolve => this.resolve = resolve)',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'a = Promise.resolve(42); a.b = 2; a',
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '({a: Promise.resolve(42)})',
      generatePreview: true
    })).result);
  },
  async function testTypedArray() {
    InspectorTest.logMessage((await evaluate({
      expression: 'a = new Uint8Array(2); a.b = 2; a',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new Int32Array(101)',
      generatePreview: true
    })).result);
  },
  async function testArrayBuffer() {
    InspectorTest.logMessage((await evaluate({
      expression: 'new Uint8Array().buffer',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new Int32Array(100).buffer',
      generatePreview: true
    })).result);
  },
  async function testDataView() {
    InspectorTest.logMessage((await evaluate({
      expression: 'new DataView(new ArrayBuffer(16))',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new DataView(new ArrayBuffer(16), 12, 4)',
      generatePreview: true
    })).result);
  },
  async function testArray() {
    InspectorTest.logMessage((await evaluate({
      expression: '[]'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '[1,2,3]'
    })).result);
  },
  async function testArgumentsObject() {
    InspectorTest.logMessage((await evaluate({
      expression: '(function() { return arguments; })(1, 2, 3)'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '(function() { "use strict"; return arguments; })(-1, 0, 1, 2, 3)'
    })).result);
  },
  async function testOtherObjects() {
    InspectorTest.logMessage((await evaluate({
      expression: '({a: 1, b:2})'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '({a: 1, b:2})',
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: '({a: 1, b:2})',
      generatePreview: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new (function Foo() { this.a = 5; })'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new (function Foo() { this.a = [1,2,3]; })',
      returnByValue: true
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'new (class Bar {})'
    })).result);
    InspectorTest.logMessage((await evaluate({
      expression: 'inspector.createObjectWithNativeDataProperty(\'title\', true)',
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: 'inspector.createObjectWithNativeDataProperty(\'title\', false)',
      generatePreview: true
    })));
    // TODO(kozyatinskiy): fix this one.
    InspectorTest.logMessage((await evaluate({
      expression: 'inspector.createObjectWithNativeDataProperty(\'title\', true)',
      returnByValue: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: '({get a() { return 42; }})',
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: '({set a(v) {}})',
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: '({a: () => 42})',
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: '({a: null})',
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: '({a: true})',
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: '({a1: -Infinity, a2: +Infinity, a3: -0, a4: NaN, a5: 1.23})',
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: '({a1: 1234567890123456789012345678901234567890n})',
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: '({a1: Symbol(42)})',
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: '({a1: /abc/i})',
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: '({a1: () => 42, a2: async () => 42})',
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: '({a1: ({}), a2: new (class Bar{})})',
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: `({a1: 'a'.repeat(100), a2: 'a'.repeat(101)})`,
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: `({a1: 1, a2: 2, a3: 3, a4:4, a5:5, a6: 6})`,
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: `([1,2,3])`,
      generatePreview: true
    })));
  },

  async function testArray2() {
    InspectorTest.logMessage((await evaluate({
      expression: `([1,2,3])`
    })));
    InspectorTest.logMessage((await evaluate({
      expression: `([1,2,3])`,
      returnByValue: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: `([1,2,3])`,
      generatePreview: true
    })));
    InspectorTest.logMessage((await evaluate({
      expression: `({a: [1,2,3]})`,
      generatePreview: true
    })));
  }
]);
