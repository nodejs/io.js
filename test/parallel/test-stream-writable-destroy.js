'use strict';

const common = require('../common');
const { Writable, addAbortSignal } = require('stream');
const assert = require('assert');

{
  const write = new Writable({
    write(chunk, enc, cb) { cb(); }
  });

  write.on('finish', common.mustNotCall());
  write.on('close', common.mustCall());

  write.destroy();
  assert.strictEqual(write.destroyed, true);
}

{
  const write = new Writable({
    write(chunk, enc, cb) {
      this.destroy(new Error('asd'));
      cb();
    }
  });

  write.on('error', common.mustCall());
  write.on('finish', common.mustNotCall());
  write.end('asd');
  assert.strictEqual(write.destroyed, true);
}

{
  const write = new Writable({
    write(chunk, enc, cb) { cb(); }
  });

  const expected = new Error('kaboom');

  write.on('finish', common.mustNotCall());
  write.on('close', common.mustCall());
  write.on('error', common.mustCall((err) => {
    assert.strictEqual(err, expected);
  }));

  write.destroy(expected);
  assert.strictEqual(write.destroyed, true);
}

{
  const write = new Writable({
    write(chunk, enc, cb) { cb(); }
  });

  write._destroy = function(err, cb) {
    assert.strictEqual(err, expected);
    cb(err);
  };

  const expected = new Error('kaboom');

  write.on('finish', common.mustNotCall('no finish event'));
  write.on('close', common.mustCall());
  write.on('error', common.mustCall((err) => {
    assert.strictEqual(err, expected);
  }));

  write.destroy(expected);
  assert.strictEqual(write.destroyed, true);
}

{
  const write = new Writable({
    write(chunk, enc, cb) { cb(); },
    destroy: common.mustCall(function(err, cb) {
      assert.strictEqual(err, expected);
      cb();
    })
  });

  const expected = new Error('kaboom');

  write.on('finish', common.mustNotCall('no finish event'));
  write.on('close', common.mustCall());

  // Error is swallowed by the custom _destroy
  write.on('error', common.mustNotCall('no error event'));

  write.destroy(expected);
  assert.strictEqual(write.destroyed, true);
}

{
  const write = new Writable({
    write(chunk, enc, cb) { cb(); }
  });

  write._destroy = common.mustCall(function(err, cb) {
    assert.strictEqual(err, null);
    cb();
  });

  write.destroy();
  assert.strictEqual(write.destroyed, true);
}

{
  const write = new Writable({
    write(chunk, enc, cb) { cb(); }
  });

  write._destroy = common.mustCall(function(err, cb) {
    assert.strictEqual(err, null);
    process.nextTick(() => {
      this.end();
      cb();
    });
  });

  const fail = common.mustNotCall('no finish event');

  write.on('finish', fail);
  write.on('close', common.mustCall());

  write.destroy();

  assert.strictEqual(write.destroyed, true);
}

{
  const write = new Writable({
    write(chunk, enc, cb) { cb(); }
  });

  const expected = new Error('kaboom');

  write._destroy = common.mustCall(function(err, cb) {
    assert.strictEqual(err, null);
    cb(expected);
  });

  write.on('close', common.mustCall());
  write.on('finish', common.mustNotCall('no finish event'));
  write.on('error', common.mustCall((err) => {
    assert.strictEqual(err, expected);
  }));

  write.destroy();
  assert.strictEqual(write.destroyed, true);
}

{
  // double error case
  const write = new Writable({
    write(chunk, enc, cb) { cb(); }
  });

  let ticked = false;
  write.on('close', common.mustCall(() => {
    assert.strictEqual(ticked, true);
  }));
  write.on('error', common.mustCall((err) => {
    assert.strictEqual(ticked, true);
    assert.strictEqual(err.message, 'kaboom 1');
    assert.strictEqual(write._writableState.errorEmitted, true);
  }));

  const expected = new Error('kaboom 1');
  write.destroy(expected);
  write.destroy(new Error('kaboom 2'));
  assert.strictEqual(write._writableState.errored, expected);
  assert.strictEqual(write._writableState.errorEmitted, false);
  assert.strictEqual(write.destroyed, true);
  ticked = true;
}

