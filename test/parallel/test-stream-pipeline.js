'use strict';

const common = require('../common');
const {
  Stream,
  Writable,
  Readable,
  Transform,
  pipeline,
  PassThrough,
  Duplex,
  addAbortSignal,
} = require('stream');
const pipelinep = require('stream/promises').pipeline;
const assert = require('assert');
const http = require('http');
const { promisify } = require('util');
const net = require('net');
const tsp = require('timers/promises');
const tmpdir = require('../common/tmpdir');
const fs = require('fs');

{
  let finished = false;
  const processed = [];
  const expected = [
    Buffer.from('a'),
    Buffer.from('b'),
    Buffer.from('c'),
  ];

  const read = new Readable({
    read() {}
  });

  const write = new Writable({
    write(data, enc, cb) {
      processed.push(data);
      cb();
    }
  });

  write.on('finish', () => {
    finished = true;
  });

  for (let i = 0; i < expected.length; i++) {
    read.push(expected[i]);
  }
  read.push(null);

  pipeline(read, write, common.mustSucceed(() => {
    assert.ok(finished);
    assert.deepStrictEqual(processed, expected);
  }));
}

{
  const read = new Readable({
    read() {}
  });

  assert.throws(() => {
    pipeline(read, () => {});
  }, /ERR_MISSING_ARGS/);
  assert.throws(() => {
    pipeline(() => {});
  }, /ERR_MISSING_ARGS/);
  assert.throws(() => {
    pipeline();
  }, /ERR_INVALID_ARG_TYPE/);
}

tmpdir.refresh();
{
  assert.rejects(async () => {
    const read = fs.createReadStream(__filename);
    const write = fs.createWriteStream(tmpdir.resolve('a'));
    const close = promisify(write.close);
    await close.call(write);
    await pipelinep(read, write);
  }, /ERR_STREAM_UNABLE_TO_PIPE/).then(common.mustCall());
}

{
  const read = new Readable({
    read() {}
  });

  const write = new Writable({
    write(data, enc, cb) {
      cb();
    }
  });

  read.push('data');
  setImmediate(() => read.destroy());

  pipeline(read, write, common.mustCall((err) => {
    assert.ok(err, 'should have an error');
  }));
}

{
  const read = new Readable({
    read() {}
  });

  const write = new Writable({
    write(data, enc, cb) {
      cb();
    }
  });

  read.push('data');
  setImmediate(() => read.destroy(new Error('kaboom')));

  const dst = pipeline(read, write, common.mustCall((err) => {
    assert.deepStrictEqual(err, new Error('kaboom'));
  }));

  assert.strictEqual(dst, write);
}

{
  const read = new Readable({
    read() {}
  });

  const transform = new Transform({
    transform(data, enc, cb) {
      cb(new Error('kaboom'));
    }
  });

  const write = new Writable({
    write(data, enc, cb) {
      cb();
    }
  });

  read.on('close', common.mustCall());
  transform.on('close', common.mustCall());
  write.on('close', common.mustCall());

  [read, transform, write].forEach((stream) => {
    stream.on('error', common.mustCall((err) => {
      assert.deepStrictEqual(err, new Error('kaboom'));
    }));
  });

  const dst = pipeline(read, transform, write, common.mustCall((err) => {
    assert.deepStrictEqual(err, new Error('kaboom'));
  }));

  assert.strictEqual(dst, write);

  read.push('hello');
}

{
  const server = http.createServer((req, res) => {
    const rs = new Readable({
      read() {
        rs.push('hello');
        rs.push(null);
      }
    });

    pipeline(rs, res, () => {});
  });

  server.listen(0, () => {
    const req = http.request({
      port: server.address().port
    });

    req.end();
    req.on('response', (res) => {
      const buf = [];
      res.on('data', (data) => buf.push(data));
      res.on('end', common.mustCall(() => {
        assert.deepStrictEqual(
          Buffer.concat(buf),
          Buffer.from('hello')
        );
        server.close();
      }));
    });
  });
}

{
  const server = http.createServer((req, res) => {
    let sent = false;
    const rs = new Readable({
      read() {
        if (sent) {
          return;
        }
        sent = true;
        rs.push('hello');
      },
      destroy: common.mustCall((err, cb) => {
        // Prevents fd leaks by destroying http pipelines
        cb();
      })
    });

    pipeline(rs, res, () => {});
  });

  server.listen(0, () => {
    const req = http.request({
      port: server.address().port
    });

    req.end();
    req.on('response', (res) => {
      setImmediate(() => {
        res.destroy();
        server.close();
      });
    });
  });
}

