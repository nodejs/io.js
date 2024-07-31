'use strict';
const { describe, it } = require('node:test');

describe('unfinished suite with asynchronous error', () => {
  it('uses callback', (t, done) => {
    setImmediate(() => {
      throw new Error('callback test does not complete');
    });
  });

  it('should pass 1');
});

it('should pass 2');
