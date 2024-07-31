// Flags: --expose-internals --no-warnings
'use strict';

const common = require('../common');
const assert = require('assert');

const {
  WritableStream,
  WritableStreamDefaultController,
  WritableStreamDefaultWriter,
  CountQueuingStrategy,
} = require('stream/web');

const {
  kState,
} = require('internal/webstreams/util');

const {
  isPromise,
} = require('util/types');

const {
  kTransfer,
} = require('internal/worker/js_transferable');

const {
  inspect,
} = require('util');

class Sink {
  constructor() {
    this.chunks = [];
  }

  start() {
    this.started = true;
  }

  write(chunk) {
    this.chunks.push(chunk);
  }

  close() {
    this.closed = true;
  }

  abort() {
    this.aborted = true;
  }
}

{
  const stream = new WritableStream();

  assert(stream[kState].controller instanceof WritableStreamDefaultController);
  assert(!stream.locked);

  assert.strictEqual(typeof stream.abort, 'function');
  assert.strictEqual(typeof stream.close, 'function');
  assert.strictEqual(typeof stream.getWriter, 'function');
}

['a', false, 1, null].forEach((sink) => {
  assert.throws(() => new WritableStream(sink), {
    code: 'ERR_INVALID_ARG_TYPE',
  });
});

['a', false, 1].forEach((strategy) => {
  assert.throws(() => new WritableStream({}, strategy), {
    code: 'ERR_INVALID_ARG_TYPE',
  });
});

[1, false, ''].forEach((type) => {
  assert.throws(() => new WritableStream({ type }), {
    code: 'ERR_INVALID_ARG_VALUE',
  });
});

['a', {}].forEach((highWaterMark) => {
  assert.throws(() => new WritableStream({}, { highWaterMark }), {
    code: 'ERR_INVALID_ARG_VALUE',
  });
});

['a', false, {}].forEach((size) => {
  assert.throws(() => new WritableStream({}, { size }), {
    code: 'ERR_INVALID_ARG_TYPE',
  });
});

{
  new WritableStream({});
  new WritableStream([]);
  new WritableStream({}, null);
  new WritableStream({}, {});
  new WritableStream({}, []);
}

{
  const sink = new Sink();
  const stream = new WritableStream(
    sink,
    new CountQueuingStrategy({ highWaterMark: 1 }));

  assert(!stream.locked);
  const writer = stream.getWriter();
  assert(stream.locked);
  assert(writer instanceof WritableStreamDefaultWriter);

  assert(isPromise(writer.closed));
  assert(isPromise(writer.ready));
  assert(typeof writer.desiredSize, 'number');
  assert(typeof writer.abort, 'function');
  assert(typeof writer.close, 'function');
  assert(typeof writer.releaseLock, 'function');
  assert(typeof writer.write, 'function');

  writer.releaseLock();
  assert(!stream.locked);

  const writer2 = stream.getWriter();

  assert(sink.started);

  writer2.closed.then(common.mustCall());
  writer2.ready.then(common.mustCall());

  writer2.close().then(common.mustCall(() => {
    assert.strict(sink.closed);
  }));
}

{
  const sink = new Sink();

  const stream = new WritableStream(
    sink,
    new CountQueuingStrategy({ highWaterMark: 1 }));

  const error = new Error('boom');

  const writer = stream.getWriter();

  assert.rejects(writer.closed, error).then(common.mustCall());

  writer.abort(error).then(common.mustCall(() => {
    assert.strictEqual(stream[kState].state, 'errored');
    assert(sink.aborted);
  }));
}

{
  const sink = new Sink();

  const stream = new WritableStream(
    sink, { highWaterMark: 1 }
  );

  async function write(stream) {
    const writer = stream.getWriter();
    const p = writer.write('hello');
    assert.strictEqual(writer.desiredSize, 0);
    await p;
    assert.strictEqual(writer.desiredSize, 1);
  }

  write(stream).then(common.mustCall(() => {
    assert.deepStrictEqual(['hello'], sink.chunks);
  }));
}

{
  assert.throws(() => Reflect.get(WritableStream.prototype, 'locked', {}), {
    code: 'ERR_INVALID_THIS',
  });
  assert.rejects(() => WritableStream.prototype.abort({}), {
    code: 'ERR_INVALID_THIS',
  }).then(common.mustCall());
  assert.rejects(() => WritableStream.prototype.close({}), {
    code: 'ERR_INVALID_THIS',
  }).then(common.mustCall());
  assert.throws(() => WritableStream.prototype.getWriter.call(), {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => WritableStream.prototype[kTransfer].call(), {
    code: 'ERR_INVALID_THIS',
  });
  assert.rejects(
    Reflect.get(WritableStreamDefaultWriter.prototype, 'closed'), {
      code: 'ERR_INVALID_THIS',
    }).then(common.mustCall());
  assert.rejects(
    Reflect.get(WritableStreamDefaultWriter.prototype, 'ready'), {
      code: 'ERR_INVALID_THIS',
    }).then(common.mustCall());
  assert.throws(
    () => Reflect.get(WritableStreamDefaultWriter.prototype, 'desiredSize'), {
      code: 'ERR_INVALID_THIS',
    });
  assert.rejects(WritableStreamDefaultWriter.prototype.abort({}), {
    code: 'ERR_INVALID_THIS',
  }).then(common.mustCall());
  assert.rejects(WritableStreamDefaultWriter.prototype.close({}), {
    code: 'ERR_INVALID_THIS',
  }).then(common.mustCall());
  assert.rejects(WritableStreamDefaultWriter.prototype.write({}), {
    code: 'ERR_INVALID_THIS',
  }).then(common.mustCall());
  assert.throws(() => WritableStreamDefaultWriter.prototype.releaseLock({}), {
    code: 'ERR_INVALID_THIS',
  });

  assert.throws(() => {
    Reflect.get(WritableStreamDefaultController.prototype, 'signal', {});
  }, {
    code: 'ERR_INVALID_THIS',
  });

  assert.throws(() => {
    WritableStreamDefaultController.prototype.error({});
  }, {
    code: 'ERR_INVALID_THIS',
  });
}

{
  let controller;
  const writable = new WritableStream({
    start(c) { controller = c; }
  });
  assert.strictEqual(
    inspect(writable),
    'WritableStream { locked: false, state: \'writable\' }');
  assert.strictEqual(
    inspect(writable, { depth: null }),
    'WritableStream { locked: false, state: \'writable\' }');
  assert.strictEqual(
    inspect(writable, { depth: 0 }),
    'WritableStream [Object]');

  const writer = writable.getWriter();
  assert.match(
    inspect(writer),
    /WritableStreamDefaultWriter/);
  assert.match(
    inspect(writer, { depth: null }),
    /WritableStreamDefaultWriter/);
  assert.match(
    inspect(writer, { depth: 0 }),
    /WritableStreamDefaultWriter \[/);

  assert.match(
    inspect(controller),
    /WritableStreamDefaultController/);
  assert.match(
    inspect(controller, { depth: null }),
    /WritableStreamDefaultController/);
  assert.match(
    inspect(controller, { depth: 0 }),
    /WritableStreamDefaultController \[/);

  writer.abort(new Error('boom'));

  assert.strictEqual(writer.desiredSize, null);
  setImmediate(() => assert.strictEqual(writer.desiredSize, null));
}
