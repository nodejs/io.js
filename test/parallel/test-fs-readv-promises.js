'use strict';
const common = require('../common');
const assert = require('assert');
const fs = require('fs').promises;
const tmpdir = require('../common/tmpdir');

tmpdir.refresh();

const expected = 'ümlaut. Лорем 運務ホソモ指及 आपको करने विकास 紙読決多密所 أضف';
const expectedBuff = Buffer.from(expected);

let cnt = 0;
function getFileName() {
  return tmpdir.resolve(`readv_promises_${++cnt}.txt`);
}

const allocateEmptyBuffers = (combinedLength) => {
  const bufferArr = [];
  // Allocate two buffers, each half the size of expectedBuff
  bufferArr[0] = Buffer.alloc(Math.floor(combinedLength / 2));
  bufferArr[1] = Buffer.alloc(combinedLength - bufferArr[0].length);

  return bufferArr;
};

(async () => {
  {
    const filename = getFileName();
    await fs.writeFile(filename, expectedBuff);
    const handle = await fs.open(filename, 'r');
    const bufferArr = allocateEmptyBuffers(expectedBuff.length);
    const expectedLength = expectedBuff.length;

    let { bytesRead, buffers } = await handle.readv([Buffer.from('')],
                                                    null);
    assert.strictEqual(bytesRead, 0);
    assert.deepStrictEqual(buffers, [Buffer.from('')]);

    ({ bytesRead, buffers } = await handle.readv(bufferArr, null));
    assert.strictEqual(bytesRead, expectedLength);
    assert.deepStrictEqual(buffers, bufferArr);
    assert(Buffer.concat(bufferArr).equals(await fs.readFile(filename)));
    handle.close();
  }

  {
    const filename = getFileName();
    await fs.writeFile(filename, expectedBuff);
    const handle = await fs.open(filename, 'r');
    const bufferArr = allocateEmptyBuffers(expectedBuff.length);
    const expectedLength = expectedBuff.length;

    let { bytesRead, buffers } = await handle.readv([Buffer.from('')]);
    assert.strictEqual(bytesRead, 0);
    assert.deepStrictEqual(buffers, [Buffer.from('')]);

    ({ bytesRead, buffers } = await handle.readv(bufferArr));
    assert.strictEqual(bytesRead, expectedLength);
    assert.deepStrictEqual(buffers, bufferArr);
    assert(Buffer.concat(bufferArr).equals(await fs.readFile(filename)));
    handle.close();
  }
})().then(common.mustCall());
