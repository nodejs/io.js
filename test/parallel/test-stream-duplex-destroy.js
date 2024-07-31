'use strict';

const common = require('../common');
const { Duplex } = require('stream');
const assert = require('assert');

{
  const duplex = new Duplex({
    write(chunk, enc, cb) { cb(); },
    read() {}
  });

  duplex.resume();

  duplex.on('end', common.mustNotCall());
  duplex.on('finish', common.mustNotCall());
  duplex.on('close', common.mustCall());

  duplex.destroy();
  assert.strictEqual(duplex.destroyed, true);
}

{
  const duplex = new Duplex({
    write(chunk, enc, cb) { cb(); },
    read() {}
  });
  duplex.resume();

  const expected = new Error('kaboom');

  duplex.on('end', common.mustNotCall());
  duplex.on('finish', common.mustNotCall());
  duplex.on('error', common.mustCall((err) => {
    assert.strictEqual(err, expected);
  }));

  duplex.destroy(expected);
  assert.strictEqual(duplex.destroyed, true);
}

{
  const duplex = new Duplex({
    write(chunk, enc, cb) { cb(); },
    read() {}
  });

  duplex._destroy = common.mustCall(function(err, cb) {
    assert.strictEqual(err, expected);
    cb(err);
  });

  const expected = new Error('kaboom');

  duplex.on('finish', common.mustNotCall('no finish event'));
  duplex.on('error', common.mustCall((err) => {
    assert.strictEqual(err, expected);
  }));

  duplex.destroy(expected);
  assert.strictEqual(duplex.destroyed, true);
}

{
  const expected = new Error('kaboom');
  const duplex = new Duplex({
    write(chunk, enc, cb) { cb(); },
    read() {},
    destroy: common.mustCall(function(err, cb) {
      assert.strictEqual(err, expected);
      cb();
    })
  });
  duplex.resume();

  duplex.on('end', common.mustNotCall('no end event'));
  duplex.on('finish', common.mustNotCall('no finish event'));

  // Error is swallowed by the custom _destroy
  duplex.on('error', common.mustNotCall('no error event'));
  duplex.on('close', common.mustCall());

  duplex.destroy(expected);
  assert.strictEqual(duplex.destroyed, true);
}

{
  const duplex = new Duplex({
    write(chunk, enc, cb) { cb(); },
    read() {}
  });

  duplex._destroy = common.mustCall(function(err, cb) {
    assert.strictEqual(err, null);
    cb();
  });

  duplex.destroy();
  assert.strictEqual(duplex.destroyed, true);
}

{
  const duplex = new Duplex({
    write(chunk, enc, cb) { cb(); },
    read() {}
  });
  duplex.resume();

  duplex._destroy = common.mustCall(function(err, cb) {
    assert.strictEqual(err, null);
    process.nextTick(() => {
      this.push(null);
      this.end();
      cb();
    });
  });

  const fail = common.mustNotCall('no finish or end event');

  duplex.on('finish', fail);
  duplex.on('end', fail);

  duplex.destroy();

  duplex.removeListener('end', fail);
  duplex.removeListener('finish', fail);
  duplex.on('end', common.mustNotCall());
  duplex.on('finish', common.mustNotCall());
  assert.strictEqual(duplex.destroyed, true);
}

{
  const duplex = new Duplex({
    write(chunk, enc, cb) { cb(); },
    read() {}
  });

  const expected = new Error('kaboom');

  duplex._destroy = common.mustCall(function(err, cb) {
    assert.strictEqual(err, null);
    cb(expected);
  });

  duplex.on('finish', common.mustNotCall('no finish event'));
  duplex.on('end', common.mustNotCall('no end event'));
  duplex.on('error', common.mustCall((err) => {
    assert.strictEqual(err, expected);
  }));

  duplex.destroy();
  assert.strictEqual(duplex.destroyed, true);
}

{
  const duplex = new Duplex({
    write(chunk, enc, cb) { cb(); },
    read() {},
    allowHalfOpen: true
  });
  duplex.resume();

  duplex.on('finish', common.mustNotCall());
  duplex.on('end', common.mustNotCall());

  duplex.destroy();
  assert.strictEqual(duplex.destroyed, true);
}

{
  const duplex = new Duplex({
    write(chunk, enc, cb) { cb(); },
    read() {},
  });

  duplex.destroyed = true;
  assert.strictEqual(duplex.destroyed, true);

  // The internal destroy() mechanism should not be triggered
  duplex.on('finish', common.mustNotCall());
  duplex.on('end', common.mustNotCall());
  duplex.destroy();
}

{
  function MyDuplex() {
    assert.strictEqual(this.destroyed, false);
    this.destroyed = false;
    Duplex.call(this);
  }

  Object.setPrototypeOf(MyDuplex.prototype, Duplex.prototype);
  Object.setPrototypeOf(MyDuplex, Duplex);

  new MyDuplex();
}

{
  const duplex = new Duplex({
    writable: false,
    autoDestroy: true,
    write(chunk, enc, cb) { cb(); },
    read() {},
  });
  duplex.push(null);
  duplex.resume();
  duplex.on('close', common.mustCall());
}

{
  const duplex = new Duplex({
    readable: false,
    autoDestroy: true,
    write(chunk, enc, cb) { cb(); },
    read() {},
  });
  duplex.end();
  duplex.on('close', common.mustCall());
}

{
  const duplex = new Duplex({
    allowHalfOpen: false,
    autoDestroy: true,
    write(chunk, enc, cb) { cb(); },
    read() {},
  });
  duplex.push(null);
  duplex.resume();
  const orgEnd = duplex.end;
  duplex.end = common.mustNotCall();
  duplex.on('end', () => {
    // Ensure end() is called in next tick to allow
    // any pending writes to be invoked first.
    process.nextTick(() => {
      duplex.end = common.mustCall(orgEnd);
    });
  });
  duplex.on('close', common.mustCall());
}

{
  // Check abort signal
  const controller = new AbortController();
  const { signal } = controller;
  const duplex = new Duplex({
    write(chunk, enc, cb) { cb(); },
    read() {},
    signal,
  });
  let count = 0;
  duplex.on('error', common.mustCall((e) => {
    assert.strictEqual(count++, 0); // Ensure not called twice
    assert.strictEqual(e.name, 'AbortError');
  }));
  duplex.on('close', common.mustCall());
  controller.abort();
}

{
  const duplex = new Duplex({
    read() {},
    write(chunk, enc, cb) { cb(); }
  });

  duplex.cork();
  duplex.write('foo', common.mustCall((err) => {
    assert.strictEqual(err.code, 'ERR_STREAM_DESTROYED');
  }));
  duplex.destroy();
}

{
  // Check Symbol.asyncDispose
  const duplex = new Duplex({
    write(chunk, enc, cb) { cb(); },
    read() {},
  });
  let count = 0;
  duplex.on('error', common.mustCall((e) => {
    assert.strictEqual(count++, 0); // Ensure not called twice
    assert.strictEqual(e.name, 'AbortError');
  }));
  duplex.on('close', common.mustCall());
  duplex[Symbol.asyncDispose]().then(common.mustCall());
}
