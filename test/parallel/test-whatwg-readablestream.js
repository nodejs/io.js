// Flags: --expose-internals --no-warnings
'use strict';

const common = require('../common');
const { isDisturbed, isErrored, isReadable } = require('stream');
const assert = require('assert');
const {
  isPromise,
} = require('util/types');
const {
  setImmediate: delay
} = require('timers/promises');

const {
  ByteLengthQueuingStrategy,
  CountQueuingStrategy,
  ReadableStream,
  ReadableStreamDefaultReader,
  ReadableStreamDefaultController,
  ReadableByteStreamController,
  ReadableStreamBYOBReader,
  ReadableStreamBYOBRequest,
  WritableStream,
} = require('stream/web');

const {
  readableStreamPipeTo,
  readableStreamTee,
  readableByteStreamControllerConvertPullIntoDescriptor,
  readableStreamDefaultControllerEnqueue,
  readableByteStreamControllerEnqueue,
  readableStreamDefaultControllerCanCloseOrEnqueue,
  readableByteStreamControllerClose,
  readableByteStreamControllerRespond,
  readableStreamReaderGenericRelease,
} = require('internal/webstreams/readablestream');

const {
  kState
} = require('internal/webstreams/util');

const {
  createReadStream,
  readFileSync,
} = require('fs');
const {
  Buffer,
} = require('buffer');

const {
  kTransfer,
} = require('internal/worker/js_transferable');

const {
  inspect,
} = require('util');

{
  const r = new ReadableStream();
  assert.strictEqual(typeof r.locked, 'boolean');
  assert.strictEqual(typeof r.cancel, 'function');
  assert.strictEqual(typeof r.getReader, 'function');
  assert.strictEqual(typeof r.pipeThrough, 'function');
  assert.strictEqual(typeof r.pipeTo, 'function');
  assert.strictEqual(typeof r.tee, 'function');

  ['', null, 'asdf'].forEach((mode) => {
    assert.throws(() => r.getReader({ mode }), {
      code: 'ERR_INVALID_ARG_VALUE',
    });
  });

  [1, 'asdf'].forEach((options) => {
    assert.throws(() => r.getReader(options), {
      code: 'ERR_INVALID_ARG_TYPE',
    });
  });

  assert(!r.locked);
  r.getReader();
  assert(r.locked);
}

{
  // Throw error and return rejected promise in `cancel()` method
  // would execute same cleanup code
  const r1 = new ReadableStream({
    cancel: () => {
      return Promise.reject('Cancel Error');
    },
  });
  r1.cancel().finally(common.mustCall(() => {
    const controllerState = r1[kState].controller[kState];

    assert.strictEqual(controllerState.pullAlgorithm, undefined);
    assert.strictEqual(controllerState.cancelAlgorithm, undefined);
    assert.strictEqual(controllerState.sizeAlgorithm, undefined);
  })).catch(() => {});

  const r2 = new ReadableStream({
    cancel() {
      throw new Error('Cancel Error');
    }
  });
  r2.cancel().finally(common.mustCall(() => {
    const controllerState = r2[kState].controller[kState];

    assert.strictEqual(controllerState.pullAlgorithm, undefined);
    assert.strictEqual(controllerState.cancelAlgorithm, undefined);
    assert.strictEqual(controllerState.sizeAlgorithm, undefined);
  })).catch(() => {});
}

{
  const source = {
    start: common.mustCall((controller) => {
      assert(controller instanceof ReadableStreamDefaultController);
    }),
    pull: common.mustCall((controller) => {
      assert(controller instanceof ReadableStreamDefaultController);
    }),
    cancel: common.mustNotCall(),
  };

  new ReadableStream(source);
}

{
  const source = {
    start: common.mustCall(async (controller) => {
      assert(controller instanceof ReadableStreamDefaultController);
    }),
    pull: common.mustCall(async (controller) => {
      assert(controller instanceof ReadableStreamDefaultController);
    }),
    cancel: common.mustNotCall(),
  };

  new ReadableStream(source);
}

{
  const source = {
    start: common.mustCall((controller) => {
      assert(controller instanceof ReadableByteStreamController);
    }),
    pull: common.mustNotCall(),
    cancel: common.mustNotCall(),
    type: 'bytes',
  };

  new ReadableStream(source);
}

{
  const source = {
    start: common.mustCall(async (controller) => {
      assert(controller instanceof ReadableByteStreamController);
    }),
    pull: common.mustNotCall(),
    cancel: common.mustNotCall(),
    type: 'bytes',
  };

  new ReadableStream(source);
}

{
  const source = {
    start: common.mustCall(async (controller) => {
      assert(controller instanceof ReadableByteStreamController);
    }),
    pull: common.mustCall(async (controller) => {
      assert(controller instanceof ReadableByteStreamController);
    }),
    cancel: common.mustNotCall(),
    type: 'bytes',
  };

  new ReadableStream(source, { highWaterMark: 10 });
}

{
  new ReadableStream({});
  new ReadableStream([]);
  new ReadableStream({}, null);
  new ReadableStream({}, {});
  new ReadableStream({}, []);
}

['a', false, 1, null].forEach((source) => {
  assert.throws(() => {
    new ReadableStream(source);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
  });
});