{
  const server = http.createServer((req, res) => {
    let sent = 0;
    const rs = new Readable({
      read() {
        if (sent++ > 10) {
          return;
        }
        rs.push('hello');
      },
      destroy: common.mustCall((err, cb) => {
        cb();
      })
    });

    pipeline(rs, res, () => {});
  });

  let cnt = 10;

  const badSink = new Writable({
    write(data, enc, cb) {
      cnt--;
      if (cnt === 0) cb(new Error('kaboom'));
      else cb();
    }
  });

  server.listen(0, () => {
    const req = http.request({
      port: server.address().port
    });

    req.end();
    req.on('response', (res) => {
      pipeline(res, badSink, common.mustCall((err) => {
        assert.deepStrictEqual(err, new Error('kaboom'));
        server.close();
      }));
    });
  });
}

{
  const server = http.createServer((req, res) => {
    pipeline(req, res, common.mustSucceed());
  });

  server.listen(0, () => {
    const req = http.request({
      port: server.address().port
    });

    let sent = 0;
    const rs = new Readable({
      read() {
        if (sent++ > 10) {
          return;
        }
        rs.push('hello');
      }
    });

    pipeline(rs, req, common.mustCall(() => {
      server.close();
    }));

    req.on('response', (res) => {
      let cnt = 10;
      res.on('data', () => {
        cnt--;
        if (cnt === 0) rs.destroy();
      });
    });
  });
}

{
  const makeTransform = () => {
    const tr = new Transform({
      transform(data, enc, cb) {
        cb(null, data);
      }
    });

    tr.on('close', common.mustCall());
    return tr;
  };

  const rs = new Readable({
    read() {
      rs.push('hello');
    }
  });

  let cnt = 10;

  const ws = new Writable({
    write(data, enc, cb) {
      cnt--;
      if (cnt === 0) return cb(new Error('kaboom'));
      cb();
    }
  });

  rs.on('close', common.mustCall());
  ws.on('close', common.mustCall());

  pipeline(
    rs,
    makeTransform(),
    makeTransform(),
    makeTransform(),
    makeTransform(),
    makeTransform(),
    makeTransform(),
    ws,
    common.mustCall((err) => {
      assert.deepStrictEqual(err, new Error('kaboom'));
    })
  );
}

{
  const oldStream = new Stream();

  oldStream.pause = oldStream.resume = () => {};
  oldStream.write = (data) => {
    oldStream.emit('data', data);
    return true;
  };
  oldStream.end = () => {
    oldStream.emit('end');
  };

  const expected = [
    Buffer.from('hello'),
    Buffer.from('world'),
  ];

  const rs = new Readable({
    read() {
      for (let i = 0; i < expected.length; i++) {
        rs.push(expected[i]);
      }
      rs.push(null);
    }
  });

  const ws = new Writable({
    write(data, enc, cb) {
      assert.deepStrictEqual(data, expected.shift());
      cb();
    }
  });

  let finished = false;

  ws.on('finish', () => {
    finished = true;
  });

  pipeline(
    rs,
    oldStream,
    ws,
    common.mustSucceed(() => {
      assert(finished, 'last stream finished');
    })
  );
}

{
  const oldStream = new Stream();

  oldStream.pause = oldStream.resume = () => {};
  oldStream.write = (data) => {
    oldStream.emit('data', data);
    return true;
  };
  oldStream.end = () => {
    oldStream.emit('end');
  };

  const destroyableOldStream = new Stream();

  destroyableOldStream.pause = destroyableOldStream.resume = () => {};
  destroyableOldStream.destroy = common.mustCall(() => {
    destroyableOldStream.emit('close');
  });
  destroyableOldStream.write = (data) => {
    destroyableOldStream.emit('data', data);
    return true;
  };
  destroyableOldStream.end = () => {
    destroyableOldStream.emit('end');
  };

  const rs = new Readable({
    read() {
      rs.destroy(new Error('stop'));
    }
  });

  const ws = new Writable({
    write(data, enc, cb) {
      cb();
    }
  });

  let finished = false;

  ws.on('finish', () => {
    finished = true;
  });

  pipeline(
    rs,
    oldStream,
    destroyableOldStream,
    ws,
    common.mustCall((err) => {
      assert.deepStrictEqual(err, new Error('stop'));
      assert(!finished, 'should not finish');
    })
  );
}

{
  const pipelinePromise = promisify(pipeline);

  async function run() {
    const read = new Readable({
      read() {}
    });

    const write = new Writable({
      write(data, enc, cb) {
        cb();
      }
    });

    read.push('data');
    read.push(null);

    let finished = false;

    write.on('finish', () => {
      finished = true;
    });

    await pipelinePromise(read, write);

    assert(finished);
  }

  run();
}

