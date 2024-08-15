'use strict';

const common = require('../common');
const {
  Writable,
  Readable,
  Transform,
  finished,
  Duplex,
  PassThrough,
  Stream,
} = require('stream');
const assert = require('assert');
const EE = require('events');
const fs = require('fs');
const { promisify } = require('util');
const http = require('http');

{
  const rs = new Readable({
    read() {}
  });

  finished(rs, common.mustSucceed());

  rs.push(null);
  rs.resume();
}

{
  const ws = new Writable({
    write(data, enc, cb) {
      cb();
    }
  });

  finished(ws, common.mustSucceed());

  ws.end();
}

{
  const tr = new Transform({
    transform(data, enc, cb) {
      cb();
    }
  });

  let finish = false;
  let ended = false;

  tr.on('end', () => {
    ended = true;
  });

  tr.on('finish', () => {
    finish = true;
  });

  finished(tr, common.mustSucceed(() => {
    assert(finish);
    assert(ended);
  }));

  tr.end();
  tr.resume();
}

{
  const rs = fs.createReadStream(__filename);

  rs.resume();
  finished(rs, common.mustCall());
}

{
  const finishedPromise = promisify(finished);

  async function run() {
    const rs = fs.createReadStream(__filename);
    const done = common.mustCall();

    let ended = false;
    rs.resume();
    rs.on('end', () => {
      ended = true;
    });
    await finishedPromise(rs);
    assert(ended);
    done();
  }

  run();
}

{
  // Check pre-cancelled
  const signal = new EventTarget();
  signal.aborted = true;

  const rs = Readable.from((function* () {})());
  finished(rs, { signal }, common.mustCall((err) => {
    assert.strictEqual(err.name, 'AbortError');
  }));
}

{
  // Check cancelled before the stream ends sync.
  const ac = new AbortController();
  const { signal } = ac;

  const rs = Readable.from((function* () {})());
  finished(rs, { signal }, common.mustCall((err) => {
    assert.strictEqual(err.name, 'AbortError');
  }));

  ac.abort();
}

{
  // Check cancelled before the stream ends async.
  const ac = new AbortController();
  const { signal } = ac;

  const rs = Readable.from((function* () {})());
  setTimeout(() => ac.abort(), 1);
  finished(rs, { signal }, common.mustCall((err) => {
    assert.strictEqual(err.name, 'AbortError');
  }));
}

{
  // Check cancelled after doesn't throw.
  const ac = new AbortController();
  const { signal } = ac;

  const rs = Readable.from((function* () {
    yield 5;
    setImmediate(() => ac.abort());
  })());
  rs.resume();
  finished(rs, { signal }, common.mustSucceed());
}

{
  // Promisified abort works
  const finishedPromise = promisify(finished);
  async function run() {
    const ac = new AbortController();
    const { signal } = ac;
    const rs = Readable.from((function* () {})());
    setImmediate(() => ac.abort());
    await finishedPromise(rs, { signal });
  }

  assert.rejects(run, { name: 'AbortError' }).then(common.mustCall());
}

{
  // Promisified pre-aborted works
  const finishedPromise = promisify(finished);
  async function run() {
    const signal = new EventTarget();
    signal.aborted = true;
    const rs = Readable.from((function* () {})());
    await finishedPromise(rs, { signal });
  }

  assert.rejects(run, { name: 'AbortError' }).then(common.mustCall());
}


{
  const rs = fs.createReadStream('file-does-not-exist');

  finished(rs, common.expectsError({
    code: 'ENOENT'
  }));
}

{
  const rs = new Readable();

  finished(rs, common.mustSucceed());

  rs.push(null);
  rs.emit('close'); // Should not trigger an error
  rs.resume();
}

{
  const rs = new Readable();

  finished(rs, common.mustCall((err) => {
    assert(err, 'premature close error');
  }));

  rs.emit('close'); // Should trigger error
  rs.push(null);
  rs.resume();
}

// Test faulty input values and options.
{
  const rs = new Readable({
    read() {}
  });

  assert.throws(
    () => finished(rs, 'foo'),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /callback/
    }
  );
  assert.throws(
    () => finished(rs, 'foo', () => {}),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /options/
    }
  );
  assert.throws(
    () => finished(rs, {}, 'foo'),
    {
      code: 'ERR_INVALID_ARG_TYPE',
      message: /callback/
    }
  );

  finished(rs, null, common.mustCall());

  rs.push(null);
  rs.resume();
}

// Test that calling returned function removes listeners
{
  const ws = new Writable({
    write(data, env, cb) {
      cb();
    }
  });
  const removeListener = finished(ws, common.mustNotCall());
  removeListener();
  ws.end();
}

