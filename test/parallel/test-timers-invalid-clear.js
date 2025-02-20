'use strict';

require('../common');
const assert = require('assert');

async function test() {
  const t = setTimeout(() => {
    console.log('1');
  }, 0);

  clearImmediate(t);

  const value1 = await new Promise((resolve) =>
    setTimeout(() => {
      console.log('2');
      resolve(2);
    }, 0)
  );

  const value2 = await new Promise((resolve) =>
    setTimeout(() => {
      console.log('3');
      resolve(3);
    }, 0)
  );

  assert.strictEqual(value1, 2);
  assert.strictEqual(value2, 3);
}

test();