['a', false, 1].forEach((strategy) => {
  assert.throws(() => {
    new ReadableStream({}, strategy);
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
  });
});

['a', {}, false].forEach((size) => {
  assert.throws(() => {
    new ReadableStream({}, { size });
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
  });
});

['a', {}].forEach((highWaterMark) => {
  assert.throws(() => {
    new ReadableStream({}, { highWaterMark });
  }, {
    code: 'ERR_INVALID_ARG_VALUE',
  });

  assert.throws(() => {
    new ReadableStream({ type: 'bytes' }, { highWaterMark });
  }, {
    code: 'ERR_INVALID_ARG_VALUE',
  });
});

[-1, NaN].forEach((highWaterMark) => {
  assert.throws(() => {
    new ReadableStream({}, { highWaterMark });
  }, {
    code: 'ERR_INVALID_ARG_VALUE',
  });

  assert.throws(() => {
    new ReadableStream({ type: 'bytes' }, { highWaterMark });
  }, {
    code: 'ERR_INVALID_ARG_VALUE',
  });
});

{
  new ReadableStream({}, new ByteLengthQueuingStrategy({ highWaterMark: 1 }));
  new ReadableStream({}, new CountQueuingStrategy({ highWaterMark: 1 }));
}

{
  const strategy = new ByteLengthQueuingStrategy({ highWaterMark: 1 });
  assert.strictEqual(strategy.highWaterMark, 1);
  assert.strictEqual(strategy.size(new ArrayBuffer(10)), 10);

  const { size } = strategy;
  assert.strictEqual(size(new ArrayBuffer(10)), 10);
}

{
  const strategy = new CountQueuingStrategy({ highWaterMark: 1 });
  assert.strictEqual(strategy.highWaterMark, 1);
  assert.strictEqual(strategy.size(new ArrayBuffer(10)), 1);

  const { size } = strategy;
  assert.strictEqual(size(new ArrayBuffer(10)), 1);
}

{
  const r = new ReadableStream({
    async start() {
      throw new Error('boom');
    }
  });

  setImmediate(() => {
    assert.strictEqual(r[kState].state, 'errored');
    assert.match(r[kState].storedError?.message, /boom/);
  });
}

{
  const data = Buffer.from('hello');
  const r = new ReadableStream({
    start(controller) {
      controller.enqueue(data);
      controller.close();
    },
  });

  (async function read() {
    const reader = r.getReader();
    let res = await reader.read();
    if (res.done) return;
    const buf = Buffer.from(res.value);
    assert.strictEqual(buf.toString(), data.toString());
    res = await reader.read();
    assert(res.done);
  })().then(common.mustCall());
}

{
  const r = new ReadableStream({
    start(controller) {
      controller.close();
    },
  });

  (async function read() {
    const reader = r.getReader();
    const res = await reader.read();
    assert(res.done);
  })().then(common.mustCall());
}

assert.throws(() => {
  new ReadableStream({
    get start() { throw new Error('boom1'); }
  }, {
    get size() { throw new Error('boom2'); }
  });
}, /boom2/);

{
  const stream = new ReadableStream();
  const reader = stream.getReader();

  assert(stream.locked);
  assert.strictEqual(reader[kState].stream, stream);
  assert.strictEqual(stream[kState].reader, reader);

  assert.throws(() => stream.getReader(), {
    code: 'ERR_INVALID_STATE',
  });

  assert(reader instanceof ReadableStreamDefaultReader);

  assert(isPromise(reader.closed));
  assert.strictEqual(typeof reader.cancel, 'function');
  assert.strictEqual(typeof reader.read, 'function');
  assert.strictEqual(typeof reader.releaseLock, 'function');

  const read1 = reader.read();
  const read2 = reader.read();

  read1.then(common.mustNotCall(), common.mustCall());
  read2.then(common.mustNotCall(), common.mustCall());

  assert.notStrictEqual(read1, read2);

  assert.strictEqual(reader[kState].readRequests.length, 2);

  delay().then(common.mustCall());

  assert(stream.locked);
  reader.releaseLock();
  assert(!stream.locked);
}

{
  const stream = new ReadableStream();
  const reader = stream.getReader();
  const closedBefore = reader.closed;
  assert(stream.locked);
  reader.releaseLock();
  assert(!stream.locked);
  const closedAfter = reader.closed;

  assert.strictEqual(closedBefore, closedAfter);

  assert.rejects(reader.read(), {
    code: 'ERR_INVALID_STATE',
  }).then(common.mustCall());

  assert.rejects(closedBefore, {
    code: 'ERR_INVALID_STATE',
  }).then(common.mustCall());
}

{
  const stream = new ReadableStream();
  const iterable = stream.values();
  readableStreamReaderGenericRelease(stream[kState].reader);
  assert.rejects(iterable.next(), {
    code: 'ERR_INVALID_STATE',
  }).then(common.mustCall());
}

