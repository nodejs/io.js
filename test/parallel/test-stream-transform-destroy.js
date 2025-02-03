'use strict';

const common = require('../common');
const { Transform } = require('stream');
const assert = require('assert');

{
  const transform = new Transform({
    transform(chunk, enc, cb) {}
  });

  transform.resume();

  transform.on('end', common.mustNotCall());
  transform.on('close', common.mustCall());
  transform.on('finish', common.mustNotCall());

  transform.destroy();
}

{
  const transform = new Transform({
    transform(chunk, enc, cb) {}
  });
  transform.resume();

  const expected = new Error('kaboom');

  transform.on('end', common.mustNotCall());
  transform.on('finish', common.mustNotCall());
  transform.on('close', common.mustCall());
  transform.on('error', common.mustCall((err) => {
    assert.strictEqual(err, expected);
  }));

  transform.destroy(expected);
}

{
  const transform = new Transform({
    transform(chunk, enc, cb) {}
  });

  transform._destroy = common.mustCall(function(err, cb) {
    assert.strictEqual(err, expected);
    cb(err);
  }, 1);

  const expected = new Error('kaboom');

  transform.on('finish', common.mustNotCall('no finish event'));
  transform.on('close', common.mustCall());
  transform.on('error', common.mustCall((err) => {
    assert.strictEqual(err, expected);
  }));

  transform.destroy(expected);
}

{
  const expected = new Error('kaboom');
  const transform = new Transform({
    transform(chunk, enc, cb) {},
    destroy: common.mustCall(function(err, cb) {
      assert.strictEqual(err, expected);
      cb();
    }, 1)
  });
  transform.resume();

  transform.on('end', common.mustNotCall('no end event'));
  transform.on('close', common.mustCall());
  transform.on('finish', common.mustNotCall('no finish event'));

  // Error is swallowed by the custom _destroy
  transform.on('error', common.mustNotCall('no error event'));

  transform.destroy(expected);
}

{
  const transform = new Transform({
    transform(chunk, enc, cb) {}
  });

  transform._destroy = common.mustCall(function(err, cb) {
    assert.strictEqual(err, null);
    cb();
  }, 1);

  transform.destroy();
}

{
  const transform = new Transform({
    transform(chunk, enc, cb) {}
  });
  transform.resume();

  transform._destroy = common.mustCall(function(err, cb) {
    assert.strictEqual(err, null);
    process.nextTick(() => {
      this.push(null);
      this.end();
      cb();
    });
  }, 1);

  const fail = common.mustNotCall('no event');

  transform.on('finish', fail);
  transform.on('end', fail);
  transform.on('close', common.mustCall());

  transform.destroy();

  transform.removeListener('end', fail);
  transform.removeListener('finish', fail);
  transform.on('end', common.mustCall());
  transform.on('finish', common.mustNotCall());
}

{
  const transform = new Transform({
    transform(chunk, enc, cb) {}
  });

  const expected = new Error('kaboom');

  transform._destroy = common.mustCall(function(err, cb) {
    assert.strictEqual(err, null);
    cb(expected);
  }, 1);

  transform.on('close', common.mustCall());
  transform.on('finish', common.mustNotCall('no finish event'));
  transform.on('end', common.mustNotCall('no end event'));
  transform.on('error', common.mustCall((err) => {
    assert.strictEqual(err, expected);
  }));

  transform.destroy();
}

{
  const transform = new Transform({
    transform(chunk, enc, cb) {}
  });
  transform.on('error', common.mustCall((err) => {
    assert.strictEqual(err.name, 'AbortError');
  }));
  transform.on('close', common.mustCall());
  transform[Symbol.asyncDispose]().then(common.mustCall());
}