{
  const rs = new Readable();
  const removeListeners = finished(rs, common.mustNotCall());
  removeListeners();

  rs.emit('close');
  rs.push(null);
  rs.resume();
}

{
  const streamLike = new EE();
  streamLike.readableEnded = true;
  streamLike.readable = true;
  assert.throws(
    () => {
      finished(streamLike, () => {});
    },
    { code: 'ERR_INVALID_ARG_TYPE' }
  );
  streamLike.emit('close');
}

{
  const writable = new Writable({ write() {} });
  writable.writable = false;
  writable.destroy();
  finished(writable, common.mustCall((err) => {
    assert.strictEqual(err.code, 'ERR_STREAM_PREMATURE_CLOSE');
  }));
}

{
  const readable = new Readable();
  readable.readable = false;
  readable.destroy();
  finished(readable, common.mustCall((err) => {
    assert.strictEqual(err.code, 'ERR_STREAM_PREMATURE_CLOSE');
  }));
}

{
  const w = new Writable({
    write(chunk, encoding, callback) {
      setImmediate(callback);
    }
  });
  finished(w, common.mustCall((err) => {
    assert.strictEqual(err.code, 'ERR_STREAM_PREMATURE_CLOSE');
  }));
  w.end('asd');
  w.destroy();
}

function testClosed(factory) {
  {
    // If already destroyed but finished is cancelled in same tick
    // don't invoke the callback,

    const s = factory();
    s.destroy();
    const dispose = finished(s, common.mustNotCall());
    dispose();
  }

  {
    // If already destroyed invoked callback.

    const s = factory();
    s.destroy();
    finished(s, common.mustCall());
  }

  {
    // Don't invoke until destroy has completed.

    let destroyed = false;
    const s = factory({
      destroy(err, cb) {
        setImmediate(() => {
          destroyed = true;
          cb();
        });
      }
    });
    s.destroy();
    finished(s, common.mustCall(() => {
      assert.strictEqual(destroyed, true);
    }));
  }

  {
    // Invoke callback even if close is inhibited.

    const s = factory({
      emitClose: false,
      destroy(err, cb) {
        cb();
        finished(s, common.mustCall());
      }
    });
    s.destroy();
  }

  {
    // Invoke with deep async.

    const s = factory({
      destroy(err, cb) {
        setImmediate(() => {
          cb();
          setImmediate(() => {
            finished(s, common.mustCall());
          });
        });
      }
    });
    s.destroy();
  }
}

testClosed((opts) => new Readable({ ...opts }));
testClosed((opts) => new Writable({ write() {}, ...opts }));

{
  const w = new Writable({
    write(chunk, encoding, cb) {
      cb();
    },
    autoDestroy: false
  });
  w.end('asd');
  process.nextTick(() => {
    finished(w, common.mustCall());
  });
}

{
  const w = new Writable({
    write(chunk, encoding, cb) {
      cb(new Error());
    },
    autoDestroy: false
  });
  w.write('asd');
  w.on('error', common.mustCall(() => {
    finished(w, common.mustCall());
  }));
}

{
  const r = new Readable({
    autoDestroy: false
  });
  r.push(null);
  r.resume();
  r.on('end', common.mustCall(() => {
    finished(r, common.mustCall());
  }));
}

{
  const rs = fs.createReadStream(__filename, { autoClose: false });
  rs.resume();
  rs.on('close', common.mustNotCall());
  rs.on('end', common.mustCall(() => {
    finished(rs, common.mustCall());
  }));
}

{
  const d = new EE();
  d._writableState = {};
  d._writableState.finished = true;
  finished(d, { readable: false, writable: true }, common.mustCall((err) => {
    assert.strictEqual(err.code, 'ERR_STREAM_PREMATURE_CLOSE');
  }));
  d._writableState.errored = true;
  d.emit('close');
}

{
  const r = new Readable();
  finished(r, common.mustCall((err) => {
    assert.strictEqual(err.code, 'ERR_STREAM_PREMATURE_CLOSE');
  }));
  r.push('asd');
  r.push(null);
  r.destroy();
}

{
  const d = new Duplex({
    final(cb) { }, // Never close writable side for test purpose
    read() {
      this.push(null);
    }
  });

  d.on('end', common.mustCall());

  finished(d, { readable: true, writable: false }, common.mustCall());

  d.end();
  d.resume();
}

{
  const d = new Duplex({
    final(cb) { }, // Never close writable side for test purpose
    read() {
      this.push(null);
    }
  });

  d.on('end', common.mustCall());

  d.end();
  finished(d, { readable: true, writable: false }, common.mustCall());

  d.resume();
}

{
  // Test for compat for e.g. fd-slicer which implements
  // non standard destroy behavior which might not emit
  // 'close'.
  const r = new Readable();
  finished(r, common.mustCall());
  r.resume();
  r.push('asd');
  r.destroyed = true;
  r.push(null);
}