{
  // Check aborted signal without values
  const pipelinePromise = promisify(pipeline);
  async function run() {
    const ac = new AbortController();
    const { signal } = ac;
    async function* producer() {
      ac.abort();
      await Promise.resolve();
      yield '8';
    }

    const w = new Writable({
      write(chunk, encoding, callback) {
        callback();
      }
    });
    await pipelinePromise(producer, w, { signal });
  }

  assert.rejects(run, { name: 'AbortError' }).then(common.mustCall());
}

{
  // Check aborted signal after init.
  const pipelinePromise = promisify(pipeline);
  async function run() {
    const ac = new AbortController();
    const { signal } = ac;
    async function* producer() {
      yield '5';
      await Promise.resolve();
      ac.abort();
      await Promise.resolve();
      yield '8';
    }

    const w = new Writable({
      write(chunk, encoding, callback) {
        callback();
      }
    });
    await pipelinePromise(producer, w, { signal });
  }

  assert.rejects(run, { name: 'AbortError' }).then(common.mustCall());
}

{
  // Check pre-aborted signal
  const pipelinePromise = promisify(pipeline);
  async function run() {
    const signal = AbortSignal.abort();
    async function* producer() {
      yield '5';
      await Promise.resolve();
      yield '8';
    }

    const w = new Writable({
      write(chunk, encoding, callback) {
        callback();
      }
    });
    await pipelinePromise(producer, w, { signal });
  }

  assert.rejects(run, { name: 'AbortError' }).then(common.mustCall());
}

{
  const read = new Readable({
    read() {}
  });

  const transform = new Transform({
    transform(data, enc, cb) {
      cb(new Error('kaboom'));
    }
  });

  const write = new Writable({
    write(data, enc, cb) {
      cb();
    }
  });

  assert.throws(
    () => pipeline(read, transform, write),
    { code: 'ERR_INVALID_ARG_TYPE' }
  );
}

{
  const server = http.Server(function(req, res) {
    res.write('asd');
  });
  server.listen(0, function() {
    http.get({ port: this.address().port }, (res) => {
      const stream = new PassThrough();

      stream.on('error', common.mustCall());

      pipeline(
        res,
        stream,
        common.mustCall((err) => {
          assert.strictEqual(err.message, 'oh no');
          server.close();
        })
      );

      stream.destroy(new Error('oh no'));
    }).on('error', common.mustNotCall());
  });
}

{
  let res = '';
  const w = new Writable({
    write(chunk, encoding, callback) {
      res += chunk;
      callback();
    }
  });
  pipeline(function*() {
    yield 'hello';
    yield 'world';
  }(), w, common.mustSucceed(() => {
    assert.strictEqual(res, 'helloworld');
  }));
}

{
  let res = '';
  const w = new Writable({
    write(chunk, encoding, callback) {
      res += chunk;
      callback();
    }
  });
  pipeline(async function*() {
    await Promise.resolve();
    yield 'hello';
    yield 'world';
  }(), w, common.mustSucceed(() => {
    assert.strictEqual(res, 'helloworld');
  }));
}

{
  let res = '';
  const w = new Writable({
    write(chunk, encoding, callback) {
      res += chunk;
      callback();
    }
  });
  pipeline(function*() {
    yield 'hello';
    yield 'world';
  }, w, common.mustSucceed(() => {
    assert.strictEqual(res, 'helloworld');
  }));
}

{
  let res = '';
  const w = new Writable({
    write(chunk, encoding, callback) {
      res += chunk;
      callback();
    }
  });
  pipeline(async function*() {
    await Promise.resolve();
    yield 'hello';
    yield 'world';
  }, w, common.mustSucceed(() => {
    assert.strictEqual(res, 'helloworld');
  }));
}

{
  let res = '';
  pipeline(async function*() {
    await Promise.resolve();
    yield 'hello';
    yield 'world';
  }, async function*(source) {
    for await (const chunk of source) {
      yield chunk.toUpperCase();
    }
  }, async function(source) {
    for await (const chunk of source) {
      res += chunk;
    }
  }, common.mustSucceed(() => {
    assert.strictEqual(res, 'HELLOWORLD');
  }));
}

{
  pipeline(async function*() {
    await Promise.resolve();
    yield 'hello';
    yield 'world';
  }, async function*(source) {
    for await (const chunk of source) {
      yield chunk.toUpperCase();
    }
  }, async function(source) {
    let ret = '';
    for await (const chunk of source) {
      ret += chunk;
    }
    return ret;
  }, common.mustSucceed((val) => {
    assert.strictEqual(val, 'HELLOWORLD');
  }));
}

{
  // AsyncIterable destination is returned and finalizes.

  const ret = pipeline(async function*() {
    await Promise.resolve();
    yield 'hello';
  }, async function*(source) { // eslint-disable-line require-yield
    for await (const chunk of source) { } // eslint-disable-line no-unused-vars, no-empty
  }, common.mustCall((err) => {
    assert.strictEqual(err, undefined);
  }));
  ret.resume();
  assert.strictEqual(typeof ret.pipe, 'function');
}