{
  const writable = new Writable({
    destroy: common.mustCall(function(err, cb) {
      process.nextTick(cb, new Error('kaboom 1'));
    }),
    write(chunk, enc, cb) {
      cb();
    }
  });

  let ticked = false;
  writable.on('close', common.mustCall(() => {
    writable.on('error', common.mustNotCall());
    writable.destroy(new Error('hello'));
    assert.strictEqual(ticked, true);
    assert.strictEqual(writable._writableState.errorEmitted, true);
  }));
  writable.on('error', common.mustCall((err) => {
    assert.strictEqual(ticked, true);
    assert.strictEqual(err.message, 'kaboom 1');
    assert.strictEqual(writable._writableState.errorEmitted, true);
  }));

  writable.destroy();
  assert.strictEqual(writable.destroyed, true);
  assert.strictEqual(writable._writableState.errored, null);
  assert.strictEqual(writable._writableState.errorEmitted, false);

  // Test case where `writable.destroy()` is called again with an error before
  // the `_destroy()` callback is called.
  writable.destroy(new Error('kaboom 2'));
  assert.strictEqual(writable._writableState.errorEmitted, false);
  assert.strictEqual(writable._writableState.errored, null);

  ticked = true;
}

{
  const write = new Writable({
    write(chunk, enc, cb) { cb(); }
  });

  write.destroyed = true;
  assert.strictEqual(write.destroyed, true);

  // The internal destroy() mechanism should not be triggered
  write.on('close', common.mustNotCall());
  write.destroy();
}

{
  function MyWritable() {
    assert.strictEqual(this.destroyed, false);
    this.destroyed = false;
    Writable.call(this);
  }

  Object.setPrototypeOf(MyWritable.prototype, Writable.prototype);
  Object.setPrototypeOf(MyWritable, Writable);

  new MyWritable();
}

{
  // Destroy and destroy callback
  const write = new Writable({
    write(chunk, enc, cb) { cb(); }
  });

  write.destroy();

  const expected = new Error('kaboom');

  write.destroy(expected, common.mustCall((err) => {
    assert.strictEqual(err, undefined);
  }));
}

{
  // Checks that `._undestroy()` restores the state so that `final` will be
  // called again.
  const write = new Writable({
    write: common.mustNotCall(),
    final: common.mustCall((cb) => cb(), 2),
    autoDestroy: true
  });

  write.end();
  write.once('close', common.mustCall(() => {
    write._undestroy();
    write.end();
  }));
}

{
  const write = new Writable();

  write.destroy();
  write.on('error', common.mustNotCall());
  write.write('asd', common.expectsError({
    name: 'Error',
    code: 'ERR_STREAM_DESTROYED',
    message: 'Cannot call write after a stream was destroyed'
  }));
}

{
  const write = new Writable({
    write(chunk, enc, cb) { cb(); }
  });

  write.on('error', common.mustNotCall());

  write.cork();
  write.write('asd', common.mustCall());
  write.uncork();

  write.cork();
  write.write('asd', common.expectsError({
    name: 'Error',
    code: 'ERR_STREAM_DESTROYED',
    message: 'Cannot call write after a stream was destroyed'
  }));
  write.destroy();
  write.write('asd', common.expectsError({
    name: 'Error',
    code: 'ERR_STREAM_DESTROYED',
    message: 'Cannot call write after a stream was destroyed'
  }));
  write.uncork();
}

{
  // Call end(cb) after error & destroy

  const write = new Writable({
    write(chunk, enc, cb) { cb(new Error('asd')); }
  });
  write.on('error', common.mustCall(() => {
    write.destroy();
    let ticked = false;
    write.end(common.mustCall((err) => {
      assert.strictEqual(ticked, true);
      assert.strictEqual(err.code, 'ERR_STREAM_DESTROYED');
    }));
    ticked = true;
  }));
  write.write('asd');
}

