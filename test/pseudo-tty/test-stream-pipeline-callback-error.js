// https://github.com/nodejs/node/issues/39447
'use strict';
const common = require('../common');
const {
  Transform,
  pipeline,
} = require('stream');
const assert = require('assert');

function createTransformStream(tf, context) {
  return new Transform({
    readableObjectMode: true,
    writableObjectMode: true,

    transform(chunk, encoding, done) {
      tf(chunk, context, done);
    }
  });
}

const ts = createTransformStream((chunk, _, done) => {
  return done(new Error('Artificial error'));
});

pipeline(ts, process.stdout, common.mustCall((err) => {
  assert.ok(err, 'should have an error');
}));

ts.write('test');
