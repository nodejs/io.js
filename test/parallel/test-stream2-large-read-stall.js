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

'use strict';
const common = require('../common');
const assert = require('assert');

// If everything aligns so that you do a read(n) of exactly the
// remaining buffer, then make sure that 'end' still emits.

const READSIZE = 100;
const PUSHSIZE = 20;
const PUSHCOUNT = 1000;
const HWM = 50;

const Readable = require('stream').Readable;
const r = new Readable({
  highWaterMark: HWM
});
const rs = r._readableState;

r._read = push;

r.on('readable', function() {
  console.error('>> readable');
  let ret;
  do {
    console.error(`  > read(${READSIZE})`);
    ret = r.read(READSIZE);
    console.error(`  < ${ret?.length} (${rs.length} remain)`);
  } while (ret && ret.length === READSIZE);

  console.error('<< after read()',
                ret?.length,
                rs.needReadable,
                rs.length);
});

r.on('end', common.mustCall(function() {
  assert.strictEqual(pushes, PUSHCOUNT + 1);
}));

let pushes = 0;
function push() {
  if (pushes > PUSHCOUNT)
    return;

  if (pushes++ === PUSHCOUNT) {
    console.error('   push(EOF)');
    return r.push(null);
  }

  console.error(`   push #${pushes}`);
  if (r.push(Buffer.allocUnsafe(PUSHSIZE)))
    setTimeout(push, 1);
}
