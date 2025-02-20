// Flags: --expose-internals --no-warnings --allow-natives-syntax
'use strict';

const common = require('../common');
const assert = require('assert');

const { internalBinding } = require('internal/test/binding');

function testFastUtf8Write() {
  {
    const buf = Buffer.from('\x80');

    assert.strictEqual(buf[0], 194);
    assert.strictEqual(buf[1], 128);
  }

  {
    const buf = Buffer.alloc(64);
    const newBuf = buf.subarray(0, buf.write('éñüçßÆ'));
    assert.deepStrictEqual(newBuf, Buffer.from([195, 169, 195, 177, 195, 188, 195, 167, 195, 159, 195, 134]));
  }

  {
    const buf = Buffer.alloc(64);
    const newBuf = buf.subarray(0, buf.write('¿'));
    assert.deepStrictEqual(newBuf, Buffer.from([194, 191]));
  }

  {
    const buf = Buffer.from(new ArrayBuffer(34), 0, 16);
    const str = Buffer.from([50, 83, 127, 39, 104, 8, 74, 65, 108, 123, 5, 4, 82, 10, 7, 53]).toString();
    const newBuf = buf.subarray(0, buf.write(str));
    assert.deepStrictEqual(newBuf, Buffer.from([ 50, 83, 127, 39, 104, 8, 74, 65, 108, 123, 5, 4, 82, 10, 7, 53]));
  }
}

eval('%PrepareFunctionForOptimization(Buffer.prototype.utf8Write)');
testFastUtf8Write();
eval('%OptimizeFunctionOnNextCall(Buffer.prototype.utf8Write)');
testFastUtf8Write();

if (common.isDebug) {
  const { getV8FastApiCallCount } = internalBinding('debug');
  assert(getV8FastApiCallCount('buffer.writeString'), 4);
}
