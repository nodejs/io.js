'use strict';

const common = require('../common');
const vm = require('node:vm');
const assert = require('node:assert');
const { describe, it } = require('node:test');

function createCircularObject() {
  const obj = {};
  obj.self = obj;
  return obj;
}

function createDeepNestedObject() {
  return { level1: { level2: { level3: 'deepValue' } } };
}

async function generateCryptoKey() {
  const { KeyObject } = require('node:crypto');
  const { subtle } = globalThis.crypto;

  const cryptoKey = await subtle.generateKey(
    {
      name: 'HMAC',
      hash: 'SHA-256',
      length: 256,
    },
    true,
    ['sign', 'verify']
  );

  const keyObject = KeyObject.from(cryptoKey);

  return { cryptoKey, keyObject };
}

describe('Object Comparison Tests', () => {
  describe('partialDeepStrictEqual', () => {
    describe('throws an error', () => {
      const tests = [
        {
          description: 'throws when only actual is provided',
          actual: { a: 1 },
          expected: undefined,
        },
        {
          description: 'throws when only expected is provided',
          actual: undefined,
          expected: { a: 1 },
        },
        {
          description: 'throws when expected has more properties than actual',
          actual: [1, 'two'],
          expected: [1, 'two', true],
        },
        {
          description: 'throws because expected has seven 2 while actual has six one',
          actual: [1, 2, 2, 2, 2, 2, 2, 3],
          expected: [1, 2, 2, 2, 2, 2, 2, 2],
        },
        {
          description: 'throws when comparing two different sets with objects',
          actual: new Set([{ a: 1 }]),
          expected: new Set([{ a: 1 }, { b: 1 }]),
        },

        {
          description: 'throws when comparing two WeakSet objects',
          actual: new WeakSet(),
          expected: new WeakSet(),
        },
        {
          description: 'throws when comparing two WeakMap objects',
          actual: new WeakMap(),
          expected: new WeakMap(),
        },
        {
          description: 'throws when comparing two different objects',
          actual: { a: 1, b: 'string' },
          expected: { a: 2, b: 'string' },
        },
        {
          description:
            'throws when comparing two objects with different nested objects',
          actual: createDeepNestedObject(),
          expected: { level1: { level2: { level3: 'differentValue' } } },
        },
        {
          description:
            'throws when comparing two objects with different RegExp properties',
          actual: { pattern: /abc/ },
          expected: { pattern: /def/ },
        },
        {
          description:
            'throws when comparing two arrays with different elements',
          actual: [1, 'two', true],
          expected: [1, 'two', false],
        },
        {
          description: 'throws when comparing [0] with [-0]',
          actual: [0],
          expected: [-0],
        },
        {
          description: 'throws when comparing [0, 0, 0] with [0, -0]',
          actual: [0, 0, 0],
          expected: [0, -0],
        },
        {
          description: 'throws when comparing ["-0"] with [-0]',
          actual: ['-0'],
          expected: [-0],
        },
        {
          description: 'throws when comparing [-0] with [0]',
          actual: [-0],
          expected: [0],
        },
        {
          description: 'throws when comparing [-0] with ["-0"]',
          actual: [-0],
          expected: ['-0'],
        },
        {
          description: 'throws when comparing ["0"] with [0]',
          actual: ['0'],
          expected: [0],
        },
        {
          description: 'throws when comparing [0] with ["0"]',
          actual: [0],
          expected: ['0'],
        },
        {
          description:
            'throws when comparing two Date objects with different times',
          actual: new Date(0),
          expected: new Date(1),
        },
        {
          description:
            'throws when comparing two objects with different large number of properties',
          actual: Object.fromEntries(
            Array.from({ length: 100 }, (_, i) => [`key${i}`, i])
          ),
          expected: Object.fromEntries(
            Array.from({ length: 100 }, (_, i) => [`key${i}`, i + 1])
          ),
        },
        {
          description:
            'throws when comparing two objects with different Symbols',
          actual: { [Symbol('test')]: 'symbol' },
          expected: { [Symbol('test')]: 'symbol' },
        },
        {
          description:
            'throws when comparing two objects with different array properties',
          actual: { a: [1, 2, 3] },
          expected: { a: [1, 2, 4] },
        },
        {
          description:
            'throws when comparing two objects with different function properties',
          actual: { fn: () => {} },
          expected: { fn: () => {} },
        },
        {
          description:
            'throws when comparing two objects with different Error instances',
          actual: { error: new Error('Test error 1') },
          expected: { error: new Error('Test error 2') },
        },
        {
          description:
            'throws when comparing two objects with different TypedArray instances and content',
          actual: { typedArray: new Uint8Array([1, 2, 3]) },
          expected: { typedArray: new Uint8Array([4, 5, 6]) },
        },
        {
          description:
            'throws when comparing two Map objects with different entries',
          actual: new Map([
            ['key1', 'value1'],
            ['key2', 'value2'],
          ]),
          expected: new Map([
            ['key1', 'value1'],
            ['key3', 'value3'],
          ]),
        },
        {
          description:
            'throws when comparing two Map objects with different keys',
          actual: new Map([
            ['key1', 'value1'],
            ['key2', 'value2'],
          ]),
          expected: new Map([
            ['key1', 'value1'],
            ['key3', 'value2'],
          ]),
        },
        {
          description:
            'throws when the expected Map has more entries than the actual Map',
          actual: new Map([
            ['key1', 'value1'],
            ['key2', 'value2'],
          ]),
          expected: new Map([
            ['key1', 'value1'],
            ['key2', 'value2'],
            ['key3', 'value3'],
          ]),
        },
        {
          description: 'throws when the nested array in the Map is not a subset of the other nested array',
          actual: new Map([
            ['key1', ['value1', 'value2']],
            ['key2', 'value2'],
          ]),
          expected: new Map([
            ['key1', ['value3']],
          ]),
        },
        {
          description:
            'throws when comparing two TypedArray instances with different content',
          actual: new Uint8Array(10),
          expected: () => {
            const typedArray2 = new Int8Array(10);
            Object.defineProperty(typedArray2, Symbol.toStringTag, {
              value: 'Uint8Array'
            });
            Object.setPrototypeOf(typedArray2, Uint8Array.prototype);

            return typedArray2;
          },
        },
        {
          description:
            'throws when comparing two Set objects from different realms with different values',
          actual: new vm.runInNewContext('new Set(["value1", "value2"])'),
          expected: new Set(['value1', 'value3']),
        },
        {
          description:
            'throws when comparing two Set objects with different values',
          actual: new Set(['value1', 'value2']),
          expected: new Set(['value1', 'value3']),
        },
        {
          description: 'throws when comparing one subset object with another',
          actual: { a: 1, b: 2, c: 3 },
          expected: { b: '2' },
        },
        {
          description: 'throws when comparing one subset array with another',
          actual: [1, 2, 3],
          expected: ['2'],
        },
        {
          description: 'throws when comparing an ArrayBuffer with a Uint8Array',
          actual: new ArrayBuffer(3),
          expected: new Uint8Array(3),
        },
        {
          description: 'throws when comparing a ArrayBuffer with a SharedArrayBuffer',
          actual: new ArrayBuffer(3),
          expected: new SharedArrayBuffer(3),
        },
        {
          description: 'throws when comparing a SharedArrayBuffer with an ArrayBuffer',
          actual: new SharedArrayBuffer(3),
          expected: new ArrayBuffer(3),
        },
        {
          description: 'throws when comparing an Int16Array with a Uint16Array',
          actual: new Int16Array(3),
          expected: new Uint16Array(3),
        },
        {
          description: 'throws when comparing two dataviews with different buffers',
          actual: { dataView: new DataView(new ArrayBuffer(3)) },
          expected: { dataView: new DataView(new ArrayBuffer(4)) },
        },
        {
          description: 'throws because expected Uint8Array(SharedArrayBuffer) is not a subset of actual',
          actual: { typedArray: new Uint8Array(new SharedArrayBuffer(3)) },
          expected: { typedArray: new Uint8Array(new SharedArrayBuffer(5)) },
        },
        {
          description: 'throws because expected SharedArrayBuffer is not a subset of actual',
          actual: { typedArray: new SharedArrayBuffer(3) },
          expected: { typedArray: new SharedArrayBuffer(5) },
        },
        {
          description: 'throws when comparing a DataView with a TypedArray',
          actual: { dataView: new DataView(new ArrayBuffer(3)) },
          expected: { dataView: new Uint8Array(3) },
        },
        {
          description: 'throws when comparing a TypedArray with a DataView',
          actual: { dataView: new Uint8Array(3) },
          expected: { dataView: new DataView(new ArrayBuffer(3)) },
        },
        {
          description: 'throws when comparing Float32Array([+0.0]) with Float32Array([-0.0])',
          actual: new Float32Array([+0.0]),
          expected: new Float32Array([-0.0]),
        },
        {
          description: 'throws when comparing two different urls',
          actual: new URL('http://foo'),
          expected: new URL('http://bar'),
        },
        {
          description: 'throws when comparing SharedArrayBuffers when expected has different elements actual',
          actual: (() => {
            const sharedBuffer = new SharedArrayBuffer(4 * Int32Array.BYTES_PER_ELEMENT);
            const sharedArray = new Int32Array(sharedBuffer);

            sharedArray[0] = 1;
            sharedArray[1] = 2;
            sharedArray[2] = 3;

            return sharedBuffer;
          })(),
          expected: (() => {
            const sharedBuffer = new SharedArrayBuffer(4 * Int32Array.BYTES_PER_ELEMENT);
            const sharedArray = new Int32Array(sharedBuffer);

            sharedArray[0] = 1;
            sharedArray[1] = 2;
            sharedArray[2] = 6;

            return sharedBuffer;
          })(),
        },
      ];

      if (common.hasCrypto) {
        tests.push({
          description:
            'throws when comparing two objects with different CryptoKey instances objects',
          actual: async () => {
            return generateCryptoKey();
          },
          expected: async () => {
            return generateCryptoKey();
          },
        });

        const { createSecretKey } = require('node:crypto');

        tests.push({
          description:
            'throws when comparing two objects with different KeyObject instances objects',
          actual: createSecretKey(Buffer.alloc(1, 0)),
          expected: createSecretKey(Buffer.alloc(1, 1)),
        });
      }

      tests.forEach(({ description, actual, expected }) => {
        it(description, () => {
          assert.throws(() => assert.partialDeepStrictEqual(actual, expected), Error);
        });
      });
    });
  });

  describe('does not throw an error', () => {
    const sym = Symbol('test');
    const func = () => {};

    [
      {
        description: 'compares two identical simple objects',
        actual: { a: 1, b: 'string' },
        expected: { a: 1, b: 'string' },
      },
      {
        description: 'compares two objects with different property order',
        actual: { a: 1, b: 'string' },
        expected: { b: 'string', a: 1 },
      },
      {
        description: 'compares two deeply nested objects with partial equality',
        actual: { a: { nested: { property: true, some: 'other' } } },
        expected: { a: { nested: { property: true } } },
      },
      {
        description:
          'compares plain objects from different realms',
        actual: vm.runInNewContext(`({
          a: 1,
          b: 2n,
          c: "3",
          d: /4/,
          e: new Set([5]),
          f: [6],
          g: new Uint8Array()
        })`),
        expected: { b: 2n, e: new Set([5]), f: [6], g: new Uint8Array() },
      },
      {
        description: 'compares two integers',
        actual: 1,
        expected: 1,
      },
      {
        description: 'compares two strings',
        actual: '1',
        expected: '1',
      },
      {
        description: 'compares two objects with nested objects',
        actual: createDeepNestedObject(),
        expected: createDeepNestedObject(),
      },
      {
        description: 'compares two objects with circular references',
        actual: createCircularObject(),
        expected: createCircularObject(),
      },
      {
        description: 'compares two arrays with identical elements',
        actual: [1, 'two', true],
        expected: [1, 'two', true],
      },
      {
        description: 'compares [0] with [0]',
        actual: [0],
        expected: [0],
      },
      {
        description: 'compares [-0] with [-0]',
        actual: [-0],
        expected: [-0],
      },
      {
        description: 'compares [0, -0, 0] with [0, 0]',
        actual: [0, -0, 0],
        expected: [0, 0],
      },
      {
        description: 'compares two Date objects with the same time',
        actual: new Date(0),
        expected: new Date(0),
      },
      {
        description: 'compares two objects with large number of properties',
        actual: Object.fromEntries(
          Array.from({ length: 100 }, (_, i) => [`key${i}`, i])
        ),
        expected: Object.fromEntries(
          Array.from({ length: 100 }, (_, i) => [`key${i}`, i])
        ),
      },
      {
        description: 'compares two objects with Symbol properties',
        actual: { [sym]: 'symbol' },
        expected: { [sym]: 'symbol' },
      },
      {
        description: 'compares two objects with RegExp properties',
        actual: { pattern: /abc/ },
        expected: { pattern: /abc/ },
      },
      {
        description: 'compares two objects with identical function properties',
        actual: { fn: func },
        expected: { fn: func },
      },
      {
        description: 'compares two objects with mixed types of properties',
        actual: { num: 1, str: 'test', bool: true, sym },
        expected: { num: 1, str: 'test', bool: true, sym },
      },
      {
        description: 'compares two objects with Buffers',
        actual: { buf: Buffer.from('Node.js') },
        expected: { buf: Buffer.from('Node.js') },
      },
      {
        description: 'compares two objects with identical Error properties',
        actual: { error: new Error('Test error') },
        expected: { error: new Error('Test error') },
      },
      {
        description: 'compares two Uint8Array objects',
        actual: { typedArray: new Uint8Array([1, 2, 3, 4, 5]) },
        expected: { typedArray: new Uint8Array([1, 2, 3]) },
      },
      {
        description: 'compares two Int16Array objects',
        actual: { typedArray: new Int16Array([1, 2, 3, 4, 5]) },
        expected: { typedArray: new Int16Array([1, 2, 3]) },
      },
      {
        description: 'compares two DataView objects with the same buffer and different views',
        actual: { dataView: new DataView(new ArrayBuffer(8), 0, 4) },
        expected: { dataView: new DataView(new ArrayBuffer(8), 4, 4) },
      },
      {
        description: 'compares two DataView objects with different buffers',
        actual: { dataView: new DataView(new ArrayBuffer(8)) },
        expected: { dataView: new DataView(new ArrayBuffer(8)) },
      },
      {
        description: 'compares two DataView objects with the same buffer and same views',
        actual: { dataView: new DataView(new ArrayBuffer(8), 0, 8) },
        expected: { dataView: new DataView(new ArrayBuffer(8), 0, 8) },
      },
      {
        description: 'compares two SharedArrayBuffers with the same length',
        actual: new SharedArrayBuffer(3),
        expected: new SharedArrayBuffer(3),
      },
      {
        description: 'compares two Uint8Array objects from SharedArrayBuffer',
        actual: { typedArray: new Uint8Array(new SharedArrayBuffer(5)) },
        expected: { typedArray: new Uint8Array(new SharedArrayBuffer(3)) },
      },
      {
        description: 'compares two Int16Array objects from SharedArrayBuffer',
        actual: { typedArray: new Int16Array(new SharedArrayBuffer(10)) },
        expected: { typedArray: new Int16Array(new SharedArrayBuffer(6)) },
      },
      {
        description: 'compares two DataView objects with the same SharedArrayBuffer and different views',
        actual: { dataView: new DataView(new SharedArrayBuffer(8), 0, 4) },
        expected: { dataView: new DataView(new SharedArrayBuffer(8), 4, 4) },
      },
      {
        description: 'compares two DataView objects with different SharedArrayBuffers',
        actual: { dataView: new DataView(new SharedArrayBuffer(8)) },
        expected: { dataView: new DataView(new SharedArrayBuffer(8)) },
      },
      {
        description: 'compares two DataView objects with the same SharedArrayBuffer and same views',
        actual: { dataView: new DataView(new SharedArrayBuffer(8), 0, 8) },
        expected: { dataView: new DataView(new SharedArrayBuffer(8), 0, 8) },
      },
      {
        description: 'compares two SharedArrayBuffers',
        actual: { typedArray: new SharedArrayBuffer(5) },
        expected: { typedArray: new SharedArrayBuffer(3) },
      },
      {
        description: 'compares two SharedArrayBuffers with data inside',
        actual: (() => {
          const sharedBuffer = new SharedArrayBuffer(4 * Int32Array.BYTES_PER_ELEMENT);
          const sharedArray = new Int32Array(sharedBuffer);

          sharedArray[0] = 1;
          sharedArray[1] = 2;
          sharedArray[2] = 3;
          sharedArray[3] = 4;

          return sharedBuffer;
        })(),
        expected: (() => {
          const sharedBuffer = new SharedArrayBuffer(3 * Int32Array.BYTES_PER_ELEMENT);
          const sharedArray = new Int32Array(sharedBuffer);

          sharedArray[0] = 1;
          sharedArray[1] = 2;
          sharedArray[2] = 3;

          return sharedBuffer;
        })(),
      },
      {
        description: 'compares two Map objects with identical entries',
        actual: new Map([
          ['key1', 'value1'],
          ['key2', 'value2'],
        ]),
        expected: new Map([
          ['key1', 'value1'],
          ['key2', 'value2'],
        ]),
      },
      {
        description: 'compares two Map where one is a subset of the other',
        actual: new Map([
          ['key1', { nested: { property: true } }],
          ['key2', new Set([1, 2, 3])],
          ['key3', new Uint8Array([1, 2, 3])],
        ]),
        expected: new Map([
          ['key1', { nested: { property: true } }],
          ['key2', new Set([1, 2, 3])],
          ['key3', new Uint8Array([1, 2, 3])],
        ])
      },
      {
        describe: 'compares two array of objects',
        actual: [{ a: 5 }],
        expected: [{ a: 5 }],
      },
      {
        describe: 'compares two array of objects where expected is a subset of actual',
        actual: [{ a: 5 }, { b: 5 }],
        expected: [{ a: 5 }],
      },
      {
        description: 'compares two Set objects with identical objects',
        actual: new Set([{ a: 1 }]),
        expected: new Set([{ a: 1 }]),
      },
      {
        description: 'compares two Set objects where expected is a subset of actual',
        actual: new Set([{ a: 1 }, { b: 1 }]),
        expected: new Set([{ a: 1 }]),
      },
      {
        description: 'compares two Set objects with identical arrays',
        actual: new Set(['value1', 'value2']),
        expected: new Set(['value1', 'value2']),
      },
      {
        description: 'compares two Set objects',
        actual: new Set(['value1', 'value2', 'value3']),
        expected: new Set(['value1', 'value2']),
      },
      {
        description:
          'compares two Map objects from different realms with identical entries',
        actual: new vm.runInNewContext(
          'new Map([["key1", "value1"], ["key2", "value2"]])'
        ),
        expected: new Map([
          ['key1', 'value1'],
          ['key2', 'value2'],
        ]),
      },
      {
        description:
          'compares two Map objects where expected is a subset of actual',
        actual: new Map([
          ['key1', 'value1'],
          ['key2', 'value2'],
        ]),
        expected: new Map([['key1', 'value1']]),
      },
      {
        description:
          'compares two deeply nested Maps',
        actual: {
          a: {
            b: {
              c: new Map([
                ['key1', 'value1'],
                ['key2', 'value2'],
              ])
            },
            z: [1, 2, 3]
          }
        },
        expected: {
          a: {
            z: [1, 2, 3],
            b: {
              c: new Map([['key1', 'value1']])
            }
          }
        },
      },
      {
        description: 'compares Maps nested into Maps',
        actual: new Map([
          ['key1', new Map([
            ['nestedKey1', 'nestedValue1'],
            ['nestedKey2', 'nestedValue2'],
          ])],
          ['key2', 'value2'],
        ]),
        expected: new Map([
          ['key1', new Map([
            ['nestedKey1', 'nestedValue1'],
          ])],
        ])
      },
      {
        description: 'compares Maps with nested arrays inside',
        actual: new Map([
          ['key1', ['value1', 'value2']],
          ['key2', 'value2'],
        ]),
        expected: new Map([
          ['key1', ['value1', 'value2']],
        ]),
      },
      {
        description:
          'compares two objects with identical getter/setter properties',
        actual: (() => {
          let value = 'test';
          return Object.defineProperty({}, 'prop', {
            get: () => value,
            set: (newValue) => {
              value = newValue;
            },
            enumerable: true,
            configurable: true,
          });
        })(),
        expected: (() => {
          let value = 'test';
          return Object.defineProperty({}, 'prop', {
            get: () => value,
            set: (newValue) => {
              value = newValue;
            },
            enumerable: true,
            configurable: true,
          });
        })(),
      },
      {
        description: 'compares two objects with no prototype',
        actual: { __proto__: null, prop: 'value' },
        expected: { __proto__: null, prop: 'value' },
      },
      {
        description:
          'compares two objects with identical non-enumerable properties',
        actual: (() => {
          const obj = {};
          Object.defineProperty(obj, 'hidden', {
            value: 'secret',
            enumerable: false,
          });
          return obj;
        })(),
        expected: (() => {
          const obj = {};
          Object.defineProperty(obj, 'hidden', {
            value: 'secret',
            enumerable: false,
          });
          return obj;
        })(),
      },
      {
        description: 'compares two identical primitives, string',
        actual: 'foo',
        expected: 'foo',
      },
      {
        description: 'compares two identical primitives, number',
        actual: 1,
        expected: 1,
      },
      {
        description: 'compares two identical primitives, boolean',
        actual: false,
        expected: false,
      },
      {
        description: 'compares two identical primitives, null',
        actual: null,
        expected: null,
      },
      {
        description: 'compares two identical primitives, undefined',
        actual: undefined,
        expected: undefined,
      },
      {
        description: 'compares two identical primitives, Symbol',
        actual: sym,
        expected: sym,
      },
      {
        description:
          'compares one subset object with another',
        actual: { a: 1, b: 2, c: 3 },
        expected: { b: 2 },
      },
      {
        description:
          'compares one subset array with another',
        actual: [1, 2, 3],
        expected: [2],
      },
      {
        description: 'ensures that File extends Blob',
        actual: Object.getPrototypeOf(File.prototype),
        expected: Blob.prototype
      },
      {
        description: 'compares NaN with NaN',
        actual: NaN,
        expected: NaN,
      },
      {
        description: 'compares two identical urls',
        actual: new URL('http://foo'),
        expected: new URL('http://foo'),
      },
    ].forEach(({ description, actual, expected }) => {
      it(description, () => {
        assert.partialDeepStrictEqual(actual, expected);
      });
    });
  });
});