{
  // AsyncFunction destination is not returned and error is
  // propagated.

  const ret = pipeline(async function*() { // eslint-disable-line require-yield
    await Promise.resolve();
    throw new Error('kaboom');
  }, async function*(source) { // eslint-disable-line require-yield
    for await (const chunk of source) { } // eslint-disable-line no-unused-vars, no-empty
  }, common.mustCall((err) => {
    assert.strictEqual(err.message, 'kaboom');
  }));
  ret.resume();
  assert.strictEqual(typeof ret.pipe, 'function');
}

{
  const s = new PassThrough();
  pipeline(async function*() { // eslint-disable-line require-yield
    throw new Error('kaboom');
  }, s, common.mustCall((err) => {
    assert.strictEqual(err.message, 'kaboom');
    assert.strictEqual(s.destroyed, true);
  }));
}

{
  const s = new PassThrough();
  pipeline(async function*() { // eslint-disable-line require-yield
    throw new Error('kaboom');
  }(), s, common.mustCall((err) => {
    assert.strictEqual(err.message, 'kaboom');
    assert.strictEqual(s.destroyed, true);
  }));
}

{
  const s = new PassThrough();
  pipeline(function*() { // eslint-disable-line require-yield
    throw new Error('kaboom');
  }, s, common.mustCall((err, val) => {
    assert.strictEqual(err.message, 'kaboom');
    assert.strictEqual(s.destroyed, true);
  }));
}

{
  const s = new PassThrough();
  pipeline(function*() { // eslint-disable-line require-yield
    throw new Error('kaboom');
  }(), s, common.mustCall((err, val) => {
    assert.strictEqual(err.message, 'kaboom');
    assert.strictEqual(s.destroyed, true);
  }));
}

{
  const s = new PassThrough();
  pipeline(async function*() {
    await Promise.resolve();
    yield 'hello';
    yield 'world';
  }, s, async function(source) {
    for await (const chunk of source) { // eslint-disable-line no-unused-vars
      throw new Error('kaboom');
    }
  }, common.mustCall((err, val) => {
    assert.strictEqual(err.message, 'kaboom');
    assert.strictEqual(s.destroyed, true);
  }));
}

{
  const s = new PassThrough();
  const ret = pipeline(function() {
    return ['hello', 'world'];
  }, s, async function*(source) { // eslint-disable-line require-yield
    for await (const chunk of source) { // eslint-disable-line no-unused-vars
      throw new Error('kaboom');
    }
  }, common.mustCall((err) => {
    assert.strictEqual(err.message, 'kaboom');
    assert.strictEqual(s.destroyed, true);
  }));
  ret.resume();
  assert.strictEqual(typeof ret.pipe, 'function');
}

{
  // Legacy streams without async iterator.

  const s = new PassThrough();
  s.push('asd');
  s.push(null);
  s[Symbol.asyncIterator] = null;
  let ret = '';
  pipeline(s, async function(source) {
    for await (const chunk of source) {
      ret += chunk;
    }
  }, common.mustCall((err) => {
    assert.strictEqual(err, undefined);
    assert.strictEqual(ret, 'asd');
  }));
}

{
  // v1 streams without read().

  const s = new Stream();
  process.nextTick(() => {
    s.emit('data', 'asd');
    s.emit('end');
  });
  // 'destroyer' can be called multiple times,
  // once from stream wrapper and
  // once from iterator wrapper.
  s.close = common.mustCallAtLeast(1);
  let ret = '';
  pipeline(s, async function(source) {
    for await (const chunk of source) {
      ret += chunk;
    }
  }, common.mustCall((err) => {
    assert.strictEqual(err, undefined);
    assert.strictEqual(ret, 'asd');
  }));
}

{
  // v1 error streams without read().

  const s = new Stream();
  process.nextTick(() => {
    s.emit('error', new Error('kaboom'));
  });
  s.destroy = common.mustCall();
  pipeline(s, async function(source) {
  }, common.mustCall((err) => {
    assert.strictEqual(err.message, 'kaboom');
  }));
}

{
  const s = new PassThrough();
  assert.throws(() => {
    pipeline(function(source) {
    }, s, () => {});
  }, (err) => {
    assert.strictEqual(err.code, 'ERR_INVALID_RETURN_VALUE');
    assert.strictEqual(s.destroyed, false);
    return true;
  });
}

{
  const s = new PassThrough();
  assert.throws(() => {
    pipeline(s, function(source) {
    }, s, () => {});
  }, (err) => {
    assert.strictEqual(err.code, 'ERR_INVALID_RETURN_VALUE');
    assert.strictEqual(s.destroyed, false);
    return true;
  });
}

