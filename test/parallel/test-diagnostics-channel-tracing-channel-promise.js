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

channel.tracePromise(function(value) {
  assert.deepStrictEqual(this, thisArg);
  return Promise.resolve(value);
}, input, thisArg, expectedResult).then(
  common.mustCall((value) => {
    assert.deepStrictEqual(value, expectedResult);
  }),
  common.mustNotCall()
);
