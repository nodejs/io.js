// Flags: --expose-internals --no-warnings
'use strict';

const common = require('../common');

const assert = require('assert');

const {
  internalBinding,
} = require('internal/test/binding');

const {
  newWritableStreamFromStreamBase,
  newReadableStreamFromStreamBase,
} = require('internal/webstreams/adapters');

const {
  JSStream
} = internalBinding('js_stream');

{
  const stream = new JSStream();
  stream.onwrite = common.mustCall((req, buf) => {
    assert.deepStrictEqual(buf[0], Buffer.from('hello'));
    req.oncomplete();
  });

  const writable = newWritableStreamFromStreamBase(stream);

  const writer = writable.getWriter();

  writer.write(Buffer.from('hello')).then(common.mustCall());
}

{
  const buf = Buffer.from('hello');
  const check = new Uint8Array(buf);

  const stream = new JSStream();

  const readable = newReadableStreamFromStreamBase(stream);

  const reader = readable.getReader();

  reader.read().then(common.mustCall(({ done, value }) => {
    assert(!done);
    assert.deepStrictEqual(new Uint8Array(value), check);

    reader.read().then(common.mustCall(({ done, value }) => {
      assert(done);
      assert.strictEqual(value, undefined);
    }));

  }));

  stream.readBuffer(buf);
  stream.emitEOF();
}

{
  const stream = new JSStream();
  stream.onshutdown = common.mustCall((req) => {
    req.oncomplete();
  });
  const readable = newReadableStreamFromStreamBase(stream);
  readable.cancel().then(common.mustCall());
}

{
  const stream = new JSStream();
  stream.onread = common.mustCall();
  assert.throws(() => newReadableStreamFromStreamBase(stream), {
    code: 'ERR_INVALID_STATE'
  });
  stream.emitEOF();
}
