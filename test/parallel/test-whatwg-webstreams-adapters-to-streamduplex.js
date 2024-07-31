// Flags: --no-warnings --expose-internals
'use strict';

const common = require('../common');

const assert = require('assert');

const {
  TransformStream,
} = require('stream/web');

const {
  newStreamDuplexFromReadableWritablePair,
} = require('internal/webstreams/adapters');

const {
  finished,
  pipeline,
  Readable,
  Writable,
} = require('stream');

const {
  kState,
} = require('internal/webstreams/util');

{
  const transform = new TransformStream();
  const duplex = newStreamDuplexFromReadableWritablePair(transform);

  assert(transform.readable.locked);
  assert(transform.writable.locked);

  duplex.destroy();

  duplex.on('close', common.mustCall(() => {
    assert.strictEqual(transform.readable[kState].state, 'closed');
    assert.strictEqual(transform.writable[kState].state, 'errored');
  }));
}

{
  const error = new Error('boom');
  const transform = new TransformStream();
  const duplex = newStreamDuplexFromReadableWritablePair(transform);

  assert(transform.readable.locked);
  assert(transform.writable.locked);

  duplex.destroy(error);
  duplex.on('error', common.mustCall((reason) => {
    assert.strictEqual(reason, error);
  }));

  duplex.on('close', common.mustCall(() => {
    assert.strictEqual(transform.readable[kState].state, 'closed');
    assert.strictEqual(transform.writable[kState].state, 'errored');
    assert.strictEqual(transform.writable[kState].storedError, error);
  }));
}

{
  const transform = new TransformStream();
  const duplex = new newStreamDuplexFromReadableWritablePair(transform);

  duplex.end();
  duplex.resume();

  duplex.on('close', common.mustCall(() => {
    assert.strictEqual(transform.readable[kState].state, 'closed');
    assert.strictEqual(transform.writable[kState].state, 'closed');
  }));
}

{
  const ec = new TextEncoder();
  const dc = new TextDecoder();
  const transform = new TransformStream({
    transform(chunk, controller) {
      const text = dc.decode(chunk);
      controller.enqueue(ec.encode(text.toUpperCase()));
    }
  });
  const duplex = new newStreamDuplexFromReadableWritablePair(transform, {
    encoding: 'utf8',
  });

  duplex.end('hello');
  duplex.on('data', common.mustCall((chunk) => {
    assert.strictEqual(chunk, 'HELLO');
  }));
  duplex.on('end', common.mustCall());

  duplex.on('close', common.mustCall(() => {
    assert.strictEqual(transform.readable[kState].state, 'closed');
    assert.strictEqual(transform.writable[kState].state, 'closed');
  }));
}

{
  const ec = new TextEncoder();
  const dc = new TextDecoder();
  const transform = new TransformStream({
    transform: common.mustCall((chunk, controller) => {
      const text = dc.decode(chunk);
      controller.enqueue(ec.encode(text.toUpperCase()));
    })
  });
  const duplex = new newStreamDuplexFromReadableWritablePair(transform, {
    encoding: 'utf8',
  });

  finished(duplex, common.mustCall());

  duplex.end('hello');
  duplex.resume();
}

{
  const ec = new TextEncoder();
  const dc = new TextDecoder();
  const transform = new TransformStream({
    transform: common.mustCall((chunk, controller) => {
      const text = dc.decode(chunk);
      controller.enqueue(ec.encode(text.toUpperCase()));
    })
  });
  const duplex = new newStreamDuplexFromReadableWritablePair(transform, {
    encoding: 'utf8',
  });

  const readable = new Readable({
    read() {
      readable.push(Buffer.from('hello'));
      readable.push(null);
    }
  });

  const writable = new Writable({
    write: common.mustCall((chunk, encoding, callback) => {
      assert.strictEqual(dc.decode(chunk), 'HELLO');
      assert.strictEqual(encoding, 'buffer');
      callback();
    })
  });

  finished(duplex, common.mustCall());
  pipeline(readable, duplex, writable, common.mustCall());
}

{
  const transform = new TransformStream();
  const duplex = newStreamDuplexFromReadableWritablePair(transform);
  duplex.setEncoding('utf-8');
  duplex.on('data', common.mustCall((data) => {
    assert.strictEqual(data, 'hello');
  }, 5));

  duplex.write(Buffer.from('hello'));
  duplex.write(Buffer.from('hello'));
  duplex.write(Buffer.from('hello'));
  duplex.write(Buffer.from('hello'));
  duplex.write(Buffer.from('hello'));

  duplex.end();
}

{
  const transform = { readable: {}, writable: {} };
  assert.throws(() => newStreamDuplexFromReadableWritablePair(transform), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
}

{
  const transform = {
    readable: new ReadableStream(),
    writable: null
  };

  assert.throws(() => newStreamDuplexFromReadableWritablePair(transform), {
    code: 'ERR_INVALID_ARG_TYPE',
  });
}