{
  const stream = new ReadableStream();
  const iterable = stream.values();
  readableStreamReaderGenericRelease(stream[kState].reader);
  assert.rejects(iterable.return(), {
    code: 'ERR_INVALID_STATE',
  }).then(common.mustCall());
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue(Buffer.from('hello'));
    }
  });

  const reader = stream.getReader();

  assert.rejects(stream.cancel(), {
    code: 'ERR_INVALID_STATE',
  }).then(common.mustCall());

  reader.cancel();

  reader.read().then(common.mustCall(({ value, done }) => {
    assert.strictEqual(value, undefined);
    assert(done);
  }));
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.close();
    }
  });
  assert(!stream.locked);

  const cancel1 = stream.cancel();
  const cancel2 = stream.cancel();

  assert.notStrictEqual(cancel1, cancel2);

  Promise.all([cancel1, cancel2]).then(common.mustCall((res) => {
    assert.deepStrictEqual(res, [undefined, undefined]);
  }));
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.close();
    }
  });

  stream.getReader().releaseLock();
  stream.getReader().releaseLock();
  stream.getReader();
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.close();
    }
  });

  stream.getReader();

  assert.throws(() => stream.getReader(), {
    code: 'ERR_INVALID_STATE',
  });
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.close();
    },
  });

  const reader = stream.getReader();

  reader.closed.then(common.mustCall());

  reader.read().then(common.mustCall(({ value, done }) => {
    assert.strictEqual(value, undefined);
    assert(done);
    reader.read().then(common.mustCall(({ value, done }) => {
      assert.strictEqual(value, undefined);
      assert(done);
    }));
  }));
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.close();
    },
  });

  const reader = stream.getReader();

  const closedBefore = reader.closed;
  reader.releaseLock();
  const closedAfter = reader.closed;
  assert.notStrictEqual(closedBefore, closedAfter);

  closedBefore.then(common.mustCall());
  assert.rejects(closedAfter, {
    code: 'ERR_INVALID_STATE',
  }).then(common.mustCall());
}

{
  let c;
  const stream = new ReadableStream({
    start(controller) {
      c = controller;
    },
  });

  const reader = stream.getReader();
  c.close();

  const closedBefore = reader.closed;
  reader.releaseLock();
  const closedAfter = reader.closed;
  assert.notStrictEqual(closedBefore, closedAfter);

  closedBefore.then(common.mustCall());
  assert.rejects(closedAfter, {
    code: 'ERR_INVALID_STATE',
  }).then(common.mustCall());
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.close();
    },
  });

  const reader = stream.getReader();

  const cancel1 = reader.cancel();
  const cancel2 = reader.cancel();
  const closed = reader.closed;

  assert.notStrictEqual(cancel1, cancel2);
  assert.notStrictEqual(cancel1, closed);
  assert.notStrictEqual(cancel2, closed);

  Promise.all([cancel1, cancel2]).then(common.mustCall((res) => {
    assert.deepStrictEqual(res, [undefined, undefined]);
  }));
}

{
  let c;
  const stream = new ReadableStream({
    start(controller) {
      c = controller;
    },
  });

  const reader = stream.getReader();
  c.close();

  const cancel1 = reader.cancel();
  const cancel2 = reader.cancel();
  const closed = reader.closed;

  assert.notStrictEqual(cancel1, cancel2);
  assert.notStrictEqual(cancel1, closed);
  assert.notStrictEqual(cancel2, closed);

  Promise.all([cancel1, cancel2]).then(common.mustCall((res) => {
    assert.deepStrictEqual(res, [undefined, undefined]);
  }));
}

{
  const stream = new ReadableStream();
  const cancel1 = stream.cancel();
  const cancel2 = stream.cancel();
  assert.notStrictEqual(cancel1, cancel2);

  Promise.all([cancel1, cancel2]).then(common.mustCall((res) => {
    assert.deepStrictEqual(res, [undefined, undefined]);
  }));

  stream.getReader().read().then(common.mustCall(({ value, done }) => {
    assert.strictEqual(value, undefined);
    assert(done);
  }));
}

{
  const error = new Error('boom');
  const stream = new ReadableStream({
    start(controller) {
      controller.error(error);
    }
  });
  stream.getReader().releaseLock();
  const reader = stream.getReader();
  assert.rejects(reader.closed, error).then(common.mustCall());
  assert.rejects(reader.read(), error).then(common.mustCall());
  assert.rejects(reader.read(), error).then(common.mustCall());
}

{
  const error = new Error('boom');
  const stream = new ReadableStream({
    start(controller) {
      controller.error(error);
    }
  });
  const reader = stream.getReader();
  const cancel1 = reader.cancel();
  const cancel2 = reader.cancel();
  assert.notStrictEqual(cancel1, cancel2);
  assert.rejects(cancel1, error).then(common.mustCall());
  assert.rejects(cancel2, error).then(common.mustCall());
}

{
  const error = new Error('boom');
  const stream = new ReadableStream({
    async start(controller) {
      throw error;
    }
  });
  stream.getReader().releaseLock();
  const reader = stream.getReader();
  assert.rejects(reader.closed, error).then(common.mustCall());
  assert.rejects(reader.read(), error).then(common.mustCall());
  assert.rejects(reader.read(), error).then(common.mustCall());
}

