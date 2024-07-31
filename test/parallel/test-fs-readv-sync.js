'use strict';

require('../common');
const assert = require('assert');
const fs = require('fs');
const tmpdir = require('../common/tmpdir');

tmpdir.refresh();

const expected = 'ümlaut. Лорем 運務ホソモ指及 आपको करने विकास 紙読決多密所 أضف';

const exptectedBuff = Buffer.from(expected);
const expectedLength = exptectedBuff.length;

const filename = tmpdir.resolve('readv_sync.txt');
fs.writeFileSync(filename, exptectedBuff);

const allocateEmptyBuffers = (combinedLength) => {
  const bufferArr = [];
  // Allocate two buffers, each half the size of exptectedBuff
  bufferArr[0] = Buffer.alloc(Math.floor(combinedLength / 2));
  bufferArr[1] = Buffer.alloc(combinedLength - bufferArr[0].length);

  return bufferArr;
};

// fs.readvSync with array of buffers with all parameters
{
  const fd = fs.openSync(filename, 'r');

  const bufferArr = allocateEmptyBuffers(exptectedBuff.length);

  let read = fs.readvSync(fd, [Buffer.from('')], 0);
  assert.strictEqual(read, 0);

  read = fs.readvSync(fd, bufferArr, 0);
  assert.strictEqual(read, expectedLength);

  fs.closeSync(fd);

  assert(Buffer.concat(bufferArr).equals(fs.readFileSync(filename)));
}

// fs.readvSync with array of buffers without position
{
  const fd = fs.openSync(filename, 'r');

  const bufferArr = allocateEmptyBuffers(exptectedBuff.length);

  let read = fs.readvSync(fd, [Buffer.from('')]);
  assert.strictEqual(read, 0);

  read = fs.readvSync(fd, bufferArr);
  assert.strictEqual(read, expectedLength);

  fs.closeSync(fd);

  assert(Buffer.concat(bufferArr).equals(fs.readFileSync(filename)));
}

/**
 * Testing with incorrect arguments
 */
const wrongInputs = [false, 'test', {}, [{}], ['sdf'], null, undefined];

{
  const fd = fs.openSync(filename, 'r');

  for (const wrongInput of wrongInputs) {
    assert.throws(
      () => fs.readvSync(fd, wrongInput, null), {
        code: 'ERR_INVALID_ARG_TYPE',
        name: 'TypeError'
      }
    );
  }

  fs.closeSync(fd);
}

{
  // fs.readv with wrong fd argument
  for (const wrongInput of wrongInputs) {
    assert.throws(
      () => fs.readvSync(wrongInput),
      {
        code: 'ERR_INVALID_ARG_TYPE',
        name: 'TypeError'
      }
    );
  }
}
