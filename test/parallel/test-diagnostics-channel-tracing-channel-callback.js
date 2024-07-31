'use strict';

const common = require('../common');
const dc = require('diagnostics_channel');
const assert = require('assert');

const channel = dc.tracingChannel('test');

const expectedResult = { foo: 'bar' };
const input = { foo: 'bar' };
const thisArg = { baz: 'buz' };

function check(found) {
  assert.deepStrictEqual(found, input);
}

function checkAsync(found) {
  check(found);
  assert.strictEqual(found.error, undefined);
  assert.deepStrictEqual(found.result, expectedResult);
}

const handlers = {
  start: common.mustCall(check),
  end: common.mustCall(check),
  asyncStart: common.mustCall(checkAsync),
  asyncEnd: common.mustCall(checkAsync),
  error: common.mustNotCall()
};

channel.subscribe(handlers);

channel.traceCallback(function(cb, err, res) {
  assert.deepStrictEqual(this, thisArg);
  setImmediate(cb, err, res);
}, 0, input, thisArg, common.mustCall((err, res) => {
  assert.strictEqual(err, null);
  assert.deepStrictEqual(res, expectedResult);
}), null, expectedResult);

assert.throws(() => {
  channel.traceCallback(common.mustNotCall(), 0, input, thisArg, 1, 2, 3);
}, /"callback" argument must be of type function/);