{
  const buf1 = Buffer.from('hello');
  const buf2 = Buffer.from('there');
  let doClose;
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue(buf1);
      controller.enqueue(buf2);
      doClose = controller.close.bind(controller);
    }
  });
  const reader = stream.getReader();
  doClose();
  reader.read().then(common.mustCall(({ value, done }) => {
    assert.deepStrictEqual(value, buf1);
    assert(!done);
    reader.read().then(common.mustCall(({ value, done }) => {
      assert.deepStrictEqual(value, buf2);
      assert(!done);
      reader.read().then(common.mustCall(({ value, done }) => {
        assert.strictEqual(value, undefined);
        assert(done);
      }));
    }));
  }));
}

{
  const buf1 = Buffer.from('hello');
  const buf2 = Buffer.from('there');
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue(buf1);
      controller.enqueue(buf2);
    }
  });
  const reader = stream.getReader();
  reader.read().then(common.mustCall(({ value, done }) => {
    assert.deepStrictEqual(value, buf1);
    assert(!done);
    reader.read().then(common.mustCall(({ value, done }) => {
      assert.deepStrictEqual(value, buf2);
      assert(!done);
      reader.read().then(common.mustNotCall());
      delay().then(common.mustCall());
    }));
  }));
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue('a');
      controller.enqueue('b');
      controller.close();
    }
  });

  const { 0: s1, 1: s2 } = stream.tee();

  assert(s1 instanceof ReadableStream);
  assert(s2 instanceof ReadableStream);

  async function read(stream) {
    const reader = stream.getReader();
    assert.deepStrictEqual(
      await reader.read(), { value: 'a', done: false });
    assert.deepStrictEqual(
      await reader.read(), { value: 'b', done: false });
    assert.deepStrictEqual(
      await reader.read(), { value: undefined, done: true });
  }

  Promise.all([
    read(s1),
    read(s2),
  ]).then(common.mustCall());
}

{
  const error = new Error('boom');
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue('a');
      controller.enqueue('b');
    },
    pull() { throw error; }
  });

  const { 0: s1, 1: s2 } = stream.tee();

  assert(stream.locked);

  assert(s1 instanceof ReadableStream);
  assert(s2 instanceof ReadableStream);

  const reader1 = s1.getReader();
  const reader2 = s2.getReader();

  const closed1 = reader1.closed;
  const closed2 = reader2.closed;

  assert.notStrictEqual(closed1, closed2);

  assert.rejects(closed1, error).then(common.mustCall());
  assert.rejects(closed2, error).then(common.mustCall());

  reader1.read();
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue('a');
      controller.enqueue('b');
      controller.close();
    }
  });

  const { 0: s1, 1: s2 } = stream.tee();

  assert(s1 instanceof ReadableStream);
  assert(s2 instanceof ReadableStream);

  s2.cancel();

  async function read(stream, canceled = false) {
    const reader = stream.getReader();
    if (!canceled) {
      assert.deepStrictEqual(
        await reader.read(), { value: 'a', done: false });
      assert.deepStrictEqual(
        await reader.read(), { value: 'b', done: false });
    }
    assert.deepStrictEqual(
      await reader.read(), { value: undefined, done: true });
  }

  Promise.all([
    read(s1),
    read(s2, true),
  ]).then(common.mustCall());
}

{
  const error1 = new Error('boom1');
  const error2 = new Error('boom2');

  const stream = new ReadableStream({
    cancel(reason) {
      assert.deepStrictEqual(reason, [error1, error2]);
    }
  });

  const { 0: s1, 1: s2 } = stream.tee();
  s1.cancel(error1);
  s2.cancel(error2);
}

{
  const error1 = new Error('boom1');
  const error2 = new Error('boom2');

  const stream = new ReadableStream({
    cancel(reason) {
      assert.deepStrictEqual(reason, [error1, error2]);
    }
  });

  const { 0: s1, 1: s2 } = stream.tee();
  s2.cancel(error2);
  s1.cancel(error1);
}

{
  const error = new Error('boom1');

  const stream = new ReadableStream({
    cancel() {
      throw error;
    }
  });

  const { 0: s1, 1: s2 } = stream.tee();

  assert.rejects(s1.cancel(), error).then(common.mustCall());
  assert.rejects(s2.cancel(), error).then(common.mustCall());
}

{
  const error = new Error('boom1');
  let c;
  const stream = new ReadableStream({
    start(controller) {
      c = controller;
    }
  });

  const { 0: s1, 1: s2 } = stream.tee();
  c.error(error);

  assert.rejects(s1.cancel(), error).then(common.mustCall());
  assert.rejects(s2.cancel(), error).then(common.mustCall());
}

{
  const error = new Error('boom1');
  let c;
  const stream = new ReadableStream({
    start(controller) {
      c = controller;
    }
  });

  const { 0: s1, 1: s2 } = stream.tee();

  const reader1 = s1.getReader();
  const reader2 = s2.getReader();

  assert.rejects(reader1.closed, error).then(common.mustCall());
  assert.rejects(reader2.closed, error).then(common.mustCall());

  assert.rejects(reader1.read(), error).then(common.mustCall());
  assert.rejects(reader2.read(), error).then(common.mustCall());

  setImmediate(() => c.error(error));
}

