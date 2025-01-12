'use strict';
require('../common');
const assert = require('assert');
const { isStringOneByteRepresentation } = require('v8');

[
  undefined,
  null,
  false,
  5n,
  5,
  Symbol(),
  () => {},
  {},
].forEach((value) => {
  assert.throws(
    () => { isStringOneByteRepresentation(value); },
    /The "content" argument must be of type string/
  );
});

{
  const latin1String = 'hello world!';
  // Run this inside a for loop to trigger the fast API
  for (let i = 0; i < 10_000; i++) {
    assert.strictEqual(isStringOneByteRepresentation(latin1String), true);
  }
}

{
  const utf16String = '你好😀😃';
  // Run this inside a for loop to trigger the fast API
  for (let i = 0; i < 10_000; i++) {
    assert.strictEqual(isStringOneByteRepresentation(utf16String), false);
  }
}