{
  const s = new PassThrough();
  assert.throws(() => {
    pipeline(s, function(source) {
    }, () => {});
  }, (err) => {
    assert.strictEqual(err.code, 'ERR_INVALID_RETURN_VALUE');
    assert.strictEqual(s.destroyed, false);
    return true;
  });
}

{
  const s = new PassThrough();
  assert.throws(() => {
    pipeline(s, function*(source) {
    }, () => {});
  }, (err) => {
    assert.strictEqual(err.code, 'ERR_INVALID_RETURN_VALUE');
    assert.strictEqual(s.destroyed, false);
    return true;
  });
}

{
  let res = '';
  pipeline(async function*() {
    await Promise.resolve();
    yield 'hello';
    yield 'world';
  }, new Transform({
    transform(chunk, encoding, cb) {
      cb(new Error('kaboom'));
    }
  }), async function(source) {
    for await (const chunk of source) {
      res += chunk;
    }
  }, common.mustCall((err) => {
    assert.strictEqual(err.message, 'kaboom');
    assert.strictEqual(res, '');
  }));
}

{
  let res = '';
  pipeline(async function*() {
    await Promise.resolve();
    yield 'hello';
    yield 'world';
  }, new Transform({
    transform(chunk, encoding, cb) {
      process.nextTick(cb, new Error('kaboom'));
    }
  }), async function(source) {
    for await (const chunk of source) {
      res += chunk;
    }
  }, common.mustCall((err) => {
    assert.strictEqual(err.message, 'kaboom');
    assert.strictEqual(res, '');
  }));
}

{
  let res = '';
  pipeline(async function*() {
    await Promise.resolve();
    yield 'hello';
    yield 'world';
  }, new Transform({
    decodeStrings: false,
    transform(chunk, encoding, cb) {
      cb(null, chunk.toUpperCase());
    }
  }), async function(source) {
    for await (const chunk of source) {
      res += chunk;
    }
  }, common.mustSucceed(() => {
    assert.strictEqual(res, 'HELLOWORLD');
  }));
}

{
  // Ensure no unhandled rejection from async function.

  pipeline(async function*() {
    yield 'hello';
  }, async function(source) {
    throw new Error('kaboom');
  }, common.mustCall((err) => {
    assert.strictEqual(err.message, 'kaboom');
  }));
}

{
  const src = new PassThrough({ autoDestroy: false });
  const dst = new PassThrough({ autoDestroy: false });
  pipeline(src, dst, common.mustCall(() => {
    assert.strictEqual(src.destroyed, false);
    assert.strictEqual(dst.destroyed, false);
  }));
  src.end();
}

{
  // Make sure 'close' before 'end' finishes without error
  // if readable has received eof.
  // Ref: https://github.com/nodejs/node/issues/29699
  const r = new Readable();
  const w = new Writable({
    write(chunk, encoding, cb) {
      cb();
    }
  });
  pipeline(r, w, (err) => {
    assert.strictEqual(err, undefined);
  });
  r.push('asd');
  r.push(null);
  r.emit('close');
}

{
  const server = http.createServer((req, res) => {
  });

  server.listen(0, () => {
    const req = http.request({
      port: server.address().port
    });

    const body = new PassThrough();
    pipeline(
      body,
      req,
      common.mustSucceed(() => {
        assert(!req.res);
        assert(!req.aborted);
        req.abort();
        server.close();
      })
    );
    body.end();
  });
}

{
  const src = new PassThrough();
  const dst = new PassThrough();
  pipeline(src, dst, common.mustSucceed(() => {
    assert.strictEqual(dst.destroyed, false);
  }));
  src.end();
}

{
  const src = new PassThrough();
  const dst = new PassThrough();
  dst.readable = false;
  pipeline(src, dst, common.mustSucceed(() => {
    assert.strictEqual(dst.destroyed, true);
  }));
  src.end();
}

{
  let res = '';
  const rs = new Readable({
    read() {
      setImmediate(() => {
        rs.push('hello');
      });
    }
  });
  const ws = new Writable({
    write: common.mustNotCall()
  });
  pipeline(rs, async function*(stream) { // eslint-disable-line require-yield
    for await (const chunk of stream) { // eslint-disable-line no-unused-vars
      throw new Error('kaboom');
    }
  }, async function *(source) { // eslint-disable-line require-yield
    for await (const chunk of source) {
      res += chunk;
    }
  }, ws, common.mustCall((err) => {
    assert.strictEqual(err.message, 'kaboom');
    assert.strictEqual(res, '');
  }));
}

{
  const server = http.createServer((req, res) => {
    req.socket.on('error', common.mustNotCall());
    pipeline(req, new PassThrough(), (err) => {
      assert.ifError(err);
      res.end();
      server.close();
    });
  });

  server.listen(0, () => {
    const req = http.request({
      method: 'PUT',
      port: server.address().port
    });
    req.end('asd123');
    req.on('response', common.mustCall());
    req.on('error', common.mustNotCall());
  });
}

