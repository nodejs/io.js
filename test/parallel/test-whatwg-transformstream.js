// Flags: --expose-internals --no-warnings
'use strict';

const common = require('../common');
const assert = require('assert');

const {
  ReadableStream,
  TransformStream,
  TransformStreamDefaultController,
} = require('stream/web');

const {
  createReadStream,
  readFileSync,
} = require('fs');

const {
  kTransfer,
} = require('internal/worker/js_transferable');

const {
  inspect,
} = require('util');

assert.throws(() => new TransformStream({ readableType: 1 }), {
  code: 'ERR_INVALID_ARG_VALUE',
});
assert.throws(() => new TransformStream({ writableType: 1 }), {
  code: 'ERR_INVALID_ARG_VALUE',
});

{
  new TransformStream({});
  new TransformStream([]);
  new TransformStream({}, null);
  new TransformStream({}, {});
  new TransformStream({}, []);
  new TransformStream({}, {}, null);
  new TransformStream({}, {}, {});
  new TransformStream({}, {}, []);
}

['a', false, 1, null].forEach((transform) => {
  assert.throws(() => {
    new TransformStream(transform);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
  });
});

['a', false, 1].forEach((writableStrategy) => {
  assert.throws(() => {
    new TransformStream({}, writableStrategy);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
  });
});

['a', false, 1].forEach((readableStrategy) => {
  assert.throws(() => {
    new TransformStream({}, {}, readableStrategy);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
  });
});

{
  const stream = new TransformStream();

  async function test(stream) {
    const writer = stream.writable.getWriter();
    const reader = stream.readable.getReader();

    const { 1: result } = await Promise.all([
      writer.write('hello'),
      reader.read(),
    ]);

    assert.strictEqual(result.value, 'hello');
  }

  test(stream).then(common.mustCall());
}

class Transform {
  start(controller) {
    this.started = true;
  }

  async transform(chunk, controller) {
    controller.enqueue(chunk.toUpperCase());
  }

  async flush() {
    this.flushed = true;
  }
}

{
  const transform = new Transform();
  const stream = new TransformStream(transform);
  assert(transform.started);

  async function test(stream) {
    const writer = stream.writable.getWriter();
    const reader = stream.readable.getReader();

    const { 1: result } = await Promise.all([
      writer.write('hello'),
      reader.read(),
    ]);

    assert.strictEqual(result.value, 'HELLO');

    await writer.close();
  }

  test(stream).then(common.mustCall(() => {
    assert(transform.flushed);
  }));
}

class Source {
  constructor() {
    this.cancelCalled = false;
  }

  start(controller) {
    this.stream = createReadStream(__filename);
    this.stream.on('data', (chunk) => {
      controller.enqueue(chunk.toString());
    });
    this.stream.once('end', () => {
      if (!this.cancelCalled)
        controller.close();
    });
    this.stream.once('error', (error) => {
      controller.error(error);
    });
  }

  cancel() {
    this.cancelCalled = true;
  }
}

{
  const instream = new ReadableStream(new Source());
  const tstream = new TransformStream(new Transform());
  const r = instream.pipeThrough(tstream);

  async function read(stream) {
    let res = '';
    for await (const chunk of stream)
      res += chunk;
    return res;
  }

  read(r).then(common.mustCall((data) => {
    const check = readFileSync(__filename);
    assert.strictEqual(check.toString().toUpperCase(), data);
  }));
}

{
  assert.throws(() => Reflect.get(TransformStream.prototype, 'readable', {}), {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => Reflect.get(TransformStream.prototype, 'writable', {}), {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => TransformStream.prototype[kTransfer]({}), {
    code: 'ERR_INVALID_THIS',
  });

  assert.throws(() => {
    Reflect.get(TransformStreamDefaultController.prototype, 'desiredSize', {});
  }, {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => {
    TransformStreamDefaultController.prototype.enqueue({});
  }, {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => {
    TransformStreamDefaultController.prototype.error({});
  }, {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => {
    TransformStreamDefaultController.prototype.terminate({});
  }, {
    code: 'ERR_INVALID_THIS',
  });

  assert.throws(() => new TransformStreamDefaultController(), {
    code: 'ERR_ILLEGAL_CONSTRUCTOR',
  });
}

{
  let controller;
  const transform = new TransformStream({
    start(c) {
      controller = c;
    }
  });

  assert.match(inspect(transform), /TransformStream/);
  assert.match(inspect(transform, { depth: null }), /TransformStream/);
  assert.match(inspect(transform, { depth: 0 }), /TransformStream \[/);

  assert.match(inspect(controller), /TransformStreamDefaultController/);
  assert.match(
    inspect(controller, { depth: null }),
    /TransformStreamDefaultController/);
  assert.match(
    inspect(controller, { depth: 0 }),
    /TransformStreamDefaultController \[/);
}

{
  Object.defineProperty(Object.prototype, 'type', {
    get: common.mustNotCall('get %Object.prototype%.type'),
    set: common.mustNotCall('set %Object.prototype%.type'),
    configurable: true,
  });

  new TransformStream({
    transform(chunk, controller) {
      controller.enqueue(chunk);
    },
    flush(controller) {
      controller.terminate();
    }
  });

  delete Object.prototype.type;
}