{
  // Call end(cb) after finish & destroy

  const write = new Writable({
    write(chunk, enc, cb) { cb(); }
  });
  write.on('finish', common.mustCall(() => {
    write.destroy();
    let ticked = false;
    write.end(common.mustCall((err) => {
      assert.strictEqual(ticked, true);
      assert.strictEqual(err.code, 'ERR_STREAM_ALREADY_FINISHED');
    }));
    ticked = true;
  }));
  write.end();
}

{
  // Call end(cb) after error & destroy and don't trigger
  // unhandled exception.

  const write = new Writable({
    write(chunk, enc, cb) { process.nextTick(cb); }
  });
  const _err = new Error('asd');
  write.once('error', common.mustCall((err) => {
    assert.strictEqual(err.message, 'asd');
  }));
  write.end('asd', common.mustCall((err) => {
    assert.strictEqual(err, _err);
  }));
  write.destroy(_err);
}

{
  // Call buffered write callback with error

  const _err = new Error('asd');
  const write = new Writable({
    write(chunk, enc, cb) {
      process.nextTick(cb, _err);
    },
    autoDestroy: false
  });
  write.cork();
  write.write('asd', common.mustCall((err) => {
    assert.strictEqual(err, _err);
  }));
  write.write('asd', common.mustCall((err) => {
    assert.strictEqual(err, _err);
  }));
  write.on('error', common.mustCall((err) => {
    assert.strictEqual(err, _err);
  }));
  write.uncork();
}

{
  // Ensure callback order.

  let state = 0;
  const write = new Writable({
    write(chunk, enc, cb) {
      // `setImmediate()` is used on purpose to ensure the callback is called
      // after `process.nextTick()` callbacks.
      setImmediate(cb);
    }
  });
  write.write('asd', common.mustCall(() => {
    assert.strictEqual(state++, 0);
  }));
  write.write('asd', common.mustCall((err) => {
    assert.strictEqual(err.code, 'ERR_STREAM_DESTROYED');
    assert.strictEqual(state++, 1);
  }));
  write.destroy();
}

{
  const write = new Writable({
    autoDestroy: false,
    write(chunk, enc, cb) {
      cb();
      cb();
    }
  });

  write.on('error', common.mustCall(() => {
    assert(write._writableState.errored);
  }));
  write.write('asd');
}

{
  const ac = new AbortController();
  const write = addAbortSignal(ac.signal, new Writable({
    write(chunk, enc, cb) { cb(); }
  }));

  write.on('error', common.mustCall((e) => {
    assert.strictEqual(e.name, 'AbortError');
    assert.strictEqual(write.destroyed, true);
  }));
  write.write('asd');
  ac.abort();
}

{
  const ac = new AbortController();
  const write = new Writable({
    signal: ac.signal,
    write(chunk, enc, cb) { cb(); }
  });

  write.on('error', common.mustCall((e) => {
    assert.strictEqual(e.name, 'AbortError');
    assert.strictEqual(write.destroyed, true);
  }));
  write.write('asd');
  ac.abort();
}

{
  const signal = AbortSignal.abort();

  const write = new Writable({
    signal,
    write(chunk, enc, cb) { cb(); }
  });

  write.on('error', common.mustCall((e) => {
    assert.strictEqual(e.name, 'AbortError');
    assert.strictEqual(write.destroyed, true);
  }));
}

{
  // Destroy twice
  const write = new Writable({
    write(chunk, enc, cb) { cb(); }
  });

  write.end(common.mustCall());
  write.destroy();
  write.destroy();
}

{
  // https://github.com/nodejs/node/issues/39356
  const s = new Writable({
    final() {}
  });
  const _err = new Error('oh no');
  // Remove `callback` and it works
  s.end(common.mustCall((err) => {
    assert.strictEqual(err, _err);
  }));
  s.on('error', common.mustCall((err) => {
    assert.strictEqual(err, _err);
  }));
  s.destroy(_err);
}

{
  const write = new Writable({
    write(chunk, enc, cb) { cb(); }
  });

  write.on('error', common.mustCall((e) => {
    assert.strictEqual(e.name, 'AbortError');
    assert.strictEqual(write.destroyed, true);
  }));
  write[Symbol.asyncDispose]().then(common.mustCall());
}
