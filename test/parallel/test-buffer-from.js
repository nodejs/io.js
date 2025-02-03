'use strict';

const common = require('../common');
const { deepStrictEqual, strictEqual, throws } = require('assert');
const { runInNewContext } = require('vm');

const checkString = 'test';

const check = Buffer.from(checkString);

class MyString extends String {
  constructor() {
    super(checkString);
  }
}

class MyPrimitive {
  [Symbol.toPrimitive]() {
    return checkString;
  }
}

class MyBadPrimitive {
  [Symbol.toPrimitive]() {
    return 1;
  }
}

deepStrictEqual(Buffer.from(new String(checkString)), check);
deepStrictEqual(Buffer.from(new MyString()), check);
deepStrictEqual(Buffer.from(new MyPrimitive()), check);
deepStrictEqual(
  Buffer.from(runInNewContext('new String(checkString)', { checkString })),
  check
);

[
  {},
  new Boolean(true),
  { valueOf() { return null; } },
  { valueOf() { return undefined; } },
  { valueOf: null },
  { __proto__: null },
  new Number(true),
  new MyBadPrimitive(),
  Symbol(),
  5n,
  (one, two, three) => {},
  undefined,
  null,
].forEach((input) => {
  const errObj = {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
    message: 'The first argument must be of type string or an instance of ' +
             'Buffer, ArrayBuffer, or Array or an Array-like Object.' +
             common.invalidArgTypeHelper(input)
  };
  throws(() => Buffer.from(input), errObj);
  throws(() => Buffer.from(input, 'hex'), errObj);
});

Buffer.allocUnsafe(10); // Should not throw.
Buffer.from('deadbeaf', 'hex'); // Should not throw.


{
  const u16 = new Uint16Array([0xffff]);
  const b16 = Buffer.copyBytesFrom(u16);
  u16[0] = 0;
  strictEqual(b16.length, 2);
  strictEqual(b16[0], 255);
  strictEqual(b16[1], 255);
}

{
  const u16 = new Uint16Array([0, 0xffff]);
  const b16 = Buffer.copyBytesFrom(u16, 1, 5);
  u16[0] = 0xffff;
  u16[1] = 0;
  strictEqual(b16.length, 2);
  strictEqual(b16[0], 255);
  strictEqual(b16[1], 255);
}

{
  const u32 = new Uint32Array([0xffffffff]);
  const b32 = Buffer.copyBytesFrom(u32);
  u32[0] = 0;
  strictEqual(b32.length, 4);
  strictEqual(b32[0], 255);
  strictEqual(b32[1], 255);
  strictEqual(b32[2], 255);
  strictEqual(b32[3], 255);
}

throws(() => {
  Buffer.copyBytesFrom();
}, {
  code: 'ERR_INVALID_ARG_TYPE',
});

['', Symbol(), true, false, {}, [], () => {}, 1, 1n, null, undefined].forEach(
  (notTypedArray) => throws(() => {
    Buffer.copyBytesFrom('nope');
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
  })
);

['', Symbol(), true, false, {}, [], () => {}, 1n].forEach((notANumber) =>
  throws(() => {
    Buffer.copyBytesFrom(new Uint8Array(1), notANumber);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
  })
);

[-1, NaN, 1.1, -Infinity].forEach((outOfRange) =>
  throws(() => {
    Buffer.copyBytesFrom(new Uint8Array(1), outOfRange);
  }, {
    code: 'ERR_OUT_OF_RANGE',
  })
);

['', Symbol(), true, false, {}, [], () => {}, 1n].forEach((notANumber) =>
  throws(() => {
    Buffer.copyBytesFrom(new Uint8Array(1), 0, notANumber);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
  })
);

[-1, NaN, 1.1, -Infinity].forEach((outOfRange) =>
  throws(() => {
    Buffer.copyBytesFrom(new Uint8Array(1), 0, outOfRange);
  }, {
    code: 'ERR_OUT_OF_RANGE',
  })
);

// Invalid encoding is allowed
Buffer.from('asd', 1);