{
  let pullCount = 0;
  const stream = new ReadableStream({
    pull(controller) {
      if (pullCount)
        controller.enqueue(pullCount);
      pullCount++;
    },
  });

  const reader = stream.getReader();

  queueMicrotask(common.mustCall(() => {
    assert.strictEqual(pullCount, 1);
    reader.read().then(common.mustCall(({ value, done }) => {
      assert.strictEqual(value, 1);
      assert(!done);

      reader.read().then(common.mustCall(({ value, done }) => {
        assert.strictEqual(value, 2);
        assert(!done);
      }));

    }));
  }));
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue('a');
    },
    pull: common.mustCall(),
  });

  stream.getReader().read().then(common.mustCall(({ value, done }) => {
    assert.strictEqual(value, 'a');
    assert(!done);
  }));
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue('a');
      controller.enqueue('b');
    },
    pull: common.mustCall(),
  });

  const reader = stream.getReader();
  reader.read().then(common.mustCall(({ value, done }) => {
    assert.strictEqual(value, 'a');
    assert(!done);

    reader.read().then(common.mustCall(({ value, done }) => {
      assert.strictEqual(value, 'b');
      assert(!done);
    }));
  }));
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue('a');
      controller.enqueue('b');
      controller.close();
    },
    pull: common.mustNotCall(),
  });

  const reader = stream.getReader();
  reader.read().then(common.mustCall(({ value, done }) => {
    assert.strictEqual(value, 'a');
    assert(!done);

    reader.read().then(common.mustCall(({ value, done }) => {
      assert.strictEqual(value, 'b');
      assert(!done);

      reader.read().then(common.mustCall(({ value, done }) => {
        assert.strictEqual(value, undefined);
        assert(done);
      }));

    }));
  }));
}

{
  let res;
  let promise;
  let calls = 0;
  const stream = new ReadableStream({
    pull(controller) {
      controller.enqueue(++calls);
      promise = new Promise((resolve) => res = resolve);
      return promise;
    }
  });

  const reader = stream.getReader();

  (async () => {
    await reader.read();
    assert.strictEqual(calls, 1);
    await delay();
    assert.strictEqual(calls, 1);
    res();
    await delay();
    assert.strictEqual(calls, 2);
  })().then(common.mustCall());
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue('a');
      controller.enqueue('b');
      controller.enqueue('c');
    },
    pull: common.mustCall(4),
  }, {
    highWaterMark: Infinity,
    size() { return 1; }
  });

  const reader = stream.getReader();
  (async () => {
    await delay();
    await reader.read();
    await reader.read();
    await reader.read();
  })().then(common.mustCall());
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue('a');
      controller.enqueue('b');
      controller.enqueue('c');
      controller.close();
    },
    pull: common.mustNotCall(),
  }, {
    highWaterMark: Infinity,
    size() { return 1; }
  });

  const reader = stream.getReader();
  (async () => {
    await delay();
    await reader.read();
    await reader.read();
    await reader.read();
  })().then(common.mustCall());
}

{
  let calls = 0;
  let res;
  const ready = new Promise((resolve) => res = resolve);

  new ReadableStream({
    pull(controller) {
      controller.enqueue(++calls);
      if (calls === 4)
        res();
    }
  }, {
    size() { return 1; },
    highWaterMark: 4
  });

  ready.then(common.mustCall(() => {
    assert.strictEqual(calls, 4);
  }));
}

{
  const stream = new ReadableStream({
    pull: common.mustCall((controller) => controller.close())
  });

  const reader = stream.getReader();

  reader.closed.then(common.mustCall());
}

{
  const error = new Error('boom');
  const stream = new ReadableStream({
    pull: common.mustCall((controller) => controller.error(error))
  });

  const reader = stream.getReader();

  assert.rejects(reader.closed, error).then(common.mustCall());
}

{
  const error = new Error('boom');
  const error2 = new Error('boom2');
  const stream = new ReadableStream({
    pull: common.mustCall((controller) => {
      controller.error(error);
      throw error2;
    })
  });

  const reader = stream.getReader();

  assert.rejects(reader.closed, error).then(common.mustCall());
}

{
  let startCalled = false;
  new ReadableStream({
    start: common.mustCall((controller) => {
      controller.enqueue('a');
      controller.close();
      assert.throws(() => controller.enqueue('b'), {
        code: 'ERR_INVALID_STATE'
      });
      startCalled = true;
    })
  });
  assert(startCalled);
}

{
  let startCalled = false;
  new ReadableStream({
    start: common.mustCall((controller) => {
      controller.close();
      assert.throws(() => controller.enqueue('b'), {
        code: 'ERR_INVALID_STATE'
      });
      startCalled = true;
    })
  });
  assert(startCalled);
}

{
  class Source {
    startCalled = false;
    pullCalled = false;
    cancelCalled = false;

    start(controller) {
      assert.strictEqual(this, source);
      this.startCalled = true;
      controller.enqueue('a');
    }

    pull() {
      assert.strictEqual(this, source);
      this.pullCalled = true;
    }

    cancel() {
      assert.strictEqual(this, source);
      this.cancelCalled = true;
    }
  }

  const source = new Source();

  const stream = new ReadableStream(source);
  const reader = stream.getReader();

  (async () => {
    await reader.read();
    reader.releaseLock();
    stream.cancel();
    assert(source.startCalled);
    assert(source.pullCalled);
    assert(source.cancelCalled);
  })().then(common.mustCall());
}