{
  // Regression https://github.com/nodejs/node/issues/33130
  const response = new PassThrough();

  class HelloWorld extends Duplex {
    constructor(response) {
      super({
        autoDestroy: false
      });

      this.response = response;
      this.readMore = false;

      response.once('end', () => {
        this.push(null);
      });

      response.on('readable', () => {
        if (this.readMore) {
          this._read();
        }
      });
    }

    _read() {
      const { response } = this;

      this.readMore = true;

      if (response.readableLength) {
        this.readMore = false;
      }

      let data;
      while ((data = response.read()) !== null) {
        this.push(data);
      }
    }
  }

  const instance = new HelloWorld(response);
  instance.setEncoding('utf8');
  instance.end();

  (async () => {
    await EE.once(instance, 'finish');

    setImmediate(() => {
      response.write('chunk 1');
      response.write('chunk 2');
      response.write('chunk 3');
      response.end();
    });

    let res = '';
    for await (const data of instance) {
      res += data;
    }

    assert.strictEqual(res, 'chunk 1chunk 2chunk 3');
  })().then(common.mustCall());
}

{
  const p = new PassThrough();
  p.end();
  finished(p, common.mustNotCall());
}

{
  const p = new PassThrough();
  p.end();
  p.on('finish', common.mustCall(() => {
    finished(p, common.mustNotCall());
  }));
}

{
  const server = http.createServer(common.mustCall((req, res) => {
    res.on('close', common.mustCall(() => {
      finished(res, common.mustCall(() => {
        server.close();
      }));
    }));
    res.end();
  }))
  .listen(0, function() {
    http.request({
      method: 'GET',
      port: this.address().port
    }).end()
      .on('response', common.mustCall());
  });
}

{
  const server = http.createServer(common.mustCall((req, res) => {
    req.on('close', common.mustCall(() => {
      finished(req, common.mustCall(() => {
        server.close();
      }));
    }));
    req.destroy();
  })).listen(0, function() {
    http.request({
      method: 'GET',
      port: this.address().port
    }).end().on('error', common.mustCall());
  });
}

{
  const w = new Writable({
    write(chunk, encoding, callback) {
      process.nextTick(callback);
    }
  });
  w.aborted = false;
  w.end();
  let closed = false;
  w.on('finish', () => {
    assert.strictEqual(closed, false);
    w.emit('aborted');
  });
  w.on('close', common.mustCall(() => {
    closed = true;
  }));

  finished(w, common.mustCall(() => {
    assert.strictEqual(closed, true);
  }));
}

{
  const w = new Writable();
  const _err = new Error();
  w.destroy(_err);
  assert.strictEqual(w.errored, _err);
  finished(w, common.mustCall((err) => {
    assert.strictEqual(_err, err);
    assert.strictEqual(w.closed, true);
    finished(w, common.mustCall((err) => {
      assert.strictEqual(_err, err);
    }));
  }));
}

{
  const w = new Writable();
  w.destroy();
  assert.strictEqual(w.errored, null);
  finished(w, common.mustCall((err) => {
    assert.strictEqual(w.closed, true);
    assert.strictEqual(err.code, 'ERR_STREAM_PREMATURE_CLOSE');
    finished(w, common.mustCall((err) => {
      assert.strictEqual(err.code, 'ERR_STREAM_PREMATURE_CLOSE');
    }));
  }));
}

{
  // Legacy Streams do not inherit from Readable or Writable.
  // We cannot really assume anything about them, so we cannot close them
  // automatically.
  const s = new Stream();
  finished(s, common.mustNotCall());
}

{
  const server = http.createServer(common.mustCall(function(req, res) {
    fs.createReadStream(__filename).pipe(res);
    finished(res, common.mustCall(function(err) {
      assert.strictEqual(err, undefined);
    }));
  })).listen(0, function() {
    http.request(
      { method: 'GET', port: this.address().port },
      common.mustCall(function(res) {
        res.resume();
        finished(res, common.mustCall(() => {
          server.close();
        }));
      })
    ).end();
  });
}

{
  let isCalled = false;
  const stream = new Duplex({
    write(chunk, enc, cb) {
      setImmediate(() => {
        isCalled = true;
        cb();
      });
    }
  });

  stream.end('foo');

  finished(stream, { readable: false }, common.mustCall((err) => {
    assert(!err);
    assert.strictEqual(isCalled, true);
    assert.strictEqual(stream._writableState.pendingcb, 0);
  }));
}

{
  const stream = new Duplex({
    write(chunk, enc, cb) {}
  });

  stream.end('foo');

  // Simulate an old stream implementation that doesn't have pendingcb
  delete stream._writableState.pendingcb;

  finished(stream, { readable: false }, common.mustCall());
}
