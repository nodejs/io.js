'use strict';
require('../common');
const {
  Transform,
  pipeline,
} = require('stream');

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

const ret = pipeline(ts, process.stdout, (err) => err);

ret.write('test');