{
  let startCalled = false;
  new ReadableStream({
    start(controller) {
      assert.strictEqual(controller.desiredSize, 10);
      controller.close();
      assert.strictEqual(controller.desiredSize, 0);
      startCalled = true;
    }
  }, {
    highWaterMark: 10
  });
  assert(startCalled);
}

{
  let startCalled = false;
  new ReadableStream({
    start(controller) {
      assert.strictEqual(controller.desiredSize, 10);
      controller.error();
      assert.strictEqual(controller.desiredSize, null);
      startCalled = true;
    }
  }, {
    highWaterMark: 10
  });
  assert(startCalled);
}

{
  class Foo extends ReadableStream {}
  const foo = new Foo();
  foo.getReader();
}

{
  let startCalled = false;
  new ReadableStream({
    start(controller) {
      assert.strictEqual(controller.desiredSize, 1);
      controller.enqueue('a');
      assert.strictEqual(controller.desiredSize, 0);
      controller.enqueue('a');
      assert.strictEqual(controller.desiredSize, -1);
      controller.enqueue('a');
      assert.strictEqual(controller.desiredSize, -2);
      controller.enqueue('a');
      assert.strictEqual(controller.desiredSize, -3);
      startCalled = true;
    }
  });
  assert(startCalled);
}

{
  let c;
  const stream = new ReadableStream({
    start(controller) {
      c = controller;
    }
  });

  const reader = stream.getReader();

  (async () => {
    assert.strictEqual(c.desiredSize, 1);
    c.enqueue(1);
    assert.strictEqual(c.desiredSize, 0);
    await reader.read();
    assert.strictEqual(c.desiredSize, 1);
    c.enqueue(1);
    c.enqueue(1);
    assert.strictEqual(c.desiredSize, -1);
    await reader.read();
    assert.strictEqual(c.desiredSize, 0);
    await reader.read();
    assert.strictEqual(c.desiredSize, 1);
  })().then(common.mustCall());
}

{
  let c;
  new ReadableStream({
    start(controller) {
      c = controller;
    }
  });
  assert(c instanceof ReadableStreamDefaultController);
  assert.strictEqual(typeof c.desiredSize, 'number');
  assert.strictEqual(typeof c.enqueue, 'function');
  assert.strictEqual(typeof c.close, 'function');
  assert.strictEqual(typeof c.error, 'function');
}

class Source {
  constructor() {
    this.cancelCalled = false;
  }

