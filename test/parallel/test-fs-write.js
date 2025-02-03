// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

// Flags: --expose_externalize_string
'use strict';
const common = require('../common');
const assert = require('assert');
const fs = require('fs');
const tmpdir = require('../common/tmpdir');

tmpdir.refresh();

const fn = tmpdir.resolve('write.txt');
const fn2 = tmpdir.resolve('write2.txt');
const fn3 = tmpdir.resolve('write3.txt');
const fn4 = tmpdir.resolve('write4.txt');
const expected = 'ümlaut.';
const constants = fs.constants;

const {
  createExternalizableString,
  externalizeString,
  isOneByteString,
} = globalThis;

assert.notStrictEqual(createExternalizableString, undefined);
assert.notStrictEqual(externalizeString, undefined);
assert.notStrictEqual(isOneByteString, undefined);

// Account for extra globals exposed by --expose_externalize_string.
common.allowGlobals(
  createExternalizableString,
  externalizeString,
  isOneByteString,
  globalThis.x,
);

{
  // Must be a unique string.
  const expected = createExternalizableString('ümlaut sechzig');
  externalizeString(expected);
  assert.strictEqual(isOneByteString(expected), true);
  const fd = fs.openSync(fn, 'w');
  fs.writeSync(fd, expected, 0, 'latin1');
  fs.closeSync(fd);
  assert.strictEqual(fs.readFileSync(fn, 'latin1'), expected);
}

{
  // Must be a unique string.
  const expected = createExternalizableString('ümlaut neunzig');
  externalizeString(expected);
  assert.strictEqual(isOneByteString(expected), true);
  const fd = fs.openSync(fn, 'w');
  fs.writeSync(fd, expected, 0, 'utf8');
  fs.closeSync(fd);
  assert.strictEqual(fs.readFileSync(fn, 'utf8'), expected);
}

{
  // Must be a unique string.
  const expected = createExternalizableString('Zhōngwén 1');
  externalizeString(expected);
  assert.strictEqual(isOneByteString(expected), false);
  const fd = fs.openSync(fn, 'w');
  fs.writeSync(fd, expected, 0, 'ucs2');
  fs.closeSync(fd);
  assert.strictEqual(fs.readFileSync(fn, 'ucs2'), expected);
}

{
  // Must be a unique string.
  const expected = createExternalizableString('Zhōngwén 2');
  externalizeString(expected);
  assert.strictEqual(isOneByteString(expected), false);
  const fd = fs.openSync(fn, 'w');
  fs.writeSync(fd, expected, 0, 'utf8');
  fs.closeSync(fd);
  assert.strictEqual(fs.readFileSync(fn, 'utf8'), expected);
}

fs.open(fn, 'w', 0o644, common.mustSucceed((fd) => {
  const done = common.mustSucceed((written) => {
    assert.strictEqual(written, Buffer.byteLength(expected));
    fs.closeSync(fd);
    const found = fs.readFileSync(fn, 'utf8');
    fs.unlinkSync(fn);
    assert.strictEqual(found, expected);
  });

  const written = common.mustSucceed((written) => {
    assert.strictEqual(written, 0);
    fs.write(fd, expected, 0, 'utf8', done);
  });

  fs.write(fd, '', 0, 'utf8', written);
}));

const args = constants.O_CREAT | constants.O_WRONLY | constants.O_TRUNC;
fs.open(fn2, args, 0o644, common.mustSucceed((fd) => {
  const done = common.mustSucceed((written) => {
    assert.strictEqual(written, Buffer.byteLength(expected));
    fs.closeSync(fd);
    const found = fs.readFileSync(fn2, 'utf8');
    fs.unlinkSync(fn2);
    assert.strictEqual(found, expected);
  });

  const written = common.mustSucceed((written) => {
    assert.strictEqual(written, 0);
    fs.write(fd, expected, 0, 'utf8', done);
  });

  fs.write(fd, '', 0, 'utf8', written);
}));

fs.open(fn3, 'w', 0o644, common.mustSucceed((fd) => {
  const done = common.mustSucceed((written) => {
    assert.strictEqual(written, Buffer.byteLength(expected));
    fs.closeSync(fd);
  });

  fs.write(fd, expected, done);
}));


[false, 'test', {}, [], null, undefined].forEach((i) => {
  assert.throws(
    () => fs.write(i, common.mustNotCall()),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    }
  );
  assert.throws(
    () => fs.writeSync(i),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      name: 'TypeError'
    }
  );
});

[
  false, 5, {}, [], null, undefined, true, 5n, () => {}, Symbol(), new Map(),
  new String('notPrimitive'),
  { [Symbol.toPrimitive]: (hint) => 'amObject' },
  { toString() { return 'amObject'; } },
  Promise.resolve('amPromise'),
  common.mustNotCall(),
].forEach((data) => {
  assert.throws(
    () => fs.write(1, data, common.mustNotCall()),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /"buffer"/
    }
  );
  assert.throws(
    () => fs.writeSync(1, data),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /"buffer"/
    }
  );
});

{
  // Regression test for https://github.com/nodejs/node/issues/38168
  const fd = fs.openSync(fn4, 'w');

  assert.throws(
    () => fs.writeSync(fd, 'abc', 0, 'hex'),
    {
      code: 'ERR_INVALID_ARG_VALUE',
      message: /'encoding' is invalid for data of length 3/
    }
  );

  assert.throws(
    () => fs.writeSync(fd, 'abc', 0, 'hex', common.mustNotCall()),
    {
      code: 'ERR_INVALID_ARG_VALUE',
      message: /'encoding' is invalid for data of length 3/
    }
  );

  assert.strictEqual(fs.writeSync(fd, 'abcd', 0, 'hex'), 2);

  fs.write(fd, 'abcd', 0, 'hex', common.mustSucceed((written) => {
    assert.strictEqual(written, 2);
    fs.closeSync(fd);
  }));
}