{
  // Might still want to be able to use the writable side
  // of src. This is in the case where e.g. the Duplex input
  // is not directly connected to its output. Such a case could
  // happen when the Duplex is reading from a socket and then echos
  // the data back on the same socket.
  const src = new PassThrough();
  assert.strictEqual(src.writable, true);
  const dst = new PassThrough();
  pipeline(src, dst, common.mustCall((err) => {
    assert.strictEqual(src.writable, true);
    assert.strictEqual(src.destroyed, false);
  }));
  src.push(null);
}

{
  const src = new PassThrough();
  const dst = pipeline(
    src,
    async function * (source) {
      for await (const chunk of source) {
        yield chunk;
      }
    },
    common.mustCall((err) => {
      assert.strictEqual(err.code, 'ERR_STREAM_PREMATURE_CLOSE');
    })
  );
  src.push('asd');
  dst.destroy();
}

{
  pipeline(async function * () {
    yield 'asd';
  }, async function * (source) {
    for await (const chunk of source) {
      yield { chunk };
    }
  }, common.mustSucceed());
}

{
  let closed = false;
  const src = new Readable({
    read() {},
    destroy(err, cb) {
      process.nextTick(cb);
    }
  });
  const dst = new Writable({
    write(chunk, encoding, callback) {
      callback();
    }
  });
  src.on('close', () => {
    closed = true;
  });
  src.push(null);
  pipeline(src, dst, common.mustCall((err) => {
    assert.strictEqual(closed, true);
  }));
}

{
  let closed = false;
  const src = new Readable({
    read() {},
    destroy(err, cb) {
      process.nextTick(cb);
    }
  });
  const dst = new Duplex({});
  src.on('close', common.mustCall(() => {
    closed = true;
  }));
  src.push(null);
  pipeline(src, dst, common.mustCall((err) => {
    assert.strictEqual(closed, true);
  }));
}

{
  const server = net.createServer(common.mustCall((socket) => {
    // echo server
    pipeline(socket, socket, common.mustSucceed());
    // 13 force destroys the socket before it has a chance to emit finish
    socket.on('finish', common.mustCall(() => {
      server.close();
    }));
  })).listen(0, common.mustCall(() => {
    const socket = net.connect(server.address().port);
    socket.end();
  }));
}

{
  const d = new Duplex({
    autoDestroy: false,
    write: common.mustCall((data, enc, cb) => {
      d.push(data);
      cb();
    }),
    read: common.mustCall(() => {
      d.push(null);
    }),
    final: common.mustCall((cb) => {
      setTimeout(() => {
        assert.strictEqual(d.destroyed, false);
        cb();
      }, 1000);
    }),
    destroy: common.mustNotCall()
  });

  const sink = new Writable({
    write: common.mustCall((data, enc, cb) => {
      cb();
    })
  });

  pipeline(d, sink, common.mustSucceed());

  d.write('test');
  d.end();
}

{
  const server = net.createServer(common.mustCall((socket) => {
    // echo server
    pipeline(socket, socket, common.mustSucceed());
    socket.on('finish', common.mustCall(() => {
      server.close();
    }));
  })).listen(0, common.mustCall(() => {
    const socket = net.connect(server.address().port);
    socket.end();
  }));
}

{
  const d = new Duplex({
    autoDestroy: false,
    write: common.mustCall((data, enc, cb) => {
      d.push(data);
      cb();
    }),
    read: common.mustCall(() => {
      d.push(null);
    }),
    final: common.mustCall((cb) => {
      setTimeout(() => {
        assert.strictEqual(d.destroyed, false);
        cb();
      }, 1000);
    }),
    // `destroy()` won't be invoked by pipeline since
    // the writable side has not completed when
    // the pipeline has completed.
    destroy: common.mustNotCall()
  });

  const sink = new Writable({
    write: common.mustCall((data, enc, cb) => {
      cb();
    })
  });

  pipeline(d, sink, common.mustSucceed());

  d.write('test');
  d.end();
}

{
  const r = new Readable({
    read() {}
  });
  r.push('hello');
  r.push('world');
  r.push(null);
  let res = '';
  const w = new Writable({
    write(chunk, encoding, callback) {
      res += chunk;
      callback();
    }
  });
  pipeline([r, w], common.mustSucceed(() => {
    assert.strictEqual(res, 'helloworld');
  }));
}

