'use strict';
const common = require('../common');
const assert = require('assert');

const { Readable, Writable } = require('stream');

const buffer = Buffer.from('ABCD');
const views = common.getArrayBufferViews(buffer);

{
  // Simple Writable test.
  let n = 0;
  const writable = new Writable({
    write: common.mustCall((chunk, encoding, cb) => {
      assert(chunk instanceof Buffer);
      assert(ArrayBuffer.isView(chunk));
      assert.deepStrictEqual(common.getBufferSources(chunk)[n], views[n]);
      n++;
      cb();
    }, views.length),
  });

  views.forEach((msg) => writable.write(msg));
  writable.end();
}

{
  // Writable test with object mode True.
  let n = 0;
  const writable = new Writable({
    objectMode: true,
    write: common.mustCall((chunk, encoding, cb) => {
      assert(!(chunk instanceof Buffer));
      assert(ArrayBuffer.isView(chunk));
      assert.deepStrictEqual(common.getBufferSources(chunk)[n], views[n]);
      n++;
      cb();
    }, views.length),
  });

  views.forEach((msg) => writable.write(msg));
  writable.end();
}


{
  // Writable test, multiple writes carried out via writev.
  let n = 0;
  let callback;
  const writable = new Writable({
    write: common.mustCall((chunk, encoding, cb) => {
      assert(chunk instanceof Buffer);
      assert(ArrayBuffer.isView(chunk));
      assert.deepStrictEqual(common.getBufferSources(chunk)[n], views[n]);
      n++;
      callback = cb;
    }),

    writev: common.mustCall((chunks, cb) => {
      assert.strictEqual(chunks.length, views.length);
      let res = '';
      for (const chunk of chunks) {
        assert.strictEqual(chunk.encoding, 'buffer');
        res += chunk.chunk;
      }
      assert.strictEqual(res, 'ABCD'.repeat(9));
    }),

  });
  views.forEach((msg) => writable.write(msg));
  writable.end(views[0]);
  callback();
}


{
  // Simple Readable test.
  const readable = new Readable({
    read() {}
  });

  readable.push(views[1]);
  readable.push(views[2]);
  readable.unshift(views[0]);

  const buf = readable.read();
  assert(buf instanceof Buffer);
  assert.deepStrictEqual([...buf], [...views[0], ...views[1], ...views[2]]);
}

{
  // Readable test, setEncoding.
  const readable = new Readable({
    read() {}
  });

  readable.setEncoding('utf8');

  readable.push(views[1]);
  readable.push(views[2]);
  readable.unshift(views[0]);

  const out = readable.read();
  assert.strictEqual(out, 'ABCD'.repeat(3));
}
