'use strict';

const common = require('../common');
const dc = require('diagnostics_channel');
const assert = require('assert');

const channel = dc.tracingChannel('test');

const expectedError = new Error('test');
const input = { foo: 'bar' };
const thisArg = { baz: 'buz' };

function check(found) {
  assert.deepStrictEqual(found, input);
}

const handlers = {
  start: common.mustCall(check),
  end: common.mustCall(check),
  asyncStart: common.mustCall(check),
  asyncEnd: common.mustCall(check),
  error: common.mustCall((found) => {
    check(found);
    assert.deepStrictEqual(found.error, expectedError);
  })
};

channel.subscribe(handlers);

channel.tracePromise(function(value) {
  assert.deepStrictEqual(this, thisArg);
  return Promise.reject(value);
}, input, thisArg, expectedError).then(
  common.mustNotCall(),
  common.mustCall((value) => {
    assert.deepStrictEqual(value, expectedError);
  })
);