{
  let flushed = false;
  const makeStream = () =>
    new Transform({
      transform: (chunk, enc, cb) => cb(null, chunk),
      flush: (cb) =>
        setTimeout(() => {
          flushed = true;
          cb(null);
        }, 1),
    });

  const input = new Readable();
  input.push(null);

  pipeline(
    input,
    makeStream(),
    common.mustCall(() => {
      assert.strictEqual(flushed, true);
    }),
  );
}
{
  function createThenable() {
    let counter = 0;
    return {
      get then() {
        if (counter++) {
          throw new Error('Cannot access `then` more than once');
        }
        return Function.prototype;
      },
    };
  }

  pipeline(
    function* () {
      yield 0;
    },
    createThenable,
    () => common.mustNotCall(),
  );
}


{
  const ac = new AbortController();
  const reason = new Error('Reason');
  const r = Readable.from(async function* () {
    for (let i = 0; i < 10; i++) {
      await Promise.resolve();
      yield String(i);
      if (i === 5) {
        ac.abort(reason);
      }
    }
  }());
  let res = '';
  const w = new Writable({
    write(chunk, encoding, callback) {
      res += chunk;
      callback();
    }
  });
  const cb = common.mustCall((err) => {
    assert.strictEqual(err.name, 'AbortError');
    assert.strictEqual(err.cause, reason);
    assert.strictEqual(res, '012345');
    assert.strictEqual(w.destroyed, true);
    assert.strictEqual(r.destroyed, true);
    assert.strictEqual(pipelined.destroyed, true);
  });
  const pipelined = addAbortSignal(ac.signal, pipeline([r, w], cb));
}

{
  pipeline([1, 2, 3], PassThrough({ objectMode: true }),
           common.mustSucceed(() => {}));

  let res = '';
  const w = new Writable({
    write(chunk, encoding, callback) {
      res += chunk;
      callback();
    },
  });
  pipeline(['1', '2', '3'], w, common.mustSucceed(() => {
    assert.strictEqual(res, '123');
  }));
}

{
  const content = 'abc';
  pipeline(Buffer.from(content), PassThrough({ objectMode: true }),
           common.mustSucceed(() => {}));

  let res = '';
  pipeline(Buffer.from(content), async function*(previous) {
    for await (const val of previous) {
      res += String.fromCharCode(val);
      yield val;
    }
  }, common.mustSucceed(() => {
    assert.strictEqual(res, content);
  }));
}

{
  const ac = new AbortController();
  const signal = ac.signal;
  pipelinep(
    async function * ({ signal }) { // eslint-disable-line require-yield
      await tsp.setTimeout(1e6, signal);
    },
    async function(source) {

    },
    { signal }
  ).catch(common.mustCall((err) => {
    assert.strictEqual(err.name, 'AbortError');
  }));
  ac.abort();
}

{
  async function run() {
    let finished = false;
    let text = '';
    const write = new Writable({
      write(data, enc, cb) {
        text += data;
        cb();
      }
    });
    write.on('finish', () => {
      finished = true;
    });

    await pipelinep([Readable.from('Hello World!'), write]);
    assert(finished);
    assert.strictEqual(text, 'Hello World!');
  }

  run();
}

{
  let finished = false;
  let text = '';
  const write = new Writable({
    write(data, enc, cb) {
      text += data;
      cb();
    }
  });
  write.on('finish', () => {
    finished = true;
  });

  pipeline([Readable.from('Hello World!'), write], common.mustSucceed(() => {
    assert(finished);
    assert.strictEqual(text, 'Hello World!');
  }));
}

{
  const pipelinePromise = promisify(pipeline);

  async function run() {
    const read = new Readable({
      read() {}
    });

    const duplex = new PassThrough();

    read.push(null);

    await pipelinePromise(read, duplex);

    assert.strictEqual(duplex.destroyed, false);
  }

  run().then(common.mustCall());
}

{
  const pipelinePromise = promisify(pipeline);

  async function run() {
    const read = new Readable({
      read() {}
    });

    const duplex = new PassThrough();
    const transform = new PassThrough();

    read.push(null);

    await pipelinePromise(read, transform, duplex, { end: false });

    assert.strictEqual(transform.destroyed, true);
    assert.strictEqual(transform.writableEnded, true);

    assert.strictEqual(duplex.destroyed, false);
    assert.strictEqual(duplex.writableEnded, false);
  }

  run().then(common.mustCall());
}

{
  const s = new PassThrough({ objectMode: true });
  pipeline(async function*() {
    await Promise.resolve();
    yield 'hello';
    yield 'world';
    yield 'world';
  }, s, async function(source) {
    let ret = '';
    let n = 0;
    for await (const chunk of source) {
      if (n++ > 1) {
        break;
      }
      ret += chunk;
    }
    return ret;
  }, common.mustCall((err, val) => {
    assert.strictEqual(err, undefined);
    assert.strictEqual(val, 'helloworld');
    assert.strictEqual(s.destroyed, true);
  }));
}

