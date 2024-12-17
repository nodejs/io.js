// https://github.com/nodejs/node/issues/39447
'use strict';
const common = require('../common');
const {
  pipeline,
} = require('stream');
const assert = require('assert');

function createIterator() {
  return async function*(stream) {
    yield 'test';
    throw new Error('Artificial Error');
  };
}

const ts = createIterator();

pipeline(ts, process.stdout, common.mustCall((err) => {
  assert.ok(err, 'should have an error');
}));
