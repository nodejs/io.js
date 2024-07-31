// Flags: --no-warnings --expose-internals
'use strict';

const common = require('../common');

const assert = require('assert');

const {
  newReadableWritablePairFromDuplex,
} = require('internal/webstreams/adapters');

const {
  PassThrough,
} = require('stream');

{
  // Destroying the duplex without an error should close
  // the readable and error the writable.

  const duplex = new PassThrough();
  const {
    readable,
    writable,
  } = newReadableWritablePairFromDuplex(duplex);

  const reader = readable.getReader();
  const writer = writable.getWriter();

  assert.rejects(reader.closed, {
    code: 'ABORT_ERR',
  }).then(common.mustCall());

  assert.rejects(writer.closed, {
    code: 'ABORT_ERR',
  }).then(common.mustCall());

  duplex.destroy();

  duplex.on('close', common.mustCall());
}

{
  // Destroying the duplex with an error should error
  // both the readable and writable

  const error = new Error('boom');
  const duplex = new PassThrough();
  const {
    readable,
    writable,
  } = newReadableWritablePairFromDuplex(duplex);

  duplex.on('close', common.mustCall());
  duplex.on('error', common.mustCall((reason) => {
    assert.strictEqual(reason, error);
  }));

  const reader = readable.getReader();
  const writer = writable.getWriter();

  assert.rejects(reader.closed, error).then(common.mustCall());
  assert.rejects(writer.closed, error).then(common.mustCall());

  duplex.destroy(error);
}

{
  const error = new Error('boom');
  const duplex = new PassThrough();
  const {
    readable,
    writable,
  } = newReadableWritablePairFromDuplex(duplex);

  duplex.on('close', common.mustCall());
  duplex.on('error', common.mustCall((reason) => {
    assert.strictEqual(reason, error);
  }));

  const reader = readable.getReader();
  const writer = writable.getWriter();

  reader.closed.then(common.mustCall());
  assert.rejects(writer.closed, error).then(common.mustCall());

  reader.cancel(error).then(common.mustCall());
}

{
  const duplex = new PassThrough();
  const {
    readable,
    writable,
  } = newReadableWritablePairFromDuplex(duplex);

  duplex.on('close', common.mustCall());
  duplex.on('error', common.mustNotCall());

  const reader = readable.getReader();
  const writer = writable.getWriter();

  reader.closed.then(common.mustCall());
  writer.closed.then(common.mustCall());

  writer.close().then(common.mustCall());
}

{
  const error = new Error('boom');
  const duplex = new PassThrough();
  const {
    readable,
    writable,
  } = newReadableWritablePairFromDuplex(duplex);

  duplex.on('close', common.mustCall());
  duplex.on('error', common.mustCall((reason) => {
    assert.strictEqual(reason, error);
  }));

  const reader = readable.getReader();
  const writer = writable.getWriter();

  assert.rejects(reader.closed, error).then(common.mustCall());
  assert.rejects(writer.closed, error).then(common.mustCall());

  writer.abort(error).then(common.mustCall());
}

{
  const duplex = new PassThrough();
  const {
    readable,
    writable,
  } = newReadableWritablePairFromDuplex(duplex);

  duplex.on('close', common.mustCall());

  duplex.on('error', common.mustCall((error) => {
    assert.strictEqual(error.code, 'ABORT_ERR');
  }));

  const reader = readable.getReader();
  const writer = writable.getWriter();

  assert.rejects(writer.closed, {
    code: 'ABORT_ERR',
  }).then(common.mustCall());

  reader.cancel();
}

{
  const duplex = new PassThrough();
  const {
    readable,
    writable,
  } = newReadableWritablePairFromDuplex(duplex);

  duplex.on('close', common.mustCall());
  duplex.on('error', common.mustNotCall());

  const reader = readable.getReader();
  const writer = writable.getWriter();

  reader.closed.then(common.mustCall());
  assert.rejects(writer.closed, {
    code: 'ABORT_ERR',
  }).then(common.mustCall());

  duplex.end();
}

{
  const duplex = new PassThrough();
  const {
    readable,
    writable,
  } = newReadableWritablePairFromDuplex(duplex);

  duplex.on('data', common.mustCall(2));
  duplex.on('close', common.mustCall());
  duplex.on('end', common.mustCall());
  duplex.on('finish', common.mustCall());

  const writer = writable.getWriter();
  const reader = readable.getReader();

  const ec = new TextEncoder();
  const dc = new TextDecoder();

  Promise.all([
    writer.write(ec.encode('hello')),
    reader.read().then(common.mustCall(({ done, value }) => {
      assert(!done);
      assert.strictEqual(dc.decode(value), 'hello');
    })),
    reader.read().then(common.mustCall(({ done, value }) => {
      assert(!done);
      assert.strictEqual(dc.decode(value), 'there');
    })),
    writer.write(ec.encode('there')),
    writer.close(),
    reader.read().then(common.mustCall(({ done, value }) => {
      assert(done);
      assert.strictEqual(value, undefined);
    })),
  ]).then(common.mustCall());
}

{
  const duplex = new PassThrough();
  duplex.destroy();
  const {
    readable,
    writable,
  } = newReadableWritablePairFromDuplex(duplex);
  const reader = readable.getReader();
  const writer = writable.getWriter();
  reader.closed.then(common.mustCall());
  writer.closed.then(common.mustCall());
}

{
  const duplex = new PassThrough({ writable: false });
  assert(duplex.readable);
  assert(!duplex.writable);
  const {
    readable,
    writable,
  } = newReadableWritablePairFromDuplex(duplex);
  const reader = readable.getReader();
  const writer = writable.getWriter();
  writer.closed.then(common.mustCall());
  reader.cancel().then(common.mustCall());
}

{
  const duplex = new PassThrough({ readable: false });
  assert(!duplex.readable);
  assert(duplex.writable);
  const {
    readable,
    writable,
  } = newReadableWritablePairFromDuplex(duplex);
  const reader = readable.getReader();
  const writer = writable.getWriter();
  reader.closed.then(common.mustCall());
  writer.close().then(common.mustCall());
}

{
  assert.throws(() => newReadableWritablePairFromDuplex(null), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
}