  start(controller) {
    this.stream = createReadStream(__filename);
    this.stream.on('data', (chunk) => {
      controller.enqueue(chunk);
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
  const source = new Source();
  const stream = new ReadableStream(source);

  async function read(stream) {
    const reader = stream.getReader();
    const chunks = [];
    let read = await reader.read();
    while (!read.done) {
      chunks.push(Buffer.from(read.value));
      read = await reader.read();
    }
    return Buffer.concat(chunks);
  }

  read(stream).then(common.mustCall((data) => {
    const check = readFileSync(__filename);
    assert.deepStrictEqual(data, check);
  }));
}

{
  const source = new Source();
  const stream = new ReadableStream(source);

  async function read(stream) {
    const chunks = [];
    for await (const chunk of stream)
      chunks.push(chunk);
    return Buffer.concat(chunks);
  }

  read(stream).then(common.mustCall((data) => {
    const check = readFileSync(__filename);
    assert.deepStrictEqual(data, check);

    assert.strictEqual(stream[kState].state, 'closed');
    assert(!stream.locked);
  }));
}

{
  const source = new Source();
  const stream = new ReadableStream(source);

  [1, false, ''].forEach((options) => {
    assert.throws(() => stream.values(options), {
      code: 'ERR_INVALID_ARG_TYPE',
    });
  });

  async function read(stream) {
    for await (const _ of stream.values({ preventCancel: true }))
      return;
  }

  read(stream).then(common.mustCall((data) => {
    assert.strictEqual(stream[kState].state, 'readable');
  }));
}

{
  const source = new Source();
  const stream = new ReadableStream(source);

  async function read(stream) {
    for await (const _ of stream.values({ preventCancel: false }))
      return;
  }

  read(stream).then(common.mustCall((data) => {
    assert.strictEqual(stream[kState].state, 'closed');
  }));
}

{
  const source = new Source();
  const stream = new ReadableStream(source);

  const error = new Error('boom');

  async function read(stream) {
    // eslint-disable-next-line no-unused-vars
    for await (const _ of stream.values({ preventCancel: true }))
      throw error;
  }

  assert.rejects(read(stream), error).then(common.mustCall(() => {
    assert.strictEqual(stream[kState].state, 'readable');
  }));
}

{
  const source = new Source();
  const stream = new ReadableStream(source);

  const error = new Error('boom');

  async function read(stream) {
    // eslint-disable-next-line no-unused-vars
    for await (const _ of stream.values({ preventCancel: false }))
      throw error;
  }

  assert.rejects(read(stream), error).then(common.mustCall(() => {
    assert.strictEqual(stream[kState].state, 'closed');
  }));
}

{
  assert.throws(() => Reflect.get(ReadableStream.prototype, 'locked', {}), {
    code: 'ERR_INVALID_THIS',
  });
  assert.rejects(() => ReadableStream.prototype.cancel.call({}), {
    code: 'ERR_INVALID_THIS',
  }).then(common.mustCall());
  assert.throws(() => ReadableStream.prototype.getReader.call({}), {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => ReadableStream.prototype.tee.call({}), {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => ReadableStream.prototype.values.call({}), {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => ReadableStream.prototype[kTransfer].call({}), {
    code: 'ERR_INVALID_THIS',
  });
  assert.rejects(() => ReadableStreamDefaultReader.prototype.read.call({}), {
    code: 'ERR_INVALID_THIS',
  }).then(common.mustCall());
  assert.rejects(() => ReadableStreamDefaultReader.prototype.cancel.call({}), {
    code: 'ERR_INVALID_THIS',
  }).then(common.mustCall());
  assert.rejects(() => {
    return Reflect.get(ReadableStreamDefaultReader.prototype, 'closed');
  }, {
    code: 'ERR_INVALID_THIS',
  }).then(common.mustCall());
  assert.throws(() => {
    ReadableStreamDefaultReader.prototype.releaseLock.call({});
  }, {
    code: 'ERR_INVALID_THIS',
  });
  assert.rejects(() => ReadableStreamBYOBReader.prototype.read.call({}), {
    code: 'ERR_INVALID_THIS',
  }).then(common.mustCall());
  assert.throws(() => {
    ReadableStreamBYOBReader.prototype.releaseLock.call({});
  }, {
    code: 'ERR_INVALID_THIS',
  });
  assert.rejects(() => {
    return Reflect.get(ReadableStreamBYOBReader.prototype, 'closed');
  }, {
    code: 'ERR_INVALID_THIS',
  }).then(common.mustCall());
  assert.rejects(() => ReadableStreamBYOBReader.prototype.cancel.call({}), {
    code: 'ERR_INVALID_THIS',
  }).then(common.mustCall());

  assert.throws(() => {
    Reflect.get(ReadableByteStreamController.prototype, 'byobRequest', {});
  }, {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => {
    Reflect.get(ReadableByteStreamController.prototype, 'desiredSize', {});
  }, {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => {
    ReadableByteStreamController.prototype.close.call({});
  }, {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => {
    ReadableByteStreamController.prototype.enqueue.call({});
  }, {
    code: 'ERR_INVALID_THIS',
  });
  assert.throws(() => {
    ReadableByteStreamController.prototype.error.call({});
  }, {
    code: 'ERR_INVALID_THIS',
  });

  assert.throws(() => new ReadableStreamBYOBRequest(), {
    code: 'ERR_ILLEGAL_CONSTRUCTOR',
  });

  assert.throws(() => new ReadableStreamDefaultController(), {
    code: 'ERR_ILLEGAL_CONSTRUCTOR',
  });

  assert.throws(() => new ReadableByteStreamController(), {
    code: 'ERR_ILLEGAL_CONSTRUCTOR',
  });
}

{
  let controller;
  const readable = new ReadableStream({
    start(c) { controller = c; }
  });

  assert.strictEqual(
    inspect(readable),
    'ReadableStream { locked: false, state: \'readable\', ' +
    'supportsBYOB: false }');
  assert.strictEqual(
    inspect(readable, { depth: null }),
    'ReadableStream { locked: false, state: \'readable\', ' +
    'supportsBYOB: false }');
  assert.strictEqual(
    inspect(readable, { depth: 0 }),
    'ReadableStream [Object]');

  assert.strictEqual(
    inspect(controller),
    'ReadableStreamDefaultController {}');
  assert.strictEqual(
    inspect(controller, { depth: null }),
    'ReadableStreamDefaultController {}');
  assert.strictEqual(
    inspect(controller, { depth: 0 }),
    'ReadableStreamDefaultController {}');

  const reader = readable.getReader();

  assert.match(
    inspect(reader),
    /ReadableStreamDefaultReader/);
  assert.match(
    inspect(reader, { depth: null }),
    /ReadableStreamDefaultReader/);
  assert.match(
    inspect(reader, { depth: 0 }),
    /ReadableStreamDefaultReader/);

  assert.rejects(readableStreamPipeTo(1), {
    code: 'ERR_INVALID_ARG_TYPE',
  }).then(common.mustCall());

  assert.rejects(readableStreamPipeTo(new ReadableStream(), 1), {
    code: 'ERR_INVALID_ARG_TYPE',
  }).then(common.mustCall());

  assert.rejects(
    readableStreamPipeTo(
      new ReadableStream(),
      new WritableStream(),
      false,
      false,
      false,
      {}),
    {
      code: 'ERR_INVALID_ARG_TYPE',
    }).then(common.mustCall());
}

{
  const readable = new ReadableStream();
  const reader = readable.getReader();
  reader.releaseLock();
  reader.releaseLock();
  assert.rejects(reader.read(), {
    code: 'ERR_INVALID_STATE',
  }).then(common.mustCall());
  assert.rejects(reader.cancel(), {
    code: 'ERR_INVALID_STATE',
  }).then(common.mustCall());
}

{
  // Test tee() cloneForBranch2 argument
  const readable = new ReadableStream({
    start(controller) {
      controller.enqueue('hello');
    }
  });
  const [r1, r2] = readableStreamTee(readable, true);
  r1.getReader().read().then(
    common.mustCall(({ value }) => assert.strictEqual(value, 'hello')));
  r2.getReader().read().then(
    common.mustCall(({ value }) => assert.strictEqual(value, 'hello')));
}

{
  assert.throws(() => {
    readableByteStreamControllerConvertPullIntoDescriptor({
      bytesFilled: 10,
      byteLength: 5
    });
  }, {
    code: 'ERR_INVALID_STATE',
  });
}

{
  let controller;
  const readable = new ReadableStream({
    start(c) { controller = c; }
  });

  controller[kState].pendingPullIntos = [{}];
  assert.throws(() => readableByteStreamControllerRespond(controller, 0), {
    code: 'ERR_INVALID_ARG_VALUE',
  });

  readable.cancel().then(common.mustCall());

  assert.throws(() => readableByteStreamControllerRespond(controller, 1), {
    code: 'ERR_INVALID_ARG_VALUE',
  });

  assert(!readableStreamDefaultControllerCanCloseOrEnqueue(controller));
  readableStreamDefaultControllerEnqueue(controller);
  readableByteStreamControllerClose(controller);
  readableByteStreamControllerEnqueue(controller, new Uint8Array(1));
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue('a');
      controller.close();
    },
    pull: common.mustNotCall(),
  });

  const reader = stream.getReader();
  (async () => {
    isDisturbed(stream, false);
    await reader.read();
    isDisturbed(stream, true);
  })().then(common.mustCall());
}

{
  const stream = new ReadableStream({
    start(controller) {
      controller.close();
    },
    pull: common.mustNotCall(),
  });

  const reader = stream.getReader();
  (async () => {
    isDisturbed(stream, false);
    await reader.read();
    isDisturbed(stream, true);
  })().then(common.mustCall());
}

{
  const stream = new ReadableStream({
    start(controller) {
    },
    pull: common.mustNotCall(),
  });
  stream.cancel();

  const reader = stream.getReader();
  (async () => {
    isDisturbed(stream, false);
    await reader.read();
    isDisturbed(stream, true);
  })().then(common.mustCall());
}

{
  const stream = new ReadableStream({
    pull: common.mustCall((controller) => {
      controller.error(new Error());
    }),
  });

  const reader = stream.getReader();
  (async () => {
    isErrored(stream, false);
    await reader.read().catch(common.mustCall());
    isErrored(stream, true);
  })().then(common.mustCall());
}

{
  const stream = new ReadableStream({
    pull: common.mustCall((controller) => {
      controller.error(new Error());
    }),
  });

  const reader = stream.getReader();
  (async () => {
    isReadable(stream, true);
    await reader.read().catch(common.mustCall());
    isReadable(stream, false);
  })().then(common.mustCall());
}

{
  const stream = new ReadableStream({
    type: 'bytes',
    start(controller) {
      controller.close();
    }
  });

  const buffer = new ArrayBuffer(1024);
  const reader = stream.getReader({ mode: 'byob' });

  reader.read(new DataView(buffer))
    .then(common.mustCall());
}

{
  const stream = new ReadableStream({
    type: 'bytes',
    autoAllocateChunkSize: 128,
    pull: common.mustCall((controller) => {
      const view = controller.byobRequest.view;
      const dest = new Uint8Array(
        view.buffer,
        view.byteOffset,
        view.byteLength
      );
      dest.fill(1);
      controller.byobRequest.respondWithNewView(dest);
    }),
  });

  const reader = stream.getReader({ mode: 'byob' });

  const buffer = new ArrayBuffer(10);
  const view = new Uint8Array(
    buffer,
    1,
    3
  );

  reader.read(view).then(common.mustCall(({ value }) => {
    assert.deepStrictEqual(value, new Uint8Array([1, 1, 1]));
  }));
}

// Initial Pull Delay
{
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue('data');
      controller.close();
    }
  });

  const iterator = stream.values();

  let microtaskCompleted = false;
  Promise.resolve().then(() => { microtaskCompleted = true; });

  iterator.next().then(common.mustCall(({ done, value }) => {
    assert.strictEqual(done, false);
    assert.strictEqual(value, 'data');
    assert.strictEqual(microtaskCompleted, true);
  }));
}

// Avoiding Prototype Pollution
{
  const stream = new ReadableStream({
    start(controller) {
      controller.enqueue('data');
      controller.close();
    }
  });

  const iterator = stream.values();

  // Modify Promise.prototype.then to simulate prototype pollution
  const originalThen = Promise.prototype.then;
  Promise.prototype.then = function(onFulfilled, onRejected) {
    return originalThen.call(this, onFulfilled, onRejected);
  };

  iterator.next().then(common.mustCall(({ done, value }) => {
    assert.strictEqual(done, false);
    assert.strictEqual(value, 'data');

    // Restore original then method
    Promise.prototype.then = originalThen;
  }));
}