{
  const s = new PassThrough({ objectMode: true });
  pipeline(async function*() {
    await Promise.resolve();
    yield 'hello';
    yield 'world';
    yield 'world';
  }, s, async function(source) {
    return null;
  }, common.mustCall((err, val) => {
    assert.strictEqual(err, undefined);
    assert.strictEqual(val, null);
  }));
}

{
  // Mimics a legacy stream without the .destroy method
  class LegacyWritable extends Stream {
    write(chunk, encoding, callback) {
      callback();
    }
  }

  const writable = new LegacyWritable();
  writable.on('error', common.mustCall((err) => {
    assert.deepStrictEqual(err, new Error('stop'));
  }));

  pipeline(
    Readable.from({
      [Symbol.asyncIterator]() {
        return {
          next() {
            return Promise.reject(new Error('stop'));
          }
        };
      }
    }),
    writable,
    common.mustCall((err) => {
      assert.deepStrictEqual(err, new Error('stop'));
    })
  );
}

{
  class CustomReadable extends Readable {
    _read() {
      this.push('asd');
      this.push(null);
    }
  }

  class CustomWritable extends Writable {
    constructor() {
      super();
      this.endCount = 0;
      this.str = '';
    }

    _write(chunk, enc, cb) {
      this.str += chunk;
      cb();
    }

    end() {
      this.endCount += 1;
      super.end();
    }
  }

  const readable = new CustomReadable();
  const writable = new CustomWritable();

  pipeline(readable, writable, common.mustSucceed(() => {
    assert.strictEqual(writable.str, 'asd');
    assert.strictEqual(writable.endCount, 1);
  }));
}

{
  const readable = new Readable({
    read() {}
  });
  readable.on('end', common.mustCall(() => {
    pipeline(readable, new PassThrough(), common.mustSucceed());
  }));
  readable.push(null);
  readable.read();
}

{
  const dup = new Duplex({
    read() {},
    write(chunk, enc, cb) {
      cb();
    }
  });
  dup.on('end', common.mustCall(() => {
    pipeline(dup, new PassThrough(), common.mustSucceed());
  }));
  dup.push(null);
  dup.read();
}

{
  let res = '';
  const writable = new Writable({
    write(chunk, enc, cb) {
      res += chunk;
      cb();
    }
  });
  pipelinep(async function*() {
    yield 'hello';
    await Promise.resolve();
    yield 'world';
  }, writable, { end: false }).then(common.mustCall(() => {
    assert.strictEqual(res, 'helloworld');
    assert.strictEqual(writable.closed, false);
  }));
}

{
  const r = new Readable();
  for (let i = 0; i < 4000; i++) {
    r.push('asdfdagljanfgkaljdfn');
  }
  r.push(null);

  let ended = false;
  r.on('end', () => {
    ended = true;
  });

  const w = new Writable({
    write(chunk, enc, cb) {
      cb(null);
    },
    final: common.mustCall((cb) => {
      assert.strictEqual(ended, true);
      cb(null);
    })
  });

  pipeline(r, w, common.mustCall((err) => {
    assert.strictEqual(err, undefined);
  }));
}

{
  // See https://github.com/nodejs/node/issues/51540 for the following 2 tests
  const src = new Readable();
  const dst = new Writable({
    destroy(error, cb) {
      // Takes a while to destroy
      setImmediate(cb);
    },
  });

  pipeline(src, dst, (err) => {
    assert.strictEqual(src.closed, true);
    assert.strictEqual(dst.closed, true);
    assert.strictEqual(err.message, 'problem');
  });
  src.destroy(new Error('problem'));
}

{
  const src = new Readable();
  const dst = new Writable({
    destroy(error, cb) {
      // Takes a while to destroy
      setImmediate(cb);
    },
  });
  const passThroughs = [];
  for (let i = 0; i < 10; i++) {
    passThroughs.push(new PassThrough());
  }

  pipeline(src, ...passThroughs, dst, (err) => {
    assert.strictEqual(src.closed, true);
    assert.strictEqual(dst.closed, true);
    assert.strictEqual(err.message, 'problem');

    for (let i = 0; i < passThroughs.length; i++) {
      assert.strictEqual(passThroughs[i].closed, true);
    }
  });
  src.destroy(new Error('problem'));
}

{
  async function* myAsyncGenerator(ag) {
    for await (const data of ag) {
      yield data;
    }
  }

  const duplexStream = Duplex.from(myAsyncGenerator);

  const r = new Readable({
    read() {
      this.push('data1\n');
      throw new Error('booom');
    },
  });

  const w = new Writable({
    write(chunk, encoding, callback) {
      callback();
    },
  });

  pipeline(r, duplexStream, w, common.mustCall((err) => {
    assert.deepStrictEqual(err, new Error('booom'));
  }));
}
