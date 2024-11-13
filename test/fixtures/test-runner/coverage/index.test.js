'use strict';

const test = require('node:test');
test('no source map', () => {});
if (false) {
  console.log('this does not execute');
}
